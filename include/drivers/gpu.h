#ifndef DRIVERS_GPU_H
#define DRIVERS_GPU_H

#include <kernel/types.h>
#include <drivers/display.h>

/*
 * GpuProvider — scanout / present abstraction under display.kmod.
 * Display kmods (virtio / vga-LFB) register here; UI never talks to them.
 *
 * Ownership: BSP register at kmod init; runtime readers via gpu_provider_active().
 */

#define GPU_PRIO_VGA     10
#define GPU_PRIO_VIRTIO  20

#define GPU_CAP_SCANOUT       (1u << 0)
#define GPU_CAP_PRESENT_RECT  (1u << 1)
#define GPU_CAP_PRESENT_RECTS (1u << 2)
#define GPU_CAP_HW_SUBMIT     (1u << 3) /* virtio-gpu 3D / virgl — v1.1 */

typedef struct gpu_provider_ops {
    const char *name;
    uint32_t    caps;
    void       *priv; /* display_ops_t* for bridge adapters */
    int (*get_mode)(struct gpu_provider_ops *self, display_mode_t *out);
    int (*present)(struct gpu_provider_ops *self,
                    const uint32_t *src, uint32_t src_stride_px);
    int (*present_rect)(struct gpu_provider_ops *self,
                         const uint32_t *src, uint32_t src_stride_px,
                         uint32_t x, uint32_t y, uint32_t w, uint32_t h);
    int (*present_rects)(struct gpu_provider_ops *self,
                          const uint32_t *src, uint32_t src_stride_px,
                          const display_rect_t *rects, uint32_t n);
    int (*gpu_submit)(struct gpu_provider_ops *self,
                       const void *cmd, uint32_t size); /* optional */
} gpu_provider_ops_t;

void                 gpu_framework_init(void);
int                  gpu_provider_register(gpu_provider_ops_t *ops, int priority);
void                 gpu_provider_unregister(gpu_provider_ops_t *ops);
gpu_provider_ops_t  *gpu_provider_active(void);
int                  gpu_get_screen_size(uint32_t *w, uint32_t *h, uint32_t *bpp);

/* Bridge: wrap an existing display_ops as a GpuProvider (legacy display_* path). */
int gpu_provider_register_display(display_ops_t *ops, int priority,
                                  gpu_provider_ops_t *out_slot);

#endif
