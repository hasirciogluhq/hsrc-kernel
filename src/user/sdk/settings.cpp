#include <user/sdk/settings.hpp>

#include <kernel/vfs.h>
#include <user/sdk/fs.hpp>
#include <user/sdk/gfx.hpp>
#include <user/sdk/process.hpp>
#include <user/sdk/syscall.hpp>
#include <user/string.h>

namespace {

constexpr const char *kSettingsTitle = "System Settings";
constexpr const char *kSettingsClass = "os-settings";
constexpr const char *kSettingsPath = "/applications/os-settings.mke";
constexpr const char *kRunDir = "/run";
constexpr const char *kDeepLinkPath = "/run/settings.deeplink";
constexpr const char *kDefaultDeepLink = "settings://general";
constexpr const char *kIniPath = "/etc/os-settings.ini";
constexpr const char *kAppearanceKey = "general.appearance";
constexpr size_t kDeepLinkBytes = 128;
constexpr size_t kIniBytes = 2048;

using hsrc::sdk::Color;
using hsrc::sdk::rgb;
using hsrc::sdk::rgba;
using hsrc::sdk::settings::Appearance;
using hsrc::sdk::settings::AppTheme;
using hsrc::sdk::settings::ThemeMode;

Appearance g_appearance = Appearance::Light;
ThemeMode g_theme_mode = ThemeMode::Light;
bool g_theme_loaded = false;

constexpr AppTheme kLightTheme = {
    rgb(255, 255, 255),          /* bg */
    rgb(247, 247, 245),          /* sidebar */
    rgb(250, 250, 248),          /* chrome */
    rgba(55, 53, 47, 22),        /* border */
    rgb(50, 48, 44),             /* text */
    rgb(115, 114, 110),          /* text_dim */
    rgba(71, 70, 68, 153),       /* text_soft */
    rgb(35, 131, 226),           /* accent */
    rgba(35, 131, 226, 28),      /* accent_soft */
    rgb(242, 241, 238),          /* hover */
    rgb(247, 247, 245),          /* card */
    rgba(242, 241, 238, 153),    /* button */
    rgba(242, 241, 238, 153),    /* inset */
    rgb(176, 96, 32),            /* warn */
    rgb(196, 66, 66),            /* danger */
    rgba(196, 66, 66, 30),       /* danger_soft */
    rgb(255, 255, 255),          /* panel */
    rgb(250, 250, 248),          /* term_bg */
    rgb(40, 40, 48),             /* term_fg */
    rgb(120, 120, 130),          /* term_dim */
    rgb(30, 140, 80),            /* term_accent */
    rgb(180, 60, 60),            /* term_err */
};

constexpr AppTheme kDarkTheme = {
    rgb(28, 28, 30),             /* bg */
    rgb(36, 36, 38),             /* sidebar */
    rgb(40, 40, 44),             /* chrome */
    rgba(255, 255, 255, 28),     /* border */
    rgb(235, 235, 240),          /* text */
    rgb(160, 160, 168),          /* text_dim */
    rgba(200, 200, 210, 153),    /* text_soft */
    rgb(70, 150, 235),           /* accent */
    rgba(70, 150, 235, 40),      /* accent_soft */
    rgb(48, 48, 52),             /* hover */
    rgb(36, 36, 40),             /* card */
    rgba(60, 60, 66, 180),       /* button */
    rgba(20, 20, 24, 180),       /* inset */
    rgb(220, 160, 80),           /* warn */
    rgb(220, 90, 90),            /* danger */
    rgba(220, 90, 90, 40),       /* danger_soft */
    rgb(32, 32, 36),             /* panel */
    rgb(18, 18, 22),             /* term_bg */
    rgb(220, 220, 225),          /* term_fg */
    rgb(120, 120, 130),          /* term_dim */
    rgb(90, 200, 120),           /* term_accent */
    rgb(240, 110, 110),          /* term_err */
};

ThemeMode resolve_mode(Appearance appearance)
{
    if (appearance == Appearance::Dark)
        return ThemeMode::Dark;
    /* Auto has no clock/sensor yet — fall back to Light. */
    return ThemeMode::Light;
}

Appearance parse_appearance_value(const char *value)
{
    if (!value || !value[0])
        return Appearance::Light;
    if (strcmp(value, "Dark") == 0)
        return Appearance::Dark;
    if (strcmp(value, "Auto") == 0)
        return Appearance::Auto;
    return Appearance::Light;
}

Appearance read_appearance_from_ini()
{
    int fd = (int)hsrc::sdk::open(kIniPath, O_RDONLY);
    if (fd < 0)
        return Appearance::Light;

    char buf[kIniBytes];
    memset(buf, 0, sizeof(buf));
    long nread = hsrc::sdk::read(fd, buf, sizeof(buf) - 1);
    (void)hsrc::sdk::close(fd);
    if (nread <= 0)
        return Appearance::Light;

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
        if (strcmp(line, kAppearanceKey) != 0)
            continue;
        return parse_appearance_value(eq);
    }
    return Appearance::Light;
}

