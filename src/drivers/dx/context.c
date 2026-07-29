#include "context.h"
#include "accel.h"
#include <kernel/string.h>

int dx_context2d_init(dx_context2d *ctx, gx_surface *target)
{
    if (!ctx || !target)
        return -1;
    memset(ctx, 0, sizeof(*ctx));
    ctx->target = target;
    ctx->clip = gx_rect_make(0, 0, (int32_t)target->width, (int32_t)target->height);
    ctx->opacity = 255;
    ctx->ready = 1;
    return 0;
}

void dx_context2d_set_clip(dx_context2d *ctx, gx_rect clip)
{
    if (!ctx || !ctx->ready)
        return;
    ctx->clip = gx_rect_intersect(
        clip, gx_rect_make(0, 0, (int32_t)ctx->target->width, (int32_t)ctx->target->height));
}

void dx_context2d_set_mask(dx_context2d *ctx, gx_surface *mask)
{
    if (!ctx)
        return;
    ctx->mask = mask;
}

void dx_context2d_set_opacity(dx_context2d *ctx, uint8_t opacity)
{
    if (!ctx)
        return;
    ctx->opacity = opacity;
}

void dx_context2d_set_round(dx_context2d *ctx, int32_t radius)
{
    if (!ctx)
        return;
    ctx->round_radius = radius;
}

int dx_context2d_fill(dx_context2d *ctx, gx_rect r, gx_color color)
{
    gx_rect clipped;
    if (!ctx || !ctx->ready || !ctx->target)
        return -1;

    clipped = gx_rect_intersect(r, ctx->clip);
    if (gx_rect_empty(clipped))
        return 0;

    if (ctx->opacity != 255)
        color = gx_color_mul_alpha(color, ctx->opacity);

    (void)ctx->mask; /* mask sampling reserved for layered blit path */

    if (ctx->round_radius > 0)
        gx_accel_fill_round(ctx->target, clipped, ctx->round_radius, color);
    else
        gx_accel_fill(ctx->target, clipped, color);
    return 0;
}
