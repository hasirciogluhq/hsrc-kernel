#ifndef BGA_CMD_H
#define BGA_CMD_H

#include <kernel/types.h>

/*
 * gpu_cmd_* walker for BGA. PRESENT → LFB present helpers.
 * CLEAR/DRAW/BLIT rejected — BGA has no 3D GPU (B24).
 */
int bga_cmd_submit_gpu(const void *gpu_cmds, uint32_t size);

#endif
