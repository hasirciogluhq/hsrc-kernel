#include <user/sdk/wm.hpp>
#include <user/sdk/reed.hpp>
#include <user/sdk/kilim.hpp>
#include <user/sdk/fs.hpp>
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
uint8_t g_prev_buttons = 0;
int g_screen_w = 0;
int g_screen_h = 0;
reed::Device *g_dev = nullptr;

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
    clear_surface(s);
    s->used = 0;
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
        return 0;
    case wm::Op::Focus:
        focus_window(req.window_id);
        return slot_by_id(req.window_id) ? 0 : -1;
    case wm::Op::Move:
        s = slot_by_id(req.window_id);
        if (!s)
            return -1;
        s->opts.x = req.x;
        s->opts.y = req.y;
        mark_damage(s, 0, 0, 0, 0);
        return 0;
    case wm::Op::Resize:
        s = slot_by_id(req.window_id);
        if (!s)
            return -1;
        s->opts.w = req.w;
        s->opts.h = req.h;
        clamp_geom(s->opts);
        mark_damage(s, 0, 0, 0, 0);
        return 0;
    case wm::Op::Damage:
        s = slot_by_id(req.window_id);
        if (!s)
            return -1;
        mark_damage(s, req.x, req.y, req.w, req.h);
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
        return import_surface(s, req.surface_token);
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

    g_hit_id = hit_test(st.mouse_x, st.mouse_y);

    uint8_t btn = st.buttons;
    uint8_t pressed = (uint8_t)(btn & ~g_prev_buttons);
    uint8_t released = (uint8_t)(g_prev_buttons & ~btn);

    if (released & INPUT_BTN_LEFT) {
        if (g_drag_id >= 0)
            g_drag_id = -1;
    }

    if (pressed & INPUT_BTN_LEFT) {
        int id = g_hit_id;
        Slot *s = slot_by_id(id);
        if (s && !s->opts.background && s->opts.accept_focus) {
            focus_window(id);
            int lx = st.mouse_x - s->opts.x;
            int ly = st.mouse_y - s->opts.y;
            int can_drag = s->opts.framed && !s->opts.no_drag && !s->opts.no_title;
            if (can_drag && ly >= 0 && ly < wm::kChromeTitleH &&
                lx >= wm::kChromeBtnZone && lx < s->opts.w) {
                g_drag_id = id;
                g_drag_off_x = lx;
                g_drag_off_y = ly;
            }
        }
    }

    if (g_drag_id >= 0 && (btn & INPUT_BTN_LEFT)) {
        Slot *s = slot_by_id(g_drag_id);
        if (s) {
            int32_t nx = st.mouse_x - g_drag_off_x;
            int32_t ny = st.mouse_y - g_drag_off_y;
            if (ny < 0)
                ny = 0;
            if (nx != s->opts.x || ny != s->opts.y) {
                s->opts.x = nx;
                s->opts.y = ny;
                mark_damage(s, 0, 0, 0, 0);
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

static void blit_window(Slot *s, uint32_t *dst, uint32_t dst_stride)
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

static void fb_fill(uint32_t *fb, uint32_t stride4, int x, int y, int w, int h,
                    uint32_t color)
{
    if (!fb || w <= 0 || h <= 0)
        return;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > g_screen_w)
        w = g_screen_w - x;
    if (y + h > g_screen_h)
        h = g_screen_h - y;
    if (w <= 0 || h <= 0)
        return;
    for (int row = 0; row < h; row++) {
        uint32_t *d = fb + (uint32_t)(y + row) * stride4 + (uint32_t)x;
        for (int col = 0; col < w; col++)
            d[col] = color;
    }
}

static void fb_disc(uint32_t *fb, uint32_t stride4, int cx, int cy, int r,
                    uint32_t color)
{
    int rr = r * r;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy > rr)
                continue;
            int x = cx + dx;
            int y = cy + dy;
            if (x < 0 || y < 0 || x >= g_screen_w || y >= g_screen_h)
                continue;
            fb[(uint32_t)y * stride4 + (uint32_t)x] = color;
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

static void draw_chrome_fb(uint32_t *fb, uint32_t stride4, Slot *s)
{
    if (!s->opts.framed || s->opts.no_title)
        return;
    int x = s->opts.x;
    int y = s->opts.y;
    int w = s->opts.w;
    fb_fill(fb, stride4, x, y, w, wm::kChromeTitleH, 0xff2d2d30u);
    fb_fill(fb, stride4, x, y + wm::kChromeTitleH - 1, w, 1, 0xff3c3c40u);
    if (s->opts.closable)
        fb_disc(fb, stride4, x + wm::kChromeBtn0X + wm::kChromeBtn / 2,
                y + wm::kChromeBtnY + wm::kChromeBtn / 2, wm::kChromeBtn / 2,
                0xffff5f57u);
    if (s->opts.can_minimize)
        fb_disc(fb, stride4,
                x + wm::kChromeBtn0X + (wm::kChromeBtn + wm::kChromeBtnGap) +
                    wm::kChromeBtn / 2,
                y + wm::kChromeBtnY + wm::kChromeBtn / 2, wm::kChromeBtn / 2,
                0xffffbd2eu);
    if (s->opts.can_maximize)
        fb_disc(fb, stride4,
                x + wm::kChromeBtn0X + 2 * (wm::kChromeBtn + wm::kChromeBtnGap) +
                    wm::kChromeBtn / 2,
                y + wm::kChromeBtnY + wm::kChromeBtn / 2, wm::kChromeBtn / 2,
                0xff28c840u);
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

    /* begin_frame clears RT immediately (Reed clear is eager). */
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

    /* Backdrop already cleared by begin_frame; paint windows + chrome. */
    for (int i = 0; i < n; i++) {
        blit_window(&g_slots[order[i]], fb, stride);
        draw_chrome_fb(fb, stride4, &g_slots[order[i]]);
    }

    /* System menubar + dock (always on top of client surfaces). */
    {
        const int menubar_h = 32;
        const int dock_h = 72;
        const int dock_pad = 24;
        const int icon_n = 6;
        const int icon = 48;
        const int gap = 14;
        int dock_w = dock_pad * 2 + icon_n * icon + (icon_n - 1) * gap;
        int dock_x = (g_screen_w - dock_w) / 2;
        int dock_y = g_screen_h - dock_h - 20;

        fb_fill(fb, stride4, 0, 0, g_screen_w, menubar_h, 0xff12141cu);
        fb_fill(fb, stride4, 0, menubar_h - 1, g_screen_w, 1, 0xff2a2f3au);

        fb_fill(fb, stride4, dock_x, dock_y, dock_w, dock_h, 0xff1c2230u);
        for (int i = 0; i < icon_n; i++) {
            int ix = dock_x + dock_pad + i * (icon + gap);
            int iy = dock_y + (dock_h - icon) / 2;
            uint32_t col = 0xff4a6fa5u;
            if (i == 0)
                col = 0xff5b8defu;
            else if (i == 1)
                col = 0xff6bcb77u;
            else if (i == 2)
                col = 0xfff0c14au;
            else if (i == 3)
                col = 0xffe06c75u;
            else if (i == 4)
                col = 0xffc792eau;
            fb_fill(fb, stride4, ix, iy, icon, icon, col);
        }
    }

    input_state_t st;
    memset(&st, 0, sizeof(st));
    if (hsrc::sdk::syscall1(SYS_INPUT_STATE, (long)&st) == 0) {
        k.set_pointer(st.mouse_x, st.mouse_y, st.buttons);
        draw_cursor_arrow(fb, stride4, st.mouse_x, st.mouse_y);
    }
    rt.color().unmap();

    for (int i = 0; i < n; i++) {
        Slot *s = &g_slots[order[i]];
        if (!s->opts.framed || s->opts.no_title || !s->opts.title[0])
            continue;
        int tx = s->opts.x + wm::kChromeBtnZone + 4;
        int ty = s->opts.y + (wm::kChromeTitleH - 12) / 2;
        k.text(s->opts.title, tx, ty, 12, kilim::rgba(240, 240, 240));
    }

    /* Menubar labels (after window titles so they stay readable). */
    k.text("hsrcOS", 14, 8, 14, kilim::rgba(240, 244, 250));
    k.text("Finder", 100, 9, 13, kilim::rgba(200, 210, 225));
    k.text("File", 170, 9, 13, kilim::rgba(200, 210, 225));
    k.text("Edit", 220, 9, 13, kilim::rgba(200, 210, 225));
    k.text("View", 270, 9, 13, kilim::rgba(200, 210, 225));
    k.text("Go", 330, 9, 13, kilim::rgba(200, 210, 225));

    (void)k.end_frame();

    for (int i = 0; i < kMaxWin; i++)
        g_slots[i].damaged = 0;
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
    /* Context holds ~3MB batch vertex buffers — MUST NOT live on the 64K ustack. */
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
        compose_frame(kctx);
        hsrc::sdk::yield(1);
    }
}
