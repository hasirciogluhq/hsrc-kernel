#ifndef DRIVERS_GPU_CMD_FB_H
#define DRIVERS_GPU_CMD_FB_H

#include <kernel/types.h>

/*
 * Execute CLEAR / DRAW / BLIT into guest RT/texture memory.
 * Used when the active provider has no VirGL (BGA, virtio-2D).
 * PRESENT/scanout stays on the provider.
 */
int gpu_cmd_fb_exec(const void *gpu_cmds, uint32_t size);

#endif
