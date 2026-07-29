#include "wm_dock.hpp"
#include "wm_state.hpp"

#include <kernel/string.h>
#include <user/sdk/process.hpp>

namespace wms {


void dock_geom(int *out_x, int *out_y, int *out_w, int *out_h) {
  int dock_w = wm::kDockPad * 2 + kDockCount * wm::kDockIcon +
               (kDockCount - 1) * wm::kDockGap;
  *out_w = dock_w;
  *out_h = wm::kDockH;
  *out_x = (g_screen_w - dock_w) / 2;
  *out_y = g_screen_h - wm::kDockH - 18;
}

int dock_hit(int mx, int my) {
  int dx, dy, dw, dh;
  dock_geom(&dx, &dy, &dw, &dh);
  if (mx < dx || my < dy || mx >= dx + dw || my >= dy + dh)
    return -1;
  for (int i = 0; i < kDockCount; i++) {
    int ix = dx + wm::kDockPad + i * (wm::kDockIcon + wm::kDockGap);
    int iy = dy + (wm::kDockH - wm::kDockIcon) / 2;
    if (mx >= ix && mx < ix + wm::kDockIcon && my >= iy &&
        my < iy + wm::kDockIcon)
      return i;
  }
  return -1;
}

void launch_or_focus(const DockItem &it) {
  Slot *s = find_class(it.cls);
  if (s) {
    if (s->opts.minimized) {
      s->opts.minimized = false;
      s->opts.visible = true;
    }
    focus_window(s->id);
    mark_damage(s, 0, 0, 0, 0);
    return;
  }
  (void)hsrc::sdk::process::spawn_ex(it.path, hsrc::sdk::process::ConsoleHidden,
                                     nullptr);
}

void draw_dock(kilim::Context &k) {
  int dx, dy, dw, dh;
  dock_geom(&dx, &dy, &dw, &dh);

  k.fill_round_rect(dx + 2, dy + 5, dw, dh, wm::kDockRadius,
                    kilim::rgba(0, 0, 0, 55));
  k.fill_round_rect(dx, dy, dw, dh, wm::kDockRadius,
                    kilim::rgba(34, 36, 44, 214));
  k.stroke_rect(dx + 1, dy + 1, dw - 2, dh - 2, 1,
                kilim::rgba(255, 255, 255, 22));

  for (int i = 0; i < kDockCount; i++) {
    int ix = dx + wm::kDockPad + i * (wm::kDockIcon + wm::kDockGap);
    int iy = dy + (wm::kDockH - wm::kDockIcon) / 2;
    int hover = (i == g_dock_hover);
    int r = hover ? 15 : 13;
    if (hover)
      iy -= 6;
    const DockItem &it = g_dock_items[i];
    if (hover) {
      k.fill_round_rect(ix - 3, iy - 3, wm::kDockIcon + 6, wm::kDockIcon + 6,
                        r + 3, kilim::rgba(255, 255, 255, 34));
    }
    k.fill_round_rect(ix, iy, wm::kDockIcon, wm::kDockIcon, r,
                      kilim::rgba(it.r, it.g, it.b, 255));
    /* Inner top highlight — subtle glass sheen, not a hard gradient. */
    k.fill_round_rect(ix + 5, iy + 4, wm::kDockIcon - 10, wm::kDockIcon / 3, 6,
                      kilim::rgba(255, 255, 255, 46));
    /* Running indicator (dot) if window class exists */
    if (find_class(it.cls)) {
      k.circle(ix + wm::kDockIcon / 2, dy + dh - 7, 2,
               kilim::rgba(255, 255, 255, 235), 1);
    }
    if (hover) {
      int tw = (int)strlen(it.label) * 7 + 16;
      int tx = ix + wm::kDockIcon / 2 - tw / 2;
      int ty = dy - 30;
      k.fill_round_rect(tx, ty, tw, 22, 6, kilim::rgba(24, 26, 32, 235));
      k.text(it.label, tx + 8, ty + 5, 12, kilim::rgba(238, 240, 245, 255));
    }
  }
}

} /* namespace wms */
