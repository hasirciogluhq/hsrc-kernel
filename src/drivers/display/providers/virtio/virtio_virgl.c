#include "virtio_virgl.h"
#include <drivers/console/vga.h>
#include <kernel/heap.h>
#include <kernel/string.h>

/*
 * VirGL / virtio-gpu 3D path — CLEAR / DRAW / BLIT via SUBMIT_3D.
 * No CPU raster: host virglrenderer executes the stream.
 * Vertex MVP prep into a staging float buffer is allowed (upload only).
 */

#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106
#define VIRTIO_GPU_CMD_CTX_CREATE              0x0200
#define VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE     0x0202
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_3D      0x0204
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D     0x0205
#define VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D   0x0206
#define VIRTIO_GPU_CMD_SUBMIT_3D               0x0207

#define VIRTIO_GPU_RESP_OK_NODATA 0x1100
#define VIRTIO_GPU_RESOURCE_FLAG_Y_0_TOP (1u << 0)

#define VIRGL_CMD0(cmd, obj, len) ((uint32_t)(cmd) | ((uint32_t)(obj) << 8) | ((uint32_t)(len) << 16))

#define VIRGL_CCMD_CREATE_OBJECT           1
#define VIRGL_CCMD_BIND_OBJECT             2
#define VIRGL_CCMD_SET_VIEWPORT_STATE      4
#define VIRGL_CCMD_SET_FRAMEBUFFER_STATE   5
#define VIRGL_CCMD_SET_VERTEX_BUFFERS      6
#define VIRGL_CCMD_CLEAR                   7
#define VIRGL_CCMD_DRAW_VBO                8
#define VIRGL_CCMD_SET_SAMPLER_VIEWS       10
#define VIRGL_CCMD_SET_INDEX_BUFFER        11
#define VIRGL_CCMD_SET_SCISSOR_STATE       15
#define VIRGL_CCMD_RESOURCE_COPY_REGION    17
#define VIRGL_CCMD_BIND_SAMPLER_STATES     18
#define VIRGL_CCMD_SET_SUB_CTX             28
#define VIRGL_CCMD_CREATE_SUB_CTX          29
#define VIRGL_CCMD_BIND_SHADER             31

#define VIRGL_OBJECT_BLEND            1
#define VIRGL_OBJECT_RASTERIZER       2
#define VIRGL_OBJECT_DSA              3
#define VIRGL_OBJECT_SHADER           4
#define VIRGL_OBJECT_VERTEX_ELEMENTS  5
#define VIRGL_OBJECT_SAMPLER_VIEW     6
#define VIRGL_OBJECT_SAMPLER_STATE    7
#define VIRGL_OBJECT_SURFACE          8

#define VIRGL_FORMAT_B8G8R8A8_UNORM       1
#define VIRGL_FORMAT_R32G32B32A32_FLOAT   31
#define VIRGL_FORMAT_R8_UNORM             64
#define VIRGL_FORMAT_R16_UINT             115

#define VIRGL_BIND_RENDER_TARGET  (1u << 1)
#define VIRGL_BIND_SAMPLER_VIEW   (1u << 3)
#define VIRGL_BIND_VERTEX_BUFFER  (1u << 4)
#define VIRGL_BIND_INDEX_BUFFER   (1u << 5)

#define PIPE_BUFFER      0
#define PIPE_TEXTURE_2D  2

#define PIPE_SHADER_VERTEX   0
#define PIPE_SHADER_FRAGMENT 1

#define PIPE_PRIM_TRIANGLES 4
#define PIPE_CLEAR_COLOR0   (1u << 2)
#define PIPE_MASK_RGBA      0xf

#define PIPE_BLEND_ADD              0
#define PIPE_BLENDFACTOR_ONE        1
#define PIPE_BLENDFACTOR_SRC_ALPHA  3
#define PIPE_BLENDFACTOR_INV_SRC_ALPHA 0x13

#define PIPE_TEX_WRAP_CLAMP_TO_EDGE 2
#define PIPE_TEX_FILTER_NEAREST     0
#define PIPE_TEX_FILTER_LINEAR      1
#define PIPE_TEX_MIPFILTER_NONE     0

#define CTX_ID          1u
#define SUB_CTX_ID      1u
#define RES_ID_BASE     100u

#define OBJ_BLEND_OPAQUE 1u
#define OBJ_BLEND_ALPHA  2u
#define OBJ_RASTERIZER   3u
#define OBJ_DSA          4u
#define OBJ_VE           5u
#define OBJ_VS_COLOR     6u
#define OBJ_FS_COLOR     7u
#define OBJ_VS_TEX       8u
#define OBJ_FS_TEX       9u
#define OBJ_SAMPLER      10u
#define OBJ_SURF_BASE    64u
#define OBJ_SVIEW_BASE   128u

#define CMDBUF_MAX_DWORDS 4096
#define RES_CACHE_MAX     32
#define STAGING_MAX_VERTS 65536
#define REED_VERT_STRIDE  36u /* float3+float3+float2+u32 */

typedef struct virtio_gpu_ctrl_hdr {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint8_t  ring_idx;
    uint8_t  padding[3];
} __attribute__((packed)) virtio_gpu_ctrl_hdr_t;

typedef struct virtio_gpu_box {
    uint32_t x, y, z;
    uint32_t w, h, d;
} __attribute__((packed)) virtio_gpu_box_t;

typedef struct virtio_gpu_mem_entry {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
} __attribute__((packed)) virtio_gpu_mem_entry_t;

typedef struct virtio_gpu_ctx_create {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t nlen;
    uint32_t context_init;
    char debug_name[64];
} __attribute__((packed)) virtio_gpu_ctx_create_t;

typedef struct virtio_gpu_ctx_resource {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed)) virtio_gpu_ctx_resource_t;

typedef struct virtio_gpu_resource_create_3d {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t target;
    uint32_t format;
    uint32_t bind;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t array_size;
    uint32_t last_level;
    uint32_t nr_samples;
    uint32_t flags;
    uint32_t padding;
} __attribute__((packed)) virtio_gpu_resource_create_3d_t;

typedef struct virtio_gpu_resource_attach_backing {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
} __attribute__((packed)) virtio_gpu_resource_attach_backing_t;

typedef struct attach_req {
    virtio_gpu_resource_attach_backing_t ab;
    virtio_gpu_mem_entry_t entry;
} __attribute__((packed)) attach_req_t;

typedef struct virtio_gpu_transfer_host_3d {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_box_t box;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t level;
    uint32_t stride;
    uint32_t layer_stride;
} __attribute__((packed)) virtio_gpu_transfer_host_3d_t;

