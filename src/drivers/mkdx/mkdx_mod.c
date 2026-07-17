#include "mkdx.h"
#include <kernel/mkdx_api.h>
#include <kernel/string.h>
#include <user/gx.h>
#include <drivers/display.h>
#include <drivers/driver.h>
#include <drivers/mouse.h>
#include <drivers/keyboard.h>
#include <drivers/serial.h>

static mkdx_gpu g_gpu;

int mkdx_get_screen_size(uint32_t *w, uint32_t *h, uint32_t *bpp)
{
    return display_get_screen_size(w, h, bpp);
}

int mkdx_present(void)
{
    if (!gx_server_get())
        return -1;
    gx_server_present();
    return 0;
}

static int api_info(uint32_t *w, uint32_t *h, uint32_t *bpp)
{
    gx_server *s = gx_server_get();
    if (!s)
        return -1;
    if (w)
        *w = s->device.mode.width;
    if (h)
        *h = s->device.mode.height;
    if (bpp)
        *bpp = s->device.mode.bpp;
    return 0;
}

static int api_present(void)
{
    return mkdx_present();
}

static void api_mark_dirty(void)
{
    gx_server_mark_dirty();
}

static long api_wm_create(const void *args, uint32_t owner_pid)
{
    const ugx_win_create *a = (const ugx_win_create *)args;
    gx_server *s = gx_server_get();
    wm_create_args wa;
    wm_window *w;

    if (!a || !s)
        return -1;
    memset(&wa, 0, sizeof(wa));
    wa.x = a->x;
    wa.y = a->y;
    wa.w = a->w;
    wa.h = a->h;
    wa.style = a->style;
    wa.radius = a->radius;
    strncpy(wa.title, a->title, WM_TITLE_MAX - 1);
    w = wm_create(&s->wm, &wa, (int)owner_pid);
    return w ? (long)w->id : -1;
}

static int api_wm_destroy(int id)
{
    gx_server *s = gx_server_get();
    if (!s)
        return -1;
    wm_destroy(&s->wm, id);
    return 0;
}

static int api_wm_map(int id, void *out)
{
    gx_server *s = gx_server_get();
    ugx_map *m = (ugx_map *)out;
    wm_map_info info;
    if (!s || !m)
        return -1;
    if (wm_map(&s->wm, id, &info) < 0)
        return -1;
    m->pixels = info.pixels;
    m->width = info.width;
    m->height = info.height;
    m->stride = info.stride;
    return 0;
}

static int api_wm_move(int id, int32_t x, int32_t y)
{
    gx_server *s = gx_server_get();
    if (!s)
        return -1;
    wm_move(&s->wm, id, x, y);
    return 0;
}

static int api_wm_resize(int id, int32_t w, int32_t h)
{
    gx_server *s = gx_server_get();
    if (!s)
        return -1;
    wm_resize(&s->wm, id, w, h);
    return 0;
}

static int api_wm_focus(int id)
{
    gx_server *s = gx_server_get();
    if (!s)
        return -1;
    wm_focus(&s->wm, id);
    return 0;
}

static int api_wm_show(int id, int vis)
{
    gx_server *s = gx_server_get();
    if (!s)
        return -1;
    wm_show(&s->wm, id, vis);
    return 0;
}

static int api_wm_get_frame(int id, void *out)
{
    gx_server *s = gx_server_get();
    ugx_frame *f = (ugx_frame *)out;
    wm_window *w;
    if (!s || !f)
        return -1;
    w = wm_get(&s->wm, id);
    if (!w)
        return -1;
    f->x = w->frame.x;
    f->y = w->frame.y;
    f->w = w->frame.w;
    f->h = w->frame.h;
    return 0;
}

static int api_wm_pop_key(int id)
{
    gx_server *s = gx_server_get();
    if (!s)
        return -1;
    return wm_pop_key(&s->wm, id);
}

static int api_wm_focused_id(void)
{
    gx_server *s = gx_server_get();
    if (!s)
        return -1;
    return wm_focused_id(&s->wm);
}

static int api_wm_find(const char *title)
{
    gx_server *s = gx_server_get();
    if (!s || !title)
        return -1;
    return wm_find_by_title(&s->wm, title);
}

