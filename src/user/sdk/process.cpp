#include <user/sdk/process.hpp>
#include <kernel/argv.h>

namespace hsrc::sdk::process {

long spawn(const char *path, const char *const *argv)
{
    return spawn_ex(path, SPAWN_CONSOLE_HIDDEN, argv);
}

long spawn_ex(const char *path, uint32_t flags, const char *const *argv)
{
    int argc = 0;
    if (argv) {
        while (argv[argc] && argc < PROC_ARGC_MAX)
            argc++;
    }
    return hsrc::sdk::syscall4(SYS_SPAWN, (long)path, (long)flags, (long)argv, (long)argc);
}

bool console_show(pid_t pid, bool visible)
{
    return hsrc::sdk::syscall2(SYS_CONSOLE_SHOW, (long)pid, visible ? 1L : 0L) == 0;
}

long getargc()
{
    return hsrc::sdk::syscall0(SYS_GETARGC);
}

long getargv(int index, char *buf, size_t buflen)
{
    return hsrc::sdk::syscall3(SYS_GETARGV, (long)index, (long)buf, (long)buflen);
}

long waitpid(pid_t pid, int *status_out, int options)
{
    return hsrc::sdk::syscall3(SYS_WAITPID, (long)pid, (long)status_out, (long)options);
}

long kill(pid_t pid)
{
    return hsrc::sdk::syscall1(SYS_KILL, (long)pid);
}

long getpid()
{
    return hsrc::sdk::syscall0(SYS_GETPID);
}

long getppid()
{
    return hsrc::sdk::syscall0(SYS_GETPPID);
}

long proc_list(ProcListEntry *entries, int max_entries)
{
    return hsrc::sdk::syscall2(SYS_PROC_LIST, (long)entries, (long)max_entries);
}

long proc_stat(pid_t pid, ProcStat *out)
{
    return hsrc::sdk::syscall2(SYS_PROC_STAT, (long)pid, (long)out);
}

long sysinfo(SysInfo *out)
{
    return hsrc::sdk::syscall1(SYS_SYSINFO, (long)out);
}

long getenv(const char *name, char *buf, size_t buflen)
{
    return hsrc::sdk::syscall3(SYS_GETENV, (long)name, (long)buf, (long)buflen);
}

long setenv(const char *name, const char *val, int global)
{
    return hsrc::sdk::syscall3(SYS_SETENV, (long)name, (long)val, (long)global);
}

long unsetenv(const char *name, int global)
{
    return hsrc::sdk::syscall3(SYS_SETENV, (long)name, 0, (long)global);
}

const char *state_name(uint32_t state)
{
    switch (state) {
    case Ready:
        return "Ready";
    case Running:
        return "Running";
    case Blocked:
        return "Blocked";
    case Zombie:
        return "Zombie";
    default:
        return "Unused";
    }
}

} // namespace hsrc::sdk::process
