#include <user/sdk/wm.hpp>
#include <user/sdk/kilim.hpp>
#include <user/sdk/reed.hpp>
#include <user/sdk/syscall.hpp>
#include <kernel/syscall.h>

/*
 * OS Shell — minimal desktop chrome on wm+kilim (full UX restore later).
 */

namespace {

[[noreturn]] void hang(void)
{
    for (;;)
        hsrc::sdk::syscall0(SYS_YIELD);
}

} // namespace

extern "C" void exec_main(void)
{
    reed::Device dev;
    if (dev.init() < 0)
        hang();

    kilim::Context k;
    if (k.init(&dev) < 0)
        hang();

    int sw = (int)dev.caps().width;
    int sh = (int)dev.caps().height;
    if (sw < 640)
        sw = 640;
    if (sh < 480)
        sh = 480;

    wm::WindowOptions opts;
    opts.x = 0;
    opts.y = 0;
    opts.w = (int32_t)sw;
    opts.h = (int32_t)sh;
    opts.background = true;
    opts.framed = false;
    opts.no_title = true;
    opts.no_drag = true;
    opts.accept_focus = false;
    opts.always_on_bottom = true;
    opts.resizable = false;
    opts.set_title("OS Shell");
    opts.set_class_name("os-shell");

    wm::Window win;
    if (!win.create(opts))
        hang();

    bool mapped = false;
    const int menubar_h = 28;
    const int dock_h = 64;
    const int dock_w = 280;

    for (;;) {
        wm::Input in;
        (void)wm::input_snapshot(in);
        k.set_pointer(in.mouse_x, in.mouse_y, in.buttons);

        if (k.begin_frame() < 0) {
            hsrc::sdk::yield(1);
            continue;
        }

        /* Full-screen desktop (same tone as WM clear). */
        k.fill_rect(0, 0, sw, sh, kilim::rgba(26, 31, 46, 255));

        /* Soft vignette strips */
        k.fill_rect(0, 0, sw, menubar_h, kilim::rgba(18, 20, 28, 230));
        k.text("hsrcOS", 14, 6, 14, kilim::rgba(235, 238, 245, 255));
        k.text("Shell", 90, 8, 12, kilim::rgba(160, 170, 190, 255));

        int dock_x = (sw - dock_w) / 2;
        int dock_y = sh - dock_h - 18;
        k.fill_round_rect(dock_x, dock_y, dock_w, dock_h, 16,
                          kilim::rgba(32, 36, 48, 220));
        /* Dock icon placeholders */
        for (int i = 0; i < 5; i++) {
            int ix = dock_x + 24 + i * 48;
            int iy = dock_y + 12;
            k.fill_round_rect(ix, iy, 40, 40, 10,
                              kilim::rgba(70, 90, 140, 255));
        }

        k.text("Desktop ready", 14, menubar_h + 16, 14,
               kilim::rgba(180, 190, 210, 255));

        (void)k.commit_frame();

        if (!mapped)
            mapped = win.map_surface(dev, k.target());
        (void)win.damage();
        hsrc::sdk::yield(1);
    }
}
