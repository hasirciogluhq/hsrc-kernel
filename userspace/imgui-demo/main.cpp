#include <user/sdk/wm.hpp>
#include <user/sdk/kilim.hpp>
#include <user/sdk/reed.hpp>
#include <user/sdk/syscall.hpp>
#include <kernel/syscall.h>

/*
 * imgui-demo — stub only. Dear ImGui / imgui_impl_ugx needs a kilim+wm rewrite.
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

    wm::WindowOptions opts;
    opts.x = 180;
    opts.y = 120;
    opts.w = 640;
    opts.h = 420;
    opts.set_title("ImGui Demo");
    opts.set_class_name("imgui-demo");

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
        k.fill_rect(20, 20, 400, 64, kilim::rgba(40, 40, 50, 255));
        k.text("ImGui demo: needs kilim+wm rewrite", 28, 28, 16,
               kilim::rgba(255, 255, 255, 255));
        k.text("(imgui_impl_ugx removed)", 28, 52, 14, kilim::rgba(180, 180, 190, 255));
        (void)k.commit_frame();

        if (!mapped)
            mapped = win.map_surface(dev, k.target());
        (void)win.damage();
        hsrc::sdk::yield(1);
    }
}