typedef struct virtio_gpu_cmd_submit {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t size;
    uint32_t padding;
} __attribute__((packed)) virtio_gpu_cmd_submit_t;

typedef struct virgl_vtx {
    float pos[4];
    float attr[4]; /* color rgba or uv+pad */
} virgl_vtx_t;

typedef struct res_slot {
    void    *guest;
    uint32_t bytes;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t res_id;
    uint32_t surf_id;  /* surface object (RT) */
    uint32_t sview_id; /* sampler view */
    uint32_t kind;     /* 0=rt 1=tex 2=buf */
} res_slot_t;

typedef struct virgl_state {
    virtio_ring_t    *ring;
    virtio_scanout_t *so;
    int               ready;

    gpu_tex_view_t rt;
    gpu_buf_view_t vb;
    gpu_buf_view_t ib;
    gpu_tex_view_t tex0;
    gpu_uniforms_t uniforms;
    uint32_t topology;
    uint32_t cull;
    uint32_t blend;
    uint32_t shade;
    uint8_t  depth_test;
    uint8_t  depth_write;
    float    vp_x, vp_y, vp_w, vp_h;
    float    vp_min_z, vp_max_z;
    int32_t  sc_x, sc_y, sc_w, sc_h;
    int      has_vp;
    int      has_scissor;
    int      has_uniforms;
    int      has_rt;
    int      has_vb;
    int      has_ib;
    int      has_tex0;

    uint32_t next_res;
    uint32_t next_surf;
    uint32_t next_sview;
    res_slot_t cache[RES_CACHE_MAX];
    int        cache_n;

    uint32_t vb_res;
    uint32_t ib_res;
    uint32_t vb_bytes;
    uint32_t ib_bytes;
    void    *vb_staging;
    uint32_t vb_staging_bytes;

    uint32_t cmd[CMDBUF_MAX_DWORDS];
    uint32_t cmd_n;
} virgl_state_t;

static virgl_state_t g_vg;

static void hdr_init(virtio_gpu_ctrl_hdr_t *h, uint32_t type)
{
    memset(h, 0, sizeof(*h));
    h->type = type;
    h->ctx_id = CTX_ID;
}

static int ring_cmd(virtio_ring_t *ring, const void *req, uint32_t req_len,
                    void *resp, uint32_t resp_len)
{
    virtio_gpu_ctrl_hdr_t *rh;
    if (virtio_ring_submit(ring, req, req_len, resp, resp_len) < 0)
        return -1;
    rh = (virtio_gpu_ctrl_hdr_t *)resp;
    if (rh->type < VIRTIO_GPU_RESP_OK_NODATA || rh->type >= 0x1200)
        return -1;
    return 0;
}

static int ring_cmd_nodata(virtio_ring_t *ring, const void *req, uint32_t req_len)
{
    virtio_gpu_ctrl_hdr_t resp;
    memset(&resp, 0, sizeof(resp));
    if (ring_cmd(ring, req, req_len, &resp, sizeof(resp)) < 0)
        return -1;
    return (resp.type == VIRTIO_GPU_RESP_OK_NODATA) ? 0 : -1;
}

