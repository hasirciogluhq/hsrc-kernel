#include <user/sdk/wm.hpp>
#include <user/sdk/kilim.hpp>
#include <user/sdk/reed.hpp>
#include <user/sdk/syscall.hpp>
#include <kernel/syscall.h>

/*
 * Minesweeper — temporary wm+kilim smoke stub (legacy gfx UI removed).
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
    opts.x = 200;
    opts.y = 140;
    opts.w = 320;
    opts.h = 360;
    opts.set_title("Minesweeper");
    opts.set_class_name("minesweeper");

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
        k.fill_rect(20, 20, 260, 48, kilim::rgba(40, 40, 50, 255));
        k.text("Minesweeper (wm+kilim stub)", 28, 32, 16,
               kilim::rgba(255, 255, 255, 255));
        (void)k.commit_frame();

        if (!mapped)
            mapped = win.map_surface(dev, k.target());
        (void)win.damage();
        hsrc::sdk::yield(1);
    }
}
