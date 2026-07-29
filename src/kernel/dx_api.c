#include <kernel/dx_api.h>
#include <kernel/klock.h>
#include <kernel/string.h>

static const dx_api_t *g_raw;
static dx_api_t g_wrap;
static int g_wrap_ready;

#define KLOCK_GFX_CALL(ret_t, call) \
    do { \
        ret_t _r; \
        klock_acquire(&klock_gfx); \
        _r = (call); \
        klock_release(&klock_gfx); \
        return _r; \
    } while (0)

static int w_info(uint32_t *w, uint32_t *h, uint32_t *bpp)
{
    if (!g_raw || !g_raw->info)
        return -1;
    KLOCK_GFX_CALL(int, g_raw->info(w, h, bpp));
}

static int w_present(const void *args)
{
    if (!g_raw || !g_raw->present)
        return -1;
    KLOCK_GFX_CALL(int, g_raw->present(args));
}

static void w_mark_dirty(int win_id)
{
    if (!g_raw || !g_raw->mark_dirty)
        return;
    klock_acquire(&klock_gfx);
    g_raw->mark_dirty(win_id);
    klock_release(&klock_gfx);
}

static void w_mark_dirty_rect(int win_id, int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (!g_raw || !g_raw->mark_dirty_rect)
        return;
    klock_acquire(&klock_gfx);
    g_raw->mark_dirty_rect(win_id, x, y, w, h);
    klock_release(&klock_gfx);
}

static long w_wm_create(const void *args, uint32_t owner_pid)
{
    if (!g_raw || !g_raw->wm_create)
        return -1;
    KLOCK_GFX_CALL(long, g_raw->wm_create(args, owner_pid));
}

static int w_wm_set(int id, const void *opts)
{
    if (!g_raw || !g_raw->wm_set)
        return -1;
    KLOCK_GFX_CALL(int, g_raw->wm_set(id, opts));
}

static int w_wm_get(int id, void *out)
{
    if (!g_raw || !g_raw->wm_get)
        return -1;
    KLOCK_GFX_CALL(int, g_raw->wm_get(id, out));
}

static int w_wm_close(int id)
{
    if (!g_raw || !g_raw->wm_close)
        return -1;
    KLOCK_GFX_CALL(int, g_raw->wm_close(id));
}

static int w_wm_destroy(int id)
{
    if (!g_raw || !g_raw->wm_destroy)
        return -1;
    KLOCK_GFX_CALL(int, g_raw->wm_destroy(id));
}

static void w_wm_destroy_by_pid(int pid)
{
    if (!g_raw || !g_raw->wm_destroy_by_pid)
        return;
    klock_acquire(&klock_gfx);
    g_raw->wm_destroy_by_pid(pid);
    klock_release(&klock_gfx);
}

static int w_wm_map(int id, void *out)
{
    if (!g_raw || !g_raw->wm_map)
        return -1;
    KLOCK_GFX_CALL(int, g_raw->wm_map(id, out));
}

static int w_wm_move(int id, int32_t x, int32_t y)
{
    if (!g_raw || !g_raw->wm_move)
        return -1;
    KLOCK_GFX_CALL(int, g_raw->wm_move(id, x, y));
}

static int w_wm_resize(int id, int32_t w, int32_t h)
{
    if (!g_raw || !g_raw->wm_resize)
        return -1;
    KLOCK_GFX_CALL(int, g_raw->wm_resize(id, w, h));
}

static int w_wm_focus(int id)
{
    if (!g_raw || !g_raw->wm_focus)
        return -1;
    KLOCK_GFX_CALL(int, g_raw->wm_focus(id));
}

static int w_wm_show(int id, int vis)
{
    if (!g_raw || !g_raw->wm_show)
        return -1;
    KLOCK_GFX_CALL(int, g_raw->wm_show(id, vis));
}

static int w_wm_get_frame(int id, void *out)
{
    if (!g_raw || !g_raw->wm_get_frame)
        return -1;
    KLOCK_GFX_CALL(int, g_raw->wm_get_frame(id, out));
}

static int w_wm_pop_key(int id)
{
    if (!g_raw || !g_raw->wm_pop_key)
        return -1;
    KLOCK_GFX_CALL(int, g_raw->wm_pop_key(id));
}

