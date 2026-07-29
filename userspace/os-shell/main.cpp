#include <user/sdk/wm.hpp>
#include <user/sdk/kilim.hpp>
#include <user/sdk/reed.hpp>
#include <user/sdk/syscall.hpp>
#include <kernel/syscall.h>

/*
 * OS Shell — wallpaper surface (painted once).
 * Menubar + dock are WM chrome.
 */

namespace {

[[noreturn]] void hang(void)
{
    for (;;)
        hsrc::sdk::syscall0(SYS_YIELD);
}

static uint32_t lerp_rgba(uint32_t a, uint32_t b, int t /*0..256*/)
{
    int ar = (int)((a >> 16) & 255), ag = (int)((a >> 8) & 255), ab = (int)(a & 255);
    int br = (int)((b >> 16) & 255), bg = (int)((b >> 8) & 255), bb = (int)(b & 255);
    int r = ar + ((br - ar) * t) / 256;
    int g = ag + ((bg - ag) * t) / 256;
    int bl = ab + ((bb - ab) * t) / 256;
    return kilim::rgba((uint8_t)r, (uint8_t)g, (uint8_t)bl, 255);
}

static void paint_wallpaper(kilim::Context &k, int sw, int sh)
{
    /* Vertical dusk gradient (Fluent-dark, no image decode). */
    uint32_t top = kilim::rgba(18, 24, 48, 255);
    uint32_t mid = kilim::rgba(32, 40, 64, 255);
    uint32_t bot = kilim::rgba(12, 14, 22, 255);
    for (int y = 0; y < sh; y++) {
        uint32_t c;
        if (y < sh / 2) {
            int t = (y * 256) / (sh / 2);
            c = lerp_rgba(top, mid, t);
        } else {
            int t = ((y - sh / 2) * 256) / (sh / 2 + 1);
            c = lerp_rgba(mid, bot, t);
        }
        k.fill_rect(0, y, sw, 1, c);
    }

    /* Soft orbs */
    k.fill_round_rect(sw / 6, sh / 5, 280, 280, 140, kilim::rgba(0, 120, 212, 28));
    k.fill_round_rect(sw - 420, sh / 3, 320, 320, 160, kilim::rgba(136, 23, 152, 22));
    k.fill_round_rect(sw / 3, sh - 360, 360, 200, 100, kilim::rgba(16, 124, 16, 18));

    /* Subtle grid */
    for (int x = 0; x < sw; x += 48)
        k.fill_rect(x, 0, 1, sh, kilim::rgba(255, 255, 255, 8));
    for (int y = 0; y < sh; y += 48)
        k.fill_rect(0, y, sw, 1, kilim::rgba(255, 255, 255, 8));

    k.fill_round_rect(sw / 2 - 200, sh / 2 - 48, 400, 96, 20,
                      kilim::rgba(20, 24, 36, 140));
    k.text("hsrcOS", sw / 2 - 52, sh / 2 - 28, 26, kilim::rgba(245, 248, 255, 255));
    k.text("Click the dock to launch apps", sw / 2 - 120, sh / 2 + 12, 14,
           kilim::rgba(180, 190, 210, 255));
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

    /* Paint once — continuous recommit was a major desktop lag source. */
    if (k.begin_frame() == 0) {
        paint_wallpaper(k, sw, sh);
        (void)k.commit_frame();
        (void)win.map_surface(dev, k.target());
        (void)win.damage();
    }

    for (;;)
        hsrc::sdk::yield(50);
}
