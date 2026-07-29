#ifndef USER_DISP_H
#define USER_DISP_H

#include <kernel/types.h>

/*
 * Userspace ABI for SYS_DISP_CALL (display.kmod).
 * Reed is the only intended client; apps use Reed/Kilim, not this directly.
 */

/* SYS_DISP_CALL a1 = opcode, a2 = &disp_* args (or 0). */
#define DISP_OP_INFO              1
#define DISP_OP_BUFFER_CREATE     2
#define DISP_OP_BUFFER_DESTROY    3
#define DISP_OP_BUFFER_MAP        4
#define DISP_OP_BUFFER_UNMAP      5
#define DISP_OP_BUFFER_UPDATE      6
#define DISP_OP_TEXTURE_CREATE    7
#define DISP_OP_TEXTURE_DESTROY   8
#define DISP_OP_TEXTURE_UPLOAD    9
#define DISP_OP_TEXTURE_MAP       10
#define DISP_OP_RT_CREATE         11
#define DISP_OP_RT_DESTROY        12
#define DISP_OP_FENCE_CREATE      13
#define DISP_OP_FENCE_DESTROY     14
#define DISP_OP_FENCE_WAIT        15
#define DISP_OP_FENCE_SIGNAL      16
#define DISP_OP_SCANOUT           17
#define DISP_OP_EXPORT            18
#define DISP_OP_IMPORT            19
#define DISP_OP_STATS             20
#define DISP_OP_SUBMIT            21  /* Reed command stream → display → GpuProvider */

#define DISP_BUF_VERTEX    1
#define DISP_BUF_INDEX     2
#define DISP_BUF_UNIFORM   3
#define DISP_BUF_STAGING   4
#define DISP_BUF_COLOR     5  /* RGBA8 linear for RT / scanout */

#define DISP_FMT_R8     1
#define DISP_FMT_RG8    2
#define DISP_FMT_RGBA8  3
#define DISP_FMT_A8     4

#define DISP_FENCE_UNSIGNALED 0
#define DISP_FENCE_SIGNALED   1
#define DISP_FENCE_CONSUMED   2

typedef struct disp_info {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t caps;              /* GPU_CAP_* mirror */
    uint32_t max_texture_size;
    uint32_t max_draw_calls_per_frame;
    uint32_t hw_accel_available; /* 0/1 */
} disp_info;

typedef struct disp_buffer_create {
    uint32_t kind;   /* DISP_BUF_* */
    uint32_t size;   /* bytes */
    uint32_t handle; /* out */
} disp_buffer_create;

typedef struct disp_handle_arg {
    uint32_t handle;
} disp_handle_arg;

typedef struct disp_buffer_map {
    uint32_t  handle;
    void     *ptr;    /* out — CPU mapping (identity AS) */
    uint32_t  size;   /* out */
} disp_buffer_map;

typedef struct disp_buffer_update {
    uint32_t    handle;
    uint32_t    offset;
    uint32_t    len;
    const void *data;
} disp_buffer_update;

typedef struct disp_texture_create {
    uint32_t format; /* DISP_FMT_* */
    uint32_t width;
    uint32_t height;
    uint32_t levels; /* mip count, min 1 */
    uint32_t handle; /* out */
} disp_texture_create;

typedef struct disp_texture_upload {
    uint32_t    handle;
    uint32_t    level;
    uint32_t    len;
    const void *data;
} disp_texture_upload;

typedef struct disp_texture_map {
    uint32_t  handle;
    void     *ptr;    /* out */
    uint32_t  width;  /* out */
    uint32_t  height; /* out */
    uint32_t  stride; /* out — bytes per row */
    uint32_t  format; /* out */
} disp_texture_map;

typedef struct disp_rt_create {
    uint32_t color_tex;   /* texture handle (RGBA8) */
    uint32_t depth_tex;   /* 0 = none */
    uint32_t handle;      /* out */
} disp_rt_create;

typedef struct disp_fence_create {
    uint32_t handle; /* out */
} disp_fence_create;

typedef struct disp_fence_wait {
    uint32_t handle;
    int32_t  timeout_ticks; /* <0 forever, 0 try */
} disp_fence_wait;

typedef struct disp_scanout {
    uint32_t handle; /* COLOR buffer or RGBA8 texture or RT */
    uint32_t x, y, w, h; /* 0,0,0,0 = full */
} disp_scanout;

typedef struct disp_export {
    uint32_t handle;      /* in */
    uint32_t token;       /* out — opaque cross-process token */
} disp_export;