static int w_wm_focused_id(void)
{
    if (!g_raw || !g_raw->wm_focused_id)
        return -1;
    KLOCK_GFX_CALL(int, g_raw->wm_focused_id());
}

static int w_wm_find(const char *title)
{
    if (!g_raw || !g_raw->wm_find)
        return -1;
    KLOCK_GFX_CALL(int, g_raw->wm_find(title));
}

static int w_wm_find_class(const char *class_name)
{
    if (!g_raw || !g_raw->wm_find_class)
        return -1;
    KLOCK_GFX_CALL(int, g_raw->wm_find_class(class_name));
}

static int w_fill(const void *args, int rounded)
{
    if (!g_raw || !g_raw->fill)
        return -1;
    KLOCK_GFX_CALL(int, g_raw->fill(args, rounded));
}

static int w_set_wallpaper(const void *args)
{
    if (!g_raw || !g_raw->set_wallpaper)
        return -1;
    KLOCK_GFX_CALL(int, g_raw->set_wallpaper(args));
}

static int w_input_state(void *out)
{
    if (!g_raw || !g_raw->input_state)
        return -1;
    KLOCK_GFX_CALL(int, g_raw->input_state(out));
}

static void w_pump_input(void)
{
    if (!g_raw || !g_raw->pump_input)
        return;
    klock_acquire(&klock_gfx);
    g_raw->pump_input();
    klock_release(&klock_gfx);
}

static int w_console_alloc(int pid, const char *name, int visible)
{
    if (!g_raw || !g_raw->console_alloc)
        return -1;
    KLOCK_GFX_CALL(int, g_raw->console_alloc(pid, name, visible));
}

static void w_console_free(int pid)
{
    if (!g_raw || !g_raw->console_free)
        return;
    klock_acquire(&klock_gfx);
    g_raw->console_free(pid);
    klock_release(&klock_gfx);
}

static ssize_t w_console_write(int pid, const void *buf, size_t len)
{
    if (!g_raw || !g_raw->console_write)
        return -1;
    KLOCK_GFX_CALL(ssize_t, g_raw->console_write(pid, buf, len));
}

static int w_console_show(int pid, int visible)
{
    if (!g_raw || !g_raw->console_show)
        return -1;
    KLOCK_GFX_CALL(int, g_raw->console_show(pid, visible));
}

static void build_wrap(void)
{
    memset(&g_wrap, 0, sizeof(g_wrap));
    g_wrap.info = w_info;
    g_wrap.present = w_present;
    g_wrap.mark_dirty = w_mark_dirty;
    g_wrap.mark_dirty_rect = w_mark_dirty_rect;
    g_wrap.wm_create = w_wm_create;
    g_wrap.wm_set = w_wm_set;
    g_wrap.wm_get = w_wm_get;
    g_wrap.wm_close = w_wm_close;
    g_wrap.wm_destroy = w_wm_destroy;
    g_wrap.wm_destroy_by_pid = w_wm_destroy_by_pid;
    g_wrap.wm_map = w_wm_map;
    g_wrap.wm_move = w_wm_move;
    g_wrap.wm_resize = w_wm_resize;
    g_wrap.wm_focus = w_wm_focus;
    g_wrap.wm_show = w_wm_show;
    g_wrap.wm_get_frame = w_wm_get_frame;
    g_wrap.wm_pop_key = w_wm_pop_key;
    g_wrap.wm_focused_id = w_wm_focused_id;
    g_wrap.wm_find = w_wm_find;
    g_wrap.wm_find_class = w_wm_find_class;
    g_wrap.fill = w_fill;
    g_wrap.set_wallpaper = w_set_wallpaper;
    g_wrap.input_state = w_input_state;
    g_wrap.pump_input = w_pump_input;
    g_wrap.console_alloc = w_console_alloc;
    g_wrap.console_free = w_console_free;
    g_wrap.console_write = w_console_write;
    g_wrap.console_show = w_console_show;
    g_wrap_ready = 1;
}

void dx_api_register(const dx_api_t *api)
{
    g_raw = api;
    if (!g_wrap_ready)
        build_wrap();
}

const dx_api_t *dx_api_get(void)
{
    if (!g_raw)
        return NULL;
    if (!g_wrap_ready)
        build_wrap();
    return &g_wrap;
}