static uint32_t f2u(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

static void mat4_mul(const float *a, const float *b, float *o)
{
    int c, r, k;
    for (c = 0; c < 4; c++) {
        for (r = 0; r < 4; r++) {
            float s = 0.0f;
            for (k = 0; k < 4; k++)
                s += a[k * 4 + r] * b[c * 4 + k];
            o[c * 4 + r] = s;
        }
    }
}

static void mat4_mul_vec4(const float *m, float x, float y, float z, float w, float *out)
{
    out[0] = m[0] * x + m[4] * y + m[8] * z + m[12] * w;
    out[1] = m[1] * x + m[5] * y + m[9] * z + m[13] * w;
    out[2] = m[2] * x + m[6] * y + m[10] * z + m[14] * w;
    out[3] = m[3] * x + m[7] * y + m[11] * z + m[15] * w;
}

static void cmd_reset(void)
{
    g_vg.cmd_n = 0;
}

static int cmd_push(uint32_t v)
{
    if (g_vg.cmd_n >= CMDBUF_MAX_DWORDS)
        return -1;
    g_vg.cmd[g_vg.cmd_n++] = v;
    return 0;
}

static int submit_3d(void)
{
    uint8_t *pkt;
    virtio_gpu_cmd_submit_t *sub;
    uint32_t bytes = g_vg.cmd_n * 4u;
    uint32_t total;
    virtio_gpu_ctrl_hdr_t resp;

    if (g_vg.cmd_n == 0)
        return 0;
    total = (uint32_t)sizeof(*sub) + bytes;
    pkt = (uint8_t *)kmalloc(total);
    if (!pkt)
        return -1;
    memset(pkt, 0, total);
    sub = (virtio_gpu_cmd_submit_t *)pkt;
    hdr_init(&sub->hdr, VIRTIO_GPU_CMD_SUBMIT_3D);
    sub->size = bytes;
    memcpy(pkt + sizeof(*sub), g_vg.cmd, bytes);
    memset(&resp, 0, sizeof(resp));
    if (ring_cmd(g_vg.ring, pkt, total, &resp, sizeof(resp)) < 0) {
        kfree(pkt);
        return -1;
    }
    kfree(pkt);
    g_vg.cmd_n = 0;
    return (resp.type == VIRTIO_GPU_RESP_OK_NODATA) ? 0 : -1;
}

static int ctx_create(void)
{
    virtio_gpu_ctx_create_t req;
    memset(&req, 0, sizeof(req));
    hdr_init(&req.hdr, VIRTIO_GPU_CMD_CTX_CREATE);
    req.nlen = 5;
    memcpy(req.debug_name, "mkgl", 5);
    return ring_cmd_nodata(g_vg.ring, &req, sizeof(req));
}

static int ctx_attach(uint32_t res_id)
{
    virtio_gpu_ctx_resource_t req;
    memset(&req, 0, sizeof(req));
    hdr_init(&req.hdr, VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE);
    req.resource_id = res_id;
    return ring_cmd_nodata(g_vg.ring, &req, sizeof(req));
}

static int res_create_3d(uint32_t res_id, uint32_t target, uint32_t format,
                         uint32_t bind, uint32_t w, uint32_t h, uint32_t d)
{
    virtio_gpu_resource_create_3d_t req;
    memset(&req, 0, sizeof(req));
    hdr_init(&req.hdr, VIRTIO_GPU_CMD_RESOURCE_CREATE_3D);
    req.resource_id = res_id;
    req.target = target;
    req.format = format;
    req.bind = bind;
    req.width = w;
    req.height = h;
    req.depth = d;
    req.array_size = 1;
    req.last_level = 0;
    req.nr_samples = 0;
    req.flags = (target == PIPE_TEXTURE_2D) ? VIRTIO_GPU_RESOURCE_FLAG_Y_0_TOP : 0;
    return ring_cmd_nodata(g_vg.ring, &req, sizeof(req));
}

static int res_attach_backing(uint32_t res_id, void *ptr, uint32_t bytes)
{
    attach_req_t attach;
    memset(&attach, 0, sizeof(attach));
    hdr_init(&attach.ab.hdr, VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING);
    attach.ab.resource_id = res_id;
    attach.ab.nr_entries = 1;
    attach.entry.addr = (uint64_t)(uintptr_t)ptr;
    attach.entry.length = bytes;
    return ring_cmd_nodata(g_vg.ring, &attach, sizeof(attach));
}

static int transfer_3d(uint32_t type, uint32_t res_id, uint32_t x, uint32_t y,
                       uint32_t w, uint32_t h, uint32_t stride)
{
    virtio_gpu_transfer_host_3d_t req;
    memset(&req, 0, sizeof(req));
    hdr_init(&req.hdr, type);
    req.box.x = x;
    req.box.y = y;
    req.box.z = 0;
    req.box.w = w;
    req.box.h = h;
    req.box.d = 1;
    req.offset = 0;
    req.resource_id = res_id;
    req.level = 0;
    req.stride = stride;
    req.layer_stride = 0;
    return ring_cmd_nodata(g_vg.ring, &req, sizeof(req));
}

static int transfer_buf_to_host(uint32_t res_id, uint32_t bytes)
{
    virtio_gpu_transfer_host_3d_t req;
    memset(&req, 0, sizeof(req));
    hdr_init(&req.hdr, VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D);
    req.box.x = 0;
    req.box.y = 0;
    req.box.z = 0;
    req.box.w = bytes;
    req.box.h = 1;
    req.box.d = 1;
    req.offset = 0;
    req.resource_id = res_id;
    req.level = 0;
    req.stride = 0;
    req.layer_stride = 0;
    return ring_cmd_nodata(g_vg.ring, &req, sizeof(req));
}

/* ---- VirGL object / state helpers ---- */

static int emit_create_blend(uint32_t handle, int alpha)
{
    uint32_t s2 = 0;
    uint32_t i;
    if (cmd_push(VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_BLEND, 11)) < 0)
        return -1;
    if (cmd_push(handle) < 0 || cmd_push(0) < 0 || cmd_push(0) < 0)
        return -1;
    if (alpha) {
        s2 = (1u << 0) /* enable */
           | ((uint32_t)PIPE_BLEND_ADD << 1)
           | ((uint32_t)PIPE_BLENDFACTOR_SRC_ALPHA << 4)
           | ((uint32_t)PIPE_BLENDFACTOR_INV_SRC_ALPHA << 9)
           | ((uint32_t)PIPE_BLEND_ADD << 14)
           | ((uint32_t)PIPE_BLENDFACTOR_ONE << 17)
           | ((uint32_t)PIPE_BLENDFACTOR_INV_SRC_ALPHA << 22)
           | ((uint32_t)PIPE_MASK_RGBA << 27);
    } else {
        s2 = ((uint32_t)PIPE_MASK_RGBA << 27);
    }
    for (i = 0; i < 8; i++) {
        if (cmd_push(i == 0 ? s2 : 0) < 0)
            return -1;
    }
    return 0;
}

static int emit_create_rasterizer(uint32_t handle)
{
    uint32_t s0 = 0;
    s0 |= (1u << 1);  /* depth_clip */
    s0 |= (1u << 14); /* scissor */
    s0 |= (1u << 29); /* half_pixel_center */
    s0 |= (1u << 30); /* bottom_edge_rule */
    if (cmd_push(VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_RASTERIZER, 9)) < 0)
        return -1;
    if (cmd_push(handle) < 0 || cmd_push(s0) < 0)
        return -1;
    if (cmd_push(f2u(1.0f)) < 0) /* point_size */
        return -1;
    if (cmd_push(0) < 0 || cmd_push(0) < 0)
        return -1;
    if (cmd_push(f2u(1.0f)) < 0) /* line_width */
        return -1;
    if (cmd_push(f2u(0.0f)) < 0 || cmd_push(f2u(0.0f)) < 0 || cmd_push(f2u(0.0f)) < 0)
        return -1;
    return 0;
}

static int emit_create_dsa(uint32_t handle)
{
    if (cmd_push(VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_DSA, 5)) < 0)
        return -1;
    if (cmd_push(handle) < 0)
        return -1;
    if (cmd_push(0) < 0 || cmd_push(0) < 0 || cmd_push(0) < 0)
        return -1;
    if (cmd_push(f2u(0.0f)) < 0)
        return -1;
    return 0;
}

static int emit_create_ve(uint32_t handle)
{
    /* float4 pos @0 + float4 attr @16 */
    if (cmd_push(VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_VERTEX_ELEMENTS, 9)) < 0)
        return -1;
    if (cmd_push(handle) < 0)
        return -1;
    if (cmd_push(0) < 0 || cmd_push(0) < 0 || cmd_push(0) < 0)
        return -1;
    if (cmd_push(VIRGL_FORMAT_R32G32B32A32_FLOAT) < 0)
        return -1;
    if (cmd_push(16) < 0 || cmd_push(0) < 0 || cmd_push(0) < 0)
        return -1;
    if (cmd_push(VIRGL_FORMAT_R32G32B32A32_FLOAT) < 0)
        return -1;
    return 0;
}

static int emit_create_shader(uint32_t handle, uint32_t type, const char *text)
{
    uint32_t slen = (uint32_t)strlen(text) + 1u;
    uint32_t ndw = (slen + 3u) / 4u;
    uint32_t len = 5u + ndw;
    uint32_t i;
    const uint8_t *src = (const uint8_t *)text;

    if (cmd_push(VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_SHADER, len)) < 0)
        return -1;
    if (cmd_push(handle) < 0)
        return -1;
    if (cmd_push(type) < 0)
        return -1;
    if (cmd_push(slen) < 0) /* offlen = string bytes */
        return -1;
    if (cmd_push(50) < 0) /* num_tokens (hint) */
        return -1;
    if (cmd_push(0) < 0) /* so outputs */
        return -1;
    for (i = 0; i < ndw; i++) {
        uint32_t w = 0;
        uint32_t b;
        for (b = 0; b < 4; b++) {
            uint32_t off = i * 4u + b;
            if (off < slen)
                w |= ((uint32_t)src[off]) << (b * 8);
        }
        if (cmd_push(w) < 0)
            return -1;
    }
    return 0;
}

