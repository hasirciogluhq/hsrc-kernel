#ifndef VIRTIO_RAST_H
#define VIRTIO_RAST_H

#include <kernel/types.h>

/*
 * VirtIO-GPU renderer backend: CPU Rasterizer.
 * Fills guest backing from gpu_cmd_* (CLEAR/DRAW/BLIT).
 * Present is always virtio ring TRANSFER/FLUSH.
 */
int virtio_rast_exec(const void *gpu_cmds, uint32_t size);

#endif
