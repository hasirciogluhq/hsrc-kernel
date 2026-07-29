#ifndef DX_CONTEXT_H
#define DX_CONTEXT_H

#include "surface.h"

/* 2D drawing context: clip / mask / opacity / round */
typedef struct dx_context2d {
    gx_surface *target;
    gx_rect     clip;
    gx_surface *mask;       /* optional A8/ARGB mask, may be NULL */
    uint8_t     opacity;    /* 0..255 */
    int32_t     round_radius;
    int         ready;
} dx_context2d;

int  dx_context2d_init(dx_context2d *ctx, gx_surface *target);
void dx_context2d_set_clip(dx_context2d *ctx, gx_rect clip);
void dx_context2d_set_mask(dx_context2d *ctx, gx_surface *mask);
void dx_context2d_set_opacity(dx_context2d *ctx, uint8_t opacity);
void dx_context2d_set_round(dx_context2d *ctx, int32_t radius);
int  dx_context2d_fill(dx_context2d *ctx, gx_rect r, gx_color color);

#endif
