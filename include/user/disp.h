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

#endif
