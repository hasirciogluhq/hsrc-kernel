#include <user/sdk/wm.hpp>
#include <user/sdk/kilim.hpp>
#include <user/sdk/reed.hpp>
#include <user/sdk/syscall.hpp>
#include <kernel/syscall.h>

/*
 * OS Shell — temporary wm+kilim smoke stub (legacy gfx desktop UI removed).
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

    wm::WindowOptions opts;
    opts.x = 0;
    opts.y = 0;
    opts.w = (int32_t)dev.caps().width;
    opts.h = (int32_t)dev.caps().height;
    if (opts.w < 640)
        opts.w = 640;
    if (opts.h < 480)
        opts.h = 480;
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
        k.fill_rect(20, 20, 280, 48, kilim::rgba(40, 40, 50, 255));
        k.text("OS Shell (wm+kilim stub)", 28, 32, 16, kilim::rgba(255, 255, 255, 255));
        (void)k.commit_frame();

        if (!mapped)
            mapped = win.map_surface(dev, k.target());
        (void)win.damage();
        hsrc::sdk::yield(1);
    }
}