static int emit_create_sampler(uint32_t handle)
{
    uint32_t s0 = 0;
    s0 |= (PIPE_TEX_WRAP_CLAMP_TO_EDGE << 0);
    s0 |= (PIPE_TEX_WRAP_CLAMP_TO_EDGE << 3);
    s0 |= (PIPE_TEX_WRAP_CLAMP_TO_EDGE << 6);
    s0 |= (PIPE_TEX_FILTER_LINEAR << 9);
    s0 |= (PIPE_TEX_MIPFILTER_NONE << 11);
    s0 |= (PIPE_TEX_FILTER_LINEAR << 13);
    if (cmd_push(VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_SAMPLER_STATE, 9)) < 0)
        return -1;
    if (cmd_push(handle) < 0 || cmd_push(s0) < 0)
        return -1;
    if (cmd_push(f2u(0.0f)) < 0 || cmd_push(f2u(0.0f)) < 0 || cmd_push(f2u(1000.0f)) < 0)
        return -1;
    if (cmd_push(0) < 0 || cmd_push(0) < 0 || cmd_push(0) < 0 || cmd_push(0) < 0)
        return -1;
    return 0;
}

static int emit_bind_obj(uint32_t type, uint32_t handle)
{
    if (cmd_push(VIRGL_CMD0(VIRGL_CCMD_BIND_OBJECT, type, 1)) < 0)
        return -1;
    return cmd_push(handle);
}

static int emit_bind_shader(uint32_t handle, uint32_t type)
{
    if (cmd_push(VIRGL_CMD0(VIRGL_CCMD_BIND_SHADER, 0, 2)) < 0)
        return -1;
    if (cmd_push(handle) < 0)
        return -1;
    return cmd_push(type);
}

static int emit_sub_ctx(void)
{
    if (cmd_push(VIRGL_CMD0(VIRGL_CCMD_CREATE_SUB_CTX, 0, 1)) < 0)
        return -1;
    if (cmd_push(SUB_CTX_ID) < 0)
        return -1;
    if (cmd_push(VIRGL_CMD0(VIRGL_CCMD_SET_SUB_CTX, 0, 1)) < 0)
        return -1;
    return cmd_push(SUB_CTX_ID);
}

static const char *vs_color_text =
    "VERT\n"
    "DCL IN[0]\n"
    "DCL IN[1]\n"
    "DCL OUT[0], POSITION\n"
    "DCL OUT[1], COLOR\n"
    " 0: MOV OUT[1], IN[1]\n"
    " 1: MOV OUT[0], IN[0]\n"
    " 2: END\n";

static const char *fs_color_text =
    "FRAG\n"
    "DCL IN[0], COLOR, LINEAR\n"
    "DCL OUT[0], COLOR\n"
    " 0: MOV OUT[0], IN[0]\n"
    " 1: END\n";

static const char *vs_tex_text =
    "VERT\n"
    "DCL IN[0]\n"
    "DCL IN[1]\n"
    "DCL OUT[0], POSITION\n"
    "DCL OUT[1], GENERIC[0]\n"
    " 0: MOV OUT[1], IN[1]\n"
    " 1: MOV OUT[0], IN[0]\n"
    " 2: END\n";

static const char *fs_tex_text =
    "FRAG\n"
    "DCL IN[0], GENERIC[0], LINEAR\n"
    "DCL OUT[0], COLOR\n"
    "DCL SAMP[0]\n"
    "DCL SVIEW[0], 2D, FLOAT\n"
    " 0: TEX OUT[0], IN[0], SAMP[0], 2D\n"
    " 1: END\n";

static int pipeline_create(void)
{
    cmd_reset();
    if (emit_sub_ctx() < 0)
        return -1;
    if (emit_create_blend(OBJ_BLEND_OPAQUE, 0) < 0)
        return -1;
    if (emit_create_blend(OBJ_BLEND_ALPHA, 1) < 0)
        return -1;
    if (emit_create_rasterizer(OBJ_RASTERIZER) < 0)
        return -1;
    if (emit_create_dsa(OBJ_DSA) < 0)
        return -1;
    if (emit_create_ve(OBJ_VE) < 0)
        return -1;
    if (emit_create_shader(OBJ_VS_COLOR, PIPE_SHADER_VERTEX, vs_color_text) < 0)
        return -1;
    if (emit_create_shader(OBJ_FS_COLOR, PIPE_SHADER_FRAGMENT, fs_color_text) < 0)
        return -1;
    if (emit_create_shader(OBJ_VS_TEX, PIPE_SHADER_VERTEX, vs_tex_text) < 0)
        return -1;
    if (emit_create_shader(OBJ_FS_TEX, PIPE_SHADER_FRAGMENT, fs_tex_text) < 0)
        return -1;
    if (emit_create_sampler(OBJ_SAMPLER) < 0)
        return -1;

    if (emit_bind_obj(VIRGL_OBJECT_BLEND, OBJ_BLEND_OPAQUE) < 0)
        return -1;
    if (emit_bind_obj(VIRGL_OBJECT_RASTERIZER, OBJ_RASTERIZER) < 0)
        return -1;
    if (emit_bind_obj(VIRGL_OBJECT_DSA, OBJ_DSA) < 0)
        return -1;
    if (emit_bind_obj(VIRGL_OBJECT_VERTEX_ELEMENTS, OBJ_VE) < 0)
        return -1;
    if (emit_bind_shader(OBJ_VS_COLOR, PIPE_SHADER_VERTEX) < 0)
        return -1;
    if (emit_bind_shader(OBJ_FS_COLOR, PIPE_SHADER_FRAGMENT) < 0)
        return -1;
    return submit_3d();
}

static res_slot_t *res_find(void *guest)
{
    int i;
    for (i = 0; i < g_vg.cache_n; i++) {
        if (g_vg.cache[i].guest == guest)
            return &g_vg.cache[i];
    }
    return NULL;
}

static res_slot_t *res_alloc_slot(void)
{
    if (g_vg.cache_n >= RES_CACHE_MAX)
        return NULL;
    return &g_vg.cache[g_vg.cache_n++];
}

