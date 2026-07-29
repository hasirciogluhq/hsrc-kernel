#include <kernel/kshell.h>
#include <kernel/vfs.h>
#include <kernel/mke.h>
#include <kernel/process.h>
#include <kernel/string.h>
#include <kernel/mkdx_api.h>
#include <drivers/console.h>
#include <drivers/vga.h>
#include <drivers/serial.h>
#include <drivers/display.h>

#define KSHELL_LINE_MAX 128

static void kshell_print(const char *s)
{
    console_print(s);
    klog(s);
}

static void kshell_prompt(void)
{
    kshell_print("mykernel> ");
}

static int kshell_readline(char *buf, size_t max)
{
    size_t n = 0;

    if (!buf || max < 2)
        return -1;
    for (;;) {
        int c = console_getc();
        if (c < 0)
            continue;
        if (c == '\r')
            c = '\n';
        if (c == '\b' || c == 127) {
            if (n > 0) {
                n--;
                console_write("\b \b", 3);
            }
            continue;
        }
        if (c == '\n') {
            console_putc('\n');
            buf[n] = 0;
            return (int)n;
        }
        if (n + 1 >= max)
            continue;
        buf[n++] = (char)c;
        console_putc((char)c);
    }
}

static void cmd_help(void)
{
    kshell_print("commands: help, clear, ls [path], cat <path>, run <path|name>, ps, gui?\n");
    kshell_print("kernel is standalone; GUI needs display+mkdx+/system/bin/window-manager\n");
}

static void cmd_clear(void)
{
    vga_init();
}

static void cmd_ls(const char *path)
{
    int fd;
    vfs_dirent_t dent;
    const char *p = path && path[0] ? path : "/";

    fd = vfs_open(p, O_RDONLY | O_DIRECTORY);
    if (fd < 0)
        fd = vfs_open(p, O_RDONLY);
    if (fd < 0) {
        kshell_print("ls: cannot open\n");
        return;
    }
    for (;;) {
        int n = vfs_readdir(fd, &dent, 1);
        if (n <= 0)
            break;
        kshell_print(dent.name);
        kshell_print("\n");
    }
    (void)vfs_close(fd);
}

static void cmd_cat(const char *path)
{
    char buf[128];
    int fd;
    ssize_t n;

    if (!path || !path[0]) {
        kshell_print("usage: cat <path>\n");
        return;
    }
    fd = vfs_open(path, O_RDONLY);
    if (fd < 0) {
        kshell_print("cat: open failed\n");
        return;
    }
    while ((n = vfs_read(fd, buf, sizeof(buf) - 1)) > 0) {
        size_t i;
        for (i = 0; i < (size_t)n; i++)
            console_putc(buf[i]);
    }
    (void)vfs_close(fd);
    kshell_print("\n");
}

static void cmd_run(const char *name)
{
    char resolved[VFS_PATH_MAX];
    int pid;
    int rc;

    if (!name || !name[0]) {
        kshell_print("usage: run <path|name>\n");
        return;
    }
    rc = exe_resolve(name, resolved, sizeof(resolved));
    if (rc < 0) {
        kshell_print("run: not found\n");
        return;
    }
    pid = mke_spawn_path(resolved);
    if (pid < 0) {
        kshell_print("run: spawn failed\n");
        return;
    }
    kshell_print("spawned ok\n");
}

static void cmd_ps(void)
{
    process_t **table = process_table();
    int i;

    for (i = 0; i < PROC_MAX; i++) {
        process_t *p = table[i];
        char line[96];
        char num[16];
        unsigned v;
        int j = 0, k, n;

        if (!p || p->state == PROC_UNUSED)
            continue;
        v = (unsigned)p->pid;
        if (v == 0)
            num[j++] = '0';
        else {
            char tmp[16];
            int t = 0;
            while (v) {
                tmp[t++] = (char)('0' + (v % 10));
                v /= 10;
            }
            while (t--)
                num[j++] = tmp[t];
        }
        num[j] = 0;
        n = 0;
        line[n++] = '[';
        for (k = 0; num[k] && n + 2 < (int)sizeof(line); k++)
            line[n++] = num[k];
        line[n++] = ']';
        line[n++] = ' ';
        for (k = 0; p->name[k] && n + 2 < (int)sizeof(line); k++)
            line[n++] = p->name[k];
        line[n++] = '\n';
        line[n] = 0;
        kshell_print(line);
    }
}

static void cmd_gui_status(void)
{
    if (display_active() && mkdx_api_get())
        kshell_print("gui: display+mkdx ready\n");
    else if (display_active())
        kshell_print("gui: display yes, mkdx no\n");
    else
        kshell_print("gui: no display (console mode)\n");
}

void kshell_run(void)
{
    char line[KSHELL_LINE_MAX];

    kshell_print("\n=== mykernel console ===\n");
    kshell_print("No GUI — kernel shell active.\n");
    cmd_help();

    for (;;) {
        char *cmd;
        char *arg;

        kshell_prompt();
        if (kshell_readline(line, sizeof(line)) < 0)
            continue;
        cmd = line;
        while (*cmd == ' ')
            cmd++;
        if (!*cmd)
            continue;
        arg = cmd;
        while (*arg && *arg != ' ')
            arg++;
        if (*arg) {
            *arg++ = 0;
            while (*arg == ' ')
                arg++;
        } else {
            arg = (char *)"";
        }

        if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0)
            cmd_help();
        else if (strcmp(cmd, "clear") == 0)
            cmd_clear();
        else if (strcmp(cmd, "ls") == 0)
            cmd_ls(arg);
        else if (strcmp(cmd, "cat") == 0)
            cmd_cat(arg);
        else if (strcmp(cmd, "run") == 0)
            cmd_run(arg);
        else if (strcmp(cmd, "ps") == 0)
            cmd_ps();
        else if (strcmp(cmd, "gui?") == 0 || strcmp(cmd, "gui") == 0)
            cmd_gui_status();
        else
            kshell_print("unknown command (help)\n");
    }
}

static void kshell_entry(void)
{
    kshell_run();
}

void kshell_start(void)
{
    pid_t pid = process_create("kshell", kshell_entry);
    if (pid < 0)
        klog("[boot] kshell_start failed\n");
    else {
        klog("[boot] kshell pid=");
        serial_print_uint((uint32_t)pid);
        klog("\n");
    }
}
