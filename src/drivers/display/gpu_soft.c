#include <drivers/display/gpu_soft.h>
#include <kernel/string.h>
#include <user/disp.h>

/*
 * Softpipe for GpuProvider::gpu_submit (BGA / virtio-2D).
 * Reed/Kilim never touch pixels — only providers call this.
 */

static gpu_submit_res_t g_submit_res;
static int              g_submit_res_valid;

void gpu_submit_res_set(const gpu_submit_res_t *res)
{
    if (res) {
        g_submit_res = *res;
        g_submit_res_valid = 1;
    } else {
        memset(&g_submit_res, 0, sizeof(g_submit_res));
        g_submit_res_valid = 0;
    }
}

const gpu_submit_res_t *gpu_submit_res_get(void)
{
    return g_submit_res_valid ? &g_submit_res : NULL;
}

#define K_MAX_VERTS_PER_DRAW 65536u
#define K_MAX_TRIS_BUDGET    200000u

typedef struct {
    uint32_t topology, cull, blend, shade;
    uint8_t  depth_test, depth_write;
} pipe_t;

typedef struct {
    const disp_vertex *vb;
    uint32_t           vb_count;
    const uint32_t    *ib;
    uint32_t           ib_count;
    gpu_tex_view_t     tex0;
    uint32_t           tex_wrap;
    uint32_t           tex_filter;
    gpu_tex_view_t     rt_color;
    int                has_rt;
    disp_uniforms      uniforms;
    float              vpx, vpy, vpw, vph;
    int32_t            sx, sy, sw, sh;
    pipe_t             pipe;
    uint32_t           draws;
} exec_t;

static void mat4_mul_vec4(const float *m, float x, float y, float z, float w,
                          float *ox, float *oy, float *oz, float *ow)
{
    *ox = m[0] * x + m[4] * y + m[8] * z + m[12] * w;
    *oy = m[1] * x + m[5] * y + m[9] * z + m[13] * w;
    *oz = m[2] * x + m[6] * y + m[10] * z + m[14] * w;
    *ow = m[3] * x + m[7] * y + m[11] * z + m[15] * w;
}

static uint32_t pack_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void unpack_rgba(uint32_t c, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a)
{
    *a = (uint8_t)((c >> 24) & 0xffu);
    *r = (uint8_t)((c >> 16) & 0xffu);
    *g = (uint8_t)((c >> 8) & 0xffu);
    *b = (uint8_t)(c & 0xffu);
}

static uint32_t blend_alpha(uint32_t dst, uint32_t src)
{
    uint8_t sr, sg, sb, sa, dr, dg, db, da;
    unpack_rgba(src, &sr, &sg, &sb, &sa);
    unpack_rgba(dst, &dr, &dg, &db, &da);
    if (sa == 255)
        return src;
    if (sa == 0)
        return dst;
    {
        uint32_t inv = 255u - (uint32_t)sa;
        uint8_t r = (uint8_t)(((uint32_t)sr * sa + (uint32_t)dr * inv) / 255u);
        uint8_t g = (uint8_t)(((uint32_t)sg * sa + (uint32_t)dg * inv) / 255u);
        uint8_t b = (uint8_t)(((uint32_t)sb * sa + (uint32_t)db * inv) / 255u);
        uint8_t a = (uint8_t)(sa + (uint32_t)da * inv / 255u);
        return pack_rgba(r, g, b, a);
    }
}

