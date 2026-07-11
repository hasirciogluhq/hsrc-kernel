#include <gfx/mkdx.h>
#include <gfx/draw.h>
#include <gfx/font.h>
#include <kernel/string.h>

static void paint_wallpaper(gx_surface *wp)
{
    gx_gradient_v(wp, gx_rect_make(0, 0, (int32_t)wp->width, (int32_t)wp->height),
                  GX_RGB(24, 32, 56), GX_RGB(12, 14, 22));

    /* soft orbs */
    for (int i = 0; i < 5; i++) {
        int32_t cx = (int32_t)(wp->width / 6) * (i + 1);
        int32_t cy = (int32_t)(wp->height / 3) + (i % 2) * 80;
        gx_fill_circle(wp, cx, cy, 90 + i * 12, GX_RGBA(60 + i * 20, 100, 180, 90));
    }

    gx_draw_text(wp, 24, 24, "mykernel / MKDX", GX_RGBA(255, 255, 255, 180));
    gx_draw_text(wp, 24, 40, "DirectX-style software graphics", GX_RGBA(200, 210, 230, 140));
}

static void paint_about(gx_surface *s)
{
    gx_surface_clear(s, GX_RGBA(255, 255, 255, 200));
    gx_draw_text(s, 16, 16, "MKDX Graphics Stack", GX_RGB(50, 48, 44));
    gx_draw_text(s, 16, 36, "- surfaces & ARGB8888", GX_RGB(115, 114, 110));
    gx_draw_text(s, 16, 52, "- fill / blit / alpha", GX_RGB(115, 114, 110));
    gx_draw_text(s, 16, 68, "- box blur & acrylic", GX_RGB(115, 114, 110));
    gx_draw_text(s, 16, 84, "- compositor + WM", GX_RGB(115, 114, 110));
    gx_fill_round_rect(s, gx_rect_make(16, 110, 120, 28), 6, GX_RGB(35, 131, 226));
    gx_draw_text(s, 36, 118, "Get Started", GX_WHITE);
}

static void paint_terminal(gx_surface *s)
{
    gx_surface_clear(s, GX_RGB(18, 18, 20));
    gx_draw_text(s, 12, 12, "$ uname -a", GX_RGB(120, 200, 120));
    gx_draw_text(s, 12, 28, "mykernel i386 mkdx", GX_RGB(200, 200, 200));
    gx_draw_text(s, 12, 52, "$ gfxinfo", GX_RGB(120, 200, 120));
    gx_draw_text(s, 12, 68, "backend: software", GX_RGB(200, 200, 200));
    gx_draw_text(s, 12, 84, "effects: blur, acrylic", GX_RGB(200, 200, 200));
    gx_draw_text(s, 12, 108, "$ _", GX_RGB(120, 200, 120));
}

static void paint_panel(gx_surface *s)
{
    gx_surface_clear(s, GX_TRANSPARENT);
    gx_fill_rect_aa(s, gx_rect_make(0, 0, (int32_t)s->width, (int32_t)s->height),
                    GX_RGBA(255, 255, 255, 30));
    gx_draw_text(s, 20, ((int32_t)s->height - GX_FONT_H) / 2,
                 "Dock  |  Files  |  Terminal  |  Settings",
                 GX_RGBA(255, 255, 255, 220));
}

void mkdx_demo(void)
{
    static gx_device device;
    static gx_compositor comp;
    static wm_t wm;

    if (gx_device_init(&device) < 0)
        return;

    gx_surface *wp = gx_surface_create(device.fb->width, device.fb->height);
    if (!wp)
        return;
    paint_wallpaper(wp);

    if (gx_compositor_init(&comp, &device) < 0)
        return;
    gx_compositor_set_wallpaper(&comp, wp);

    if (wm_init(&wm, &comp) < 0)
        return;

    wm_window *about = wm_create(&wm, "About MKDX",
                                 gx_rect_make(80, 70, 340, 200),
                                 WM_STYLE_ACRYLIC);
    if (about) {
        paint_about(about->client_surf);
        wm_paint_chrome(&wm, about->id);
    }

    wm_window *term = wm_create(&wm, "Terminal",
                                gx_rect_make(360, 160, 420, 240),
                                WM_STYLE_NORMAL);
    if (term) {
        paint_terminal(term->client_surf);
        wm_paint_chrome(&wm, term->id);
    }

    /* acrylic bottom dock */
    wm_window *dock = wm_create(&wm, "Dock",
                                gx_rect_make(120, (int32_t)device.fb->height - 64,
                                             (int32_t)device.fb->width - 240, 48),
                                WM_STYLE_ACRYLIC | WM_STYLE_NO_TITLE | WM_STYLE_NO_BORDER);
    if (dock) {
        paint_panel(dock->client_surf);
        wm_paint_chrome(&wm, dock->id);
    }

    if (about)
        wm_focus(&wm, about->id);

    gx_compositor_compose(&comp);
}
