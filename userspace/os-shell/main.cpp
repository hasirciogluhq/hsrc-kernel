#include <user/sdk/wm.hpp>
#include <user/sdk/kilim.hpp>
#include <user/sdk/reed.hpp>
#include <user/sdk/syscall.hpp>
#include <kernel/syscall.h>

/*
 * OS Shell — desktop wallpaper / background surface for the WM.
 * Menubar + dock chrome are drawn by window-manager (always-on-top).
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
    static reed::Device dev;
    static kilim::Context k;

    if (dev.init() < 0)
        hang();

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

    for (;;) {
        wm::Input in;
        (void)wm::input_snapshot(in);
        k.set_pointer(in.mouse_x, in.mouse_y, in.buttons);

        if (k.begin_frame() < 0) {
            hsrc::sdk::yield(1);
            continue;
        }

        /* Wallpaper — opaque so WM blit is solid. */
        k.fill_rect(0, 0, sw, sh, kilim::rgba(26, 31, 46, 255));
        /* Soft top/bottom bands under system chrome. */
        k.fill_rect(0, 0, sw, 40, kilim::rgba(20, 24, 34, 255));
        k.fill_rect(0, sh - 100, sw, 100, kilim::rgba(18, 22, 32, 255));
        k.text("Desktop", 14, 48, 16, kilim::rgba(170, 180, 200, 255));

        (void)k.commit_frame();

        if (!mapped)
            mapped = win.map_surface(dev, k.target());
        (void)win.damage();
        hsrc::sdk::yield(1);
    }
}
