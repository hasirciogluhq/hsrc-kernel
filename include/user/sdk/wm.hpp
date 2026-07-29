#pragma once

#include <stdint.h>
#include <user/input.h>
#include <user/sdk/reed.hpp>

/*
 * Usermode window-manager client (file IPC under /tmp/wm/).
 * No SYS_WM_* — policy and compose live in userspace/window-manager.
 */

namespace wm {

constexpr uint32_t kProtoMagicReq = 0x71524d57u; /* 'WMRq' LE */
constexpr uint32_t kProtoMagicRsp = 0x73524d57u; /* 'WMRs' LE */
constexpr int kMaxWindows = 64;
constexpr int kChromeTitleH = 28;
constexpr int kChromeBtn = 12;
constexpr int kChromeBtnY = 8;
constexpr int kChromeBtn0X = 10;
constexpr int kChromeBtnGap = 8;
constexpr int kChromeBtnZone = 56;

enum class Op : uint32_t {
    Create = 1,
    Destroy = 2,
    Set = 3,
    Get = 4,
    Show = 5,
    Focus = 6,
    Move = 7,
    Resize = 8,
    Damage = 9,
    Find = 10,
    FindClass = 11,
    Close = 12,
    AttachSurface = 13,
};

struct WindowOptions {
    int32_t x = 0;
    int32_t y = 0;
    int32_t w = 640;
    int32_t h = 480;
    int32_t min_w = 0;
    int32_t min_h = 0;
    int32_t max_w = 0;
    int32_t max_h = 0;
    int32_t radius = 0;
    uint8_t opacity = 255;
    char title[64]{};
    char class_name[32]{};
    int32_t owner_id = -1;
    int32_t parent_id = -1;

    bool acrylic = false;
    bool rounded = false;
    bool alpha = false;
    bool background = false;
    bool no_drag = false;
    bool no_title = false;
    bool topmost = false;
    bool always_on_bottom = false;
    bool resizable = true;
    bool fullscreen = false;
    bool framed = true;
    bool shadow = false;

    bool visible = true;
    bool minimized = false;
    bool maximized = false;
    bool closable = true;
    bool can_minimize = true;
    bool can_maximize = true;
    bool accept_focus = true;
    bool modal = false;

    bool capture_keys = false;
    bool capture_mouse = false;
    bool mouse_passthrough = false;

    WindowOptions();
    void set_title(const char *s);
    void set_class_name(const char *s);
};

/* Binary request written to /tmp/wm/in/<pid>.req */
struct Request {
    uint32_t magic;
    uint32_t op;
    int32_t window_id;
    int32_t pid;
    WindowOptions opts;
    int32_t x, y, w, h;
    uint32_t surface_token;
    char name[64];
    uint8_t show; /* SHOW visible flag */
    uint8_t _pad[3];
};

/* Binary response at /tmp/wm/out/<pid>.rsp */
struct Response {
    uint32_t magic;
    int32_t status;
    int32_t window_id;
    WindowOptions opts;
};

struct Input {
    int32_t mouse_x = 0;
    int32_t mouse_y = 0;
    uint8_t buttons = 0;
    uint8_t mods = 0;
    int32_t focus_id = -1;
    int32_t hit_id = -1;
    int32_t wheel = 0;
    uint32_t seq = 0;
    uint8_t keys[32]{};
    int32_t drag_id = -1;

    bool hits(int window_id) const
    {
        return window_id >= 0 && hit_id == window_id;
    }
    bool focused(int window_id) const
    {
        return window_id >= 0 && focus_id == window_id;
    }
    bool key_down(int vk) const
    {
        if (vk < 0 || vk > 255)
            return false;
        return (keys[vk >> 3] & (uint8_t)(1u << (vk & 7))) != 0;
    }
};

/* SYS_INPUT_STATE snapshot (dx-free mouse/kbd when compositor not in kernel). */
bool input_snapshot(Input &out);

class Connection {
public:
    Connection() = default;

    bool ready() const { return ready_; }
    int pid() const { return pid_; }

    bool connect();
    int transact(Request &req, Response &rsp);

private:
    int pid_ = -1;
    bool ready_ = false;
};

class Window {
public:
    Window() = default;
    ~Window() = default;

    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;

    bool create(const WindowOptions &opts);
    bool destroy();
    bool close();
    bool set_options(const WindowOptions &opts);
    bool get_options(WindowOptions &out) const;
    bool show(bool visible = true);
    bool hide() { return show(false); }
    bool focus();
    bool move(int32_t x, int32_t y);
    bool resize(int32_t w, int32_t h);
    bool damage();
    bool damage(int32_t x, int32_t y, int32_t w, int32_t h);
    /* Export Reed RT color texture and attach to this window. */
    bool map_surface(reed::Device &dev, reed::RenderTarget &rt);
    bool attach_surface(uint32_t export_token);

    int id() const { return id_; }
    bool ok() const { return id_ >= 0; }
    uint32_t surface_token() const { return surface_token_; }

    static int find(const char *title);
    static int find_class(const char *class_name);

private:
    int id_ = -1;
    uint32_t surface_token_ = 0;
    mutable Connection conn_;
};

} /* namespace wm */
