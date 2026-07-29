#ifndef DRIVERS_GPU_CMD_H
#define DRIVERS_GPU_CMD_H

#include <kernel/types.h>

/*
 * Display-driver command IR (between display.kmod and GpuProvider).
 *
 * Reed records DISP_CMD_* (handles).
 * display.kmod resolves handles → gpu_cmd_* (ready buffers / views).
 * Virtio converts gpu_cmd_* → virtio-gpu ring packets, processes, returns status.
 *
 * No CPU raster here — packets carry ready resource views only.
 */

#define GPU_CMD_MAX_BYTES (2u * 1024u * 1024u)

#define GPU_CMD_BIND_PIPELINE  1
#define GPU_CMD_BIND_VB        2
#define GPU_CMD_BIND_IB        3
#define GPU_CMD_BIND_TEX       4
#define GPU_CMD_BIND_RT        5
#define GPU_CMD_SET_UNIFORM    6
#define GPU_CMD_SET_VIEWPORT   7
#define GPU_CMD_SET_SCISSOR    8
#define GPU_CMD_CLEAR          9
#define GPU_CMD_DRAW           10
#define GPU_CMD_DRAW_INDEXED   11
#define GPU_CMD_BLIT           12
#define GPU_CMD_PRESENT        13 /* ready color buffer → device scanout */

typedef struct gpu_cmd_hdr {
    uint16_t op;
    uint16_t size; /* total packet bytes incl. hdr; 4-aligned */
} gpu_cmd_hdr_t;

/* Resolved guest view — display.kmod fills; provider never looks up handles. */
typedef struct gpu_buf_view {
    void    *data;
    uint32_t size;
} gpu_buf_view_t;

typedef struct gpu_tex_view {
    void    *data;
    uint32_t width;
    uint32_t height;
    uint32_t stride; /* bytes */
    uint32_t format; /* DISP_FMT_* */
} gpu_tex_view_t;

typedef struct gpu_cmd_bind_pipeline {
    gpu_cmd_hdr_t hdr;
    uint32_t topology;
    uint32_t cull;
    uint32_t blend;
    uint32_t shade;
    uint8_t  depth_test;
    uint8_t  depth_write;
    uint8_t  _pad[2];
} gpu_cmd_bind_pipeline_t;

typedef struct gpu_cmd_bind_vb {
    gpu_cmd_hdr_t hdr;
    gpu_buf_view_t vb;
} gpu_cmd_bind_vb_t;

typedef struct gpu_cmd_bind_ib {
    gpu_cmd_hdr_t hdr;
    gpu_buf_view_t ib;
} gpu_cmd_bind_ib_t;

typedef struct gpu_cmd_bind_tex {
    gpu_cmd_hdr_t hdr;
    uint32_t      slot;
    gpu_tex_view_t tex;
    uint32_t      wrap;
    uint32_t      filter;
} gpu_cmd_bind_tex_t;

typedef struct gpu_cmd_bind_rt {
    gpu_cmd_hdr_t hdr;
    gpu_tex_view_t color;
} gpu_cmd_bind_rt_t;

typedef struct gpu_light {
    float    x, y, z;
    float    intensity;
    uint32_t color;
    uint32_t directional;
} gpu_light_t;

typedef struct gpu_uniforms {
    float      model[16];
    float      view[16];
    float      proj[16];
    gpu_light_t lights[4];
    uint32_t   light_count;
    uint32_t   color;
    float      tint[4];
    float      blur_radius;
} gpu_uniforms_t;

typedef struct gpu_cmd_set_uniform {
    gpu_cmd_hdr_t hdr;
    gpu_uniforms_t u;
} gpu_cmd_set_uniform_t;

typedef struct gpu_cmd_set_viewport {
    gpu_cmd_hdr_t hdr;
    float x, y, w, h;
    float min_depth, max_depth;
} gpu_cmd_set_viewport_t;

typedef struct gpu_cmd_set_scissor {
    gpu_cmd_hdr_t hdr;
    int32_t x, y, w, h;
} gpu_cmd_set_scissor_t;

typedef struct gpu_cmd_clear {
    gpu_cmd_hdr_t hdr;
    uint32_t color_rgba;
    float    depth;
} gpu_cmd_clear_t;

typedef struct gpu_cmd_draw {
    gpu_cmd_hdr_t hdr;
    uint32_t count;
    uint32_t first;
} gpu_cmd_draw_t;

typedef struct gpu_cmd_draw_indexed {
    gpu_cmd_hdr_t hdr;
    uint32_t count;
    uint32_t first_index;
    int32_t  base_vertex;
} gpu_cmd_draw_indexed_t;

typedef struct gpu_cmd_blit {
    gpu_cmd_hdr_t hdr;
    gpu_tex_view_t src;
    gpu_tex_view_t dst; /* RT color */
    int32_t  dst_x, dst_y;
    int32_t  src_x, src_y, src_w, src_h;
    uint32_t blend;
} gpu_cmd_blit_t;

/* Ready framebuffer → device scanout (no host memcpy when stride matches). */
typedef struct gpu_cmd_present {
    gpu_cmd_hdr_t hdr;
    gpu_tex_view_t color;
    uint32_t x, y, w, h; /* 0,0,0,0 = full */
} gpu_cmd_present_t;

#endif
