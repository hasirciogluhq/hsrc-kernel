#include <user/sdk/process.hpp>
#include <kernel/argv.h>
#include <user/string.h>

namespace hsrc::sdk::process {
namespace {

const proc_page_t *g_page = nullptr;

void copy_page_entry(ProcListEntry *dst, const proc_page_entry_t *src)
{
    dst->pid = src->pid;
    dst->ppid = src->ppid;
    dst->state = src->state;
    dst->is_user = src->is_user;
    dst->cpu_ticks = src->cpu_ticks;
    dst->uptime_ticks = src->uptime_ticks;
    dst->mem_bytes = src->mem_bytes;
    memcpy(dst->name, src->name, sizeof(dst->name));
    dst->name[sizeof(dst->name) - 1] = 0;
}

/*
 * Seqlock-read the mapped proc page in place — never memcpy the full page
 * (~17KiB) onto the 8KiB user stack.
 */
bool read_snapshot(ProcListEntry *entries, int max_entries, int *count_out, SysInfo *info_out)
{
    if (!map_proc_page() || !g_page)
        return false;

    for (int spin = 0; spin < 64; spin++) {
        uint32_t s1 = g_page->seq;
        __asm__ volatile("" ::: "memory");
        if (s1 & 1u)
            continue;

        int page_count = (int)g_page->count;
        if (page_count < 0)
            page_count = 0;
        int n = page_count;
        if (max_entries > 0 && n > max_entries)
            n = max_entries;

        SysInfo info{};
        info.uptime_ticks = g_page->uptime_ticks;
        info.total_cpu_ticks = g_page->total_cpu_ticks;
        info.total_ram_bytes = g_page->total_ram_bytes;
        info.used_ram_bytes = g_page->used_ram_bytes;
        info.free_ram_bytes = g_page->free_ram_bytes;
        info.process_count = g_page->process_count;

        if (entries && n > 0) {
            for (int i = 0; i < n; i++)
                copy_page_entry(&entries[i], &g_page->entries[i]);
        }

        __asm__ volatile("" ::: "memory");
        uint32_t s2 = g_page->seq;
        if (!(s1 & 1u) && s1 == s2 && g_page->magic == PROC_PAGE_MAGIC) {
            if (count_out)
                *count_out = n;
            if (info_out)
                *info_out = info;
            return true;
        }
    }
    return false;
}

} // namespace

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

bool map_proc_page()
{
    if (g_page && g_page->magic == PROC_PAGE_MAGIC)
        return true;
    long p = hsrc::sdk::syscall0(SYS_PROC_MAP);
    if (p <= 0)
        return false;
    const proc_page_t *pp = (const proc_page_t *)(uintptr_t)p;
    if (!pp || pp->magic != PROC_PAGE_MAGIC)
        return false;
    g_page = pp;
    return true;
}

bool refresh_snapshot()
{
    /* Always hits kernel publish path (throttled server-side). */
    long p = hsrc::sdk::syscall0(SYS_PROC_MAP);
    if (p <= 0)
        return false;
    const proc_page_t *pp = (const proc_page_t *)(uintptr_t)p;
    if (!pp || pp->magic != PROC_PAGE_MAGIC)
        return false;
    g_page = pp;
    return true;
}

bool snapshot(ProcListEntry *entries, int max_entries, int *count_out, SysInfo *info_out)
{
    return read_snapshot(entries, max_entries, count_out, info_out);
}

int snapshot_count()
{
    int count = 0;
    if (read_snapshot(nullptr, 0, &count, nullptr))
        return count;
    return -1;
}

bool snapshot_entry(int index, ProcListEntry *out)
{
    if (!out || index < 0)
        return false;
    if (!map_proc_page() || !g_page)
        return false;

    for (int spin = 0; spin < 64; spin++) {
        uint32_t s1 = g_page->seq;
        __asm__ volatile("" ::: "memory");
        if (s1 & 1u)
            continue;

        int page_count = (int)g_page->count;
        if (index >= page_count)
            return false;

        copy_page_entry(out, &g_page->entries[index]);

        __asm__ volatile("" ::: "memory");
        uint32_t s2 = g_page->seq;
        if (!(s1 & 1u) && s1 == s2 && g_page->magic == PROC_PAGE_MAGIC)
            return true;
    }
    return false;
}

uint32_t snapshot_seq()
{
    if (!map_proc_page() || !g_page)
        return 0;
    return g_page->seq;
}

uint32_t snapshot_generation()
{
    if (!map_proc_page() || !g_page)
        return 0;
    return g_page->generation;
}

long proc_list(ProcListEntry *entries, int max_entries)
{
    int count = 0;
    if (snapshot(entries, max_entries, &count, nullptr))
        return count;
    return hsrc::sdk::syscall2(SYS_PROC_LIST, (long)entries, (long)max_entries);
}

long proc_stat(pid_t pid, ProcStat *out)
{
    return hsrc::sdk::syscall2(SYS_PROC_STAT, (long)pid, (long)out);
}

long sysinfo(SysInfo *out)
{
    if (snapshot(nullptr, 0, nullptr, out))
        return 0;
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
