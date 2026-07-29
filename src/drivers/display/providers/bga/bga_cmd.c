#include "bga_cmd.h"
#include "bga.h"
#include <drivers/display/gpu_cmd.h>

/*
 * BGA gpu_cmd stream — present only. No soft-GPU / CPU clear/draw/blit.
 */

int bga_cmd_submit_gpu(const void *gpu_cmds, uint32_t size)
{
    const uint8_t *p = (const uint8_t *)gpu_cmds;
    const uint8_t *end;

    if (!gpu_cmds || size < sizeof(gpu_cmd_hdr_t))
        return -1;
    end = p + size;

    while (p + sizeof(gpu_cmd_hdr_t) <= end) {
        const gpu_cmd_hdr_t *h = (const gpu_cmd_hdr_t *)p;
        uint32_t psz;

        if (h->size < sizeof(gpu_cmd_hdr_t) || (h->size & 3u))
            return -1;
        psz = h->size;
        if (p + psz > end)
            return -1;

        switch (h->op) {
        case GPU_CMD_PRESENT: {
            const gpu_cmd_present_t *c = (const gpu_cmd_present_t *)p;
            if (psz < sizeof(*c) || !c->color.data)
                return -1;
            if (bga_present_from_cmd(c->color.data, c->color.width, c->color.height,
                                     c->color.stride, c->x, c->y, c->w, c->h) < 0)
                return -1;
            break;
        }
        /* No 3D / soft raster on BGA (B23/B24). */
        case GPU_CMD_BIND_PIPELINE:
        case GPU_CMD_BIND_VB:
        case GPU_CMD_BIND_IB:
        case GPU_CMD_BIND_TEX:
        case GPU_CMD_BIND_RT:
        case GPU_CMD_SET_UNIFORM:
        case GPU_CMD_SET_VIEWPORT:
        case GPU_CMD_SET_SCISSOR:
        case GPU_CMD_CLEAR:
        case GPU_CMD_DRAW:
        case GPU_CMD_DRAW_INDEXED:
        case GPU_CMD_BLIT:
            return -1;
        default:
            return -1;
        }
        p += psz;
    }
    return 0;
}
