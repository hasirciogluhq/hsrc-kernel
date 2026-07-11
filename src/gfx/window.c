#include <gfx/window.h>
#include <gfx/draw.h>
#include <gfx/font.h>
#include <kernel/string.h>
#include <kernel/heap.h>

static void recompute_client(wm_window *w)
{
    int title_h = (w->style & WM_STYLE_NO_TITLE) ? 0 : WM_TITLEBAR_H;
    int border = (w->style & WM_STYLE_NO_BORDER) ? 0 : WM_BORDER;
    w->client.x = w->frame.x + border;
    w->client.y = w->frame.y + border + title_h;
    w->client.w = w->frame.w - border * 2;
    w->client.h = w->frame.h - border * 2 - title_h;
    if (w->client.w < 0)
        w->client.w = 0;
    if (w->client.h < 0)
        w->client.h = 0;
}

static wm_window *slot_by_id(wm_t *wm, int id)
{
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (wm->windows[i].used && wm->windows[i].id == id)
            return &wm->windows[i];
    }
    return NULL;
}

int wm_init(wm_t *wm, gx_compositor *comp)
{
    if (!wm || !comp)
        return -1;
    memset(wm, 0, sizeof(*wm));
    wm->comp = comp;
    wm->focus_id = -1;
    wm->next_id = 1;
    return 0;
}

void wm_shutdown(wm_t *wm)
{
    if (!wm)
        return;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (wm->windows[i].used)
            wm_destroy(wm, wm->windows[i].id);
    }
}

wm_window *wm_create(wm_t *wm, const char *title, gx_rect frame, uint32_t style)
{
    if (!wm || frame.w < 64 || frame.h < 48)
        return NULL;

    wm_window *w = NULL;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (!wm->windows[i].used) {
            w = &wm->windows[i];
            break;
        }
    }
    if (!w)
        return NULL;

    memset(w, 0, sizeof(*w));
    w->used = 1;
    w->id = wm->next_id++;
    w->frame = frame;
    w->style = style;
    w->visible = 1;
    w->accent = GX_RGB(35, 131, 226);
    strncpy(w->title, title ? title : "Window", WM_TITLE_MAX - 1);
    recompute_client(w);

    w->client_surf = gx_surface_create((uint32_t)w->client.w, (uint32_t)w->client.h);
    if (!w->client_surf) {
        w->used = 0;
        return NULL;
    }
    gx_surface_clear(w->client_surf, GX_RGB(247, 247, 245));

    /* Full window surface for chrome + client */
    gx_surface *full = gx_surface_create((uint32_t)frame.w, (uint32_t)frame.h);
    if (!full) {
        gx_surface_destroy(w->client_surf);
        w->used = 0;
        return NULL;
    }

    gx_layer layer;
    memset(&layer, 0, sizeof(layer));
    layer.visible = 1;
    layer.z = w->id;
    layer.bounds = frame;
    layer.surface = full;
    layer.opacity = 255;
    layer.blur_radius = 4;

    if (style & WM_STYLE_ACRYLIC) {
        layer.style = GX_LAYER_ACRYLIC;
        layer.tint = GX_RGBA(255, 255, 255, 140);
    } else {
        layer.style = GX_LAYER_OPAQUE;
    }

    w->layer_id = gx_compositor_add_layer(wm->comp, &layer);
    if (w->layer_id < 0) {
        gx_surface_destroy(full);
        gx_surface_destroy(w->client_surf);
        w->used = 0;
        return NULL;
    }

    wm_paint_chrome(wm, w->id);
    wm_focus(wm, w->id);
    return w;
}

void wm_destroy(wm_t *wm, int id)
{
    wm_window *w = slot_by_id(wm, id);
    if (!w)
        return;

    gx_layer *L = gx_compositor_layer(wm->comp, w->layer_id);
    if (L && L->surface) {
        gx_surface_destroy(L->surface);
        L->surface = NULL;
    }
    gx_compositor_remove_layer(wm->comp, w->layer_id);

    if (w->client_surf)
        gx_surface_destroy(w->client_surf);

    if (wm->focus_id == id)
        wm->focus_id = -1;
    memset(w, 0, sizeof(*w));
}

wm_window *wm_get(wm_t *wm, int id)
{
    return slot_by_id(wm, id);
}

void wm_set_title(wm_t *wm, int id, const char *title)
{
    wm_window *w = slot_by_id(wm, id);
    if (!w || !title)
        return;
    strncpy(w->title, title, WM_TITLE_MAX - 1);
    wm_paint_chrome(wm, id);
}

