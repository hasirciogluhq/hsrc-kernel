#include <user/mke.h>
#include <user/sdk/gfx.hpp>
#include <user/sdk/process.hpp>
#include <user/sdk/settings.hpp>
#include <user/sdk/syscall.hpp>

/*
 * HSRC OS — macOS-inspired desktop shell (ring-3 C++ via hsrc::sdk).
 * Single .mke app: menubar + dock + desktop watermark.
 */

namespace {

using hsrc::sdk::Color;
using hsrc::sdk::Input;
using hsrc::sdk::ScreenInfo;
using hsrc::sdk::Surface;
using hsrc::sdk::Window;
using hsrc::sdk::WindowOptions;
using hsrc::sdk::kTransparent;
using hsrc::sdk::rgb;
using hsrc::sdk::rgba;

constexpr int kMenubarH = 28;
constexpr int kDockH    = 72;
constexpr int kDockIcon = 52;
constexpr int kDockGap  = 16;
constexpr int kDockPad  = 20;
constexpr int kDockRad  = 18;
constexpr int kIconRad  = 12;

constexpr Color kDesktop   = rgb(48, 92, 140);
constexpr Color kMenubarFg = rgb(28, 28, 30);
constexpr Color kMenubarAccent = rgb(35, 131, 226);
constexpr Color kDockFg    = rgb(255, 255, 255);
constexpr Color kWatermark = rgba(255, 255, 255, 48);

struct DockItem {
    const char *label;
    Color       color;
};

constexpr DockItem kDockItems[] = {
    { "Mon", rgb(75, 180, 120) },
    { "Term", rgb(36, 36, 40) },
    { "Files", rgb(255, 190, 60) },
    { "Prefs", rgb(150, 150, 160) },
};

constexpr int kDockCount = (int)(sizeof(kDockItems) / sizeof(kDockItems[0]));

int g_sw = 0;
int g_sh = 0;

Window g_desktop;
Window g_menubar;
Window g_dock;

int g_dock_x = 0;
int g_dock_y = 0;
int g_dock_w = 0;
int g_hover  = -1;
int g_menu_hover = -1;
uint8_t g_prev_buttons = 0;

constexpr int kMenuSettingsX = 92;
constexpr int kMenuSystemInfoX = 160;

int text_width(const char *s)
{
    return Surface::text_width(s, 1);
}

int dock_width()
{
    return kDockCount * kDockIcon + (kDockCount - 1) * kDockGap + kDockPad * 2;
}

void paint_desktop()
{
    Surface &s = g_desktop.surface();
    s.clear(kTransparent);
    /* Small watermark window (not fullscreen) — wallpaper carries the desktop color. */
    s.text_centered((int)s.width() / 2, (int)s.height() / 2 - 10, "HSRC OS", kWatermark, 2);
    s.text_centered((int)s.width() / 2, (int)s.height() / 2 + 24, "desktop",
                    rgba(255, 255, 255, 36), 1);
    g_desktop.damage();
}

void paint_menubar()
{
    Surface &s = g_menubar.surface();
    s.clear(kTransparent);
    s.fill(0, 0, g_sw, 1, rgba(255, 255, 255, 96));
    s.fill(0, kMenubarH - 1, g_sw, 1, rgba(0, 0, 0, 48));

    /* Menubar glyphs stay fully opaque above the acrylic layer. */
    s.fill_round(10, 6, 16, 16, 8, rgb(30, 30, 32));
    s.text(32, 10, "HSRC", kMenubarFg, 1);

    s.text(kMenuSettingsX, 10, "Settings",
           g_menu_hover == 0 ? kMenubarAccent : kMenubarFg, 1);
    s.text(kMenuSystemInfoX, 10, "System Information",
           g_menu_hover == 1 ? kMenubarAccent : kMenubarFg, 1);

    g_menubar.damage();
}

void paint_dock()
{
    Surface &s = g_dock.surface();
    s.clear(kTransparent);
    s.fill_round(0, 0, g_dock_w, kDockH, kDockRad, rgba(255, 255, 255, 18));
    s.fill_round(1, 1, g_dock_w - 2, kDockH - 2, kDockRad - 1, rgba(255, 255, 255, 12));
    s.fill(12, 1, g_dock_w - 24, 1, rgba(255, 255, 255, 78));
    s.fill(16, kDockH - 2, g_dock_w - 32, 1, rgba(0, 0, 0, 48));

    const int total = kDockCount * kDockIcon + (kDockCount - 1) * kDockGap;
    const int x0 = (g_dock_w - total) / 2;
    const int y0 = (kDockH - kDockIcon) / 2;

    for (int i = 0; i < kDockCount; i++) {
        const int ix = x0 + i * (kDockIcon + kDockGap);
        const int lift = (i == g_hover) ? 4 : 0;
        const int iy = y0 - lift;
        const int sz = kDockIcon + (i == g_hover ? 4 : 0);

        s.fill_round(ix - (sz - kDockIcon) / 2, iy, sz, sz, kIconRad, kDockItems[i].color);
        s.fill_round(ix - (sz - kDockIcon) / 2 + 6, iy + 4, sz - 12, 12, 6,
                     rgba(255, 255, 255, 55));
        const int tw = text_width(kDockItems[i].label);
        s.text(ix + (kDockIcon - tw) / 2, iy + sz / 2 - 4,
               kDockItems[i].label, kDockFg, 1);
    }

    g_dock.damage();
}

int dock_hit(int x, int y)
{
    if (x < g_dock_x || y < g_dock_y || x >= g_dock_x + g_dock_w ||
        y >= g_dock_y + kDockH)
        return -1;

    const int lx = x - g_dock_x;
    const int total = kDockCount * kDockIcon + (kDockCount - 1) * kDockGap;
    const int x0 = (g_dock_w - total) / 2;
    for (int i = 0; i < kDockCount; i++) {
        const int ix = x0 + i * (kDockIcon + kDockGap);
        if (lx >= ix && lx < ix + kDockIcon)
            return i;
    }
    return -1;
}

int menubar_hit(int x, int y)
{
    if (y < 0 || y >= kMenubarH)
        return -1;

    const int settings_w = text_width("Settings");
    if (x >= kMenuSettingsX && x < kMenuSettingsX + settings_w)
        return 0;

    const int info_w = text_width("System Information");
    if (x >= kMenuSystemInfoX && x < kMenuSystemInfoX + info_w)
        return 1;

    return -1;
}

bool build_ui()
{
    if (!hsrc::sdk::set_wallpaper_default() &&
        !hsrc::sdk::set_wallpaper_color(kDesktop))
        return false;

    /* Avoid a fullscreen surface (~2MB); wallpaper already fills the screen. */
    constexpr int kMarkW = 360;
    constexpr int kMarkH = 100;
    WindowOptions desktop_opts;
    desktop_opts.x = (g_sw - kMarkW) / 2;
    desktop_opts.y = (g_sh - kMarkH) / 2 - 20;
    desktop_opts.w = kMarkW;
    desktop_opts.h = kMarkH;
    desktop_opts.background = true;
    desktop_opts.no_drag = true;
    desktop_opts.no_title = true;
    desktop_opts.alpha = true;
    desktop_opts.accept_focus = false;
    desktop_opts.set_title("desktop");
    desktop_opts.set_class_name("shell.desktop");
    if (!g_desktop.create(desktop_opts))
        return false;

    WindowOptions menubar_opts;
    menubar_opts.x = 0;
    menubar_opts.y = 0;
    menubar_opts.w = g_sw;
    menubar_opts.h = kMenubarH;
    menubar_opts.background = true;
    menubar_opts.no_drag = true;
    menubar_opts.no_title = true;
    menubar_opts.acrylic = true;
    menubar_opts.alpha = true;
    menubar_opts.accept_focus = false;
    menubar_opts.topmost = true;
    menubar_opts.set_title("menubar");
    menubar_opts.set_class_name("shell.menubar");
    if (!g_menubar.create(menubar_opts))
        return false;

    g_dock_w = dock_width();
    g_dock_x = (g_sw - g_dock_w) / 2;
    g_dock_y = g_sh - kDockH - 16;

    WindowOptions dock_opts;
    dock_opts.x = g_dock_x;
    dock_opts.y = g_dock_y;
    dock_opts.w = g_dock_w;
    dock_opts.h = kDockH;
    dock_opts.radius = kDockRad;
    dock_opts.rounded = true;
    dock_opts.no_drag = true;
    dock_opts.no_title = true;
    dock_opts.background = true;
    dock_opts.acrylic = true;
    dock_opts.alpha = true;
    dock_opts.accept_focus = false;
    dock_opts.topmost = true;
    dock_opts.set_title("dock");
    dock_opts.set_class_name("shell.dock");
    if (!g_dock.create(dock_opts))
        return false;

    paint_desktop();
    paint_menubar();
    paint_dock();
    return true;
}

} // namespace

