#include <gfx/compositor.h>
#include <gfx/draw.h>
#include <gfx/blur.h>
#include <kernel/string.h>

static int alloc_slot(gx_compositor *c)
{
    for (int i = 0; i < GX_MAX_LAYERS; i++) {
        if (!c->layers[i].used)
            return i;
    }
    return -1;
}

static void sort_ids_by_z(gx_compositor *c, int *ids, int *n)
{
    *n = 0;
    for (int i = 0; i < GX_MAX_LAYERS; i++) {
        if (c->layers[i].used && c->layers[i].visible)
            ids[(*n)++] = i;
    }
    for (int i = 0; i < *n; i++) {
        for (int j = i + 1; j < *n; j++) {
            if (c->layers[ids[j]].z < c->layers[ids[i]].z) {
                int t = ids[i];
                ids[i] = ids[j];
                ids[j] = t;
            }
        }
    }
}

int gx_compositor_init(gx_compositor *c, gx_device *dev)
{
    if (!c || !dev || !dev->ready)
        return -1;
    memset(c, 0, sizeof(*c));
    c->device = dev;
    return 0;
}

void gx_compositor_shutdown(gx_compositor *c)
{
    if (!c)
        return;
    if (c->wallpaper_blurred) {
        gx_surface_destroy(c->wallpaper_blurred);
        c->wallpaper_blurred = NULL;
    }
    c->wallpaper = NULL;
    memset(c->layers, 0, sizeof(c->layers));
}

void gx_compositor_set_wallpaper(gx_compositor *c, gx_surface *wp)
{
    if (!c)
        return;
    c->wallpaper = wp;

    if (c->wallpaper_blurred) {
        gx_surface_destroy(c->wallpaper_blurred);
        c->wallpaper_blurred = NULL;
    }
    if (!wp)
        return;

    c->wallpaper_blurred = gx_surface_create(wp->width, wp->height);
    if (!c->wallpaper_blurred)
        return;

    gx_blit(c->wallpaper_blurred, 0, 0, wp);
    gx_blur_box(c->wallpaper_blurred, 4);
}

int gx_compositor_add_layer(gx_compositor *c, gx_layer *desc)
{
    if (!c || !desc)
        return -1;
    int id = alloc_slot(c);
    if (id < 0)
        return -1;

    c->layers[id] = *desc;
    c->layers[id].used = 1;
    if (c->layers[id].opacity == 0 && desc->style != GX_LAYER_OPAQUE)
        c->layers[id].opacity = 255;
    if (!c->layers[id].visible)
        c->layers[id].visible = 1;
    c->layer_count++;
    return id;
}

void gx_compositor_remove_layer(gx_compositor *c, int id)
{
    if (!c || id < 0 || id >= GX_MAX_LAYERS || !c->layers[id].used)
        return;
    memset(&c->layers[id], 0, sizeof(c->layers[id]));
    c->layer_count--;
}

gx_layer *gx_compositor_layer(gx_compositor *c, int id)
{
    if (!c || id < 0 || id >= GX_MAX_LAYERS || !c->layers[id].used)
        return NULL;
    return &c->layers[id];
}

void gx_compositor_raise(gx_compositor *c, int id)
{
    gx_layer *L = gx_compositor_layer(c, id);
    if (!L)
        return;
    int maxz = L->z;
    for (int i = 0; i < GX_MAX_LAYERS; i++) {
        if (c->layers[i].used && c->layers[i].z > maxz)
            maxz = c->layers[i].z;
    }
    L->z = maxz + 1;
}

static void paint_acrylic(gx_compositor *c, gx_surface *bb, gx_layer *L)
{
    gx_surface *blurred = c->wallpaper_blurred ? c->wallpaper_blurred : c->wallpaper;
    if (!blurred)
        return;

    gx_rect r = L->bounds;
    for (int32_t y = 0; y < r.h; y++) {
        int32_t sy = r.y + y;
        if (sy < 0 || (uint32_t)sy >= bb->height)
            continue;
        for (int32_t x = 0; x < r.w; x++) {
            int32_t sx = r.x + x;
            if (sx < 0 || (uint32_t)sx >= bb->width)
                continue;
            gx_color base = gx_surface_get(blurred, sx, sy);
            gx_color tinted = gx_blend(base, L->tint);
            bb->pixels[(uint32_t)sy * bb->stride + (uint32_t)sx] = tinted;
        }
    }

    if (L->surface)
        gx_blit_alpha(bb, r.x, r.y, L->surface, L->opacity ? L->opacity : 255);
}

static void paint_blur_behind(gx_compositor *c, gx_surface *bb, gx_layer *L)
{
    (void)c;
    gx_rect r = L->bounds;
    if (gx_rect_empty(r))
        return;

    gx_surface *tmp = gx_surface_create((uint32_t)r.w, (uint32_t)r.h);
    if (!tmp) {
        if (L->surface)
            gx_blit(bb, r.x, r.y, L->surface);
        return;
    }

    int radius = L->blur_radius > 0 ? L->blur_radius : 3;
    gx_blur_copy(bb, r, tmp, radius);
    gx_blit(bb, r.x, r.y, tmp);
    gx_fill_rect_aa(bb, r, L->tint);
    if (L->surface)
        gx_blit_alpha(bb, r.x, r.y, L->surface, L->opacity ? L->opacity : 255);
    gx_surface_destroy(tmp);
}

void gx_compositor_compose(gx_compositor *c)
{
    if (!c || !c->device || !c->device->backbuffer)
        return;

    gx_surface *bb = c->device->backbuffer;

    if (c->wallpaper)
        gx_blit(bb, 0, 0, c->wallpaper);
    else
        gx_surface_clear(bb, GX_RGB(20, 22, 30));

    int ids[GX_MAX_LAYERS];
    int n = 0;
    sort_ids_by_z(c, ids, &n);

    for (int i = 0; i < n; i++) {
        gx_layer *L = &c->layers[ids[i]];
        switch (L->style) {
        case GX_LAYER_ACRYLIC:
            paint_acrylic(c, bb, L);
            break;
        case GX_LAYER_BLUR_BEHIND:
            paint_blur_behind(c, bb, L);
            break;
        case GX_LAYER_ALPHA:
            if (L->surface)
                gx_blit_alpha(bb, L->bounds.x, L->bounds.y, L->surface,
                              L->opacity ? L->opacity : 255);
            break;
        case GX_LAYER_OPAQUE:
        default:
            if (L->surface)
                gx_blit(bb, L->bounds.x, L->bounds.y, L->surface);
            break;
        }
    }

    gx_device_present(c->device);
}
