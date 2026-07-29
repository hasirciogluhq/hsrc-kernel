#include <user/sdk/wm.hpp>
#include <user/sdk/reed.hpp>
#include <user/sdk/kilim.hpp>
#include <user/sdk/fs.hpp>
#include <user/sdk/process.hpp>
#include <user/sdk/syscall.hpp>
#include <user/disp.h>
#include <user/input.h>
#include <kernel/syscall.h>
#include <kernel/string.h>
#include <kernel/vfs.h>
#include <kernel/types.h>

/*
 * Usermode compositor — file IPC under /tmp/wm/, Reed+Kilim present.
 * Kernel SYS_WM_* is not used.
 */

namespace {

constexpr int kMaxWin = wm::kMaxWindows;

struct Slot {
    int used;
    int id;
    int owner_pid;
    int z;
    wm::WindowOptions opts;
    uint32_t surface_token;
    uint32_t imported_handle;
    int32_t damage_x, damage_y, damage_w, damage_h;
    int damaged;
};

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
int g_menu_open = 0; /* 0=none 1=system */
int g_compose_dirty = 1;
/* Dock hover glow/tooltip only — cheap region-scoped redraw, not a full
 * screen recompose (see graphics-pipeline P05/P06, compose_dock_partial). */
int g_dock_only_dirty = 0;
int g_cursor_x = -1000;
int g_cursor_y = -1000;
uint32_t g_cursor_under[24 * 24];
int g_cursor_saved = 0;
reed::Device *g_dev = nullptr;

static void focus_window(int id);
static void mark_damage(Slot *s, int32_t x, int32_t y, int32_t w, int32_t h);
static int handle_destroy(int id);

/* Fluent-ish dark palette; macOS layout (menubar top, dock bottom). */
struct DockItem {
    const char *label;
    const char *path;
    const char *cls;
    uint8_t r, g, b;
};

static const DockItem g_dock_items[] = {
    {"Files", "files", "files", 0, 120, 212},
    {"Terminal", "terminal", "terminal", 16, 124, 16},
    {"Settings", "os-settings", "os.settings", 0, 153, 188},
    {"Monitor", "activity-monitor", "activity-monitor", 136, 23, 152},
    {"Mines", "minesweeper", "minesweeper", 232, 17, 35},
    {"ImGui", "imgui-demo", "imgui-demo", 255, 140, 0},
};
static constexpr int kDockCount =
    (int)(sizeof(g_dock_items) / sizeof(g_dock_items[0]));

static void dock_geom(int *out_x, int *out_y, int *out_w, int *out_h)
{
    int dock_w = wm::kDockPad * 2 + kDockCount * wm::kDockIcon +
                 (kDockCount - 1) * wm::kDockGap;
    *out_w = dock_w;
    *out_h = wm::kDockH;
    *out_x = (g_screen_w - dock_w) / 2;
    *out_y = g_screen_h - wm::kDockH - 18;
}

static int dock_hit(int mx, int my)
{
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

static Slot *find_class(const char *cls)
{
    if (!cls || !cls[0])
        return nullptr;
    for (int i = 0; i < kMaxWin; i++) {
        if (g_slots[i].used &&
            strcmp(g_slots[i].opts.class_name, cls) == 0)
            return &g_slots[i];
    }
    return nullptr;
}

static void launch_or_focus(const DockItem &it)
{
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
    (void)hsrc::sdk::process::spawn_ex(
        it.path, hsrc::sdk::process::ConsoleHidden, nullptr);
}

/* Traffic-light index: 0=close 1=min 2=max; -1=none */
static int chrome_btn_at(const Slot *s, int lx, int ly)
{
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
        if (dx * dx + dy * dy <= (wm::kChromeBtn / 2 + 2) * (wm::kChromeBtn / 2 + 2)) {
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

static int in_resize_grip(const Slot *s, int lx, int ly)
{
    if (!s || !s->opts.resizable || !s->opts.framed)
        return 0;
    return (lx >= s->opts.w - wm::kResizeGrip &&
            ly >= s->opts.h - wm::kResizeGrip);
}

static void str_cat(char *dst, const char *src)
{
    dst += strlen(dst);
    while (*src)
        *dst++ = *src++;
    *dst = '\0';
}

static void append_u32(char *dst, uint32_t v)
{
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

static int parse_pid_from_name(const char *name, int *pid_out)
{
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

static Slot *slot_by_id(int id)
{
    if (id < 0)
        return nullptr;
    for (int i = 0; i < kMaxWin; i++) {
        if (g_slots[i].used && g_slots[i].id == id)
            return &g_slots[i];
    }
    return nullptr;
}

static Slot *alloc_slot(void)
{
    for (int i = 0; i < kMaxWin; i++) {
        if (!g_slots[i].used) {
            memset(&g_slots[i], 0, sizeof(g_slots[i]));
            g_slots[i].used = 1;
            return &g_slots[i];
        }
    }
    return nullptr;
}

static int effective_visible(const wm::WindowOptions &o)
{
    return o.visible && !o.minimized;
}

static void raise_window(Slot *s)
{
    if (!s)
        return;
    s->z = g_next_z++;
    if (s->opts.always_on_bottom)
        s->z = -s->id;
    else if (s->opts.topmost)
        s->z += 100000;
}

static int hit_test(int32_t x, int32_t y)
{
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
        if (x < s->opts.x || y < s->opts.y ||
            x >= s->opts.x + s->opts.w || y >= s->opts.y + s->opts.h)
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

static void focus_window(int id)
{
    Slot *s = slot_by_id(id);
    if (!s || s->opts.background || !s->opts.accept_focus)
        return;
    g_focus_id = id;
    raise_window(s);
}

static void clear_surface(Slot *s)
{
    if (!s)
        return;
    s->surface_token = 0;
    s->imported_handle = 0;
}

static int import_surface(Slot *s, uint32_t token)
{
    uint32_t handle = 0;
    if (!s || !g_dev || token == 0)
        return -1;
    if (g_dev->import_handle(token, &handle) < 0 || handle == 0) {
        clear_surface(s);
        return -1;
    }
    s->surface_token = token;
    s->imported_handle = handle;
    s->damaged = 1;
    s->damage_x = 0;
    s->damage_y = 0;
    s->damage_w = s->opts.w;
    s->damage_h = s->opts.h;
    return 0;
}

static void mark_damage(Slot *s, int32_t x, int32_t y, int32_t w, int32_t h)
{
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

static int clamp_geom(wm::WindowOptions &o)
{
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

static int handle_create(wm::Request &req, wm::Response &rsp)
{
    Slot *s = alloc_slot();
    if (!s)
        return -1;
    s->id = g_next_id++;
    s->owner_pid = req.pid;
    s->opts = req.opts;
    clamp_geom(s->opts);
    raise_window(s);
    if (s->opts.visible && s->opts.accept_focus && !s->opts.background)
        g_focus_id = s->id;
    s->damaged = 1;
    rsp.window_id = s->id;
    rsp.opts = s->opts;
    g_compose_dirty = 1;
    return 0;
}

static int handle_destroy(int id)
{
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

static int handle_op(wm::Request &req, wm::Response &rsp)
{
    Slot *s;
    rsp.window_id = req.window_id;
    rsp.opts = req.opts;

    switch ((wm::Op)req.op) {
    case wm::Op::Create:
        return handle_create(req, rsp);
    case wm::Op::Destroy:
    case wm::Op::Close:
        return handle_destroy(req.window_id);
    case wm::Op::Set:
        s = slot_by_id(req.window_id);
        if (!s)
            return -1;
        s->opts = req.opts;
        clamp_geom(s->opts);
        raise_window(s);
        mark_damage(s, 0, 0, 0, 0);
        rsp.opts = s->opts;
        g_compose_dirty = 1;
        return 0;
    case wm::Op::Get:
        s = slot_by_id(req.window_id);
        if (!s)
            return -1;
        rsp.opts = s->opts;
        return 0;
    case wm::Op::Show:
        s = slot_by_id(req.window_id);
        if (!s)
            return -1;
        s->opts.visible = req.show != 0;
        mark_damage(s, 0, 0, 0, 0);
        g_compose_dirty = 1;
        return 0;
    case wm::Op::Focus:
        focus_window(req.window_id);
        g_compose_dirty = 1;
        return slot_by_id(req.window_id) ? 0 : -1;
    case wm::Op::Move:
        s = slot_by_id(req.window_id);
        if (!s)
            return -1;
        s->opts.x = req.x;
        s->opts.y = req.y;
        mark_damage(s, 0, 0, 0, 0);
        g_compose_dirty = 1;
        return 0;
    case wm::Op::Resize:
        s = slot_by_id(req.window_id);
        if (!s)
            return -1;
        s->opts.w = req.w;
        s->opts.h = req.h;
        clamp_geom(s->opts);
        mark_damage(s, 0, 0, 0, 0);
        g_compose_dirty = 1;
        return 0;
    case wm::Op::Damage:
        s = slot_by_id(req.window_id);
        if (!s)
            return -1;
        mark_damage(s, req.x, req.y, req.w, req.h);
        g_compose_dirty = 1;
        return 0;
    case wm::Op::Find: {
        rsp.window_id = -1;
        for (int i = 0; i < kMaxWin; i++) {
            if (!g_slots[i].used)
                continue;
            if (strcmp(g_slots[i].opts.title, req.name) == 0) {
                rsp.window_id = g_slots[i].id;
                return 0;
            }
        }
        return -1;
    }
    case wm::Op::FindClass: {
        rsp.window_id = -1;
        for (int i = 0; i < kMaxWin; i++) {
            if (!g_slots[i].used)
                continue;
            if (strcmp(g_slots[i].opts.class_name, req.name) == 0) {
                rsp.window_id = g_slots[i].id;
                return 0;
            }
        }
        return -1;
    }
    case wm::Op::AttachSurface:
        s = slot_by_id(req.window_id);
        if (!s)
            return -1;
        if (import_surface(s, req.surface_token) == 0)
            g_compose_dirty = 1;
        return s->imported_handle ? 0 : -1;
    default:
        return -1;
    }
}

static void write_rsp(int pid, const wm::Response &rsp)
{
    char path[96];
    strcpy(path, "/tmp/wm/out/");
    append_u32(path + strlen(path), (uint32_t)pid);
    str_cat(path, ".rsp");
    int fd = (int)hsrc::sdk::open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0)
        return;
    (void)hsrc::sdk::write(fd, (void *)&rsp, sizeof(rsp));
    hsrc::sdk::close(fd);
}

static void poll_requests(void)
{
    int dfd = (int)hsrc::sdk::open("/tmp/wm/in", O_RDONLY);
    if (dfd < 0)
        return;

    vfs_dirent_t ents[32];
    for (;;) {
        long n = hsrc::sdk::getdents(dfd, ents, 32);
        if (n <= 0)
            break;
        for (long i = 0; i < n; i++) {
            int pid = 0;
            char path[96];
            wm::Request req{};
            wm::Response rsp{};
            int fd;
            long rn;

            if (ents[i].name[0] == '.')
                continue;
            if (parse_pid_from_name(ents[i].name, &pid) < 0)
                continue;

            strcpy(path, "/tmp/wm/in/");
            str_cat(path, ents[i].name);
            fd = (int)hsrc::sdk::open(path, O_RDONLY);
            if (fd < 0)
                continue;
            rn = hsrc::sdk::read(fd, &req, sizeof(req));
            hsrc::sdk::close(fd);
            (void)hsrc::sdk::unlink(path);

            memset(&rsp, 0, sizeof(rsp));
            rsp.magic = wm::kProtoMagicRsp;
            if (rn != (long)sizeof(req) || req.magic != wm::kProtoMagicReq) {
                rsp.status = -1;
                rsp.window_id = -1;
            } else {
                rsp.status = handle_op(req, rsp);
            }
            write_rsp(pid, rsp);
        }
    }
    hsrc::sdk::close(dfd);
}

static void handle_input(void)
{
    input_state_t st;
    memset(&st, 0, sizeof(st));
    if (hsrc::sdk::syscall1(SYS_INPUT_STATE, (long)&st) < 0)
        return;

    int prev_hover = g_dock_hover;
    g_dock_hover = dock_hit(st.mouse_x, st.mouse_y);
    if (g_dock_hover != prev_hover) {
        /*
         * Hover glow + tooltip only touch the dock's own small rect — do
         * NOT set g_compose_dirty here, that forces a full-screen software
         * re-clear + re-blit of every window every time the mouse crosses
         * an icon boundary (see compose_dock_partial for the cheap path).
         * This used to make sweeping the mouse across the dock stall the
         * whole desktop to a few FPS.
         */
        g_dock_only_dirty = 1;
    }
    g_hit_id = (g_dock_hover >= 0) ? -1 : hit_test(st.mouse_x, st.mouse_y);

    uint8_t btn = st.buttons;
    uint8_t pressed = (uint8_t)(btn & ~g_prev_buttons);
    uint8_t released = (uint8_t)(g_prev_buttons & ~btn);

    if (released & INPUT_BTN_LEFT) {
        if (g_drag_id >= 0 || g_resize_id >= 0)
            g_compose_dirty = 1;
        g_drag_id = -1;
        g_resize_id = -1;
    }

    if (pressed & INPUT_BTN_LEFT) {
        g_compose_dirty = 1;
        if (st.mouse_y < wm::kMenubarH && st.mouse_x < 90) {
            g_menu_open = g_menu_open ? 0 : 1;
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
                int can_drag =
                    s->opts.framed && !s->opts.no_drag && !s->opts.no_title;
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
                g_compose_dirty = 1;
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
                g_compose_dirty = 1;
            }
        }
    }

    g_prev_buttons = btn;
}

static uint32_t blend_px(uint32_t dst, uint32_t src)
{
    uint32_t sa = (src >> 24) & 0xffu;
    if (sa == 0)
        return dst;
    if (sa == 255)
        return src;
    uint32_t inv = 255u - sa;
    uint32_t sr = (src >> 16) & 0xffu;
    uint32_t sg = (src >> 8) & 0xffu;
    uint32_t sb = src & 0xffu;
    uint32_t dr = (dst >> 16) & 0xffu;
    uint32_t dg = (dst >> 8) & 0xffu;
    uint32_t db = dst & 0xffu;
    uint32_t da = (dst >> 24) & 0xffu;
    uint32_t r = (sr * sa + dr * inv) / 255u;
    uint32_t g = (sg * sa + dg * inv) / 255u;
    uint32_t b = (sb * sa + db * inv) / 255u;
    uint32_t a = sa + (da * inv) / 255u;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

static void blit_window(Slot *s, uint32_t *dst, uint32_t dst_stride,
                        int32_t clip_x0 = 0, int32_t clip_y0 = 0,
                        int32_t clip_x1 = 0x7fffffff, int32_t clip_y1 = 0x7fffffff)
{
    disp_texture_map tm;
    uint32_t *src;
    int32_t x0, y0, x1, y1;

    if (!s || !s->imported_handle || !dst)
        return;

    memset(&tm, 0, sizeof(tm));
    tm.handle = s->imported_handle;
    if (hsrc::sdk::syscall2(SYS_DISP_CALL, DISP_OP_TEXTURE_MAP, (long)&tm) < 0 ||
        !tm.ptr) {
        /* Token/owner gone — drop surface. */
        clear_surface(s);
        return;
    }

    src = (uint32_t *)tm.ptr;
    x0 = s->opts.x;
    y0 = s->opts.y;
    x1 = x0 + s->opts.w;
    y1 = y0 + s->opts.h;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > g_screen_w)
        x1 = g_screen_w;
    if (y1 > g_screen_h)
        y1 = g_screen_h;
    /* Extra caller-supplied clip — used by compose_dock_partial() to avoid
     * touching pixels outside the small dirty region being redrawn. */
    if (x0 < clip_x0)
        x0 = clip_x0;
    if (y0 < clip_y0)
        y0 = clip_y0;
    if (x1 > clip_x1)
        x1 = clip_x1;
    if (y1 > clip_y1)
        y1 = clip_y1;

    for (int32_t y = y0; y < y1; y++) {
        int32_t sy = y - s->opts.y;
        if (sy < 0 || (uint32_t)sy >= tm.height)
            continue;
        uint32_t *drow = dst + (uint32_t)y * (dst_stride / 4u);
        uint32_t *srow =
            src + (uint32_t)sy * (tm.stride / 4u);
        for (int32_t x = x0; x < x1; x++) {
            int32_t sx = x - s->opts.x;
            if (sx < 0 || (uint32_t)sx >= tm.width)
                continue;
            uint32_t sp = srow[sx];
            if (s->opts.alpha || s->opts.acrylic || ((sp >> 24) & 0xffu) != 255u)
                drow[x] = blend_px(drow[x], sp);
            else
                drow[x] = sp;
        }
    }
}

static void fb_put(uint32_t *fb, uint32_t stride4, int x, int y, uint32_t color)
{
    if (x < 0 || y < 0 || x >= g_screen_w || y >= g_screen_h)
        return;
    fb[(uint32_t)y * stride4 + (uint32_t)x] = color;
}

/* Classic arrow pointer (not a + crosshair). Hotspot = tip at (x,y). */
static void draw_cursor_arrow(uint32_t *fb, uint32_t stride4, int x, int y)
{
    /* 12x19 mask: 1=white fill, 2=black outline */
    static const uint8_t tip[19][12] = {
        {2,0,0,0,0,0,0,0,0,0,0,0},
        {2,2,0,0,0,0,0,0,0,0,0,0},
        {2,1,2,0,0,0,0,0,0,0,0,0},
        {2,1,1,2,0,0,0,0,0,0,0,0},
        {2,1,1,1,2,0,0,0,0,0,0,0},
        {2,1,1,1,1,2,0,0,0,0,0,0},
        {2,1,1,1,1,1,2,0,0,0,0,0},
        {2,1,1,1,1,1,1,2,0,0,0,0},
        {2,1,1,1,1,1,1,1,2,0,0,0},
        {2,1,1,1,1,1,1,1,1,2,0,0},
        {2,1,1,1,1,1,2,2,2,2,2,0},
        {2,1,1,2,1,1,2,0,0,0,0,0},
        {2,1,2,0,2,1,1,2,0,0,0,0},
        {2,2,0,0,2,1,1,2,0,0,0,0},
        {2,0,0,0,0,2,1,1,2,0,0,0},
        {0,0,0,0,0,2,1,1,2,0,0,0},
        {0,0,0,0,0,0,2,1,1,2,0,0},
        {0,0,0,0,0,0,2,1,1,2,0,0},
        {0,0,0,0,0,0,0,2,2,0,0,0},
    };
    for (int row = 0; row < 19; row++) {
        for (int col = 0; col < 12; col++) {
            uint8_t v = tip[row][col];
            if (v == 1)
                fb_put(fb, stride4, x + col, y + row, 0xffffffffu);
            else if (v == 2)
                fb_put(fb, stride4, x + col, y + row, 0xff111111u);
        }
    }
}

static void draw_window_chrome(kilim::Context &k, Slot *s)
{
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
    uint32_t title = active ? kilim::rgba(38, 40, 47, 250)
                            : kilim::rgba(32, 34, 40, 235);
    k.fill_round_rect(x, y, w, wm::kChromeTitleH + wm::kWinRadius, wm::kWinRadius, title);
    k.fill_rect(x, y + wm::kChromeTitleH, w, wm::kWinRadius, title);
    k.fill_rect(x, y + wm::kChromeTitleH - 1, w, 1,
                kilim::rgba(90, 140, 255, active ? 200 : 0)); /* accent underline */

    /* Thin 1px frame around whole window — hairline, not a heavy border. */
    k.stroke_rect(x, y, w, h, 1,
                  active ? kilim::rgba(255, 255, 255, 30)
                         : kilim::rgba(255, 255, 255, 14));

    /* macOS traffic lights (left) */
    int cy = y + wm::kChromeBtnY + wm::kChromeBtn / 2;
    if (s->opts.closable)
        k.circle(x + wm::kChromeBtn0X + wm::kChromeBtn / 2, cy,
                 wm::kChromeBtn / 2, kilim::rgba(255, 95, 87, 255), 1);
    if (s->opts.can_minimize)
        k.circle(x + wm::kChromeBtn0X + (wm::kChromeBtn + wm::kChromeBtnGap) +
                     wm::kChromeBtn / 2,
                 cy, wm::kChromeBtn / 2, kilim::rgba(255, 189, 46, 255), 1);
    if (s->opts.can_maximize)
        k.circle(x + wm::kChromeBtn0X +
                     2 * (wm::kChromeBtn + wm::kChromeBtnGap) +
                     wm::kChromeBtn / 2,
                 cy, wm::kChromeBtn / 2, kilim::rgba(40, 200, 64, 255), 1);

    if (s->opts.resizable) {
        int gx = x + w - 11;
        int gy = y + h - 11;
        k.line(gx, gy + 8, gx + 8, gy, kilim::rgba(140, 150, 165, 200));
        k.line(gx + 3, gy + 8, gx + 8, gy + 3, kilim::rgba(140, 150, 165, 160));
    }
}

/* Dock bar + icons + hover tooltip label — the one piece of chrome that
 * changes on every mouse-over, so it is factored out for the cheap
 * region-scoped redraw path (compose_dock_partial). Pure function of
 * global dock/hover state; safe to call under a scissored partial frame. */
static void draw_dock(kilim::Context &k)
{
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
        k.fill_round_rect(ix + 5, iy + 4, wm::kDockIcon - 10, wm::kDockIcon / 3,
                          6, kilim::rgba(255, 255, 255, 46));
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

static void draw_system_chrome(kilim::Context &k, int mx, int my)
{
    (void)mx;
    (void)my;

    /* Menubar — flat, low-contrast strip (content stays the focus). */
    k.fill_rect(0, 0, g_screen_w, wm::kMenubarH,
                kilim::rgba(24, 26, 32, 235));
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

static void compose_frame(kilim::Context &k)
{
    int order[kMaxWin];
    int n = 0;
    for (int i = 0; i < kMaxWin; i++) {
        if (g_slots[i].used && effective_visible(g_slots[i].opts))
            order[n++] = i;
    }
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (g_slots[order[j]].z < g_slots[order[i]].z) {
                int t = order[i];
                order[i] = order[j];
                order[j] = t;
            }
        }
    }

    if (!g_dev)
        return;

    if (k.begin_frame() < 0)
        return;

    reed::RenderTarget &rt = k.target();
    uint32_t *fb = (uint32_t *)rt.color().map();
    uint32_t stride = rt.color().stride();
    uint32_t stride4 = stride / 4u;
    if (!fb) {
        (void)k.end_frame();
        return;
    }

    /* Client surfaces first (under chrome). */
    for (int i = 0; i < n; i++)
        blit_window(&g_slots[order[i]], fb, stride);

    input_state_t st;
    memset(&st, 0, sizeof(st));
    int mx = 0, my = 0;
    if (hsrc::sdk::syscall1(SYS_INPUT_STATE, (long)&st) == 0) {
        mx = st.mouse_x;
        my = st.mouse_y;
        k.set_pointer(mx, my, st.buttons);
    }
    rt.color().unmap();

    /* Window frames / traffic lights / shadows via Kilim */
    for (int i = 0; i < n; i++) {
        Slot *s = &g_slots[order[i]];
        if (s->opts.background)
            continue;
        draw_window_chrome(k, s);
        if (s->opts.framed && !s->opts.no_title && s->opts.title[0]) {
            int tx = s->opts.x + wm::kChromeBtnZone + 6;
            int ty = s->opts.y + (wm::kChromeTitleH - 12) / 2;
            uint32_t tc = (s->id == g_focus_id)
                              ? kilim::rgba(245, 246, 250, 255)
                              : kilim::rgba(180, 186, 198, 255);
            k.text(s->opts.title, tx, ty, 13, tc);
        }
    }

    draw_system_chrome(k, mx, my);

    k.text("hsrcOS", 14, 6, 13, kilim::rgba(240, 242, 248, 255));
    k.text("File", 96, 6, 12, kilim::rgba(180, 186, 198, 235));
    k.text("Edit", 140, 6, 12, kilim::rgba(180, 186, 198, 235));
    k.text("View", 186, 6, 12, kilim::rgba(180, 186, 198, 235));
    k.text("Go", 234, 6, 12, kilim::rgba(180, 186, 198, 235));
    k.text("Window", 268, 6, 12, kilim::rgba(180, 186, 198, 235));
    k.text("Help", 336, 6, 12, kilim::rgba(180, 186, 198, 235));

    /* Cursor on top — remap briefly */
    fb = (uint32_t *)rt.color().map();
    if (fb) {
        /* Save underlay then draw (for cursor-only updates). */
        g_cursor_x = mx;
        g_cursor_y = my;
        g_cursor_saved = 1;
        for (int row = 0; row < 24; row++) {
            for (int col = 0; col < 24; col++) {
                int px = mx + col;
                int py = my + row;
                uint32_t c = 0;
                if (px >= 0 && py >= 0 && px < g_screen_w && py < g_screen_h)
                    c = fb[(uint32_t)py * stride4 + (uint32_t)px];
                g_cursor_under[row * 24 + col] = c;
            }
        }
        draw_cursor_arrow(fb, stride4, mx, my);
        rt.color().unmap();
    }

    (void)k.end_frame();

    for (int i = 0; i < kMaxWin; i++)
        g_slots[i].damaged = 0;
    g_compose_dirty = 0;
}

/* Mouse-only present: restore old cursor pixels, draw at new pos, damage present. */
static void compose_cursor_only(kilim::Context &k, int mx, int my)
{
    reed::RenderTarget &rt = k.target();
    uint32_t *fb = (uint32_t *)rt.color().map();
    uint32_t stride4 = rt.color().stride() / 4u;
    if (!fb)
        return;

    int ox = g_cursor_x;
    int oy = g_cursor_y;
    if (g_cursor_saved) {
        for (int row = 0; row < 24; row++) {
            for (int col = 0; col < 24; col++) {
                int px = ox + col;
                int py = oy + row;
                if (px < 0 || py < 0 || px >= g_screen_w || py >= g_screen_h)
                    continue;
                fb[(uint32_t)py * stride4 + (uint32_t)px] =
                    g_cursor_under[row * 24 + col];
            }
        }
    }

    g_cursor_x = mx;
    g_cursor_y = my;
    g_cursor_saved = 1;
    for (int row = 0; row < 24; row++) {
        for (int col = 0; col < 24; col++) {
            int px = mx + col;
            int py = my + row;
            uint32_t c = 0;
            if (px >= 0 && py >= 0 && px < g_screen_w && py < g_screen_h)
                c = fb[(uint32_t)py * stride4 + (uint32_t)px];
            g_cursor_under[row * 24 + col] = c;
        }
    }
    draw_cursor_arrow(fb, stride4, mx, my);
    rt.color().unmap();

    int x0 = ox < mx ? ox : mx;
    int y0 = oy < my ? oy : my;
    int x1 = (ox > mx ? ox : mx) + 24;
    int y1 = (oy > my ? oy : my) + 24;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    (void)k.present_damage(x0, y0, x1 - x0, y1 - y0);
}

/*
 * Region-scoped redraw for dock hover glow/tooltip changes (see
 * g_dock_only_dirty). Re-blits only the windows intersecting the dock's
 * bounding rect (usually just the always-on-bottom wallpaper), draws the
 * dock on top, then presents just that rect — instead of the full-screen
 * clear + full-screen re-blit compose_frame() does. This is the fix for
 * the multi-second desktop freeze that used to happen while sweeping the
 * mouse across the dock (graphics-pipeline P05/P06/C07/C22).
 */
static void compose_dock_partial(kilim::Context &k)
{
    if (!g_dev)
        return;

    input_state_t st;
    memset(&st, 0, sizeof(st));
    int mx = g_cursor_x, my = g_cursor_y;
    if (hsrc::sdk::syscall1(SYS_INPUT_STATE, (long)&st) == 0) {
        mx = st.mouse_x;
        my = st.mouse_y;
    }

    int dx, dy, dw, dh;
    dock_geom(&dx, &dy, &dw, &dh);
    /* Dock rect + headroom for the hover-lift (-6px), glow (+3px) and the
     * tooltip bubble drawn above it, plus the cursor sprite (24x24) which
     * is almost certainly inside/near this rect while hovering. */
    int rx = dx - 6;
    int ry = dy - 34;
    int rw = dw + 12;
    int rh = dh + 40;
    if (mx - 2 < rx) { int d = rx - (mx - 2); rx -= d; rw += d; }
    if (my - 2 < ry) { int d = ry - (my - 2); ry -= d; rh += d; }
    if (mx + 26 > rx + rw)
        rw = mx + 26 - rx;
    if (my + 26 > ry + rh)
        rh = my + 26 - ry;
    if (rx < 0) { rw += rx; rx = 0; }
    if (ry < 0) { rh += ry; ry = 0; }
    if (rx + rw > g_screen_w)
        rw = g_screen_w - rx;
    if (ry + rh > g_screen_h)
        rh = g_screen_h - ry;
    if (rw <= 0 || rh <= 0)
        return;

    if (k.begin_frame_region(rx, ry, rw, rh) < 0)
        return;

    reed::RenderTarget &rt = k.target();
    uint32_t *fb = (uint32_t *)rt.color().map();
    uint32_t stride = rt.color().stride();
    uint32_t stride4 = stride / 4u;
    if (!fb) {
        (void)k.commit_frame();
        return;
    }

    int order[kMaxWin];
    int n = 0;
    for (int i = 0; i < kMaxWin; i++) {
        if (g_slots[i].used && effective_visible(g_slots[i].opts))
            order[n++] = i;
    }
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (g_slots[order[j]].z < g_slots[order[i]].z) {
                int t = order[i];
                order[i] = order[j];
                order[j] = t;
            }
        }
    }
    for (int i = 0; i < n; i++)
        blit_window(&g_slots[order[i]], fb, stride, rx, ry, rx + rw, ry + rh);
    rt.color().unmap();

    draw_dock(k); /* scissored to [rx,ry,rw,rh] by begin_frame_region() */

    fb = (uint32_t *)rt.color().map();
    if (fb) {
        g_cursor_x = mx;
        g_cursor_y = my;
        g_cursor_saved = 1;
        for (int row = 0; row < 24; row++) {
            for (int col = 0; col < 24; col++) {
                int px = mx + col;
                int py = my + row;
                uint32_t c = 0;
                if (px >= 0 && py >= 0 && px < g_screen_w && py < g_screen_h)
                    c = fb[(uint32_t)py * stride4 + (uint32_t)px];
                g_cursor_under[row * 24 + col] = c;
            }
        }
        draw_cursor_arrow(fb, stride4, mx, my);
        rt.color().unmap();
    }

    (void)k.commit_frame();
    (void)k.present_damage(rx, ry, rw, rh);
}

static int setup_dirs(void)
{
    (void)hsrc::sdk::mkdir("/tmp", 0755);
    (void)hsrc::sdk::mkdir("/tmp/wm", 0755);
    (void)hsrc::sdk::mkdir("/tmp/wm/in", 0755);
    (void)hsrc::sdk::mkdir("/tmp/wm/out", 0755);

    char pidbuf[16];
    int pid = (int)hsrc::sdk::getpid();
    pidbuf[0] = '\0';
    append_u32(pidbuf, (uint32_t)pid);
    str_cat(pidbuf, "\n");
    int fd = (int)hsrc::sdk::open("/tmp/wm/pid", O_WRONLY | O_CREAT | O_TRUNC);
    if (fd >= 0) {
        (void)hsrc::sdk::write(fd, pidbuf, strlen(pidbuf));
        hsrc::sdk::close(fd);
    }
    return 0;
}

} /* namespace */

extern "C" void exec_main(void)
{
    /* Context holds ~3MB batch vertex buffers — MUST NOT live on the user ustack. */
    static reed::Device device;
    static kilim::Context kctx;

    if (setup_dirs() < 0) {
        for (;;)
            hsrc::sdk::yield(10);
    }

    if (device.init() < 0) {
        for (;;)
            hsrc::sdk::yield(10);
    }
    g_dev = &device;
    g_screen_w = (int)device.caps().width;
    g_screen_h = (int)device.caps().height;

    if (kctx.init(&device) < 0) {
        for (;;)
            hsrc::sdk::yield(10);
    }

    memset(g_slots, 0, sizeof(g_slots));

    /* First present ASAP — avoid long black/splash hang before clients connect. */
    compose_frame(kctx);

    for (;;) {
        poll_requests();
        handle_input();

        input_state_t st;
        memset(&st, 0, sizeof(st));
        (void)hsrc::sdk::syscall1(SYS_INPUT_STATE, (long)&st);

        int need_full = g_compose_dirty || g_drag_id >= 0 || g_resize_id >= 0 ||
                        g_menu_open;
        if (need_full) {
            compose_frame(kctx);
            g_dock_only_dirty = 0;
        } else if (g_dock_only_dirty) {
            compose_dock_partial(kctx);
            g_dock_only_dirty = 0;
        } else if (st.mouse_x != g_cursor_x || st.mouse_y != g_cursor_y) {
            compose_cursor_only(kctx, st.mouse_x, st.mouse_y);
        }
        /* No busy yield when idle — still pump often for input. */
        hsrc::sdk::yield(need_full ? 0 : 0);
    }
}
