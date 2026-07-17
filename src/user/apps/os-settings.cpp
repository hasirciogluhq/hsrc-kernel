#include <user/mke.h>
#include <kernel/syscall.h>
#include <user/sdk/fs.hpp>
#include <user/sdk/gfx.hpp>
#include <user/sdk/settings.hpp>
#include <user/sdk/syscall.hpp>
#include <user/sdk/time.hpp>
#include <user/string.h>

/*
 * System Settings — gfx API, terminal-clean layout.
 * Desktop & Dock: pin/unpin apps for the dynamic shell dock.
 */

namespace {

using hsrc::sdk::ChromeHit;
using hsrc::sdk::Input;
using hsrc::sdk::ScreenInfo;
using hsrc::sdk::Surface;
using hsrc::sdk::Window;
using hsrc::sdk::WindowOptions;
using hsrc::sdk::kChromeTitleH;
using hsrc::sdk::settings::theme;
using hsrc::sdk::settings::refresh_theme;

constexpr int kWinW = 820;
constexpr int kWinH = 500;
constexpr int kSidebarW = 200;
constexpr int kPad = 14;
constexpr int kRowH = 28;
constexpr int kRowGap = 6;
constexpr int kContentX = kSidebarW + kPad;
constexpr int kContentW = kWinW - kContentX - kPad;
constexpr int kThemePollEvery = 96;

constexpr const char *kIniDir = "/etc";
constexpr const char *kIniPath = "/etc/os-settings.ini";
constexpr const char *kDeepLinkPath = "/run/settings.deeplink";
constexpr size_t kDeepLinkBytes = 128;
constexpr size_t kIniBytes = 2048;

enum CategoryIndex {
    CAT_GENERAL = 0,
    CAT_APPEARANCE,
    CAT_DESKTOP_DOCK,
    CAT_DATE_TIME,
    CAT_DISPLAY,
    CAT_ABOUT,
    CAT_COUNT
};

enum TargetKind {
    TARGET_SETTING = 0,
};

struct Category {
    const char *id;
    const char *label;
};

struct CycleSetting {
    const char *key;
    int category;
    const char *label;
    const char *const *choices;
    int choice_count;
    int current;
};

struct ClickTarget {
    int y;
    int h;
    int kind;
    int index;
};

constexpr Category kCategories[CAT_COUNT] = {
    { "general", "General" },
    { "appearance", "Appearance" },
    { "desktop-dock", "Desktop & Dock" },
    { "date-time", "Date & Time" },
    { "display", "Display" },
    { "about", "About" },
};

constexpr const char *kAppearanceChoices[] = { "Light", "Dark", "Auto" };
constexpr const char *kAccentChoices[] = { "Blue", "Graphite", "Forest" };
constexpr const char *kMotionChoices[] = { "Fast", "Reduced" };
constexpr const char *kOnOffChoices[] = { "On", "Off" };
constexpr const char *kDockChoices[] = { "Small", "Medium", "Large" };
constexpr const char *kWallpaperChoices[] = { "Cover", "Center", "Stretch" };
constexpr const char *kClockChoices[] = { "24-hour", "12-hour" };
constexpr const char *kTimezoneChoices[] = {
    "UTC-8", "UTC-5", "UTC", "UTC+1", "UTC+3", "UTC+8", "UTC+9"
};

CycleSetting g_settings[] = {
    { "general.appearance", CAT_APPEARANCE, "Appearance", kAppearanceChoices, 3, 0 },
    { "general.accent", CAT_APPEARANCE, "Accent Color", kAccentChoices, 3, 0 },
    { "general.motion", CAT_APPEARANCE, "Animation Speed", kMotionChoices, 2, 0 },
    { "desktop.dock-size", CAT_DESKTOP_DOCK, "Dock Size", kDockChoices, 3, 1 },
    { "desktop.wallpaper", CAT_DESKTOP_DOCK, "Wallpaper Fit", kWallpaperChoices, 3, 0 },
    { "dock.pin.monitor", CAT_DESKTOP_DOCK, "Pin Activity Monitor", kOnOffChoices, 2, 0 },
    { "dock.pin.terminal", CAT_DESKTOP_DOCK, "Pin Terminal", kOnOffChoices, 2, 0 },
    { "dock.pin.files", CAT_DESKTOP_DOCK, "Pin Files", kOnOffChoices, 2, 0 },
    { "dock.pin.settings", CAT_DESKTOP_DOCK, "Pin System Settings", kOnOffChoices, 2, 0 },
    { "datetime.clock", CAT_DATE_TIME, "Clock Format", kClockChoices, 2, 0 },
    { "datetime.timezone", CAT_DATE_TIME, "Timezone", kTimezoneChoices, 7, 4 },
};

constexpr int kSettingCount = (int)(sizeof(g_settings) / sizeof(g_settings[0]));

Window g_win;
WindowOptions g_win_opts;
ScreenInfo g_screen{};
Input g_prev_input{};
bool g_dirty = true;
int g_theme_poll = 0;
int g_clock_poll = 0;
char g_last_live_clock[32] = "";
int g_active_category = CAT_GENERAL;
int g_hover_sidebar = -1;
int g_hover_setting = -1;
int g_deeplink_cooldown = 0;
ClickTarget g_targets[32];
int g_target_count = 0;
bool g_was_minimized = false;

void append_text(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0 || !src)
        return;
    size_t len = strlen(dst);
    if (len >= dst_size)
        return;
    strncpy(dst + len, src, dst_size - len - 1);
    dst[dst_size - 1] = 0;
}