static int ensure_tex_res(const gpu_tex_view_t *tv, int is_rt, res_slot_t **out)
{
    res_slot_t *s;
    uint32_t bytes;
    uint32_t bind;
    uint32_t res_id;

    if (!tv || !tv->data || tv->width == 0 || tv->height == 0)
        return -1;
    bytes = tv->stride * tv->height;
    if (bytes == 0)
        bytes = tv->width * tv->height * 4u;

    s = res_find(tv->data);
    if (s && s->width == tv->width && s->height == tv->height && s->bytes >= bytes) {
        *out = s;
        return 0;
    }

    s = res_alloc_slot();
    if (!s)
        return -1;
    memset(s, 0, sizeof(*s));
    res_id = g_vg.next_res++;
    bind = VIRGL_BIND_SAMPLER_VIEW;
    if (is_rt)
        bind |= VIRGL_BIND_RENDER_TARGET;

    if (res_create_3d(res_id, PIPE_TEXTURE_2D, VIRGL_FORMAT_B8G8R8A8_UNORM,
                      bind, tv->width, tv->height, 1) < 0)
        return -1;
    if (res_attach_backing(res_id, tv->data, bytes) < 0)
        return -1;
    if (ctx_attach(res_id) < 0)
        return -1;

    /* Upload current guest contents to host. */
    if (transfer_3d(VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D, res_id,
                    0, 0, tv->width, tv->height, tv->stride) < 0)
        return -1;

    s->guest = tv->data;
    s->bytes = bytes;
    s->width = tv->width;
    s->height = tv->height;
    s->stride = tv->stride ? tv->stride : tv->width * 4u;
    s->res_id = res_id;
    s->kind = is_rt ? 0u : 1u;
    s->surf_id = 0;
    s->sview_id = 0;
    *out = s;
    return 0;
}

static int ensure_surface(res_slot_t *s)
{
    if (s->surf_id)
        return 0;
    s->surf_id = g_vg.next_surf++;
    cmd_reset();
    if (cmd_push(VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_SURFACE, 5)) < 0)
        return -1;
    if (cmd_push(s->surf_id) < 0)
        return -1;
    if (cmd_push(s->res_id) < 0)
        return -1;
    if (cmd_push(VIRGL_FORMAT_B8G8R8A8_UNORM) < 0)
        return -1;
    if (cmd_push(0) < 0) /* level */
        return -1;
    /* first/last layer packed: first=0 last=0 */
    if (cmd_push(0) < 0)
        return -1;
    return submit_3d();
}

static int ensure_sview(res_slot_t *s)
{
    uint32_t swz;
    if (s->sview_id)
        return 0;
    s->sview_id = g_vg.next_sview++;
    swz = (0u) | (1u << 3) | (2u << 6) | (3u << 9); /* XYZW */
    cmd_reset();
    if (cmd_push(VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_SAMPLER_VIEW, 6)) < 0)
        return -1;
    if (cmd_push(s->sview_id) < 0)
        return -1;
    if (cmd_push(s->res_id) < 0)
        return -1;
    if (cmd_push(VIRGL_FORMAT_B8G8R8A8_UNORM) < 0)
        return -1;
    if (cmd_push(0) < 0) /* layer first/last */
        return -1;
    if (cmd_push(0) < 0) /* level first/last */
        return -1;
    if (cmd_push(swz) < 0)
        return -1;
    return submit_3d();
}

static int emit_set_framebuffer(uint32_t surf_id)
{
    if (cmd_push(VIRGL_CMD0(VIRGL_CCMD_SET_FRAMEBUFFER_STATE, 0, 3)) < 0)
        return -1;
    if (cmd_push(1) < 0) /* nr_cbufs */
        return -1;
    if (cmd_push(0) < 0) /* zsurf */
        return -1;
    return cmd_push(surf_id);
}

static int emit_set_viewport(float x, float y, float w, float h, float z0, float z1)
{
    float sx = w * 0.5f;
    float sy = h * 0.5f;
    float sz = (z1 - z0) * 0.5f;
    float tx = x + sx;
    float ty = y + sy;
    float tz = z0 + sz;
    if (cmd_push(VIRGL_CMD0(VIRGL_CCMD_SET_VIEWPORT_STATE, 0, 7)) < 0)
        return -1;
    if (cmd_push(0) < 0)
        return -1;
    if (cmd_push(f2u(sx)) < 0 || cmd_push(f2u(sy)) < 0 || cmd_push(f2u(sz)) < 0)
        return -1;
    if (cmd_push(f2u(tx)) < 0 || cmd_push(f2u(ty)) < 0 || cmd_push(f2u(tz)) < 0)
        return -1;
    return 0;
}

static int emit_set_scissor(int32_t x, int32_t y, int32_t w, int32_t h)
{
    uint32_t minxy, maxxy;
    int32_t x1 = x + w;
    int32_t y1 = y + h;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x1 < x)
        x1 = x;
    if (y1 < y)
        y1 = y;
    minxy = ((uint32_t)(uint16_t)x) | (((uint32_t)(uint16_t)y) << 16);
    maxxy = ((uint32_t)(uint16_t)x1) | (((uint32_t)(uint16_t)y1) << 16);
    if (cmd_push(VIRGL_CMD0(VIRGL_CCMD_SET_SCISSOR_STATE, 0, 3)) < 0)
        return -1;
    if (cmd_push(0) < 0)
        return -1;
    if (cmd_push(minxy) < 0)
        return -1;
    return cmd_push(maxxy);
}

static int emit_clear_color(uint32_t rgba)
{
    float r = ((rgba >> 0) & 0xff) / 255.0f;
    float g = ((rgba >> 8) & 0xff) / 255.0f;
    float b = ((rgba >> 16) & 0xff) / 255.0f;
    float a = ((rgba >> 24) & 0xff) / 255.0f;
    if (cmd_push(VIRGL_CMD0(VIRGL_CCMD_CLEAR, 0, 8)) < 0)
        return -1;
    if (cmd_push(PIPE_CLEAR_COLOR0) < 0)
        return -1;
    if (cmd_push(f2u(r)) < 0 || cmd_push(f2u(g)) < 0 ||
        cmd_push(f2u(b)) < 0 || cmd_push(f2u(a)) < 0)
        return -1;
    if (cmd_push(0) < 0 || cmd_push(0) < 0) /* depth double = 0.0 */
        return -1;
    return cmd_push(0); /* stencil */
}