typedef struct disp_import {
    uint32_t token;       /* in */
    uint32_t handle;      /* out — local handle */
} disp_import;

typedef struct disp_stats {
    uint32_t buffers;
    uint32_t textures;
    uint32_t render_targets;
    uint32_t fences;
    uint32_t bytes_live;
} disp_stats;

/*
 * DISP_OP_SUBMIT — Reed command stream.
 * display.kmod resolves handles, then GpuProvider::gpu_submit executes
 * (softpipe or HW). Userspace never rasterizes.
 */
typedef struct disp_submit {
    const void *cmds;   /* packed disp_cmd_* packets */
    uint32_t    size;   /* bytes */
    uint32_t    fence;  /* optional fence handle; 0 = none */
} disp_submit;

#define DISP_MAX_SUBMIT_BYTES     (2u * 1024u * 1024u)
#define DISP_MAX_DRAW_PER_FRAME   4096u

#define DISP_CMD_BIND_PIPELINE  1
#define DISP_CMD_BIND_VB        2
#define DISP_CMD_BIND_IB        3
#define DISP_CMD_BIND_TEX       4
#define DISP_CMD_BIND_RT        5
#define DISP_CMD_SET_UNIFORM    6
#define DISP_CMD_SET_VIEWPORT   7
#define DISP_CMD_SET_SCISSOR    8
#define DISP_CMD_CLEAR          9
#define DISP_CMD_DRAW           10
#define DISP_CMD_DRAW_INDEXED   11
#define DISP_CMD_BLIT           12

typedef struct disp_cmd_hdr {
    uint16_t op;
    uint16_t size; /* total packet bytes incl. hdr; 4-aligned */
} disp_cmd_hdr;

typedef struct disp_cmd_bind_pipeline {
    disp_cmd_hdr hdr;
    uint32_t topology;
    uint32_t cull;
    uint32_t blend;
    uint32_t shade;
    uint8_t  depth_test;
    uint8_t  depth_write;
    uint8_t  _pad[2];
} disp_cmd_bind_pipeline;

typedef struct disp_cmd_bind_handle {
    disp_cmd_hdr hdr;
    uint32_t handle;
} disp_cmd_bind_handle;

typedef struct disp_cmd_bind_tex {
    disp_cmd_hdr hdr;
    uint32_t slot;
    uint32_t handle;
    uint32_t wrap;   /* 0=clamp 1=repeat */
    uint32_t filter; /* 0=nearest 1=bilinear */
} disp_cmd_bind_tex;

typedef struct disp_light {
    float    x, y, z;
    float    intensity;
    uint32_t color;
    uint32_t directional;
} disp_light;

typedef struct disp_uniforms {
    float     model[16];
    float     view[16];
    float     proj[16];
    disp_light lights[4];
    uint32_t  light_count;
    uint32_t  color;
    float     tint[4];
    float     blur_radius;
} disp_uniforms;

typedef struct disp_cmd_set_uniform {
    disp_cmd_hdr   hdr;
    disp_uniforms  u;
} disp_cmd_set_uniform;

typedef struct disp_cmd_set_viewport {
    disp_cmd_hdr hdr;
    float x, y, w, h;
    float min_depth, max_depth;
} disp_cmd_set_viewport;

typedef struct disp_cmd_set_scissor {
    disp_cmd_hdr hdr;
    int32_t x, y, w, h;
} disp_cmd_set_scissor;

typedef struct disp_cmd_clear {
    disp_cmd_hdr hdr;
    uint32_t color_rgba;
    float    depth;
} disp_cmd_clear;

typedef struct disp_cmd_draw {
    disp_cmd_hdr hdr;
    uint32_t count;
    uint32_t first;
} disp_cmd_draw;

typedef struct disp_cmd_draw_indexed {
    disp_cmd_hdr hdr;
    uint32_t count;
    uint32_t first_index;
    int32_t  base_vertex;
} disp_cmd_draw_indexed;

typedef struct disp_cmd_blit {
    disp_cmd_hdr hdr;
    uint32_t src_tex;   /* RGBA8 texture handle */
    uint32_t dst_rt;    /* render-target handle */
    int32_t  dst_x, dst_y;
    int32_t  src_x, src_y, src_w, src_h; /* src_w/h <=0 → full */
    uint32_t blend;     /* 0=opaque 1=alpha 2=premul */
} disp_cmd_blit;

typedef struct disp_vertex {
    float    x, y, z;
    float    nx, ny, nz;
    float    u, v;
    uint32_t color;
} disp_vertex;

#endif
