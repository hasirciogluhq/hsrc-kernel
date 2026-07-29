#pragma once

#include <kernel/types.h>
#include <user/sdk/reed.hpp>
#include <user/sdk/wm.hpp>

namespace wms {

constexpr int kMaxWin = wm::kMaxWindows;

struct Slot {
  int used;
  int id;
  int owner_pid;
  int z;
  wm::WindowOptions opts;
  uint32_t surface_token;
  reed::Texture2D surface; /* imported client color texture (Reed) */
  int32_t damage_x, damage_y, damage_w, damage_h;
  int damaged;
};

struct DockItem {
  const char *label;
  const char *path;
  const char *cls;
  uint8_t r, g, b;
};

extern Slot g_slots[kMaxWin];
extern int g_next_id;
extern int g_next_z;
extern int g_focus_id;
extern int g_hit_id;
extern int g_drag_id;
extern int g_drag_off_x;
extern int g_drag_off_y;
extern int g_resize_id;
extern int g_resize_ox;
extern int g_resize_oy;
extern int g_resize_ow;
extern int g_resize_oh;
extern uint8_t g_prev_buttons;
extern int g_screen_w;
extern int g_screen_h;
extern int g_dock_hover;
extern int g_menu_open;
extern int g_compose_dirty;
extern int g_cursor_x;
extern int g_cursor_y;
extern reed::Device *g_dev;
extern reed::Texture2D g_cursor_tex;

extern const DockItem g_dock_items[];
extern const int kDockCount;

void str_cat(char *dst, const char *src);
void append_u32(char *dst, uint32_t v);
int parse_pid_from_name(const char *name, int *pid_out);

Slot *slot_by_id(int id);
Slot *alloc_slot(void);
int effective_visible(const wm::WindowOptions &o);
void raise_window(Slot *s);
int hit_test(int32_t x, int32_t y);
void focus_window(int id);
void clear_surface(Slot *s);
int import_surface(Slot *s, uint32_t token);
void mark_damage(Slot *s, int32_t x, int32_t y, int32_t w, int32_t h);
int clamp_geom(wm::WindowOptions &o);
Slot *find_class(const char *cls);
int chrome_btn_at(const Slot *s, int lx, int ly);
int in_resize_grip(const Slot *s, int lx, int ly);
int handle_destroy(int id);

} /* namespace wms */