static int apply_pipeline_binds(void)
{
    uint32_t blend = (g_vg.blend == 1 || g_vg.blend == 2) ? OBJ_BLEND_ALPHA : OBJ_BLEND_OPAQUE;
    int tex = (g_vg.shade == 1) && g_vg.has_tex0;

    if (emit_bind_obj(VIRGL_OBJECT_BLEND, blend) < 0)
        return -1;
    if (emit_bind_obj(VIRGL_OBJECT_RASTERIZER, OBJ_RASTERIZER) < 0)
        return -1;
    if (emit_bind_obj(VIRGL_OBJECT_DSA, OBJ_DSA) < 0)
        return -1;
    if (emit_bind_obj(VIRGL_OBJECT_VERTEX_ELEMENTS, OBJ_VE) < 0)
        return -1;
    if (tex) {
        if (emit_bind_shader(OBJ_VS_TEX, PIPE_SHADER_VERTEX) < 0)
            return -1;
        if (emit_bind_shader(OBJ_FS_TEX, PIPE_SHADER_FRAGMENT) < 0)
            return -1;
    } else {
        if (emit_bind_shader(OBJ_VS_COLOR, PIPE_SHADER_VERTEX) < 0)
            return -1;
        if (emit_bind_shader(OBJ_FS_COLOR, PIPE_SHADER_FRAGMENT) < 0)
            return -1;
    }
    return 0;
}

static int ensure_vb_res(uint32_t need_bytes)
{
    if (g_vg.vb_res && g_vg.vb_bytes >= need_bytes && g_vg.vb_staging)
        return 0;
    if (g_vg.vb_staging) {
        kfree(g_vg.vb_staging);
        g_vg.vb_staging = NULL;
        g_vg.vb_staging_bytes = 0;
    }
    g_vg.vb_res = g_vg.next_res++;
    g_vg.vb_staging = kmalloc_aligned(need_bytes, 4096);
    if (!g_vg.vb_staging)
        return -1;
    g_vg.vb_staging_bytes = need_bytes;
    memset(g_vg.vb_staging, 0, need_bytes);
    if (res_create_3d(g_vg.vb_res, PIPE_BUFFER, VIRGL_FORMAT_R8_UNORM,
                      VIRGL_BIND_VERTEX_BUFFER, need_bytes, 1, 1) < 0)
        return -1;
    if (res_attach_backing(g_vg.vb_res, g_vg.vb_staging, need_bytes) < 0)
        return -1;
    if (ctx_attach(g_vg.vb_res) < 0)
        return -1;
    g_vg.vb_bytes = need_bytes;
    return 0;
}

static int ensure_ib_res(uint32_t need_bytes)
{
    if (g_vg.ib_res && g_vg.ib_bytes >= need_bytes)
        return 0;
    g_vg.ib_res = g_vg.next_res++;
    if (res_create_3d(g_vg.ib_res, PIPE_BUFFER, VIRGL_FORMAT_R16_UINT,
                      VIRGL_BIND_INDEX_BUFFER, need_bytes, 1, 1) < 0)
        return -1;
    g_vg.ib_bytes = need_bytes;
    return 0;
}

static void rgba_u32_to_f4(uint32_t c, float *o)
{
    o[0] = ((c >> 0) & 0xff) / 255.0f;
    o[1] = ((c >> 8) & 0xff) / 255.0f;
    o[2] = ((c >> 16) & 0xff) / 255.0f;
    o[3] = ((c >> 24) & 0xff) / 255.0f;
}

static int prep_vertices(uint32_t first, uint32_t count, int textured)
{
    const uint8_t *src;
    virgl_vtx_t *dst;
    float mvp[16], mv[16];
    uint32_t i;
    uint32_t need;

    if (!g_vg.has_vb || !g_vg.vb.data || count == 0)
        return -1;
    need = count * (uint32_t)sizeof(virgl_vtx_t);
    if (ensure_vb_res(need) < 0)
        return -1;

    if (g_vg.has_uniforms) {
        mat4_mul(g_vg.uniforms.view, g_vg.uniforms.model, mv);
        mat4_mul(g_vg.uniforms.proj, mv, mvp);
    } else {
        memset(mvp, 0, sizeof(mvp));
        mvp[0] = mvp[5] = mvp[10] = mvp[15] = 1.0f;
    }

    src = (const uint8_t *)g_vg.vb.data + first * REED_VERT_STRIDE;
    dst = (virgl_vtx_t *)g_vg.vb_staging;
    for (i = 0; i < count; i++) {
        const float *p = (const float *)(src + i * REED_VERT_STRIDE);
        float x = p[0], y = p[1], z = p[2];
        float u = p[6], v = p[7];
        uint32_t col;
        memcpy(&col, src + i * REED_VERT_STRIDE + 32, 4);
        mat4_mul_vec4(mvp, x, y, z, 1.0f, dst[i].pos);
        if (textured) {
            dst[i].attr[0] = u;
            dst[i].attr[1] = v;
            dst[i].attr[2] = 0.0f;
            dst[i].attr[3] = 1.0f;
        } else {
            rgba_u32_to_f4(col, dst[i].attr);
        }
    }
    return transfer_buf_to_host(g_vg.vb_res, need);
}

static int do_clear(const gpu_cmd_clear_t *c)
{
    res_slot_t *rt;
    if (!g_vg.has_rt)
        return -1;
    if (ensure_tex_res(&g_vg.rt, 1, &rt) < 0)
        return -1;
    if (ensure_surface(rt) < 0)
        return -1;

    cmd_reset();
    if (emit_set_framebuffer(rt->surf_id) < 0)
        return -1;
    if (g_vg.has_scissor) {
        if (emit_set_scissor(g_vg.sc_x, g_vg.sc_y, g_vg.sc_w, g_vg.sc_h) < 0)
            return -1;
    } else {
        if (emit_set_scissor(0, 0, (int32_t)rt->width, (int32_t)rt->height) < 0)
            return -1;
    }
    if (emit_clear_color(c->color_rgba) < 0)
        return -1;
    if (submit_3d() < 0)
        return -1;

    /* Host → guest so later blit/present sees the cleared RT. */
    return transfer_3d(VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D, rt->res_id,
                       0, 0, rt->width, rt->height, rt->stride);
}

