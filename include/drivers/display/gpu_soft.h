#ifndef DRIVERS_GPU_SOFT_H
#define DRIVERS_GPU_SOFT_H

#include <kernel/types.h>

/*
 * Softpipe inside the GPU provider path (BGA / virtio-2D).
 * Userspace Reed/Kilim NEVER call this — only GpuProvider::gpu_submit.
 */

typedef struct gpu_tex_view {
    uint8_t *data;
    uint32_t width;
    uint32_t height;
    uint32_t stride; /* bytes */
    uint32_t format; /* DISP_FMT_* */
} gpu_tex_view_t;

typedef struct gpu_submit_res {
    int (*lookup_buf)(uint32_t handle, uint32_t pid, void **data, uint32_t *size,
                      void *ctx);
    int (*lookup_tex)(uint32_t handle, uint32_t pid, gpu_tex_view_t *out, void *ctx);
    int (*lookup_rt_color)(uint32_t handle, uint32_t pid, uint32_t *color_tex,
                           void *ctx);
    void    *ctx;
    uint32_t pid;
} gpu_submit_res_t;

/* Stashed under klock_disp by display.kmod before gpu_submit. */
void                     gpu_submit_res_set(const gpu_submit_res_t *res);
const gpu_submit_res_t  *gpu_submit_res_get(void);

/* Execute packed DISP_CMD_* stream against resolved resources. */
long gpu_soft_submit(const void *cmds, uint32_t size);

#endif
