#ifndef MYKERNEL_DRIVERS_DISPLAY_PROVIDERS_BGA_H
#define MYKERNEL_DRIVERS_DISPLAY_PROVIDERS_BGA_H

#include <kernel/types.h>

/* Bochs/QEMU -vga std linear framebuffer GpuProvider. */
int kmod_init(void);

/* Used by bga_cmd: ready color buffer → LFB present (full or rect). */
int bga_present_from_cmd(void *data, uint32_t width, uint32_t height,
                         uint32_t stride_bytes, uint32_t x, uint32_t y,
                         uint32_t w, uint32_t h);

#endif
