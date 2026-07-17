#ifndef MYKERNEL_MKDX_DEVICE_H
#define MYKERNEL_MKDX_DEVICE_H

#include "surface.h"
#include <drivers/display.h>

/* MKDX device — software render target backed by active display. */
typedef struct gx_device {
    gx_surface     *backbuffer;
    display_mode_t  mode;
    int             ready;
} gx_device;

int         gx_device_init(gx_device *dev);
void        gx_device_shutdown(gx_device *dev);
gx_surface *gx_device_target(gx_device *dev);
void        gx_device_present(gx_device *dev);
void        gx_device_begin(gx_device *dev, gx_color clear);
void        gx_device_end(gx_device *dev);

#endif
