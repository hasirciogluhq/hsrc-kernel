#include "wm_compose.hpp"
#include "wm_dock.hpp"
#include "wm_state.hpp"

#include <kernel/string.h>
#include <kernel/syscall.h>
#include <user/input.h>
#include <user/sdk/syscall.hpp>
#include <user/sdk/wm.hpp>

namespace wms {

/*
 * Compositor frame — Kilim drawlist → Reed cmdbuf → GPU.
 * No CPU pixel loops, no texture bake, no FB write.
 */

static void blit_window(kilim::Context &k, Slot *s) {
  if (!s || !s->surface.valid())
    return;
  /* Full window surface; chrome draws on top via Kilim. */
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

  k.fill_round_rect(x + 3, y + 5, w, h, wm::kWinRadius,
                    kilim::rgba(0, 0, 0, active ? 55 : 35));

  uint32_t title =
      active ? kilim::rgba(38, 40, 47, 250) : kilim::rgba(32, 34, 40, 235);
  k.fill_round_rect(x, y, w, wm::kChromeTitleH + wm::kWinRadius, wm::kWinRadius,
                    title);
  k.fill_rect(x, y + wm::kChromeTitleH, w, wm::kWinRadius, title);
  k.fill_rect(x, y + wm::kChromeTitleH - 1, w, 1,
              kilim::rgba(90, 140, 255, active ? 200 : 0));

  k.stroke_rect(x, y, w, h, 1,
                active ? kilim::rgba(255, 255, 255, 30)
                       : kilim::rgba(255, 255, 255, 14));

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

static void draw_system_chrome(kilim::Context &k) {
  k.fill_rect(0, 0, g_screen_w, wm::kMenubarH, kilim::rgba(24, 26, 32, 235));
  k.fill_rect(0, wm::kMenubarH - 1, g_screen_w, 1,
              kilim::rgba(255, 255, 255, 18));
  k.text("Menu", 10, 5, 13, kilim::rgba(230, 235, 245, 255));

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

/* Software cursor shape via Kilim polys — Reed cmds only, no pixel bake. */
static void draw_cursor(kilim::Context &k, int mx, int my) {
  if (mx < -32 || my < -32 || mx > g_screen_w + 32 || my > g_screen_h + 32)
    return;

  int outline[14] = {
      mx + 0,  my + 0, mx + 12, my + 8, mx + 7,  my + 9, mx + 10,
      my + 18, mx + 7, my + 19, mx + 4, my + 10, mx + 0, my + 13,
  };
  int fill[14] = {
      mx + 1,  my + 2, mx + 10, my + 8, mx + 6,  my + 9, mx + 8,
      my + 16, mx + 7, my + 16, mx + 4, my + 10, mx + 1, my + 12,
  };
  k.polygon(outline, 7, kilim::rgba(17, 17, 17, 255), 1);
  k.polygon(fill, 7, kilim::rgba(255, 255, 255, 255), 1);
}

static void sort_slots(Slot **arr, int n) {
  for (int i = 1; i < n; i++) {
    Slot *key = arr[i];
    int j = i - 1;
    while (j >= 0 && arr[j]->z > key->z) {
      arr[j + 1] = arr[j];
      j--;
    }
    arr[j + 1] = key;
  }
}

void compose_frame(kilim::Context &k) {
  input_state_t st;
  memset(&st, 0, sizeof(st));
  if (hsrc::sdk::syscall1(SYS_INPUT_STATE, (long)&st) == 0) {
    if (st.mouse_x != g_cursor_x || st.mouse_y != g_cursor_y) {
      g_cursor_x = st.mouse_x;
      g_cursor_y = st.mouse_y;
      g_compose_dirty = 1;
    }
    k.set_pointer(st.mouse_x, st.mouse_y, st.buttons);
  }

  if (!g_compose_dirty) {
    (void)hsrc::sdk::syscall3(SYS_INPUT_WAIT, -1, 0, 1);
    return;
  }

  if (k.begin_frame() < 0) {
    (void)hsrc::sdk::syscall3(SYS_INPUT_WAIT, -1, 0, 1);
    return;
  }

  Slot *order[kMaxWin];
  int n = 0;
  for (int i = 0; i < kMaxWin; i++) {
    if (!g_slots[i].used)
      continue;
    if (!effective_visible(g_slots[i].opts))
      continue;
    order[n++] = &g_slots[i];
  }
  sort_slots(order, n);

  for (int i = 0; i < n; i++) {
    blit_window(k, order[i]);
    draw_window_chrome(k, order[i]);
  }

  draw_system_chrome(k);
  draw_cursor(k, g_cursor_x, g_cursor_y);

  (void)k.end_frame();
  g_compose_dirty = 0;
}

} /* namespace wms */