static float clampf(float v, float lo, float hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static int32_t clampi(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static uint32_t sample_tex(const gpu_tex_view_t *tex, uint32_t wrap, float u, float v)
{
    uint32_t tw, th;
    float fx, fy;
    int32_t x0, y0;
    const uint8_t *base;

    if (!tex || !tex->data || tex->width == 0 || tex->height == 0)
        return 0xffffffffu;
    tw = tex->width;
    th = tex->height;

    if (wrap == 1) {
        u = u - (float)(int32_t)u;
        v = v - (float)(int32_t)v;
        if (u < 0)
            u += 1.0f;
        if (v < 0)
            v += 1.0f;
    } else {
        u = clampf(u, 0.0f, 1.0f);
        v = clampf(v, 0.0f, 1.0f);
    }

    fx = u * (float)(tw - 1u);
    fy = v * (float)(th - 1u);
    x0 = clampi((int32_t)fx, 0, (int32_t)tw - 1);
    y0 = clampi((int32_t)fy, 0, (int32_t)th - 1);
    base = tex->data;

    if (tex->format == DISP_FMT_RGBA8) {
        const uint32_t *row = (const uint32_t *)(base + (uint32_t)y0 * tex->stride);
        return row[x0];
    }
    if (tex->format == DISP_FMT_A8 || tex->format == DISP_FMT_R8) {
        uint8_t a = base[(uint32_t)y0 * tex->stride + (uint32_t)x0];
        return pack_rgba(255, 255, 255, a);
    }
    return 0xffffffffu;
}

static uint32_t shade_pixel(const exec_t *e, const disp_vertex *v)
{
    uint32_t base = v->color ? v->color : e->uniforms.color;
    if (base == 0)
        base = 0xffffffffu;

    switch (e->pipe.shade) {
    case 0: /* UnlitColor */
        return base;
    case 1: { /* UnlitTextured */
        uint32_t tc = sample_tex(&e->tex0, e->tex_wrap, v->u, v->v);
        uint8_t tr, tg, tb, ta, br, bg, bb, ba;
        unpack_rgba(tc, &tr, &tg, &tb, &ta);
        unpack_rgba(base, &br, &bg, &bb, &ba);
        return pack_rgba((uint8_t)((tr * br) / 255u), (uint8_t)((tg * bg) / 255u),
                         (uint8_t)((tb * bb) / 255u), (uint8_t)((ta * ba) / 255u));
    }
    case 2: { /* VertexLit */
        float ndl = 0.25f;
        uint32_t i;
        for (i = 0; i < e->uniforms.light_count && i < 4u; i++) {
            float lx = e->uniforms.lights[i].x;
            float ly = e->uniforms.lights[i].y;
            float lz = e->uniforms.lights[i].z;
            float len = lx * lx + ly * ly + lz * lz;
            float d;
            if (len > 0.0001f) {
                float xhalf = 0.5f * len;
                union {
                    float    f;
                    uint32_t i;
                } u = {len};
                float inv;
                u.i = 0x5f3759dfu - (u.i >> 1);
                inv = u.f * (1.5f - xhalf * u.f * u.f);
                lx *= inv;
                ly *= inv;
                lz *= inv;
            }
            d = v->nx * lx + v->ny * ly + v->nz * lz;
            if (d < 0)
                d = 0;
            ndl += d * e->uniforms.lights[i].intensity;
        }
        ndl = clampf(ndl, 0.0f, 1.0f);
        {
            uint8_t r, g, b, a;
            unpack_rgba(base, &r, &g, &b, &a);
            return pack_rgba((uint8_t)((float)r * ndl), (uint8_t)((float)g * ndl),
                             (uint8_t)((float)b * ndl), a);
        }
    }
    default:
        return base;
    }
}

static void exec_clear(exec_t *e, uint32_t color)
{
    uint32_t *px;
    uint32_t w, h, stride;
    int32_t x0, y0, x1, y1, y, x, n;

    if (!e->has_rt || !e->rt_color.data)
        return;
    px = (uint32_t *)e->rt_color.data;
    w = e->rt_color.width;
    h = e->rt_color.height;
    stride = e->rt_color.stride / 4u;
    x0 = 0;
    y0 = 0;
    x1 = (int32_t)w;
    y1 = (int32_t)h;
    if (e->sw > 0 && e->sh > 0) {
        x0 = clampi(e->sx, 0, (int32_t)w);
        y0 = clampi(e->sy, 0, (int32_t)h);
        x1 = clampi(e->sx + e->sw, 0, (int32_t)w);
        y1 = clampi(e->sy + e->sh, 0, (int32_t)h);
    }
    for (y = y0; y < y1; y++) {
        uint32_t *row = px + (uint32_t)y * stride + (uint32_t)x0;
        n = x1 - x0;
        for (x = 0; x < n; x++)
            row[x] = color;
    }
}

static void exec_blit(exec_t *e, const disp_cmd_blit *c, const gpu_submit_res_t *ops)
{
    gpu_tex_view_t src, dst;
    uint32_t color_tex;
    uint32_t pid;
    int32_t sx0, sy0, sx1, sy1, bw, bh, dx0, dy0, y, x;
    uint32_t ss, ds;

    (void)e;
    if (!ops || !ops->lookup_tex || !ops->lookup_rt_color)
        return;
    pid = ops->pid;
    if (ops->lookup_tex(c->src_tex, pid, &src, ops->ctx) < 0)
        return;
    if (ops->lookup_rt_color(c->dst_rt, pid, &color_tex, ops->ctx) < 0)
        return;
    if (ops->lookup_tex(color_tex, pid, &dst, ops->ctx) < 0)
        return;
    if (!src.data || !dst.data || src.width == 0 || src.height == 0)
        return;

    sx0 = 0;
    sy0 = 0;
    sx1 = (int32_t)src.width;
    sy1 = (int32_t)src.height;
    if (c->src_w > 0 && c->src_h > 0) {
        sx0 = clampi(c->src_x, 0, (int32_t)src.width);
        sy0 = clampi(c->src_y, 0, (int32_t)src.height);
        sx1 = clampi(c->src_x + c->src_w, 0, (int32_t)src.width);
        sy1 = clampi(c->src_y + c->src_h, 0, (int32_t)src.height);
    }
    bw = sx1 - sx0;
    bh = sy1 - sy0;
    if (bw <= 0 || bh <= 0)
        return;

    dx0 = c->dst_x;
    dy0 = c->dst_y;
    if (dx0 < 0) {
        sx0 -= dx0;
        bw += dx0;
        dx0 = 0;
    }
    if (dy0 < 0) {
        sy0 -= dy0;
        bh += dy0;
        dy0 = 0;
    }
    if (dx0 + bw > (int32_t)dst.width)
        bw = (int32_t)dst.width - dx0;
    if (dy0 + bh > (int32_t)dst.height)
        bh = (int32_t)dst.height - dy0;
    if (bw <= 0 || bh <= 0)
        return;

    ss = src.stride / 4u;
    ds = dst.stride / 4u;
    for (y = 0; y < bh; y++) {
        uint32_t *srow = (uint32_t *)src.data + (uint32_t)(sy0 + y) * ss + (uint32_t)sx0;
        uint32_t *drow = (uint32_t *)dst.data + (uint32_t)(dy0 + y) * ds + (uint32_t)dx0;
        if (c->blend == 0) {
            for (x = 0; x < bw; x++)
                drow[x] = srow[x];
        } else {
            for (x = 0; x < bw; x++) {
                uint32_t sp = srow[x];
                if (((sp >> 24) & 0xffu) == 255u)
                    drow[x] = sp;
                else if (((sp >> 24) & 0xffu) != 0u)
                    drow[x] = blend_alpha(drow[x], sp);
            }
        }
    }
}

static void raster_triangles(exec_t *e, uint32_t count, uint32_t first, int indexed,
                             int32_t base_vertex)
{
    uint32_t *fb;
    uint32_t fw, fh, fstride, ntri, t;
    float vpx, vpy, vpw, vph;
    int32_t sx0, sy0, sx1, sy1;

    if (!e->has_rt || !e->vb || !e->rt_color.data)
        return;
    fb = (uint32_t *)e->rt_color.data;
    fw = e->rt_color.width;
    fh = e->rt_color.height;
    fstride = e->rt_color.stride / 4u;
    if (fw == 0 || fh == 0)
        return;

    vpx = e->vpw > 0 ? e->vpx : 0.0f;
    vpy = e->vph > 0 ? e->vpy : 0.0f;
    vpw = e->vpw > 0 ? e->vpw : (float)fw;
    vph = e->vph > 0 ? e->vph : (float)fh;

    sx0 = 0;
    sy0 = 0;
    sx1 = (int32_t)fw;
    sy1 = (int32_t)fh;
    if (e->sw > 0 && e->sh > 0) {
        sx0 = clampi(e->sx, 0, (int32_t)fw);
        sy0 = clampi(e->sy, 0, (int32_t)fh);
        sx1 = clampi(e->sx + e->sw, 0, (int32_t)fw);
        sy1 = clampi(e->sy + e->sh, 0, (int32_t)fh);
    }

    ntri = count / 3u;
    if (ntri > K_MAX_TRIS_BUDGET)
        ntri = K_MAX_TRIS_BUDGET;

    for (t = 0; t < ntri; t++) {
        uint32_t i0, i1, i2;
        disp_vertex v0, v1, v2;
        float x0, y0, z0, w0, x1, y1, z1, w1, x2, y2, z2, w2;
        float mx, my, mz, mw;
        float px0, py0, px1, py1, px2, py2, area, inv_area;
        int32_t minx, maxx, miny, maxy, y, x;

        if (indexed) {
            uint32_t base = first + t * 3u;
            if (!e->ib || base + 2u >= e->ib_count)
                break;
            i0 = (uint32_t)((int32_t)e->ib[base + 0] + base_vertex);
            i1 = (uint32_t)((int32_t)e->ib[base + 1] + base_vertex);
            i2 = (uint32_t)((int32_t)e->ib[base + 2] + base_vertex);
        } else {
            i0 = first + t * 3u + 0u;
            i1 = first + t * 3u + 1u;
            i2 = first + t * 3u + 2u;
        }
        if (i0 >= e->vb_count || i1 >= e->vb_count || i2 >= e->vb_count)
            continue;

        v0 = e->vb[i0];
        v1 = e->vb[i1];
        v2 = e->vb[i2];

        mat4_mul_vec4(e->uniforms.model, v0.x, v0.y, v0.z, 1.0f, &mx, &my, &mz, &mw);
        mat4_mul_vec4(e->uniforms.view, mx, my, mz, mw, &mx, &my, &mz, &mw);
        mat4_mul_vec4(e->uniforms.proj, mx, my, mz, mw, &x0, &y0, &z0, &w0);

        mat4_mul_vec4(e->uniforms.model, v1.x, v1.y, v1.z, 1.0f, &mx, &my, &mz, &mw);
        mat4_mul_vec4(e->uniforms.view, mx, my, mz, mw, &mx, &my, &mz, &mw);
        mat4_mul_vec4(e->uniforms.proj, mx, my, mz, mw, &x1, &y1, &z1, &w1);

        mat4_mul_vec4(e->uniforms.model, v2.x, v2.y, v2.z, 1.0f, &mx, &my, &mz, &mw);
        mat4_mul_vec4(e->uniforms.view, mx, my, mz, mw, &mx, &my, &mz, &mw);
        mat4_mul_vec4(e->uniforms.proj, mx, my, mz, mw, &x2, &y2, &z2, &w2);

        if (w0 == 0.0f || w1 == 0.0f || w2 == 0.0f)
            continue;
        x0 /= w0;
        y0 /= w0;
        x1 /= w1;
        y1 /= w1;
        x2 /= w2;
        y2 /= w2;

        px0 = vpx + (x0 * 0.5f + 0.5f) * vpw;
        py0 = vpy + (1.0f - (y0 * 0.5f + 0.5f)) * vph;
        px1 = vpx + (x1 * 0.5f + 0.5f) * vpw;
        py1 = vpy + (1.0f - (y1 * 0.5f + 0.5f)) * vph;
        px2 = vpx + (x2 * 0.5f + 0.5f) * vpw;
        py2 = vpy + (1.0f - (y2 * 0.5f + 0.5f)) * vph;

        area = (px1 - px0) * (py2 - py0) - (px2 - px0) * (py1 - py0);
        if (e->pipe.cull == 1 && area <= 0.0f) /* Back */
            continue;
        if (e->pipe.cull == 2 && area >= 0.0f) /* Front */
            continue;
        if (area == 0.0f)
            continue;

        minx = (int32_t)(px0 < px1 ? (px0 < px2 ? px0 : px2) : (px1 < px2 ? px1 : px2));
        maxx = (int32_t)(px0 > px1 ? (px0 > px2 ? px0 : px2) : (px1 > px2 ? px1 : px2)) + 1;
        miny = (int32_t)(py0 < py1 ? (py0 < py2 ? py0 : py2) : (py1 < py2 ? py1 : py2));
        maxy = (int32_t)(py0 > py1 ? (py0 > py2 ? py0 : py2) : (py1 > py2 ? py1 : py2)) + 1;
        minx = clampi(minx, sx0, sx1 - 1);
        maxx = clampi(maxx, sx0, sx1 - 1);
        miny = clampi(miny, sy0, sy1 - 1);
        maxy = clampi(maxy, sy0, sy1 - 1);

        inv_area = 1.0f / area;
        for (y = miny; y <= maxy; y++) {
            for (x = minx; x <= maxx; x++) {
                float px = (float)x + 0.5f;
                float py = (float)y + 0.5f;
                float w0b = ((px1 - px) * (py2 - py) - (px2 - px) * (py1 - py)) * inv_area;
                float w1b = ((px2 - px) * (py0 - py) - (px0 - px) * (py2 - py)) * inv_area;
                float w2b = 1.0f - w0b - w1b;
                disp_vertex pv;
                uint32_t src;
                uint32_t *dst;
                if (w0b < 0.0f || w1b < 0.0f || w2b < 0.0f)
                    continue;
                pv.x = v0.x * w0b + v1.x * w1b + v2.x * w2b;
                pv.y = v0.y * w0b + v1.y * w1b + v2.y * w2b;
                pv.z = v0.z * w0b + v1.z * w1b + v2.z * w2b;
                pv.nx = v0.nx * w0b + v1.nx * w1b + v2.nx * w2b;
                pv.ny = v0.ny * w0b + v1.ny * w1b + v2.ny * w2b;
                pv.nz = v0.nz * w0b + v1.nz * w1b + v2.nz * w2b;
                pv.u = v0.u * w0b + v1.u * w1b + v2.u * w2b;
                pv.v = v0.v * w0b + v1.v * w1b + v2.v * w2b;
                pv.color = v0.color;
                src = shade_pixel(e, &pv);
                dst = &fb[(uint32_t)y * fstride + (uint32_t)x];
                if (e->pipe.blend == 0)
                    *dst = src;
                else
                    *dst = blend_alpha(*dst, src);
            }
        }
    }
}

static int bind_rt(exec_t *e, uint32_t rt_handle, const gpu_submit_res_t *ops)
{
    uint32_t color_tex;
    if (!ops || !ops->lookup_rt_color || !ops->lookup_tex)
        return -1;
    if (ops->lookup_rt_color(rt_handle, ops->pid, &color_tex, ops->ctx) < 0)
        return -1;
    if (ops->lookup_tex(color_tex, ops->pid, &e->rt_color, ops->ctx) < 0)
        return -1;
    e->has_rt = 1;
    return 0;
}

long gpu_soft_submit(const void *cmds, uint32_t size)
{
    const uint8_t *p;
    const uint8_t *end;
    const gpu_submit_res_t *ops;
    uint32_t pid;
    exec_t e;

    ops = gpu_submit_res_get();
    if (!cmds || size == 0 || size > DISP_MAX_SUBMIT_BYTES || !ops)
        return -1;
    pid = ops->pid;

    memset(&e, 0, sizeof(e));
    e.uniforms.model[0] = e.uniforms.model[5] = e.uniforms.model[10] = e.uniforms.model[15] =
        1.0f;
    e.uniforms.view[0] = e.uniforms.view[5] = e.uniforms.view[10] = e.uniforms.view[15] = 1.0f;
    e.uniforms.proj[0] = e.uniforms.proj[5] = e.uniforms.proj[10] = e.uniforms.proj[15] = 1.0f;
    e.pipe.topology = 0;
    e.pipe.blend = 1;

    p = (const uint8_t *)cmds;
    end = p + size;

    while (p + sizeof(disp_cmd_hdr) <= end) {
        const disp_cmd_hdr *hdr = (const disp_cmd_hdr *)p;
        uint16_t psz = hdr->size;
        if (psz < sizeof(disp_cmd_hdr) || (psz & 3u) || p + psz > end)
            return -1;

        switch (hdr->op) {
        case DISP_CMD_BIND_PIPELINE: {
            const disp_cmd_bind_pipeline *c = (const disp_cmd_bind_pipeline *)p;
            if (psz < sizeof(*c))
                return -1;
            e.pipe.topology = c->topology;
            e.pipe.cull = c->cull;
            e.pipe.blend = c->blend;
            e.pipe.shade = c->shade;
            e.pipe.depth_test = c->depth_test;
            e.pipe.depth_write = c->depth_write;
            break;
        }
        case DISP_CMD_BIND_VB: {
            const disp_cmd_bind_handle *c = (const disp_cmd_bind_handle *)p;
            void *data = NULL;
            uint32_t bsz = 0;
            if (psz < sizeof(*c) || !ops->lookup_buf)
                return -1;
            if (ops->lookup_buf(c->handle, pid, &data, &bsz, ops->ctx) < 0) {
                e.vb = NULL;
                e.vb_count = 0;
            } else {
                e.vb = (const disp_vertex *)data;
                e.vb_count = bsz / (uint32_t)sizeof(disp_vertex);
            }
            break;
        }
        case DISP_CMD_BIND_IB: {
            const disp_cmd_bind_handle *c = (const disp_cmd_bind_handle *)p;
            void *data = NULL;
            uint32_t bsz = 0;
            if (psz < sizeof(*c) || !ops->lookup_buf)
                return -1;
            if (ops->lookup_buf(c->handle, pid, &data, &bsz, ops->ctx) < 0) {
                e.ib = NULL;
                e.ib_count = 0;
            } else {
                e.ib = (const uint32_t *)data;
                e.ib_count = bsz / 4u;
            }
            break;
        }
        case DISP_CMD_BIND_TEX: {
            const disp_cmd_bind_tex *c = (const disp_cmd_bind_tex *)p;
            if (psz < sizeof(*c) || !ops->lookup_tex)
                return -1;
            if (c->slot == 0) {
                if (ops->lookup_tex(c->handle, pid, &e.tex0, ops->ctx) < 0)
                    memset(&e.tex0, 0, sizeof(e.tex0));
                e.tex_wrap = c->wrap;
                e.tex_filter = c->filter;
            }
            break;
        }
        case DISP_CMD_BIND_RT: {
            const disp_cmd_bind_handle *c = (const disp_cmd_bind_handle *)p;
            if (psz < sizeof(*c))
                return -1;
            if (bind_rt(&e, c->handle, ops) < 0)
                e.has_rt = 0;
            break;
        }
        case DISP_CMD_SET_UNIFORM: {
            const disp_cmd_set_uniform *c = (const disp_cmd_set_uniform *)p;
            if (psz < sizeof(*c))
                return -1;
            e.uniforms = c->u;
            break;
        }
        case DISP_CMD_SET_VIEWPORT: {
            const disp_cmd_set_viewport *c = (const disp_cmd_set_viewport *)p;
            if (psz < sizeof(*c))
                return -1;
            e.vpx = c->x;
            e.vpy = c->y;
            e.vpw = c->w;
            e.vph = c->h;
            break;
        }
        case DISP_CMD_SET_SCISSOR: {
            const disp_cmd_set_scissor *c = (const disp_cmd_set_scissor *)p;
            if (psz < sizeof(*c))
                return -1;
            e.sx = c->x;
            e.sy = c->y;
            e.sw = c->w;
            e.sh = c->h;
            break;
        }
        case DISP_CMD_CLEAR: {
            const disp_cmd_clear *c = (const disp_cmd_clear *)p;
            if (psz < sizeof(*c))
                return -1;
            exec_clear(&e, c->color_rgba);
            break;
        }
        case DISP_CMD_DRAW: {
            const disp_cmd_draw *c = (const disp_cmd_draw *)p;
            uint32_t count;
            if (psz < sizeof(*c))
                return -1;
            if (e.draws >= DISP_MAX_DRAW_PER_FRAME)
                break;
            count = c->count;
            if (count > K_MAX_VERTS_PER_DRAW)
                count = K_MAX_VERTS_PER_DRAW;
            e.draws++;
            if (e.pipe.topology == 0)
                raster_triangles(&e, count, c->first, 0, 0);
            break;
        }
        case DISP_CMD_DRAW_INDEXED: {
            const disp_cmd_draw_indexed *c = (const disp_cmd_draw_indexed *)p;
            uint32_t count;
            if (psz < sizeof(*c))
                return -1;
            if (e.draws >= DISP_MAX_DRAW_PER_FRAME)
                break;
            count = c->count;
            if (count > K_MAX_VERTS_PER_DRAW)
                count = K_MAX_VERTS_PER_DRAW;
            e.draws++;
            if (e.pipe.topology == 0)
                raster_triangles(&e, count, c->first_index, 1, c->base_vertex);
            break;
        }
        case DISP_CMD_BLIT: {
            const disp_cmd_blit *c = (const disp_cmd_blit *)p;
            if (psz < sizeof(*c))
                return -1;
            exec_blit(&e, c, ops);
            break;
        }
        default:
            return -1;
        }
        p += psz;
    }
    return 0;
}
