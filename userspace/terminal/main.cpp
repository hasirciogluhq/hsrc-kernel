#include <user/sdk/wm.hpp>
#include <user/sdk/kilim.hpp>
#include <user/sdk/reed.hpp>
#include <user/sdk/syscall.hpp>
#include <kernel/syscall.h>

/* Terminal — dark console panel with prompt. */

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

    if (dev.init() < 0 || k.init(&dev) < 0)
        hang();

    wm::WindowOptions opts;
    opts.x = 120;
    opts.y = 110;
    opts.w = 720;
    opts.h = 440;
    opts.min_w = 480;
    opts.min_h = 280;
    opts.set_title("Terminal");
    opts.set_class_name("terminal");

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

        int w = opts.w;
        int h = opts.h;
        int top = wm::kChromeTitleH;
        k.fill_rect(0, 0, w, h, kilim::rgba(18, 20, 24, 255));
        k.fill_rect(0, top, w, h - top, kilim::rgba(12, 14, 18, 255));
        k.fill_round_rect(10, top + 10, w - 20, h - top - 20, 8,
                          kilim::rgba(16, 18, 22, 255));

        int y = top + 28;
        k.text("hsrcOS Terminal", 24, y, 13, kilim::rgba(0, 153, 188, 255));
        y += 28;
        k.text("user@hsrcos:~$ uname -a", 24, y, 14, kilim::rgba(80, 250, 123, 255));
        y += 22;
        k.text("hsrcOS i686 mykernel development", 24, y, 14,
               kilim::rgba(220, 225, 235, 255));
        y += 28;
        k.text("user@hsrcos:~$ ls /system/bin", 24, y, 14,
               kilim::rgba(80, 250, 123, 255));
        y += 22;
        k.text("window-manager  os-shell  terminal  files  os-settings", 24, y, 13,
               kilim::rgba(220, 225, 235, 255));
        y += 28;
        k.text("user@hsrcos:~$ _", 24, y, 14, kilim::rgba(80, 250, 123, 255));

        (void)k.commit_frame();
        if (!mapped) {
            mapped = win.map_surface(dev, k.target());
            (void)win.damage();
        }
        hsrc::sdk::yield(2);
    }
}
