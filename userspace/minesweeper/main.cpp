#include <user/sdk/wm.hpp>
#include <user/sdk/kilim.hpp>
#include <user/sdk/reed.hpp>
#include <user/sdk/syscall.hpp>
#include <kernel/syscall.h>

/* Minesweeper — simple clickable grid. */

namespace {

[[noreturn]] void hang(void)
{
    for (;;)
        hsrc::sdk::syscall0(SYS_YIELD);
}

constexpr int kN = 9;
constexpr int kCell = 28;

} // namespace

extern "C" void exec_main(void)
{
    static reed::Device dev;
    static kilim::Context k;
    static uint8_t open[kN][kN];
    static uint8_t mine[kN][kN];
    static int inited;

    if (dev.init() < 0 || k.init(&dev) < 0)
        hang();

    if (!inited) {
        for (int y = 0; y < kN; y++) {
            for (int x = 0; x < kN; x++) {
                open[y][x] = 0;
                mine[y][x] = ((x * 7 + y * 13) % 11 == 0) ? 1 : 0;
            }
        }
        inited = 1;
    }

    wm::WindowOptions opts;
    opts.x = 220;
    opts.y = 140;
    opts.w = 320;
    opts.h = 380;
    opts.resizable = false;
    opts.set_title("Minesweeper");
    opts.set_class_name("minesweeper");

    wm::Window win;
    if (!win.create(opts))
        hang();

    bool mapped = false;
    uint8_t prev = 0;
    for (;;) {
        wm::Input in;
        (void)wm::input_snapshot(in);
        k.set_pointer(in.mouse_x, in.mouse_y, in.buttons);

        int top = wm::kChromeTitleH;
        int ox = (opts.w - kN * kCell) / 2;
        int oy = top + 48;
        uint8_t pressed = (uint8_t)(in.buttons & ~prev);
        if (pressed & 1) {
            int lx = in.mouse_x - opts.x - ox;
            int ly = in.mouse_y - opts.y - oy;
            if (lx >= 0 && ly >= 0) {
                int cx = lx / kCell;
                int cy = ly / kCell;
                if (cx >= 0 && cx < kN && cy >= 0 && cy < kN)
                    open[cy][cx] = 1;
            }
        }
        prev = in.buttons;

        if (k.begin_frame() < 0) {
            hsrc::sdk::yield(1);
            continue;
        }

        k.fill_rect(0, 0, opts.w, opts.h, kilim::rgba(36, 38, 46, 255));
        k.fill_round_rect(16, top + 10, opts.w - 32, 28, 8,
                          kilim::rgba(45, 48, 58, 255));
        k.text("Minesweeper", 28, top + 16, 14, kilim::rgba(245, 246, 250, 255));

        for (int y = 0; y < kN; y++) {
            for (int x = 0; x < kN; x++) {
                int px = ox + x * kCell;
                int py = oy + y * kCell;
                if (!open[y][x]) {
                    k.fill_round_rect(px + 1, py + 1, kCell - 2, kCell - 2, 4,
                                      kilim::rgba(70, 78, 96, 255));
                } else if (mine[y][x]) {
                    k.fill_round_rect(px + 1, py + 1, kCell - 2, kCell - 2, 4,
                                      kilim::rgba(232, 17, 35, 255));
                    k.text("*", px + 9, py + 6, 14, kilim::rgba(255, 255, 255, 255));
                } else {
                    k.fill_round_rect(px + 1, py + 1, kCell - 2, kCell - 2, 4,
                                      kilim::rgba(50, 54, 64, 255));
                    k.text("1", px + 9, py + 6, 13, kilim::rgba(100, 180, 255, 255));
                }
            }
        }

        (void)k.commit_frame();
        if (!mapped) {
            mapped = win.map_surface(dev, k.target());
            (void)win.damage();
        } else if (pressed & 1) {
            (void)win.damage();
        }
        hsrc::sdk::yield(2);
    }
}
