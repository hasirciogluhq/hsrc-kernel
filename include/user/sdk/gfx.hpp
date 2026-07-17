#pragma once

#include <user/gx.h>
#include <user/sdk/color.hpp>

namespace hsrc::sdk {

struct ScreenInfo {
    uint32_t width  = 0;
    uint32_t height = 0;
    uint32_t bpp    = 0;
};

struct Rect {
    int32_t x = 0, y = 0, w = 0, h = 0;
};

struct Input {
    int32_t mouse_x = 0;
    int32_t mouse_y = 0;
    uint8_t buttons = 0;
    uint8_t mods    = 0;
    int32_t focus_id = -1;
};

/* Mapped window pixel buffer (kernel-backed). */
class Surface {
public:
    Surface() = default;
    explicit Surface(const ugx_map &m) : m_(m) {}

    bool valid() const { return m_.pixels != nullptr && m_.width > 0; }
    uint32_t width() const { return m_.width; }
    uint32_t height() const { return m_.height; }
    uint32_t stride() const { return m_.stride; }
    Color *pixels() { return reinterpret_cast<Color *>(m_.pixels); }
    const Color *pixels() const { return reinterpret_cast<const Color *>(m_.pixels); }
    ugx_map *raw() { return &m_; }

    void clear(Color c);
    void set(int x, int y, Color c);
    void fill(int x, int y, int w, int h, Color c);
    void fill_round(int x, int y, int w, int h, int radius, Color c);
    void rect(int x, int y, int w, int h, Color c, int thickness = 1);
    void text(int x, int y, const char *s, Color c, int scale = 1);
    void text_centered(int cx, int cy, const char *s, Color c, int scale = 1);

private:
    ugx_map m_{};
};

class Window {
public:
    Window() = default;
    ~Window() = default; /* no atexit / static dtor — call destroy() if needed */

    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;

    bool create(int x, int y, int w, int h, uint32_t style, int radius,
                const char *title);
    void destroy();

    int id() const { return id_; }
    bool ok() const { return id_ >= 0 && surf_.valid(); }
    Surface &surface() { return surf_; }
    const Surface &surface() const { return surf_; }

    void fill(int x, int y, int w, int h, Color c);
    void fill_round(int x, int y, int w, int h, int radius, Color c);
    void show(bool visible);
    void focus();
    void damage();

private:
    int id_ = -1;
    Surface surf_;
};

bool screen_info(ScreenInfo &out);
bool present();
bool set_wallpaper_color(Color c);
bool input(Input &out);
void damage();

} // namespace hsrc::sdk