void append_uint(char *dst, size_t dst_size, uint32_t value)
{
    char tmp[16];
    int i = 0;
    if (value == 0) {
        append_text(dst, dst_size, "0");
        return;
    }
    while (value > 0 && i < (int)sizeof(tmp)) {
        tmp[i++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (i-- > 0) {
        char ch[2] = { tmp[i], 0 };
        append_text(dst, dst_size, ch);
    }
}

int text_width(const char *s)
{
    return Surface::text_width(s, 1);
}

void register_target(int y, int h, int kind, int index)
{
    if (g_target_count >= (int)(sizeof(g_targets) / sizeof(g_targets[0])))
        return;
    g_targets[g_target_count].y = y;
    g_targets[g_target_count].h = h;
    g_targets[g_target_count].kind = kind;
    g_targets[g_target_count].index = index;
    g_target_count++;
}

void unlink_deeplink()
{
    (void)hsrc::sdk::syscall1(SYS_UNLINK, (long)kDeepLinkPath);
}

bool refresh_window_options()
{
    if (!g_win.get_options(g_win_opts))
        return false;
    if (g_was_minimized && !g_win_opts.minimized && g_win_opts.visible)
        g_dirty = true;
    g_was_minimized = g_win_opts.minimized;
    return true;
}

void apply_time_settings()
{
    (void)hsrc::sdk::time::init();
    for (int i = 0; i < kSettingCount; i++) {
        const char *value = g_settings[i].choices[g_settings[i].current];
        if (strcmp(g_settings[i].key, "datetime.timezone") == 0) {
            int off = 0;
            if (hsrc::sdk::time::parse_timezone_label(value, &off))
                (void)hsrc::sdk::time::set_timezone(off, value);
        } else if (strcmp(g_settings[i].key, "datetime.clock") == 0) {
            (void)hsrc::sdk::time::set_hour12(strcmp(value, "12-hour") == 0);
        }
    }
}

void save_settings()
{
    (void)hsrc::sdk::mkdir(kIniDir, 0755);
    int fd = (int)hsrc::sdk::open(kIniPath, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0)
        return;

    for (int i = 0; i < kSettingCount; i++) {
        const char *value = g_settings[i].choices[g_settings[i].current];
        (void)hsrc::sdk::write(fd, g_settings[i].key, strlen(g_settings[i].key));
        (void)hsrc::sdk::write(fd, "=", 1);
        (void)hsrc::sdk::write(fd, value, strlen(value));
        (void)hsrc::sdk::write(fd, "\n", 1);
    }
    (void)hsrc::sdk::close(fd);
    apply_time_settings();
}

void load_settings()
{
    int fd = (int)hsrc::sdk::open(kIniPath, O_RDONLY);
    if (fd < 0)
        return;

    char buf[kIniBytes];
    memset(buf, 0, sizeof(buf));
    long nread = hsrc::sdk::read(fd, buf, sizeof(buf) - 1);
    (void)hsrc::sdk::close(fd);
    if (nread <= 0)
        return;

    int start = 0;
    for (long i = 0; i <= nread; i++) {
        if (buf[i] != '\n' && buf[i] != '\r' && buf[i] != 0)
            continue;
        buf[i] = 0;
        char *line = buf + start;
        start = (int)i + 1;
        if (!line[0])
            continue;

        char *eq = line;
        while (*eq && *eq != '=')
            eq++;
        if (*eq != '=')
            continue;
        *eq++ = 0;

        for (int s = 0; s < kSettingCount; s++) {
            if (strcmp(line, g_settings[s].key) != 0)
                continue;
            for (int c = 0; c < g_settings[s].choice_count; c++) {
                if (strcmp(eq, g_settings[s].choices[c]) == 0) {
                    g_settings[s].current = c;
                    break;
                }
            }
            break;
        }
    }
}

int category_from_id(const char *id)
{
    if (!id || !id[0])
        return CAT_GENERAL;
    if (strcmp(id, "system-information") == 0 || strcmp(id, "about") == 0)
        return CAT_ABOUT;
    for (int i = 0; i < CAT_COUNT; i++) {
        if (strcmp(id, kCategories[i].id) == 0)
            return i;
    }
    return CAT_GENERAL;
}

void poll_deeplink()
{
    if (g_deeplink_cooldown > 0) {
        g_deeplink_cooldown--;
        return;
    }

    int fd = (int)hsrc::sdk::open(kDeepLinkPath, O_RDONLY);
    if (fd < 0) {
        g_deeplink_cooldown = 8;
        return;
    }

    char buf[kDeepLinkBytes];
    memset(buf, 0, sizeof(buf));
    long nread = hsrc::sdk::read(fd, buf, sizeof(buf) - 1);
    (void)hsrc::sdk::close(fd);
    unlink_deeplink();
    g_deeplink_cooldown = 4;

    if (nread <= 0 || buf[0] == 0)
        return;

    const char *id = buf;
    if (strncmp(buf, "settings://", 11) == 0)
        id = buf + 11;
    while (*id == '/' || *id == ' ')
        id++;

    char clean[64];
    size_t n = 0;
    while (id[n] && id[n] != '\n' && id[n] != '\r' && id[n] != ' ' && n + 1 < sizeof(clean)) {
        clean[n] = id[n];
        n++;
    }
    clean[n] = 0;

    g_active_category = category_from_id(clean);
    g_hover_setting = -1;
    g_dirty = true;
}

int sidebar_hit(int lx, int ly)
{
    const int top = kChromeTitleH + 40;
    if (lx < 8 || lx >= kSidebarW - 8 || ly < top)
        return -1;
    for (int i = 0; i < CAT_COUNT; i++) {
        int y = top + i * 30;
        if (ly >= y && ly < y + 28)
            return i;
    }
    return -1;
}

const ClickTarget *target_hit(int lx, int ly)
{
    if (lx < kContentX || lx >= kContentX + kContentW)
        return nullptr;
    for (int i = 0; i < g_target_count; i++) {
        if (ly >= g_targets[i].y && ly < g_targets[i].y + g_targets[i].h)
            return &g_targets[i];
    }
    return nullptr;
}

void cycle_setting(int idx)
{
    if (idx < 0 || idx >= kSettingCount)
        return;
    g_settings[idx].current++;
    if (g_settings[idx].current >= g_settings[idx].choice_count)
        g_settings[idx].current = 0;
    save_settings();
    (void)refresh_theme();
    g_dirty = true;
}

void draw_row(Surface &s, int y, const char *label, const char *value, bool interactive, bool hover)
{
    const auto &t = theme();
    if (hover && interactive)
        s.fill(kContentX, y, kContentW, kRowH, t.hover);
    s.text(kContentX + 4, y + 8, label, t.text, 1);
    int value_x = kContentX + kContentW - 4 - text_width(value);
    if (value_x < kContentX + 200)
        value_x = kContentX + 200;
    s.text(value_x, y + 8, value, interactive ? t.accent : t.text_dim, 1);
    s.fill(kContentX, y + kRowH - 1, kContentW, 1, t.border);
}

void draw_sidebar(Surface &s)
{
    const auto &t = theme();
    s.fill(0, kChromeTitleH, kSidebarW, kWinH - kChromeTitleH, t.sidebar);
    s.fill(kSidebarW - 1, kChromeTitleH, 1, kWinH - kChromeTitleH, t.border);
    s.text(kPad, kChromeTitleH + 12, "Settings", t.text, 1);
    s.text(kPad, kChromeTitleH + 28, "preferences", t.text_dim, 1);

    const int top = kChromeTitleH + 40;
    for (int i = 0; i < CAT_COUNT; i++) {
        int y = top + i * 30;
        bool selected = (i == g_active_category);
        bool hover = (i == g_hover_sidebar);
        if (selected)
            s.fill(8, y, kSidebarW - 16, 28, t.accent_soft);
        else if (hover)
            s.fill(8, y, kSidebarW - 16, 28, t.hover);
        s.text(16, y + 8, kCategories[i].label, selected ? t.accent : t.text, 1);
    }
}

void draw_settings_for_category(Surface &s, int cat, const char *hint)
{
    const auto &t = theme();
    g_target_count = 0;

    s.text(kContentX, kChromeTitleH + 12, kCategories[cat].label, t.text, 1);
    s.text(kContentX, kChromeTitleH + 30, hint, t.text_dim, 1);

    int y = kChromeTitleH + 56;
    for (int i = 0; i < kSettingCount; i++) {
        if (g_settings[i].category != cat)
            continue;
        bool hover = (i == g_hover_setting);
        draw_row(s, y, g_settings[i].label,
                 g_settings[i].choices[g_settings[i].current], true, hover);
        register_target(y, kRowH, TARGET_SETTING, i);
        y += kRowH + kRowGap;
    }
}

void draw_display_page(Surface &s)
{
    const auto &t = theme();
    g_target_count = 0;

    char resolution[40];
    char depth[24];
    resolution[0] = 0;
    append_uint(resolution, sizeof(resolution), g_screen.width);
    append_text(resolution, sizeof(resolution), " x ");
    append_uint(resolution, sizeof(resolution), g_screen.height);
    depth[0] = 0;
    append_uint(depth, sizeof(depth), g_screen.bpp);
    append_text(depth, sizeof(depth), "-bit");

    s.text(kContentX, kChromeTitleH + 12, "Display", t.text, 1);
    s.text(kContentX, kChromeTitleH + 30, "Live screen_info() readout.", t.text_dim, 1);

    int y = kChromeTitleH + 56;
    draw_row(s, y, "Resolution", resolution, false, false);
    y += kRowH + kRowGap;
    draw_row(s, y, "Color Depth", depth, false, false);
    y += kRowH + kRowGap;
    draw_row(s, y, "Window Server", "MKDX", false, false);
}

void draw_datetime_page(Surface &s)
{
    const auto &t = theme();
    g_target_count = 0;

    char live[48];
    char iso[40];
    hsrc::sdk::time::format_clock(live, sizeof(live));
    hsrc::sdk::time::format_iso_local(iso, sizeof(iso));

    s.text(kContentX, kChromeTitleH + 12, "Date & Time", t.text, 1);
    s.text(kContentX, kChromeTitleH + 30,
           "Timezone and clock format drive the menubar clock.", t.text_dim, 1);

    int y = kChromeTitleH + 56;
    draw_row(s, y, "Now (local)", live, false, false);
    y += kRowH + kRowGap;
    draw_row(s, y, "ISO local", iso, false, false);
    y += kRowH + kRowGap;

    for (int i = 0; i < kSettingCount; i++) {
        if (g_settings[i].category != CAT_DATE_TIME)
            continue;
        bool hover = (g_hover_setting == i);
        draw_row(s, y, g_settings[i].label, g_settings[i].choices[g_settings[i].current],
                 true, hover);
        register_target(y, kRowH, TARGET_SETTING, i);
        y += kRowH + kRowGap;
    }
}

void draw_about_page(Surface &s)
{
    const auto &t = theme();
    g_target_count = 0;

    char resolution[40];
    resolution[0] = 0;
    append_uint(resolution, sizeof(resolution), g_screen.width);
    append_text(resolution, sizeof(resolution), " x ");
    append_uint(resolution, sizeof(resolution), g_screen.height);

    s.text(kContentX, kChromeTitleH + 12, "About", t.text, 1);
    s.text(kContentX, kChromeTitleH + 30, "HSRC OS control center.", t.text_dim, 1);

    int y = kChromeTitleH + 56;
    draw_row(s, y, "Product", "HSRC OS", false, false);
    y += kRowH + kRowGap;
    draw_row(s, y, "Settings", "os-settings.mke", false, false);
    y += kRowH + kRowGap;
    draw_row(s, y, "Display", resolution, false, false);
    y += kRowH + kRowGap;
    draw_row(s, y, "Dock", "pinned | running unpinned", false, false);
}

void draw_general_page(Surface &s)
{
    const auto &t = theme();
    g_target_count = 0;
    s.text(kContentX, kChromeTitleH + 12, "General", t.text, 1);
    s.text(kContentX, kChromeTitleH + 30, "Shell, dock and appearance live under their tabs.", t.text_dim, 1);

    int y = kChromeTitleH + 56;
    draw_row(s, y, "Ini path", "/etc/os-settings.ini", false, false);
    y += kRowH + kRowGap;
    draw_row(s, y, "Dock layout", "pinned | live unpinned", false, false);
    y += kRowH + kRowGap;
    draw_row(s, y, "Tip", "Use Desktop & Dock to pin apps", false, false);
}

void paint()
{
    if (!g_win.ok())
        return;

    const auto &t = theme();
    Surface &s = g_win.surface();
    s.clear(t.bg);
    s.draw_window_chrome(kWinW, g_win_opts.title, g_win_opts, t.chrome, t.text, t.border);
    draw_sidebar(s);

    switch (g_active_category) {
    case CAT_GENERAL:
        draw_general_page(s);
        break;
    case CAT_APPEARANCE:
        draw_settings_for_category(s, CAT_APPEARANCE, "Click a row to cycle. Persists to ini.");
        break;
    case CAT_DESKTOP_DOCK:
        draw_settings_for_category(s, CAT_DESKTOP_DOCK,
                                   "Pinned stay centered; unpinned open apps sit right of |");
        break;
    case CAT_DATE_TIME:
        draw_datetime_page(s);
        break;
    case CAT_DISPLAY:
        draw_display_page(s);
        break;
    case CAT_ABOUT:
        draw_about_page(s);
        break;
    default:
        draw_general_page(s);
        break;
    }

    g_win.damage();
    g_dirty = false;
}

void update_hover(int lx, int ly)
{
    int next_sidebar = sidebar_hit(lx, ly);
    int next_setting = -1;
    const ClickTarget *hit = target_hit(lx, ly);
    if (hit && hit->kind == TARGET_SETTING)
        next_setting = hit->index;

    if (next_sidebar != g_hover_sidebar || next_setting != g_hover_setting) {
        g_hover_sidebar = next_sidebar;
        g_hover_setting = next_setting;
        g_dirty = true;
    }
}

void handle_click(const Input &in)
{
    if (!refresh_window_options())
        return;
    if (g_win_opts.minimized || !g_win_opts.visible)
        return;

    int lx = in.mouse_x - g_win_opts.x;
    int ly = in.mouse_y - g_win_opts.y;
    if (lx < 0 || ly < 0 || lx >= g_win_opts.w || ly >= g_win_opts.h)
        return;

    ChromeHit chrome = g_win.hit_chrome(lx, ly, g_win_opts);
    if (chrome != ChromeHit::None) {
        (void)g_win.handle_chrome_hit(chrome);
        (void)refresh_window_options();
        g_dirty = true;
        return;
    }

    if (lx >= kContentX) {
        const ClickTarget *hit = target_hit(lx, ly);
        if (hit && hit->kind == TARGET_SETTING) {
            cycle_setting(hit->index);
            return;
        }
    }

    int cat = sidebar_hit(lx, ly);
    if (cat >= 0 && g_active_category != cat) {
        g_active_category = cat;
        g_hover_setting = -1;
        g_dirty = true;
    }
}

} // namespace

extern "C" void mke_main(void)
{
    g_dirty = true;

    if (!hsrc::sdk::screen_info(g_screen) || g_screen.width == 0 || g_screen.height == 0) {
        for (;;)
            hsrc::sdk::yield();
    }

    load_settings();
    (void)refresh_theme();
    apply_time_settings();

    WindowOptions opts;
    opts.x = (int)g_screen.width > kWinW ? ((int)g_screen.width - kWinW) / 2 : 40;
    opts.y = (int)g_screen.height > kWinH ? ((int)g_screen.height - kWinH) / 2 : 40;
    opts.w = kWinW;
    opts.h = kWinH;
    opts.radius = 10;
    opts.rounded = true;
    opts.shadow = true;
    opts.resizable = false;
    opts.framed = true;
    opts.closable = true;
    opts.can_minimize = true;
    opts.can_maximize = true;
    opts.accept_focus = true;
    opts.set_title("System Settings");
    opts.set_class_name("os.settings");

    if (!g_win.create(opts)) {
        for (;;)
            hsrc::sdk::yield();
    }
    (void)refresh_window_options();

    g_win.show(true);
    g_win.focus();
    poll_deeplink();
    paint();
    (void)hsrc::sdk::present();

    for (;;) {
        if (!g_win.ok()) {
            (void)g_win.close();
            hsrc::sdk::exit(0);
        }

        poll_deeplink();

        g_theme_poll++;
        if (g_theme_poll >= kThemePollEvery) {
            g_theme_poll = 0;
            if (refresh_theme())
                g_dirty = true;
        }

        /* Live clock on Date & Time page — shared time page, no gettime spam. */
        if (g_active_category == CAT_DATE_TIME) {
            if (++g_clock_poll >= 8) {
                g_clock_poll = 0;
                char live[32];
                hsrc::sdk::time::format_clock(live, sizeof(live));
                if (strcmp(live, g_last_live_clock) != 0) {
                    strncpy(g_last_live_clock, live, sizeof(g_last_live_clock) - 1);
                    g_last_live_clock[sizeof(g_last_live_clock) - 1] = 0;
                    g_dirty = true;
                }
            }
        }

        (void)refresh_window_options();

        Input in{};
        if (hsrc::sdk::input(in)) {
            const bool interactive = !g_win_opts.minimized && g_win_opts.visible;
            if (interactive) {
                int lx = in.mouse_x - g_win_opts.x;
                int ly = in.mouse_y - g_win_opts.y;
                if (lx >= 0 && ly >= 0 && lx < g_win_opts.w && ly < g_win_opts.h)
                    update_hover(lx, ly);
            }

            uint8_t pressed = (uint8_t)(in.buttons & ~g_prev_input.buttons);
            if (pressed & UGX_BTN_LEFT) {
                int click_lx = in.mouse_x - g_win_opts.x;
                int click_ly = in.mouse_y - g_win_opts.y;
                bool over = interactive && click_lx >= 0 && click_ly >= 0 &&
                            click_lx < g_win_opts.w && click_ly < g_win_opts.h;
                if (over || (interactive && in.focus_id == g_win.id()))
                    handle_click(in);
            }
            g_prev_input = in;
        }

        if (g_dirty && !g_win_opts.minimized) {
            paint();
            (void)hsrc::sdk::present();
        }
        hsrc::sdk::yield();
    }
}
