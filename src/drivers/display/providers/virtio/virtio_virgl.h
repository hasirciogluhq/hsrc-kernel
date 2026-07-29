#ifndef VIRTIO_VIRGL_H
#define VIRTIO_VIRGL_H

#include "virtio_ring.h"
#include "virtio_gpu.h"
#include <drivers/display/gpu_cmd.h>
#include <kernel/types.h>

/*
 * VirGL / virtio-gpu 3D — CLEAR / DRAW / BLIT via SUBMIT_3D.
 * No CPU raster: host virglrenderer executes the stream.
 */

int  virtio_virgl_init(virtio_ring_t *ring, virtio_scanout_t *so);
void virtio_virgl_shutdown(void);
int  virtio_virgl_ready(void);

/* Execute one gpu_cmd_* (CLEAR/DRAW/BLIT/bind state). PRESENT stays in virtio_gpu. */
int  virtio_virgl_exec(const void *gpu_cmds, uint32_t size);

#endif
