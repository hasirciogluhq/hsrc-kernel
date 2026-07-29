#include <user/mke.h>
#include <user/sdk/process.hpp>
#include <user/sdk/syscall.hpp>
#include <kernel/syscall.h>

/*
 * /init — PID1.
 * Starts the GUI session package from /system/bin when present.
 * Kernel stays usable without these binaries (console / no-GUI).
 */

namespace {

struct Unit {
    const char *name;
    int         respawn;
    pid_t       pid;
};

/* System session — not user apps. Resolved via PATH (/system/bin). */
Unit g_units[] = {
    {"window-manager", 1, 0},
    {"os-shell",       1, 0},
};

constexpr int kUnitCount = (int)(sizeof(g_units) / sizeof(g_units[0]));

void start_unit(Unit *u)
{
    if (!u || u->pid > 0)
        return;
    long pid = hsrc::sdk::process::spawn(u->name, nullptr);
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

extern "C" void mke_main(void)
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
