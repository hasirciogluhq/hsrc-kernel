#include <kernel/syscall.h>
#include <user/sdk/kilim.hpp>
#include <user/sdk/reed.hpp>
#include <user/sdk/syscall.hpp>
#include <user/sdk/wm.hpp>

/*
 * OS Shell — wallpaper surface (painted once).
 * Menubar + dock are WM chrome.
 */

namespace {

static uint32_t lerp_rgba(uint32_t a, uint32_t b, int t /*0..256*/) {
  int ar = (int)((a >> 16) & 255), ag = (int)((a >> 8) & 255),
      ab = (int)(a & 255);
  int br = (int)((b >> 16) & 255), bg = (int)((b >> 8) & 255),
      bb = (int)(b & 255);
  int r = ar + ((br - ar) * t) / 256;
  int g = ag + ((bg - ag) * t) / 256;
  int bl = ab + ((bb - ab) * t) / 256;
  return kilim::rgba((uint8_t)r, (uint8_t)g, (uint8_t)bl, 255);
}

static void paint_wallpaper(kilim::Context &k, int sw, int sh) {
  /*
   * Compact/modern desktop background — quiet, low-contrast graphite
   * gradient (content-first, no decoration for its own sake). Painted
   * once at startup; never recommitted per frame.
   */
  uint32_t top = kilim::rgba(15, 17, 23, 255);
  uint32_t mid = kilim::rgba(22, 25, 33, 255);
  uint32_t bot = kilim::rgba(11, 12, 16, 255);
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

  /* Two restrained accent glows instead of three loud orbs. */
  k.fill_round_rect(sw - sw / 4 - 180, sh / 6, 360, 360, 180,
                    kilim::rgba(88, 120, 255, 20));
  k.fill_round_rect(-80, sh - 320, 420, 420, 210,
                    kilim::rgba(60, 200, 170, 14));

  /* Faint dot grid — read as texture, not a decoration. */
  for (int y = 32; y < sh; y += 32)
    for (int x = 32; x < sw; x += 32)
      k.fill_rect(x, y, 1, 1, kilim::rgba(255, 255, 255, 14));

  /* Compact glass card — centered brand mark, no big headline block. */
  int cw = 300, ch = 74;
  int cx = sw / 2 - cw / 2, cy = sh / 2 - ch / 2;
  k.fill_round_rect(cx + 2, cy + 3, cw, ch, 16, kilim::rgba(0, 0, 0, 50));
  k.fill_round_rect(cx, cy, cw, ch, 16, kilim::rgba(255, 255, 255, 10));
  k.stroke_rect(cx, cy, cw, ch, 1, kilim::rgba(255, 255, 255, 22));
  k.text("hsrcOS", cx + 24, cy + 16, 22, kilim::rgba(245, 247, 252, 255));
  k.text("Click a dock icon to launch an app", cx + 24, cy + 46, 12,
         kilim::rgba(165, 172, 188, 235));
}

} // namespace

extern "C" void exec_main(void) {
  static reed::Device dev;
  static kilim::Context k;

  while (dev.init() < 0 || k.init(&dev) < 0)
    (void)hsrc::sdk::syscall3(SYS_INPUT_WAIT, -1, 0, 1);

  int sw = (int)dev.caps().width;
  int sh = (int)dev.caps().height;

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
  while (!win.create(opts))
    (void)hsrc::sdk::syscall3(SYS_INPUT_WAIT, -1, 0, 1);

  if (k.begin_frame() == 0) {
    paint_wallpaper(k, sw, sh);
    (void)k.commit_frame();
    (void)win.map_surface(dev, k.target());
    (void)win.damage();
  }

  uint32_t last_seq = 0;
  for (;;) {
    long seq = hsrc::sdk::syscall3(SYS_INPUT_WAIT, -1, (long)last_seq, -1);
    last_seq = (uint32_t)seq;
  }
}
