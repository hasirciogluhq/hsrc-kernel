#include <user/sdk/gfx.hpp>
#include <user/sdk/syscall.hpp>
#include <user/string.h>

namespace hsrc::sdk {
namespace {

#include "../ugx_font.inc"

bool point_in_round(int lx, int ly, int w, int h, int r)
{
    if (lx < 0 || ly < 0 || lx >= w || ly >= h)
        return false;
    if (r <= 0)
        return true;
    if (r * 2 > w)
        r = w / 2;
    if (r * 2 > h)
        r = h / 2;
    if (r <= 0)
        return true;

    int x = lx;
    int y = ly;
    if (x >= w - r)
        x = w - 1 - x;
    if (y >= h - r)
        y = h - 1 - y;
    if (x >= r || y >= r)
        return true;

    int dx = r - x;
    int dy = r - y;
    return dx * dx + dy * dy <= r * r;
}

const uint8_t *glyph(uint8_t ch)
{
    if (ch >= 32 && ch <= 126)
        return ugx_font[ch - 32];
    return ugx_font['?' - 32];
}

} // namespace

void Surface::clear(Color c)
{
    if (!valid())
        return;
    const uint32_t n = stride() * height();
    Color *p = pixels();
    for (uint32_t i = 0; i < n; i++)
        p[i] = c;
}

void Surface::set(int x, int y, Color c)
{
    if (!valid() || x < 0 || y < 0 ||
        (uint32_t)x >= width() || (uint32_t)y >= height())
        return;
    pixels()[(uint32_t)y * stride() + (uint32_t)x] = c;
}

void Surface::fill(int x, int y, int w, int h, Color c)
{
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++)
            set(x + xx, y + yy, c);
}

void Surface::fill_round(int x, int y, int w, int h, int radius, Color c)
{
    if (w <= 0 || h <= 0)
        return;
    for (int ly = 0; ly < h; ly++) {
        for (int lx = 0; lx < w; lx++) {
            if (point_in_round(lx, ly, w, h, radius))
                set(x + lx, y + ly, c);
        }
    }
}

void Surface::rect(int x, int y, int w, int h, Color c, int thickness)
{
    if (thickness < 1)
        thickness = 1;
    fill(x, y, w, thickness, c);
    fill(x, y + h - thickness, w, thickness, c);
    fill(x, y, thickness, h, c);
    fill(x + w - thickness, y, thickness, h, c);
}

void Surface::text(int x, int y, const char *s, Color c, int scale)
{
    if (!s || scale < 1)
        return;
    int cx = x;
    while (*s) {
        uint8_t ch = (uint8_t)*s++;
        if (ch == '\n') {
            cx = x;
            y += 10 * scale;
            continue;
        }
        const uint8_t *g = glyph(ch);
        for (int row = 0; row < 8; row++) {
            uint8_t bits = g[row];
            for (int col = 0; col < 8; col++) {
                if (!(bits & (1u << col)))
                    continue;
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        set(cx + col * scale + sx, y + row * scale + sy, c);
            }
        }
        cx += 8 * scale;
    }
}

void Surface::text_centered(int cx, int cy, const char *s, Color c, int scale)
{
    if (!s || scale < 1)
        return;
    int len = 0;
    while (s[len])
        len++;
    int tw = len * 8 * scale;
    int th = 8 * scale;
    text(cx - tw / 2, cy - th / 2, s, c, scale);
}

bool Window::create(int x, int y, int w, int h, uint32_t style, int radius,
                    const char *title)
{
    destroy();

    ugx_win_create a{};
    a.x = x;
    a.y = y;
    a.w = w;
    a.h = h;
    a.style = style;
    a.radius = radius;
    if (title)
        strncpy(a.title, title, sizeof(a.title) - 1);

    id_ = (int)syscall1(SYS_WM_CREATE, (long)&a);
    if (id_ < 0)
        return false;

    ugx_map map{};
    if (syscall2(SYS_WM_MAP, id_, (long)&map) < 0) {
        destroy();
        return false;
    }
    surf_ = Surface(map);
    return surf_.valid();
}

void Window::destroy()
{
    if (id_ >= 0) {
        (void)syscall1(SYS_WM_DESTROY, id_);
        id_ = -1;
    }
    surf_ = Surface();
}

void Window::fill(int x, int y, int w, int h, Color c)
{
    ugx_fill_args a{};
    a.win = id_;
    a.x = x;
    a.y = y;
    a.w = w;
    a.h = h;
    a.color = c;
    a.radius = 0;
    (void)syscall1(SYS_GX_FILL, (long)&a);
}

void Window::fill_round(int x, int y, int w, int h, int radius, Color c)
{
    ugx_fill_args a{};
    a.win = id_;
    a.x = x;
    a.y = y;
    a.w = w;
    a.h = h;
    a.color = c;
    a.radius = radius;
    (void)syscall1(SYS_GX_FILL_ROUND, (long)&a);
}

void Window::show(bool visible)
{
    (void)syscall2(SYS_WM_SHOW, id_, visible ? 1 : 0);
}

void Window::focus()
{
    (void)syscall1(SYS_WM_FOCUS, id_);
}

void Window::damage()
{
    (void)syscall1(SYS_GX_DAMAGE, 0);
}

bool screen_info(ScreenInfo &out)
{
    ugx_info info{};
    if (syscall1(SYS_GX_INFO, (long)&info) < 0)
        return false;
    out.width = info.width;
    out.height = info.height;
    out.bpp = info.bpp;
    return true;
}

bool present()
{
    return syscall1(SYS_GX_PRESENT, 0) == 0;
}

bool set_wallpaper_color(Color c)
{
    ugx_wallpaper args{};
    args.pixels = &c;
    args.width = 1;
    args.height = 1;
    args.stride = 1;
    return syscall1(SYS_GX_SET_WALLPAPER, (long)&args) == 0;
}

bool input(Input &out)
{
    ugx_input_state st{};
    if (syscall1(SYS_INPUT_STATE, (long)&st) < 0)
        return false;
    out.mouse_x = st.mouse_x;
    out.mouse_y = st.mouse_y;
    out.buttons = st.buttons;
    out.mods = st.mods;
    out.focus_id = st.focus_id;
    return true;
}

void damage()
{
    (void)syscall1(SYS_GX_DAMAGE, 0);
}

} // namespace hsrc::sdk
