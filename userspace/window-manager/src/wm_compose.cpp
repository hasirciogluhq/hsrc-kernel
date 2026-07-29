#include "wm_compose.hpp"
#include "wm_dock.hpp"
#include "wm_state.hpp"

#include <kernel/string.h>
#include <kernel/syscall.h>
#include <user/input.h>
#include <user/sdk/syscall.hpp>
#include <user/sdk/wm.hpp>

namespace wms {

static void ensure_cursor_tex(reed::Device *dev) {
  if (!dev || g_cursor_tex.valid())
    return;

  /* 12x19 classic arrow — 0=transparent, 1=white fill, 2=black outline */
  static const uint8_t tip[19][12] = {
      {2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {2, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {2, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0},
      {2, 1, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0},
      {2, 1, 1, 1, 1, 2, 0, 0, 0, 0, 0, 0},
      {2, 1, 1, 1, 1, 1, 2, 0, 0, 0, 0, 0},
      {2, 1, 1, 1, 1, 1, 1, 2, 0, 0, 0, 0},
      {2, 1, 1, 1, 1, 1, 1, 1, 2, 0, 0, 0},
      {2, 1, 1, 1, 1, 1, 1, 1, 1, 2, 0, 0},
      {2, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 0},
      {2, 1, 1, 2, 1, 1, 2, 0, 0, 0, 0, 0},
      {2, 1, 2, 0, 2, 1, 1, 2, 0, 0, 0, 0},
      {2, 2, 0, 0, 2, 1, 1, 2, 0, 0, 0, 0},
      {2, 0, 0, 0, 0, 2, 1, 1, 2, 0, 0, 0},
      {0, 0, 0, 0, 0, 2, 1, 1, 2, 0, 0, 0},
      {0, 0, 0, 0, 0, 0, 2, 1, 1, 2, 0, 0},
      {0, 0, 0, 0, 0, 0, 2, 1, 1, 2, 0, 0},
      {0, 0, 0, 0, 0, 0, 0, 2, 2, 0, 0, 0},
  };

  constexpr int cw = 12, ch = 19;
  g_cursor_tex = dev->create_texture(reed::TexFormat::RGBA8, cw, ch, 1);
  if (!g_cursor_tex.valid())
    return;

  uint32_t px[cw * ch];
  for (int row = 0; row < ch; row++) {
    for (int col = 0; col < cw; col++) {
      uint8_t v = tip[row][col];
      if (v == 1)
        px[row * cw + col] = 0xffffffffu;
      else if (v == 2)
        px[row * cw + col] = 0xff111111u;
      else
        px[row * cw + col] = 0x00000000u;
    }
  }
  (void)g_cursor_tex.upload(0, px, sizeof(px));
}

static void blit_window(kilim::Context &k, Slot *s) {
  if (!s || !s->surface.valid())
    return;

  /* Dead exporter / revoked token → TEXTURE_MAP fails. */
  if (!s->surface.map()) {
    clear_surface(s);
    return;
  }
  s->surface.unmap();

    /* Kilim→Reed blit cmd (GPU submit); alpha path matches per-pixel A. */
  k.blit(s->surface, s->opts.x, s->opts.y, s->opts.w, s->opts.h, 1);
}

static void draw_window_chrome(kilim::Context &k, Slot *s) {
  if (!s || !s->opts.framed || s->opts.no_title)
    return;
  int x = s->opts.x;
  int y = s->opts.y;
  int w = s->opts.w;
  int h = s->opts.h;
  int active = (s->id == g_focus_id);

  /* Soft, tight drop shadow — do not cover client pixels. */
  k.fill_round_rect(x + 3, y + 5, w, h, wm::kWinRadius,
                    kilim::rgba(0, 0, 0, active ? 55 : 35));

  /* Titlebar only (client surface already blitted underneath). */
  uint32_t title =
      active ? kilim::rgba(38, 40, 47, 250) : kilim::rgba(32, 34, 40, 235);
  k.fill_round_rect(x, y, w, wm::kChromeTitleH + wm::kWinRadius, wm::kWinRadius,
                    title);
  k.fill_rect(x, y + wm::kChromeTitleH, w, wm::kWinRadius, title);
  k.fill_rect(
      x, y + wm::kChromeTitleH - 1, w, 1,
      kilim::rgba(90, 140, 255, active ? 200 : 0)); /* accent underline */

  /* Thin 1px frame around whole window — hairline, not a heavy border. */
  k.stroke_rect(x, y, w, h, 1,
                active ? kilim::rgba(255, 255, 255, 30)
                       : kilim::rgba(255, 255, 255, 14));

  /* macOS traffic lights (left) */
  int cy = y + wm::kChromeBtnY + wm::kChromeBtn / 2;
  if (s->opts.closable)
    k.circle(x + wm::kChromeBtn0X + wm::kChromeBtn / 2, cy, wm::kChromeBtn / 2,
             kilim::rgba(255, 95, 87, 255), 1);
  if (s->opts.can_minimize)
    k.circle(x + wm::kChromeBtn0X + (wm::kChromeBtn + wm::kChromeBtnGap) +
                 wm::kChromeBtn / 2,
             cy, wm::kChromeBtn / 2, kilim::rgba(255, 189, 46, 255), 1);
  if (s->opts.can_maximize)
    k.circle(x + wm::kChromeBtn0X + 2 * (wm::kChromeBtn + wm::kChromeBtnGap) +
                 wm::kChromeBtn / 2,
             cy, wm::kChromeBtn / 2, kilim::rgba(40, 200, 64, 255), 1);

  if (s->opts.resizable) {
    int gx = x + w - 11;
    int gy = y + h - 11;
    k.line(gx, gy + 8, gx + 8, gy, kilim::rgba(140, 150, 165, 200));
    k.line(gx + 3, gy + 8, gx + 8, gy + 3, kilim::rgba(140, 150, 165, 160));
  }
}

static void draw_system_chrome(kilim::Context &k, int mx, int my) {
  (void)mx;
  (void)my;

  /* Menubar — flat, low-contrast strip (content stays the focus). */
  k.fill_rect(0, 0, g_screen_w, wm::kMenubarH, kilim::rgba(24, 26, 32, 235));
  k.fill_rect(0, wm::kMenubarH - 1, g_screen_w, 1,
              kilim::rgba(255, 255, 255, 18));

  draw_dock(k);

  if (g_menu_open) {
    k.fill_round_rect(8, wm::kMenubarH + 4, 200, 28 * 4 + 12, 10,
                      kilim::rgba(32, 34, 41, 248));
    k.stroke_rect(8, wm::kMenubarH + 4, 200, 28 * 4 + 12, 1,
                  kilim::rgba(255, 255, 255, 24));
    for (int i = 0; i < 4 && i < kDockCount; i++) {
      k.text(g_dock_items[i].label, 24, wm::kMenubarH + 12 + i * 28, 13,
             kilim::rgba(230, 235, 245, 255));
    }
  }
}


} /* namespace wms */
