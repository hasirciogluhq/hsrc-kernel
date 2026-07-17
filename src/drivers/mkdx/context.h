#ifndef MYKERNEL_MKDX_CONTEXT_H
#define MYKERNEL_MKDX_CONTEXT_H

#include "surface.h"

/* 2D drawing context: clip / mask / opacity / round */
typedef struct mkdx_context2d {
    gx_surface *target;
    gx_rect     clip;
    gx_surface *mask;       /* optional A8/ARGB mask, may be NULL */
    uint8_t     opacity;    /* 0..255 */
    int32_t     round_radius;
    int         ready;
} mkdx_context2d;

int  mkdx_context2d_init(mkdx_context2d *ctx, gx_surface *target);
void mkdx_context2d_set_clip(mkdx_context2d *ctx, gx_rect clip);
void mkdx_context2d_set_mask(mkdx_context2d *ctx, gx_surface *mask);
void mkdx_context2d_set_opacity(mkdx_context2d *ctx, uint8_t opacity);
void mkdx_context2d_set_round(mkdx_context2d *ctx, int32_t radius);
int  mkdx_context2d_fill(mkdx_context2d *ctx, gx_rect r, gx_color color);

#endif
