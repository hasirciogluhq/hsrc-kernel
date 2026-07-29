#include "wm_state.hpp"

#include <kernel/string.h>
#include <kernel/syscall.h>
#include <user/sdk/syscall.hpp>

namespace wms {

Slot g_slots[kMaxWin];
int g_next_id = 1;
int g_next_z = 1;
int g_focus_id = -1;
int g_hit_id = -1;
int g_drag_id = -1;
int g_drag_off_x = 0;
int g_drag_off_y = 0;
int g_resize_id = -1;
int g_resize_ox = 0;
int g_resize_oy = 0;
int g_resize_ow = 0;
int g_resize_oh = 0;
uint8_t g_prev_buttons = 0;
int g_screen_w = 0;
int g_screen_h = 0;
int g_dock_hover = -1;
int g_menu_open = 0;
int g_compose_dirty = 1;
int g_cursor_x = -1000;
int g_cursor_y = -1000;
reed::Device *g_dev = nullptr;

const DockItem g_dock_items[] = {
    {"Files", "files", "files", 0, 120, 212},
    {"Terminal", "terminal", "terminal", 16, 124, 16},
    {"Settings", "os-settings", "os.settings", 0, 153, 188},
    {"Monitor", "activity-monitor", "activity-monitor", 136, 23, 152},
    {"Mines", "minesweeper", "minesweeper", 232, 17, 35},
    {"ImGui", "imgui-demo", "imgui-demo", 255, 140, 0},
};
const int kDockCount = (int)(sizeof(g_dock_items) / sizeof(g_dock_items[0]));


void str_cat(char *dst, const char *src) {
  dst += strlen(dst);
  while (*src)
    *dst++ = *src++;
  *dst = '\0';
}

void append_u32(char *dst, uint32_t v) {
  char tmp[12];
  int n = 0;
  if (v == 0) {
    tmp[n++] = '0';
  } else {
    while (v) {
      tmp[n++] = (char)('0' + (v % 10u));
      v /= 10u;
    }
  }
  while (n--)
    *dst++ = tmp[n];
  *dst = '\0';
}

int parse_pid_from_name(const char *name, int *pid_out) {
  /* "<pid>.req" */
  uint32_t v = 0;
  int i = 0;
  if (!name || !pid_out)
    return -1;
  while (name[i] >= '0' && name[i] <= '9') {
    v = v * 10u + (uint32_t)(name[i] - '0');
    i++;
  }
  if (i == 0 || name[i] != '.' || name[i + 1] != 'r' || name[i + 2] != 'e' ||
      name[i + 3] != 'q' || name[i + 4] != '\0')
    return -1;
  *pid_out = (int)v;
  return 0;
}

Slot *slot_by_id(int id) {
  if (id < 0)
    return nullptr;
  for (int i = 0; i < kMaxWin; i++) {
    if (g_slots[i].used && g_slots[i].id == id)
      return &g_slots[i];
  }
  return nullptr;
}

Slot *alloc_slot(void) {
  for (int i = 0; i < kMaxWin; i++) {
    if (!g_slots[i].used) {
      memset(&g_slots[i], 0, sizeof(g_slots[i]));
      g_slots[i].used = 1;
      return &g_slots[i];
    }
  }
  return nullptr;
}

int effective_visible(const wm::WindowOptions &o) {
  return o.visible && !o.minimized;
}

void raise_window(Slot *s) {
  if (!s)
    return;
  s->z = g_next_z++;
  if (s->opts.always_on_bottom)
    s->z = -s->id;
  else if (s->opts.topmost)
    s->z += 100000;
}

int hit_test(int32_t x, int32_t y) {
  int best_id = -1;
  int best_z = -0x7fffffff;
  int best_fg = 0;

  for (int i = 0; i < kMaxWin; i++) {
    Slot *s = &g_slots[i];
    int fg;
    if (!s->used || !effective_visible(s->opts))
      continue;
    if (s->opts.mouse_passthrough)
      continue;
    if (x < s->opts.x || y < s->opts.y || x >= s->opts.x + s->opts.w ||
        y >= s->opts.y + s->opts.h)
      continue;
    fg = (!s->opts.background && s->opts.accept_focus) ? 1 : 0;
    if (s->z > best_z || (s->z == best_z && fg >= best_fg)) {
      best_z = s->z;
      best_fg = fg;
      best_id = s->id;
    }
  }
  return best_id;
}

void focus_window(int id) {
  Slot *s = slot_by_id(id);
  if (!s || s->opts.background || !s->opts.accept_focus)
    return;
  g_focus_id = id;
  raise_window(s);
}

void clear_surface(Slot *s) {
  if (!s)
    return;
  if (s->surface.valid())
    s->surface.destroy();
  s->surface_token = 0;
}

int import_surface(Slot *s, uint32_t token) {
  if (!s || !g_dev || token == 0)
    return -1;
  clear_surface(s);
  reed::Texture2D tex = g_dev->import_texture(token);
  if (!tex.valid())
    return -1;
  s->surface_token = token;
  s->surface = tex;
  s->damaged = 1;
  s->damage_x = 0;
  s->damage_y = 0;
  s->damage_w = s->opts.w;
  s->damage_h = s->opts.h;
  return 0;
}

void mark_damage(Slot *s, int32_t x, int32_t y, int32_t w, int32_t h) {
  if (!s)
    return;
  if (w <= 0 || h <= 0) {
    s->damaged = 1;
    s->damage_x = 0;
    s->damage_y = 0;
    s->damage_w = s->opts.w;
    s->damage_h = s->opts.h;
    return;
  }
  if (!s->damaged) {
    s->damage_x = x;
    s->damage_y = y;
    s->damage_w = w;
    s->damage_h = h;
    s->damaged = 1;
    return;
  }
  /* Union */
  int32_t x1 = s->damage_x;
  int32_t y1 = s->damage_y;
  int32_t x2 = s->damage_x + s->damage_w;
  int32_t y2 = s->damage_y + s->damage_h;
  if (x < x1)
    x1 = x;
  if (y < y1)
    y1 = y;
  if (x + w > x2)
    x2 = x + w;
  if (y + h > y2)
    y2 = y + h;
  s->damage_x = x1;
  s->damage_y = y1;
  s->damage_w = x2 - x1;
  s->damage_h = y2 - y1;
}

int clamp_geom(wm::WindowOptions &o) {
  if (o.w < 1)
    o.w = 1;
  if (o.h < 1)
    o.h = 1;
  if (o.min_w > 0 && o.w < o.min_w)
    o.w = o.min_w;
  if (o.min_h > 0 && o.h < o.min_h)
    o.h = o.min_h;
  if (o.max_w > 0 && o.w > o.max_w)
    o.w = o.max_w;
  if (o.max_h > 0 && o.h > o.max_h)
    o.h = o.max_h;
  return 0;
}

Slot *find_class(const char *cls) {
  if (!cls || !cls[0])
    return nullptr;
  for (int i = 0; i < kMaxWin; i++) {
    if (g_slots[i].used && strcmp(g_slots[i].opts.class_name, cls) == 0)
      return &g_slots[i];
  }
  return nullptr;
}

int chrome_btn_at(const Slot *s, int lx, int ly) {
  if (!s || !s->opts.framed || s->opts.no_title)
    return -1;
  if (ly < 0 || ly >= wm::kChromeTitleH || lx < 0)
    return -1;
  for (int i = 0; i < 3; i++) {
    int cx = wm::kChromeBtn0X + i * (wm::kChromeBtn + wm::kChromeBtnGap) +
             wm::kChromeBtn / 2;
    int cy = wm::kChromeBtnY + wm::kChromeBtn / 2;
    int dx = lx - cx;
    int dy = ly - cy;
    if (dx * dx + dy * dy <=
        (wm::kChromeBtn / 2 + 2) * (wm::kChromeBtn / 2 + 2)) {
      if (i == 0 && !s->opts.closable)
        return -1;
      if (i == 1 && !s->opts.can_minimize)
        return -1;
      if (i == 2 && !s->opts.can_maximize)
        return -1;
      return i;
    }
  }
  return -1;
}

int in_resize_grip(const Slot *s, int lx, int ly) {
  if (!s || !s->opts.resizable || !s->opts.framed)
    return 0;
  return (lx >= s->opts.w - wm::kResizeGrip &&
          ly >= s->opts.h - wm::kResizeGrip);
}

int handle_destroy(int id) {
  Slot *s = slot_by_id(id);
  if (!s)
    return -1;
  if (g_focus_id == id)
    g_focus_id = -1;
  if (g_drag_id == id)
    g_drag_id = -1;
  if (g_resize_id == id)
    g_resize_id = -1;
  clear_surface(s);
  s->used = 0;
  g_compose_dirty = 1;
  return 0;
}

} /* namespace wms */