static int do_blit(const gpu_cmd_blit_t *c)
{
    res_slot_t *src, *dst;
    int32_t sw, sh;

    if (ensure_tex_res(&c->src, 0, &src) < 0)
        return -1;
    if (ensure_tex_res(&c->dst, 1, &dst) < 0)
        return -1;

    sw = c->src_w > 0 ? c->src_w : (int32_t)c->src.width;
    sh = c->src_h > 0 ? c->src_h : (int32_t)c->src.height;
    if (sw <= 0 || sh <= 0)
        return -1;

    /* Re-upload src (guest may have changed). */
    if (transfer_3d(VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D, src->res_id,
                    0, 0, src->width, src->height, src->stride) < 0)
        return -1;

    cmd_reset();
    if (cmd_push(VIRGL_CMD0(VIRGL_CCMD_RESOURCE_COPY_REGION, 0, 13)) < 0)
        return -1;
    if (cmd_push(dst->res_id) < 0 || cmd_push(0) < 0)
        return -1;
    if (cmd_push((uint32_t)c->dst_x) < 0 || cmd_push((uint32_t)c->dst_y) < 0 || cmd_push(0) < 0)
        return -1;
    if (cmd_push(src->res_id) < 0 || cmd_push(0) < 0)
        return -1;
    if (cmd_push((uint32_t)c->src_x) < 0 || cmd_push((uint32_t)c->src_y) < 0 || cmd_push(0) < 0)
        return -1;
    if (cmd_push((uint32_t)sw) < 0 || cmd_push((uint32_t)sh) < 0 || cmd_push(1) < 0)
        return -1;
    if (submit_3d() < 0)
        return -1;

    return transfer_3d(VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D, dst->res_id,
                       0, 0, dst->width, dst->height, dst->stride);
}

static int do_draw(uint32_t count, uint32_t first, int indexed,
                   uint32_t index_count, uint32_t first_index, int32_t base_vertex)
{
    res_slot_t *rt;
    res_slot_t *tex = NULL;
    int textured;
    uint32_t draw_count;
    float vpw, vph;

    (void)base_vertex;
    if (!g_vg.has_rt || count == 0)
        return -1;
    textured = (g_vg.shade == 1) && g_vg.has_tex0;
    draw_count = indexed ? index_count : count;
    if (draw_count == 0)
        return -1;

    if (ensure_tex_res(&g_vg.rt, 1, &rt) < 0)
        return -1;
    if (ensure_surface(rt) < 0)
        return -1;
    if (textured) {
        if (ensure_tex_res(&g_vg.tex0, 0, &tex) < 0)
            return -1;
        if (ensure_sview(tex) < 0)
            return -1;
        if (transfer_3d(VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D, tex->res_id,
                        0, 0, tex->width, tex->height, tex->stride) < 0)
            return -1;
    }

    if (indexed) {
        uint32_t nvert = g_vg.vb.size / REED_VERT_STRIDE;
        if (nvert == 0 || nvert > STAGING_MAX_VERTS)
            return -1;
        if (prep_vertices(0, nvert, textured) < 0)
            return -1;
    } else {
        if (count > STAGING_MAX_VERTS)
            return -1;
        if (prep_vertices(first, count, textured) < 0)
            return -1;
    }

    if (indexed) {
        if (!g_vg.has_ib || !g_vg.ib.data)
            return -1;
        if (ensure_ib_res(g_vg.ib.size) < 0)
            return -1;
        if (res_attach_backing(g_vg.ib_res, g_vg.ib.data, g_vg.ib.size) < 0)
            return -1;
        if (ctx_attach(g_vg.ib_res) < 0)
            return -1;
        if (transfer_buf_to_host(g_vg.ib_res, g_vg.ib.size) < 0)
            return -1;
    }

    vpw = g_vg.has_vp ? g_vg.vp_w : (float)rt->width;
    vph = g_vg.has_vp ? g_vg.vp_h : (float)rt->height;

    cmd_reset();
    if (emit_set_framebuffer(rt->surf_id) < 0)
        return -1;
    if (apply_pipeline_binds() < 0)
        return -1;
    if (emit_set_viewport(g_vg.has_vp ? g_vg.vp_x : 0.0f,
                          g_vg.has_vp ? g_vg.vp_y : 0.0f,
                          vpw, vph,
                          g_vg.has_vp ? g_vg.vp_min_z : 0.0f,
                          g_vg.has_vp ? g_vg.vp_max_z : 1.0f) < 0)
        return -1;
    if (g_vg.has_scissor) {
        if (emit_set_scissor(g_vg.sc_x, g_vg.sc_y, g_vg.sc_w, g_vg.sc_h) < 0)
            return -1;
    } else {
        if (emit_set_scissor(0, 0, (int32_t)rt->width, (int32_t)rt->height) < 0)
            return -1;
    }

    /* SET_VERTEX_BUFFERS: stride, offset, handle */
    if (cmd_push(VIRGL_CMD0(VIRGL_CCMD_SET_VERTEX_BUFFERS, 0, 3)) < 0)
        return -1;
    if (cmd_push((uint32_t)sizeof(virgl_vtx_t)) < 0)
        return -1;
    if (cmd_push(0) < 0)
        return -1;
    if (cmd_push(g_vg.vb_res) < 0)
        return -1;

    if (indexed) {
        if (cmd_push(VIRGL_CMD0(VIRGL_CCMD_SET_INDEX_BUFFER, 0, 3)) < 0)
            return -1;
        if (cmd_push(g_vg.ib_res) < 0)
            return -1;
        if (cmd_push(2) < 0) /* index_size = 2 (uint16) */
            return -1;
        if (cmd_push(first_index * 2u) < 0)
            return -1;
    }

    if (textured && tex) {
        if (cmd_push(VIRGL_CMD0(VIRGL_CCMD_BIND_SAMPLER_STATES, 0, 3)) < 0)
            return -1;
        if (cmd_push(PIPE_SHADER_FRAGMENT) < 0 || cmd_push(0) < 0)
            return -1;
        if (cmd_push(OBJ_SAMPLER) < 0)
            return -1;
        if (cmd_push(VIRGL_CMD0(VIRGL_CCMD_SET_SAMPLER_VIEWS, 0, 3)) < 0)
            return -1;
        if (cmd_push(PIPE_SHADER_FRAGMENT) < 0 || cmd_push(0) < 0)
            return -1;
        if (cmd_push(tex->sview_id) < 0)
            return -1;
    }

    if (cmd_push(VIRGL_CMD0(VIRGL_CCMD_DRAW_VBO, 0, 12)) < 0)
        return -1;
    /* Staging VB always starts at element 0 after prep_vertices. */
    if (cmd_push(0) < 0)
        return -1;
    if (cmd_push(draw_count) < 0)
        return -1;
    if (cmd_push(PIPE_PRIM_TRIANGLES) < 0)
        return -1;
    if (cmd_push(indexed ? 1u : 0u) < 0)
        return -1;
    if (cmd_push(1) < 0) /* instance_count */
        return -1;
    if (cmd_push((uint32_t)base_vertex) < 0)
        return -1;
    if (cmd_push(0) < 0) /* start_instance */
        return -1;
    if (cmd_push(0) < 0 || cmd_push(0) < 0) /* restart */
        return -1;
    if (cmd_push(0) < 0 || cmd_push(0xffffffffu) < 0) /* min/max index */
        return -1;
    if (cmd_push(0) < 0) /* count_from_so */
        return -1;

    if (submit_3d() < 0)
        return -1;

    return transfer_3d(VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D, rt->res_id,
                       0, 0, rt->width, rt->height, rt->stride);
}