void ensure_theme_loaded()
{
    if (g_theme_loaded)
        return;
    g_appearance = read_appearance_from_ini();
    g_theme_mode = resolve_mode(g_appearance);
    g_theme_loaded = true;
}

void copy_text(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0)
        return;
    dst[0] = 0;
    if (!src)
        return;
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = 0;
}

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

bool write_deeplink_file(const char *uri)
{
    (void)hsrc::sdk::mkdir(kRunDir, 0755);

    int fd = (int)hsrc::sdk::open(kDeepLinkPath, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0)
        return false;

    char buf[kDeepLinkBytes];
    memset(buf, 0, sizeof(buf));
    copy_text(buf, sizeof(buf), uri && uri[0] ? uri : kDefaultDeepLink);
    size_t len = strlen(buf);
    if (len == 0) {
        copy_text(buf, sizeof(buf), kDefaultDeepLink);
        len = strlen(buf);
    }

    long wrote = hsrc::sdk::write(fd, buf, len);
    (void)hsrc::sdk::close(fd);
    return wrote == (long)len;
}

void reveal_settings_window(long wid)
{
    if (wid < 0)
        return;

    hsrc::sdk::WindowOptions opts;
    if (hsrc::sdk::window_get((int)wid, opts)) {
        opts.visible = true;
        opts.minimized = false;
        (void)hsrc::sdk::window_set((int)wid, opts);
    }

    (void)hsrc::sdk::syscall2(SYS_WM_SHOW, wid, 1);
    (void)hsrc::sdk::syscall1(SYS_WM_FOCUS, wid);
}

long find_settings_window()
{
    long wid = hsrc::sdk::syscall1(SYS_WM_FIND, (long)kSettingsTitle);
    if (wid >= 0)
        return wid;

    /* Fallback: scan by class_name if title was changed. */
    for (int id = 1; id < 32; id++) {
        hsrc::sdk::WindowOptions opts;
        if (!hsrc::sdk::window_get(id, opts))
            continue;
        if (opts.class_name[0] && strcmp(opts.class_name, kSettingsClass) == 0)
            return id;
    }
    return -1;
}

} // namespace

namespace hsrc::sdk::settings {

bool open()
{
    return open_category("general");
}

bool open_category(const char *id)
{
    char uri[kDeepLinkBytes];
    memset(uri, 0, sizeof(uri));
    copy_text(uri, sizeof(uri), "settings://");
    append_text(uri, sizeof(uri), (id && id[0]) ? id : "general");
    return open_deeplink(uri);
}

bool open_deeplink(const char *uri)
{
    const char *target = (uri && uri[0]) ? uri : kDefaultDeepLink;
    if (!write_deeplink_file(target))
        return false;

    long wid = find_settings_window();
    if (wid >= 0) {
        reveal_settings_window(wid);
        return true;
    }

    long pid = hsrc::sdk::process::spawn(kSettingsPath);
    return pid > 0;
}

Appearance appearance()
{
    ensure_theme_loaded();
    return g_appearance;
}

ThemeMode theme_mode()
{
    ensure_theme_loaded();
    return g_theme_mode;
}

const AppTheme &theme()
{
    ensure_theme_loaded();
    return g_theme_mode == ThemeMode::Dark ? kDarkTheme : kLightTheme;
}

bool refresh_theme()
{
    Appearance next_appearance = read_appearance_from_ini();
    ThemeMode next_mode = resolve_mode(next_appearance);
    bool changed = !g_theme_loaded || next_mode != g_theme_mode || next_appearance != g_appearance;
    g_appearance = next_appearance;
    g_theme_mode = next_mode;
    g_theme_loaded = true;
    return changed;
}

} // namespace hsrc::sdk::settings
