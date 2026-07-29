#include "wm_input.hpp"
#include "wm_dock.hpp"
#include "wm_state.hpp"

#include <kernel/string.h>
#include <kernel/syscall.h>
#include <user/input.h>
#include <user/sdk/syscall.hpp>
#include <user/sdk/wm.hpp>

namespace wms {


void handle_input(void) {
  input_state_t st;
  memset(&st, 0, sizeof(st));
  if (hsrc::sdk::syscall1(SYS_INPUT_STATE, (long)&st) < 0)
    return;

  int prev_hover = g_dock_hover;
  g_dock_hover = dock_hit(st.mouse_x, st.mouse_y);
  g_hit_id = (g_dock_hover >= 0) ? -1 : hit_test(st.mouse_x, st.mouse_y);

  if (st.mouse_x != g_cursor_x || st.mouse_y != g_cursor_y) {
    g_cursor_x = st.mouse_x;
    g_cursor_y = st.mouse_y;
    g_compose_dirty = 1;
  }
  if (prev_hover != g_dock_hover)
    g_compose_dirty = 1;

  uint8_t btn = st.buttons;
  uint8_t pressed = (uint8_t)(btn & ~g_prev_buttons);
  uint8_t released = (uint8_t)(g_prev_buttons & ~btn);

  if (released & INPUT_BTN_LEFT) {
    g_drag_id = -1;
    g_resize_id = -1;
  }

  if (pressed & INPUT_BTN_LEFT) {
    if (st.mouse_y < wm::kMenubarH && st.mouse_x < 90) {
      g_menu_open = g_menu_open ? 0 : 1;
      g_compose_dirty = 1;
      g_prev_buttons = btn;
      return;
    }
    if (g_menu_open) {
      int my = st.mouse_y;
      if (my >= wm::kMenubarH && my < wm::kMenubarH + 28 * 4 &&
          st.mouse_x < 220) {
        int row = (my - wm::kMenubarH) / 28;
        if (row >= 0 && row < kDockCount && row < 4)
          launch_or_focus(g_dock_items[row]);
      }
      g_menu_open = 0;
      g_compose_dirty = 1;
      g_prev_buttons = btn;
      return;
    }

    if (g_dock_hover >= 0) {
      launch_or_focus(g_dock_items[g_dock_hover]);
      g_prev_buttons = btn;
      return;
    }

    int id = g_hit_id;
    Slot *s = slot_by_id(id);
    if (s && !s->opts.background && s->opts.accept_focus) {
      focus_window(id);
      int lx = st.mouse_x - s->opts.x;
      int ly = st.mouse_y - s->opts.y;
      int btn_i = chrome_btn_at(s, lx, ly);
      if (btn_i == 0) {
        (void)handle_destroy(id);
      } else if (btn_i == 1) {
        s->opts.minimized = true;
        mark_damage(s, 0, 0, 0, 0);
      } else if (btn_i == 2) {
        if (s->opts.maximized) {
          s->opts.maximized = false;
        } else {
          s->opts.maximized = true;
          s->opts.x = 8;
          s->opts.y = wm::kMenubarH + 4;
          s->opts.w = g_screen_w - 16;
          s->opts.h = g_screen_h - wm::kMenubarH - wm::kDockH - 40;
          clamp_geom(s->opts);
        }
        mark_damage(s, 0, 0, 0, 0);
      } else if (in_resize_grip(s, lx, ly)) {
        g_resize_id = id;
        g_resize_ox = st.mouse_x;
        g_resize_oy = st.mouse_y;
        g_resize_ow = s->opts.w;
        g_resize_oh = s->opts.h;
      } else {
        int can_drag = s->opts.framed && !s->opts.no_drag && !s->opts.no_title;
        if (can_drag && ly >= 0 && ly < wm::kChromeTitleH &&
            lx >= wm::kChromeBtnZone && lx < s->opts.w) {
          g_drag_id = id;
          g_drag_off_x = lx;
          g_drag_off_y = ly;
        }
      }
    } else {
      g_menu_open = 0;
    }
  }

  if (g_resize_id >= 0 && (btn & INPUT_BTN_LEFT)) {
    Slot *s = slot_by_id(g_resize_id);
    if (s) {
      int32_t nw = g_resize_ow + (st.mouse_x - g_resize_ox);
      int32_t nh = g_resize_oh + (st.mouse_y - g_resize_oy);
      if (nw < 160)
        nw = 160;
      if (nh < 100)
        nh = 100;
      if (nw != s->opts.w || nh != s->opts.h) {
        s->opts.w = nw;
        s->opts.h = nh;
        clamp_geom(s->opts);
        mark_damage(s, 0, 0, 0, 0);
      }
    }
  }

  if (g_drag_id >= 0 && (btn & INPUT_BTN_LEFT)) {
    Slot *s = slot_by_id(g_drag_id);
    if (s) {
      int32_t nx = st.mouse_x - g_drag_off_x;
      int32_t ny = st.mouse_y - g_drag_off_y;
      if (ny < wm::kMenubarH)
        ny = wm::kMenubarH;
      if (nx != s->opts.x || ny != s->opts.y) {
        s->opts.x = nx;
        s->opts.y = ny;
        s->opts.maximized = false;
        mark_damage(s, 0, 0, 0, 0);
      }
    }
  }

  g_prev_buttons = btn;
}

} /* namespace wms */
