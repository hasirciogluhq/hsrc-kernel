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
constexpr size_t kDeepLinkBytes = 128;

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

} // namespace hsrc::sdk::settings
