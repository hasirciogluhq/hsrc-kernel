#ifndef VIRTIO_GPU_H
#define VIRTIO_GPU_H

#include "virtio_ring.h"
#include <kernel/types.h>

/*
 * App/Reed → display.kmod → VirtIO-GPU Driver (this)
 *   → Renderer Backend: VirGL OR CPU rast
 *   → Present (ring TRANSFER/FLUSH)
 */

typedef struct virtio_scanout {
    virtio_ring_t *ring;
    uint32_t       resource_id;
    uint32_t       width;
    uint32_t       height;
    void          *attach_ptr;
    uint32_t       attach_bytes;
} virtio_scanout_t;

int virtio_gpu_get_display_size(virtio_ring_t *ring, uint32_t *w, uint32_t *h);
int virtio_gpu_setup_scanout(virtio_scanout_t *so, void *fb, uint32_t bytes,
                             uint32_t width, uint32_t height);
int virtio_gpu_present(virtio_scanout_t *so, void *data, uint32_t width,
                       uint32_t height, uint32_t stride_bytes,
                       uint32_t x, uint32_t y, uint32_t w, uint32_t h);
int virtio_gpu_submit(virtio_scanout_t *so, const void *gpu_cmds, uint32_t size);

#endif