static int api_fill(const void *args, int rounded)
{
    const ugx_fill_args *a = (const ugx_fill_args *)args;
    gx_server *s = gx_server_get();
    wm_window *w;
    gx_rect r;

    if (!a || !s)
        return -1;
    w = wm_get(&s->wm, a->win);
    if (!w || !w->surface)
        return -1;

    r = gx_rect_make(a->x, a->y, a->w, a->h);
    if (rounded)
        gx_accel_fill_round(w->surface, r, a->radius, a->color);
    else
        gx_accel_fill(w->surface, r, a->color);
    gx_server_mark_dirty();
    return 0;
}

static int api_set_wallpaper(const void *args)
{
    const ugx_wallpaper *a = (const ugx_wallpaper *)args;
    gx_server *s = gx_server_get();
    uint32_t tw, th;

    if (!a || !a->pixels || !s || a->width == 0 || a->height == 0)
        return -1;

    if (s->wallpaper)
        gx_surface_destroy(s->wallpaper);

    /* Keep solid colors as 1x1 — expanding to fullscreen OOMs the bump heap. */
    tw = a->width;
    th = a->height;

    s->wallpaper = gx_surface_create(tw, th);
    if (!s->wallpaper)
        return -1;

    if (a->width == 1 && a->height == 1) {
        s->wallpaper->pixels[0] = a->pixels[0];
    } else {
        uint32_t y, x;
        for (y = 0; y < a->height && y < th; y++) {
            for (x = 0; x < a->width && x < tw; x++) {
                s->wallpaper->pixels[y * s->wallpaper->stride + x] =
                    a->pixels[y * a->stride + x];
            }
        }
    }

    gx_compositor_set_wallpaper(&s->comp, s->wallpaper);
    gx_server_mark_dirty();
    return 0;
}

static int api_input_state(void *out)
{
    ugx_input_state *o = (ugx_input_state *)out;
    gx_server *s = gx_server_get();
    const mouse_state_t *ms = mouse_get();
    if (!o || !s || !ms)
        return -1;
    o->mouse_x = ms->x;
    o->mouse_y = ms->y;
    o->buttons = ms->buttons;
    o->mods = keyboard_modifiers();
    o->focus_id = wm_focused_id(&s->wm);
    return 0;
}

static const mkdx_api_t g_api = {
    .info = api_info,
    .present = api_present,
    .mark_dirty = api_mark_dirty,
    .wm_create = api_wm_create,
    .wm_destroy = api_wm_destroy,
    .wm_map = api_wm_map,
    .wm_move = api_wm_move,
    .wm_resize = api_wm_resize,
    .wm_focus = api_wm_focus,
    .wm_show = api_wm_show,
    .wm_get_frame = api_wm_get_frame,
    .wm_pop_key = api_wm_pop_key,
    .wm_focused_id = api_wm_focused_id,
    .wm_find = api_wm_find,
    .fill = api_fill,
    .set_wallpaper = api_set_wallpaper,
    .input_state = api_input_state,
};

static int mkdx_drv_probe(driver_t *drv, void *ctx)
{
    (void)drv;
    (void)ctx;
    return display_active() ? 0 : -1;
}

static int mkdx_drv_init(driver_t *drv, void *ctx)
{
    (void)drv;
    (void)ctx;
    klog("[mkdx] init\n");
    if (gx_server_init() < 0) {
        klog("[mkdx] gx_server_init FAILED\n");
        return -1;
    }
    if (mkdx_gpu_init(&g_gpu) < 0) {
        klog("[mkdx] mkdx_gpu_init FAILED\n");
        return -1;
    }
    mkdx_api_register(&g_api);
    klog("[mkdx] ready\n");
    return 0;
}

int kmod_init(void)
{
    driver_t d;
    memset(&d, 0, sizeof(d));
    strncpy(d.name, "mkdx", DRIVER_NAME_MAX - 1);
    strncpy(d.version, "1.0", DRIVER_VERSION_MAX - 1);
    d.kind = DRIVER_KIND_CUSTOM;
    d.class = DRIVER_CLASS_DISPLAY;
    d.flags = 0;
    d.priority = 50;
    d.probe = mkdx_drv_probe;
    d.init = mkdx_drv_init;

    if (driver_register(&d) < 0)
        return -1;
    if (driver_load("mkdx", NULL) < 0)
        return -1;
    return 0;
}
