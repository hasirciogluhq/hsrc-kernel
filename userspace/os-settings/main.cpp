#include <user/sdk/wm.hpp>
#include <user/sdk/kilim.hpp>
#include <user/sdk/reed.hpp>
#include <user/sdk/syscall.hpp>
#include <kernel/syscall.h>

/* System Settings — Fluent hub (sidebar + content). */

namespace {

[[noreturn]] void hang(void)
{
    for (;;)
        hsrc::sdk::syscall0(SYS_YIELD);
}

static const char *kNav[] = {
    "System", "Display", "Sound", "Network", "Personalization", "Apps", "About",
};
static constexpr int kNavN = 7;

} // namespace

extern "C" void exec_main(void)
{
    static reed::Device dev;
    static kilim::Context k;

    if (dev.init() < 0 || k.init(&dev) < 0)
        hang();

    wm::WindowOptions opts;
    opts.x = 160;
    opts.y = 90;
    opts.w = 860;
    opts.h = 560;
    opts.min_w = 640;
    opts.min_h = 400;
    opts.set_title("Settings");
    opts.set_class_name("os.settings");

    wm::Window win;
    if (!win.create(opts))
        hang();

    int sel = 0;
    bool mapped = false;
    for (;;) {
        wm::Input in;
        (void)wm::input_snapshot(in);
        k.set_pointer(in.mouse_x, in.mouse_y, in.buttons);

        int lx = in.mouse_x - opts.x;
        int ly = in.mouse_y - opts.y;
        if ((in.buttons & 1) && lx >= 12 && lx < 220) {
            int row = (ly - (wm::kChromeTitleH + 24)) / 40;
            if (row >= 0 && row < kNavN)
                sel = row;
        }

        if (k.begin_frame() < 0) {
            hsrc::sdk::sleep_ticks(1);
            continue;
        }

        int w = opts.w;
        int h = opts.h;
        int top = wm::kChromeTitleH;
        k.fill_rect(0, 0, w, h, kilim::rgba(32, 34, 40, 255));
        k.fill_rect(0, top, 220, h - top, kilim::rgba(28, 30, 36, 255));
        k.fill_rect(220, top, w - 220, h - top, kilim::rgba(36, 38, 46, 255));

        k.text("Settings", 24, top + 10, 18, kilim::rgba(245, 246, 250, 255));
        for (int i = 0; i < kNavN; i++) {
            int y = top + 48 + i * 40;
            if (i == sel)
                k.fill_round_rect(12, y - 6, 196, 34, 8,
                                  kilim::rgba(0, 120, 212, 255));
            k.text(kNav[i], 28, y, 14,
                   i == sel ? kilim::rgba(255, 255, 255, 255)
                            : kilim::rgba(200, 206, 218, 255));
        }

        k.fill_round_rect(248, top + 48, w - 280, 120, 12,
                          kilim::rgba(45, 48, 58, 255));
        k.text(kNav[sel], 268, top + 68, 20, kilim::rgba(245, 246, 250, 255));
        k.text("Windows-style settings hub — macOS navigation feel.", 268,
               top + 100, 13, kilim::rgba(170, 178, 192, 255));

        int on = 1;
        k.text("Dark mode", 268, top + 200, 14, kilim::rgba(230, 235, 245, 255));
        (void)k.toggle(400, top + 196, &on);
        float bright = 0.7f;
        k.text("Brightness", 268, top + 250, 14, kilim::rgba(230, 235, 245, 255));
        (void)k.slider(400, top + 248, 220, &bright);

        (void)k.commit_frame();
        static int prev_sel = -1;
        if (!mapped) {
            mapped = win.map_surface(dev, k.target());
            (void)win.damage();
            prev_sel = sel;
        } else if (sel != prev_sel) {
            (void)win.damage();
            prev_sel = sel;
        }
        hsrc::sdk::sleep_ticks(2);
    }
}
