#include <user/sdk/wm.hpp>
#include <user/sdk/kilim.hpp>
#include <user/sdk/reed.hpp>
#include <user/sdk/fs.hpp>
#include <user/sdk/process.hpp>
#include <user/sdk/syscall.hpp>
#include <user/input.h>
#include <kernel/syscall.h>
#include <kernel/string.h>
#include <kernel/vfs.h>

/*
 * Terminal — keyboard line editor + built-in cmds + spawn.
 * PS/2 set-1 scancode → ASCII (US layout).
 */

namespace {

constexpr int kRows = 18;
constexpr int kCols = 88;

static char g_lines[kRows][kCols];
static int g_line_n;
static char g_input[kCols];
static int g_input_n;
static uint8_t g_prev_keys[32];
static int g_dirty = 1;
static char g_cwd[VFS_PATH_MAX];

static void str_copy(char *d, const char *s, size_t n)
{
    size_t i = 0;
    while (s[i] && i + 1 < n) {
        d[i] = s[i];
        i++;
    }
    d[i] = '\0';
}

static void push_line(const char *s)
{
    if (g_line_n >= kRows) {
        for (int i = 1; i < kRows; i++)
            str_copy(g_lines[i - 1], g_lines[i], kCols);
        g_line_n = kRows - 1;
    }
    str_copy(g_lines[g_line_n], s ? s : "", kCols);
    g_line_n++;
    g_dirty = 1;
}

static int key_down(const wm::Input &in, int sc)
{
    if (sc < 0 || sc > 255)
        return 0;
    return (in.keys[sc >> 3] & (uint8_t)(1u << (sc & 7))) != 0;
}

static int key_pressed(const wm::Input &in, int sc)
{
    int now = key_down(in, sc);
    int was = (g_prev_keys[sc >> 3] & (uint8_t)(1u << (sc & 7))) != 0;
    return now && !was;
}

/* US QWERTY set-1 make codes → char (unshifted / shifted). */
static char sc_to_char(int sc, int shift)
{
    static const char *un =
        "\0\0331234567890-=\b\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
    static const char *sh =
        "\0\033!@#$%^&*()_+\b\tQWERTYUIOP{}\n\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";
    if (sc <= 0 || sc >= 58)
        return 0;
    const char *t = shift ? sh : un;
    return t[sc];
}

static void run_cmd(const char *cmd)
{
    char msg[kCols];
    if (!cmd || !cmd[0])
        return;
    if (cmd[0] == 'c' && cmd[1] == 'd' && (cmd[2] == ' ' || cmd[2] == '\0')) {
        const char *path = cmd[2] == ' ' ? cmd + 3 : "/";
        if (hsrc::sdk::chdir(path) == 0) {
            (void)hsrc::sdk::getcwd(g_cwd, sizeof(g_cwd));
            push_line(g_cwd);
        } else
            push_line("cd: failed");
        return;
    }
    if (strcmp(cmd, "pwd") == 0) {
        (void)hsrc::sdk::getcwd(g_cwd, sizeof(g_cwd));
        push_line(g_cwd);
        return;
    }
    if (strcmp(cmd, "clear") == 0) {
        g_line_n = 0;
        g_dirty = 1;
        return;
    }
    if (strcmp(cmd, "help") == 0) {
        push_line("cmds: help pwd cd ls clear <app>");
        push_line("apps: files terminal os-settings activity-monitor");
        return;
    }
    if (strcmp(cmd, "ls") == 0) {
        vfs_dirent_t ents[32];
        int fd = (int)hsrc::sdk::open(".", O_RDONLY | O_DIRECTORY);
        if (fd < 0)
            fd = (int)hsrc::sdk::open(g_cwd, O_RDONLY | O_DIRECTORY);
        if (fd < 0) {
            push_line("ls: cannot open");
            return;
        }
        for (;;) {
            long n = hsrc::sdk::getdents(fd, ents, 32);
            if (n <= 0)
                break;
            for (long i = 0; i < n; i++) {
                char line[kCols];
                line[0] = S_ISDIR(ents[i].type) ? 'd' : '-';
                line[1] = ' ';
                str_copy(line + 2, ents[i].name, kCols - 2);
                push_line(line);
            }
        }
        hsrc::sdk::close(fd);
        return;
    }
    /* Try spawn bare name from PATH */
    {
        long pid = hsrc::sdk::process::spawn(cmd, nullptr);
        if (pid > 0) {
            /* "spawned pid=N" without printf */
            char *p = msg;
            const char *pre = "spawned pid=";
            while (*pre)
                *p++ = *pre++;
            unsigned v = (unsigned)pid;
            char tmp[12];
            int n = 0;
            if (v == 0)
                tmp[n++] = '0';
            while (v) {
                tmp[n++] = (char)('0' + (v % 10u));
                v /= 10u;
            }
            while (n--)
                *p++ = tmp[n];
            *p = '\0';
            push_line(msg);
        } else
            push_line("spawn failed (try help)");
    }
}

} // namespace

