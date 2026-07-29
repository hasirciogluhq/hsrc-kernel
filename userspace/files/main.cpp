#include <user/sdk/wm.hpp>
#include <user/sdk/kilim.hpp>
#include <user/sdk/reed.hpp>
#include <user/sdk/syscall.hpp>
#include <kernel/syscall.h>

/* Files — Finder-like sidebar + list (Fluent colors). */

namespace {

[[noreturn]] void hang(void)
{
    for (;;)
        hsrc::sdk::syscall0(SYS_YIELD);
}

static const char *kSide[] = {"Recents", "Desktop", "Documents", "Downloads",
                              "Applications", "System"};
static const char *kRows[] = {"Documents", "Projects", "disk.img", "README.md",
                              "kernel.bin", "initrd.img"};
static constexpr int kSideN = 6;
static constexpr int kRowN = 6;

} // namespace

extern "C" void exec_main(void)
{
    static reed::Device dev;
    static kilim::Context k;

    if (dev.init() < 0 || k.init(&dev) < 0)
        hang();

    wm::WindowOptions opts;
    opts.x = 140;
    opts.y = 80;
    opts.w = 900;
    opts.h = 560;
    opts.min_w = 640;
    opts.min_h = 400;
    opts.set_title("Files");
    opts.set_class_name("files");

    wm::Window win;
    if (!win.create(opts))
        hang();

    int side = 1;
    int row = 0;
    bool mapped = false;
    for (;;) {
        wm::Input in;
        (void)wm::input_snapshot(in);
        k.set_pointer(in.mouse_x, in.mouse_y, in.buttons);

        int lx = in.mouse_x - opts.x;
        int ly = in.mouse_y - opts.y;
        int top = wm::kChromeTitleH;
        if (in.buttons & 1) {
            if (lx >= 8 && lx < 200) {
                int r = (ly - (top + 56)) / 36;
                if (r >= 0 && r < kSideN)
                    side = r;
            } else if (lx >= 220) {
                int r = (ly - (top + 56)) / 36;
                if (r >= 0 && r < kRowN)
                    row = r;
            }
        }

        if (k.begin_frame() < 0) {
            hsrc::sdk::yield(1);
            continue;
        }

        int w = opts.w;
        int h = opts.h;
        k.fill_rect(0, 0, w, h, kilim::rgba(32, 34, 40, 255));
        k.fill_rect(0, top, 200, h - top, kilim::rgba(28, 30, 36, 255));
        k.fill_rect(200, top, w - 200, h - top, kilim::rgba(36, 38, 46, 255));

        k.fill_round_rect(220, top + 12, w - 240, 28, 8,
                          kilim::rgba(45, 48, 58, 255));
        k.text("/home/user", 236, top + 18, 13, kilim::rgba(180, 186, 198, 255));

        for (int i = 0; i < kSideN; i++) {
            int y = top + 56 + i * 36;
            if (i == side)
                k.fill_round_rect(10, y - 6, 180, 32, 8,
                                  kilim::rgba(0, 120, 212, 255));
            k.text(kSide[i], 24, y, 14,
                   i == side ? kilim::rgba(255, 255, 255, 255)
                             : kilim::rgba(200, 206, 218, 255));
        }

        for (int i = 0; i < kRowN; i++) {
            int y = top + 56 + i * 36;
            if (i == row)
                k.fill_round_rect(220, y - 6, w - 240, 32, 8,
                                  kilim::rgba(55, 60, 72, 255));
            k.fill_round_rect(232, y - 2, 22, 22, 6,
                              kilim::rgba(0, 120, 212, 200));
            k.text(kRows[i], 268, y, 14, kilim::rgba(230, 235, 245, 255));
        }

        (void)k.commit_frame();
        static int prev_side = -1, prev_row = -1;
        if (!mapped) {
            mapped = win.map_surface(dev, k.target());
            (void)win.damage();
            prev_side = side;
            prev_row = row;
        } else if (side != prev_side || row != prev_row) {
            (void)win.damage();
            prev_side = side;
            prev_row = row;
        }
        hsrc::sdk::yield(2);
    }
}