void wm_move(wm_t *wm, int id, int32_t x, int32_t y)
{
    wm_window *w = slot_by_id(wm, id);
    if (!w)
        return;
    w->frame.x = x;
    w->frame.y = y;
    recompute_client(w);
    gx_layer *L = gx_compositor_layer(wm->comp, w->layer_id);
    if (L)
        L->bounds = w->frame;
}

void wm_resize(wm_t *wm, int id, int32_t wdt, int32_t hgt)
{
    wm_window *w = slot_by_id(wm, id);
    if (!w || wdt < 64 || hgt < 48)
        return;

    w->frame.w = wdt;
    w->frame.h = hgt;
    recompute_client(w);

    if (w->client_surf)
        gx_surface_destroy(w->client_surf);
    w->client_surf = gx_surface_create((uint32_t)w->client.w, (uint32_t)w->client.h);
    if (w->client_surf)
        gx_surface_clear(w->client_surf, GX_RGB(247, 247, 245));

    gx_layer *L = gx_compositor_layer(wm->comp, w->layer_id);
    if (L) {
        if (L->surface)
            gx_surface_destroy(L->surface);
        L->surface = gx_surface_create((uint32_t)wdt, (uint32_t)hgt);
        L->bounds = w->frame;
    }
    wm_paint_chrome(wm, id);
}

void wm_focus(wm_t *wm, int id)
{
    if (!wm)
        return;

    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (wm->windows[i].used)
            wm->windows[i].focused = (wm->windows[i].id == id);
    }
    wm->focus_id = id;
    wm_window *focused = slot_by_id(wm, id);
    if (focused) {
        gx_compositor_raise(wm->comp, focused->layer_id);
        wm_paint_chrome(wm, id);
        for (int i = 0; i < WM_MAX_WINDOWS; i++) {
            if (wm->windows[i].used && wm->windows[i].id != id)
                wm_paint_chrome(wm, wm->windows[i].id);
        }
    }
}

void wm_show(wm_t *wm, int id, int visible)
{
    wm_window *w = slot_by_id(wm, id);
    if (!w)
        return;
    w->visible = visible;
    gx_layer *L = gx_compositor_layer(wm->comp, w->layer_id);
    if (L)
        L->visible = visible;
}

gx_surface *wm_client_surface(wm_t *wm, int id)
{
    wm_window *w = slot_by_id(wm, id);
    return w ? w->client_surf : NULL;
}

void wm_paint_chrome(wm_t *wm, int id)
{
    wm_window *w = slot_by_id(wm, id);
    if (!w)
        return;

    gx_layer *L = gx_compositor_layer(wm->comp, w->layer_id);
    if (!L || !L->surface)
        return;

    gx_surface *s = L->surface;
    gx_surface_clear(s, GX_TRANSPARENT);

    int border = (w->style & WM_STYLE_NO_BORDER) ? 0 : WM_BORDER;
    int title_h = (w->style & WM_STYLE_NO_TITLE) ? 0 : WM_TITLEBAR_H;

    /* body background for non-acrylic */
    if (!(w->style & WM_STYLE_ACRYLIC)) {
        gx_fill_rect(s, gx_rect_make(0, 0, w->frame.w, w->frame.h), GX_RGB(247, 247, 245));
    } else {
        /* translucent panel so acrylic shows through; client draws opaque-ish */
        gx_fill_rect(s, gx_rect_make(0, 0, w->frame.w, w->frame.h), GX_RGBA(255, 255, 255, 40));
    }

    if (title_h > 0) {
        gx_color bar = w->focused ? w->accent : GX_RGB(90, 90, 88);
        gx_fill_rect(s, gx_rect_make(0, 0, w->frame.w, title_h), bar);
        gx_draw_text(s, 10, (title_h - GX_FONT_H) / 2, w->title, GX_WHITE);

        /* close affordance */
        int cx = w->frame.w - 22;
        int cy = (title_h - 10) / 2;
        gx_draw_line(s, cx, cy, cx + 10, cy + 10, GX_WHITE);
        gx_draw_line(s, cx + 10, cy, cx, cy + 10, GX_WHITE);
    }

    if (border > 0) {
        gx_color bc = w->focused ? w->accent : GX_RGBA(55, 53, 47, 80);
        gx_draw_rect(s, gx_rect_make(0, 0, w->frame.w, w->frame.h), bc, border);
    }

    /* blit client content into window surface */
    if (w->client_surf) {
        int ox = border;
        int oy = border + title_h;
        gx_blit(s, ox, oy, w->client_surf);
    }
}

void wm_paint_all(wm_t *wm)
{
    if (!wm)
        return;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (wm->windows[i].used)
            wm_paint_chrome(wm, wm->windows[i].id);
    }
}