extern "C" void exec_main(void)
{
    static reed::Device dev;
    static kilim::Context k;

    while (dev.init() < 0 || k.init(&dev) < 0)
        hsrc::sdk::sleep(100);

    str_copy(g_cwd, "/", sizeof(g_cwd));
    (void)hsrc::sdk::getcwd(g_cwd, sizeof(g_cwd));
    push_line("hsrcOS terminal — type help");
    g_input_n = 0;
    g_input[0] = '\0';

    wm::WindowOptions opts;
    opts.x = 100;
    opts.y = 90;
    opts.w = 780;
    opts.h = 480;
    opts.min_w = 520;
    opts.min_h = 320;
    opts.capture_keys = true;
    opts.set_title("Terminal");
    opts.set_class_name("terminal");

    wm::Window win;
    if (!win.create(opts))
        hsrc::sdk::exit(1);

    bool mapped = false;
    for (;;) {
        wm::Input in;
        (void)wm::input_snapshot(in);
        k.set_pointer(in.mouse_x, in.mouse_y, in.buttons);

        int shift = (in.mods & 0x01) != 0; /* KBD_MOD_SHIFT */
        for (int sc = 1; sc < 58; sc++) {
            if (!key_pressed(in, sc))
                continue;
            if (sc == 0x1C) { /* Enter */
                char echo[kCols];
                echo[0] = '$';
                echo[1] = ' ';
                str_copy(echo + 2, g_input, kCols - 2);
                push_line(echo);
                run_cmd(g_input);
                g_input_n = 0;
                g_input[0] = '\0';
                g_dirty = 1;
                continue;
            }
            if (sc == 0x0E) { /* Backspace */
                if (g_input_n > 0) {
                    g_input[--g_input_n] = '\0';
                    g_dirty = 1;
                }
                continue;
            }
            char ch = sc_to_char(sc, shift);
            if (ch >= 32 && ch < 127 && g_input_n + 1 < kCols) {
                g_input[g_input_n++] = ch;
                g_input[g_input_n] = '\0';
                g_dirty = 1;
            }
        }
        for (int i = 0; i < 32; i++)
            g_prev_keys[i] = in.keys[i];

        if (k.begin_frame() < 0)
            continue;

        int w = opts.w;
        int h = opts.h;
        int top = wm::kChromeTitleH;
        k.fill_rect(0, 0, w, h, kilim::rgba(12, 14, 18, 255));
        k.fill_round_rect(8, top + 8, w - 16, h - top - 16, 8,
                          kilim::rgba(16, 18, 22, 255));

        int y = top + 20;
        for (int i = 0; i < g_line_n; i++) {
            k.text(g_lines[i], 20, y, 13, kilim::rgba(210, 220, 230, 255));
            y += 18;
        }
        char prompt[kCols];
        prompt[0] = '$';
        prompt[1] = ' ';
        str_copy(prompt + 2, g_input, kCols - 3);
        size_t plen = 0;
        while (prompt[plen])
            plen++;
        if (plen + 1 < kCols) {
            prompt[plen] = '_';
            prompt[plen + 1] = '\0';
        }
        k.text(prompt, 20, y, 13, kilim::rgba(80, 250, 123, 255));

        (void)k.commit_frame();
        if (!mapped) {
            mapped = win.map_surface(dev, k.target());
            (void)win.damage();
            g_dirty = 0;
        } else if (g_dirty) {
            (void)win.damage();
            g_dirty = 0;
        }
    }
}