int virtio_virgl_ready(void)
{
    return g_vg.ready;
}

void virtio_virgl_shutdown(void)
{
    if (g_vg.vb_staging)
        kfree(g_vg.vb_staging);
    memset(&g_vg, 0, sizeof(g_vg));
}

int virtio_virgl_init(virtio_ring_t *ring, virtio_scanout_t *so)
{
    if (!ring || !so)
        return -1;

    memset(&g_vg, 0, sizeof(g_vg));
    g_vg.ring = ring;
    g_vg.so = so;
    g_vg.next_res = RES_ID_BASE;
    g_vg.next_surf = OBJ_SURF_BASE;
    g_vg.next_sview = OBJ_SVIEW_BASE;

    if (ctx_create() < 0) {
        vga_print("virgl: CTX_CREATE failed\n");
        return -1;
    }
    if (pipeline_create() < 0) {
        vga_print("virgl: pipeline create failed\n");
        return -1;
    }

    g_vg.ready = 1;
    vga_print("virgl: ring SUBMIT_3D ready\n");
    return 0;
}

int virtio_virgl_exec(const void *gpu_cmds, uint32_t size)
{
    const uint8_t *p = (const uint8_t *)gpu_cmds;
    const uint8_t *end;

    if (!g_vg.ready || !gpu_cmds || size < sizeof(gpu_cmd_hdr_t))
        return -1;
    end = p + size;

    while (p + sizeof(gpu_cmd_hdr_t) <= end) {
        const gpu_cmd_hdr_t *h = (const gpu_cmd_hdr_t *)p;
        uint32_t psz;

        if (h->size < sizeof(gpu_cmd_hdr_t) || (h->size & 3u))
            return -1;
        psz = h->size;
        if (p + psz > end)
            return -1;

        switch (h->op) {
        case GPU_CMD_BIND_PIPELINE: {
            const gpu_cmd_bind_pipeline_t *c = (const gpu_cmd_bind_pipeline_t *)p;
            if (psz < sizeof(*c))
                return -1;
            g_vg.topology = c->topology;
            g_vg.cull = c->cull;
            g_vg.blend = c->blend;
            g_vg.shade = c->shade;
            g_vg.depth_test = c->depth_test;
            g_vg.depth_write = c->depth_write;
            break;
        }
        case GPU_CMD_BIND_VB: {
            const gpu_cmd_bind_vb_t *c = (const gpu_cmd_bind_vb_t *)p;
            if (psz < sizeof(*c))
                return -1;
            g_vg.vb = c->vb;
            g_vg.has_vb = c->vb.data ? 1 : 0;
            break;
        }
        case GPU_CMD_BIND_IB: {
            const gpu_cmd_bind_ib_t *c = (const gpu_cmd_bind_ib_t *)p;
            if (psz < sizeof(*c))
                return -1;
            g_vg.ib = c->ib;
            g_vg.has_ib = c->ib.data ? 1 : 0;
            break;
        }
        case GPU_CMD_BIND_TEX: {
            const gpu_cmd_bind_tex_t *c = (const gpu_cmd_bind_tex_t *)p;
            if (psz < sizeof(*c))
                return -1;
            if (c->slot == 0) {
                g_vg.tex0 = c->tex;
                g_vg.has_tex0 = c->tex.data ? 1 : 0;
            }
            break;
        }
        case GPU_CMD_BIND_RT: {
            const gpu_cmd_bind_rt_t *c = (const gpu_cmd_bind_rt_t *)p;
            if (psz < sizeof(*c))
                return -1;
            g_vg.rt = c->color;
            g_vg.has_rt = c->color.data ? 1 : 0;
            break;
        }
        case GPU_CMD_SET_UNIFORM: {
            const gpu_cmd_set_uniform_t *c = (const gpu_cmd_set_uniform_t *)p;
            if (psz < sizeof(*c))
                return -1;
            g_vg.uniforms = c->u;
            g_vg.has_uniforms = 1;
            break;
        }
        case GPU_CMD_SET_VIEWPORT: {
            const gpu_cmd_set_viewport_t *c = (const gpu_cmd_set_viewport_t *)p;
            if (psz < sizeof(*c))
                return -1;
            g_vg.vp_x = c->x;
            g_vg.vp_y = c->y;
            g_vg.vp_w = c->w;
            g_vg.vp_h = c->h;
            g_vg.vp_min_z = c->min_depth;
            g_vg.vp_max_z = c->max_depth;
            g_vg.has_vp = 1;
            break;
        }
        case GPU_CMD_SET_SCISSOR: {
            const gpu_cmd_set_scissor_t *c = (const gpu_cmd_set_scissor_t *)p;
            if (psz < sizeof(*c))
                return -1;
            g_vg.sc_x = c->x;
            g_vg.sc_y = c->y;
            g_vg.sc_w = c->w;
            g_vg.sc_h = c->h;
            g_vg.has_scissor = 1;
            break;
        }
        case GPU_CMD_CLEAR: {
            const gpu_cmd_clear_t *c = (const gpu_cmd_clear_t *)p;
            if (psz < sizeof(*c))
                return -1;
            if (do_clear(c) < 0)
                return -1;
            break;
        }
        case GPU_CMD_DRAW: {
            const gpu_cmd_draw_t *c = (const gpu_cmd_draw_t *)p;
            if (psz < sizeof(*c))
                return -1;
            if (do_draw(c->count, c->first, 0, 0, 0, 0) < 0)
                return -1;
            break;
        }
        case GPU_CMD_DRAW_INDEXED: {
            const gpu_cmd_draw_indexed_t *c = (const gpu_cmd_draw_indexed_t *)p;
            if (psz < sizeof(*c))
                return -1;
            if (do_draw(c->count, 0, 1, c->count, c->first_index, c->base_vertex) < 0)
                return -1;
            break;
        }
        case GPU_CMD_BLIT: {
            const gpu_cmd_blit_t *c = (const gpu_cmd_blit_t *)p;
            if (psz < sizeof(*c))
                return -1;
            if (do_blit(c) < 0)
                return -1;
            break;
        }
        case GPU_CMD_PRESENT:
            return -1; /* virtio_cmd owns present */
        default:
            return -1;
        }
        p += psz;
    }
    return 0;
}
