#include <user/sdk/wm.hpp>
#include <user/sdk/kilim.hpp>
#include <user/sdk/reed.hpp>
#include <user/sdk/fs.hpp>
#include <user/sdk/syscall.hpp>
#include <kernel/syscall.h>
#include <kernel/string.h>
#include <kernel/vfs.h>

/* Files — real directory browser via getdents/chdir. */

namespace {

[[noreturn]] void hang(void)
{
    for (;;)
        hsrc::sdk::syscall0(SYS_YIELD);
}

constexpr int kMaxEnt = 96;
constexpr int kSideN = 5;

struct Entry {
    char name[64];
    uint32_t type;
};

static const char *kQuick[] = {"/", "/system", "/system/bin", "/applications", "/tmp"};
static char g_cwd[VFS_PATH_MAX];
static Entry g_ents[kMaxEnt];
static int g_nent;
static int g_sel;
static int g_side;
static int g_dirty = 1;
static uint8_t g_prev_btn;

static void path_copy(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    if (!dst || !src || n == 0)
        return;
    while (src[i] && i + 1 < n) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void path_join(char *dst, size_t n, const char *base, const char *name)
{
    size_t i = 0;
    if (!dst || !base || !name || n == 0)
        return;
    while (base[i] && i + 1 < n) {
        dst[i] = base[i];
        i++;
    }
    if (i > 0 && dst[i - 1] != '/' && i + 1 < n)
        dst[i++] = '/';
    size_t j = 0;
    while (name[j] && i + 1 < n) {
        dst[i++] = name[j++];
    }
    dst[i] = '\0';
}

static int reload_dir(void)
{
    vfs_dirent_t buf[32];
    int fd;
    g_nent = 0;
    g_sel = 0;
    fd = (int)hsrc::sdk::open(g_cwd, O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        path_copy(g_cwd, "/", sizeof(g_cwd));
        fd = (int)hsrc::sdk::open(g_cwd, O_RDONLY | O_DIRECTORY);
        if (fd < 0)
            return -1;
    }
    for (;;) {
        long n = hsrc::sdk::getdents(fd, buf, 32);
        if (n <= 0)
            break;
        for (long i = 0; i < n && g_nent < kMaxEnt; i++) {
            if (buf[i].name[0] == '.' && buf[i].name[1] == '\0')
                continue;
            path_copy(g_ents[g_nent].name, buf[i].name, sizeof(g_ents[0].name));
            g_ents[g_nent].type = buf[i].type;
            g_nent++;
        }
    }
    hsrc::sdk::close(fd);
    g_dirty = 1;
    return 0;
}

static void go_parent(void)
{
    char *slash = nullptr;
    for (char *p = g_cwd; *p; p++)
        if (*p == '/')
            slash = p;
    if (!slash || slash == g_cwd) {
        path_copy(g_cwd, "/", sizeof(g_cwd));
    } else {
        *slash = '\0';
        if (g_cwd[0] == '\0')
            path_copy(g_cwd, "/", sizeof(g_cwd));
    }
    (void)hsrc::sdk::chdir(g_cwd);
    (void)reload_dir();
}

static void open_selected(void)
{
    char next[VFS_PATH_MAX];
    if (g_sel < 0 || g_sel >= g_nent)
        return;
    if (g_ents[g_sel].name[0] == '.' && g_ents[g_sel].name[1] == '.' &&
        g_ents[g_sel].name[2] == '\0') {
        go_parent();
        return;
    }
    path_join(next, sizeof(next), g_cwd, g_ents[g_sel].name);
    if (S_ISDIR(g_ents[g_sel].type)) {
        path_copy(g_cwd, next, sizeof(g_cwd));
        (void)hsrc::sdk::chdir(g_cwd);
        (void)reload_dir();
    }
}

} // namespace

extern "C" void exec_main(void)
{
    static reed::Device dev;
    static kilim::Context k;

    if (dev.init() < 0 || k.init(&dev) < 0)
        hang();

    path_copy(g_cwd, "/", sizeof(g_cwd));
    (void)hsrc::sdk::getcwd(g_cwd, sizeof(g_cwd));
    if (g_cwd[0] == '\0')
        path_copy(g_cwd, "/", sizeof(g_cwd));
    (void)reload_dir();

    wm::WindowOptions opts;
    opts.x = 120;
    opts.y = 70;
    opts.w = 960;
    opts.h = 600;
    opts.min_w = 640;
    opts.min_h = 400;
    opts.set_title("Files");
    opts.set_class_name("files");

    wm::Window win;
    if (!win.create(opts))
        hang();

    bool mapped = false;
    for (;;) {
        wm::Input in;
        (void)wm::input_snapshot(in);
        k.set_pointer(in.mouse_x, in.mouse_y, in.buttons);

        uint8_t pressed = (uint8_t)(in.buttons & ~g_prev_btn);
        g_prev_btn = in.buttons;

        int top = wm::kChromeTitleH;
        int lx = in.mouse_x - opts.x;
        int ly = in.mouse_y - opts.y;

        if (pressed & 1) {
            if (lx >= 8 && lx < 200 && ly >= top + 56) {
                int r = (ly - (top + 56)) / 36;
                if (r >= 0 && r < kSideN) {
                    g_side = r;
                    path_copy(g_cwd, kQuick[r], sizeof(g_cwd));
                    (void)hsrc::sdk::chdir(g_cwd);
                    (void)reload_dir();
                }
            } else if (lx >= 220 && ly >= top + 56) {
                int r = (ly - (top + 56)) / 32;
                if (r >= 0 && r < g_nent) {
                    if (r == g_sel)
                        open_selected();
                    else {
                        g_sel = r;
                        g_dirty = 1;
                    }
                }
            } else if (lx >= 220 && lx < 300 && ly >= top + 12 && ly < top + 40) {
                go_parent();
            }
        }

        if (k.begin_frame() < 0) {
            hsrc::sdk::sleep_ticks(1);
            continue;
        }

        int w = opts.w;
        int h = opts.h;
        k.fill_rect(0, 0, w, h, kilim::rgba(32, 34, 40, 255));
        k.fill_rect(0, top, 200, h - top, kilim::rgba(28, 30, 36, 255));
        k.fill_rect(200, top, w - 200, h - top, kilim::rgba(36, 38, 46, 255));

        k.fill_round_rect(220, top + 10, 70, 26, 6, kilim::rgba(0, 120, 212, 255));
        k.text("Up", 242, top + 15, 13, kilim::rgba(255, 255, 255, 255));
        k.fill_round_rect(300, top + 10, w - 320, 26, 6, kilim::rgba(45, 48, 58, 255));
        k.text(g_cwd, 312, top + 15, 13, kilim::rgba(200, 206, 218, 255));

        for (int i = 0; i < kSideN; i++) {
            int y = top + 56 + i * 36;
            if (i == g_side)
                k.fill_round_rect(10, y - 6, 180, 32, 8, kilim::rgba(0, 120, 212, 255));
            k.text(kQuick[i], 24, y, 13,
                   i == g_side ? kilim::rgba(255, 255, 255, 255)
                               : kilim::rgba(200, 206, 218, 255));
        }

        for (int i = 0; i < g_nent; i++) {
            int y = top + 56 + i * 32;
            if (y + 28 > h)
                break;
            if (i == g_sel)
                k.fill_round_rect(220, y - 4, w - 240, 28, 6, kilim::rgba(55, 60, 72, 255));
            uint32_t ic = S_ISDIR(g_ents[i].type) ? kilim::rgba(0, 153, 188, 255)
                                                   : kilim::rgba(0, 120, 212, 200);
            k.fill_round_rect(232, y, 20, 20, 5, ic);
            k.text(g_ents[i].name, 264, y + 2, 13, kilim::rgba(230, 235, 245, 255));
        }

        if (g_nent == 0)
            k.text("(empty)", 240, top + 80, 14, kilim::rgba(160, 170, 185, 255));

        (void)k.commit_frame();
        if (!mapped) {
            mapped = win.map_surface(dev, k.target());
            (void)win.damage();
            g_dirty = 0;
        } else if (g_dirty) {
            (void)win.damage();
            g_dirty = 0;
        }
        hsrc::sdk::sleep_ticks(2);
    }
}