extern "C" void mke_main(void)
{
    ScreenInfo info{};
    if (!hsrc::sdk::screen_info(info) || info.width == 0 || info.height == 0) {
        for (;;)
            hsrc::sdk::yield();
    }

    g_sw = (int)info.width;
    g_sh = (int)info.height;

    if (!build_ui()) {
        (void)hsrc::sdk::set_wallpaper_color(rgb(160, 30, 30));
        (void)hsrc::sdk::present();
        for (;;)
            hsrc::sdk::yield();
    }

    (void)hsrc::sdk::present();

    for (;;) {
        Input in{};
        if (hsrc::sdk::input(in)) {
            const int menu_hover = menubar_hit(in.mouse_x, in.mouse_y);
            const int hover = dock_hit(in.mouse_x, in.mouse_y);
            if (menu_hover != g_menu_hover) {
                g_menu_hover = menu_hover;
                paint_menubar();
            }
            if (hover != g_hover) {
                g_hover = hover;
                paint_dock();
            }

            const uint8_t pressed = (uint8_t)(in.buttons & ~g_prev_buttons);
            if (pressed & UGX_BTN_LEFT) {
                if (g_menu_hover == 0)
                    (void)hsrc::sdk::settings::open();
                else if (g_menu_hover == 1)
                    (void)hsrc::sdk::settings::open_deeplink("settings://about");

                if (g_hover == 0) {
                    constexpr const char *kMonTitle = "Activity Monitor";
                    constexpr const char *kMonPath = "/applications/activity-monitor.mke";
                    long mid = hsrc::sdk::syscall1(SYS_WM_FIND, (long)kMonTitle);
                    if (mid >= 0) {
                        WindowOptions opts;
                        if (hsrc::sdk::window_get((int)mid, opts)) {
                            opts.visible = true;
                            opts.minimized = false;
                            (void)hsrc::sdk::window_set((int)mid, opts);
                        }
                        (void)hsrc::sdk::syscall1(SYS_WM_FOCUS, mid);
                    } else {
                        (void)hsrc::sdk::process::spawn(kMonPath);
                    }
                } else if (g_hover == 1) {
                    constexpr const char *kTermTitle = "Terminal";
                    constexpr const char *kTermPath = "/applications/terminal.mke";
                    long tid = hsrc::sdk::syscall1(SYS_WM_FIND, (long)kTermTitle);
                    if (tid >= 0) {
                        WindowOptions opts;
                        if (hsrc::sdk::window_get((int)tid, opts)) {
                            opts.visible = true;
                            opts.minimized = false;
                            (void)hsrc::sdk::window_set((int)tid, opts);
                        }
                        (void)hsrc::sdk::syscall1(SYS_WM_FOCUS, tid);
                    } else {
                        (void)hsrc::sdk::process::spawn(kTermPath);
                    }
                } else if (g_hover == 2) {
                    constexpr const char *kFilesTitle = "Files";
                    constexpr const char *kFilesPath = "/applications/files.mke";
                    long fid = hsrc::sdk::syscall1(SYS_WM_FIND, (long)kFilesTitle);
                    if (fid >= 0) {
                        WindowOptions opts;
                        if (hsrc::sdk::window_get((int)fid, opts)) {
                            opts.visible = true;
                            opts.minimized = false;
                            (void)hsrc::sdk::window_set((int)fid, opts);
                        }
                        (void)hsrc::sdk::syscall1(SYS_WM_FOCUS, fid);
                    } else {
                        (void)hsrc::sdk::process::spawn(kFilesPath);
                    }
                } else if (g_hover == 3) {
                    (void)hsrc::sdk::settings::open();
                }
                if (g_hover >= 0)
                    paint_dock();
            }
            g_prev_buttons = in.buttons;
        }

        (void)hsrc::sdk::present();
        hsrc::sdk::yield();
    }
}
