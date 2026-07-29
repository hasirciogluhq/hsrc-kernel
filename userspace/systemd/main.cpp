#include <user/mke.h>
#include <user/sdk/process.hpp>
#include <user/sdk/syscall.hpp>
#include <user/sdk/fs.hpp>
#include <user/string.h>
#include <kernel/syscall.h>
#include <kernel/vfs.h>
#include <kernel/errno.h>

/*
 * Minimal userspace service manager (PID1's child).
 * Starts session units from the on-disk /applications tree, then
 * respawns them if they die. Kernel only boots init; init only
 * starts systemd; systemd owns window-manager + os-shell (+ more later).
 */

namespace {

struct Unit {
    const char *name;
    const char *path;
    int         respawn;
    pid_t       pid;
};

Unit g_units[] = {
    {"window-manager", "/applications/window-manager.mke", 1, 0},
    {"os-shell",       "/applications/os-shell.mke",       1, 0},
};

constexpr int kUnitCount = (int)(sizeof(g_units) / sizeof(g_units[0]));

int path_exists(const char *path)
{
    int fd = (int)hsrc::sdk::open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    (void)hsrc::sdk::close(fd);
    return 1;
}

void start_unit(Unit *u)
{
    if (!u || u->pid > 0)
        return;
    if (!path_exists(u->path))
        return;
    long pid = hsrc::sdk::process::spawn(u->path, nullptr);
    if (pid > 0)
        u->pid = (pid_t)pid;
}

void start_all(void)
{
    for (int i = 0; i < kUnitCount; i++)
        start_unit(&g_units[i]);
}

void on_child_exit(pid_t pid)
{
    for (int i = 0; i < kUnitCount; i++) {
        if (g_units[i].pid != pid)
            continue;
        g_units[i].pid = 0;
        if (g_units[i].respawn)
            start_unit(&g_units[i]);
        return;
    }
}

} /* namespace */

void mke_main(void)
{
    start_all();

    for (;;) {
        int status = 0;
        long rc = hsrc::sdk::process::waitpid(-1, &status, 0);
        if (rc > 0)
            on_child_exit((pid_t)rc);
        else
            hsrc::sdk::syscall0(SYS_YIELD);
    }
}
