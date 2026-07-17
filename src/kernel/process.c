#include <kernel/process.h>
#include <kernel/env.h>
#include <kernel/errno.h>
#include <kernel/heap.h>
#include <kernel/string.h>
#include <kernel/scheduler.h>
#include <kernel/socket.h>
#include <kernel/syscall.h>
#include <kernel/vfs.h>
#include <kernel/mkdx_api.h>
#include <drivers/serial.h>
#include <arch/x86/gdt.h>

static process_t processes[PROC_MAX];
static uint32_t  kstacks[PROC_MAX][PROC_KSTACK_SIZE / sizeof(uint32_t)];
static uint32_t  ustacks[PROC_MAX][PROC_USTACK_SIZE / sizeof(uint32_t)];
static pid_t     next_pid = 1;
static process_t *current;

static void process_clear_slot(process_t *p)
{
    if (!p)
        return;
    memset(p, 0, sizeof(*p));
    p->state = PROC_UNUSED;
    for (int fd = 0; fd < VFS_MAX_FD; fd++)
        p->fds[fd] = -1;
}

static uint32_t process_mem_bytes(const process_t *p)
{
    uint32_t total = 0;

    if (!p || p->state == PROC_UNUSED)
        return 0;

    total += PROC_KSTACK_SIZE;
    if (p->is_user)
        total += PROC_USTACK_SIZE;

    for (int i = 0; i < VMA_MAX; i++) {
        if (!p->vmas[i].used)
            continue;
        total += (uint32_t)(p->vmas[i].npages * PAGE_SIZE);
    }

    return total;
}

static uint64_t process_uptime_ticks(const process_t *p, uint64_t now_ticks)
{
    if (!p || now_ticks < p->start_ticks)
        return 0;
    return now_ticks - p->start_ticks;
}

static void process_free_console(pid_t pid)
{
    const mkdx_api_t *api = mkdx_api_get();
    if (api && api->console_free)
        api->console_free((int)pid);
}

static void process_free_windows(pid_t pid)
{
    const mkdx_api_t *api = mkdx_api_get();
    if (api && api->wm_destroy_by_pid)
        api->wm_destroy_by_pid((int)pid);
}

static void process_release_fds(process_t *p)
{
    if (!p)
        return;

    for (int i = 0; i < VFS_MAX_FD; i++) {
        int fd = p->fds[i];
        if (fd < 0)
            continue;
        if (PROC_FD_IS_SOCK(fd))
            (void)sock_close(PROC_FD_SOCK_ID(fd));
        else
            (void)vfs_close(fd);
        p->fds[i] = -1;
    }
}

static void process_trampoline(void (*entry)(void))
{
    entry();
    process_exit(0);
}

static void user_trampoline(void (*entry)(void))
{
    process_t *p = process_current();
    klog("[user] enter name=");
    klog(p && p->name[0] ? p->name : "?");
    klog(" entry=");
    serial_print_hex((uint32_t)(uintptr_t)entry);
    klog(" ustack=");
    serial_print_hex(p ? p->ustack_top : 0);
    klog("\n");
    if (!p || !entry) {
        klog("[user] FATAL: bad trampoline state\n");
        for (;;)
            __asm__ volatile("hlt");
    }
    gdt_set_kernel_stack(p->kstack_top);
    enter_usermode((uint32_t)entry, p->ustack_top);
    klog("[user] FATAL: enter_usermode returned\n");
    for (;;)
        __asm__ volatile("hlt");
}

static process_t *alloc_process(const char *name)
{
    process_t *p = NULL;
    int idx = -1;

    for (int i = 0; i < PROC_MAX; i++) {
        if (processes[i].state == PROC_UNUSED) {
            p = &processes[i];
            idx = i;
            break;
        }
    }
    if (!p)
        return NULL;

    memset(p, 0, sizeof(*p));
    p->pid = next_pid++;
    p->ppid = current ? current->pid : 0;
    p->state = PROC_READY;
    strncpy(p->name, name, PROC_NAME_MAX - 1);
    p->kstack_base = kstacks[idx];
    p->kstack_top = (uint32_t)(kstacks[idx] + (PROC_KSTACK_SIZE / sizeof(uint32_t)));
    p->ustack_top = (uint32_t)(ustacks[idx] + (PROC_USTACK_SIZE / sizeof(uint32_t)));
    p->start_ticks = scheduler_tick_count();
    for (int f = 0; f < VFS_MAX_FD; f++)
        p->fds[f] = -1;

    if (current) {
        p->uid = current->uid;
        p->euid = current->euid;
        strncpy(p->cwd, current->cwd, sizeof(p->cwd) - 1);
    } else {
        /* Default credentials: root. Shell / desktop spawn with full rights. */
        p->uid = 0;
        p->euid = 0;
        strcpy(p->cwd, "/");
    }

    env_inherit(p, current);

    int cfd = vfs_open("/dev/console", O_RDWR);
    if (cfd >= 0) {
        p->fds[STDIN_FILENO] = cfd;
        p->fds[STDOUT_FILENO] = cfd;
        p->fds[STDERR_FILENO] = cfd;
    }

    return p;
}

static void setup_kstack(process_t *p, void (*trampoline)(void (*)(void)), void (*entry)(void))
{
    uint32_t *sp = (uint32_t *)p->kstack_top;
    *--sp = (uint32_t)entry;
    *--sp = 0;
    *--sp = (uint32_t)trampoline;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    p->esp = sp;
}

void process_init(void)
{
    memset(processes, 0, sizeof(processes));
    for (int i = 0; i < PROC_MAX; i++) {
        processes[i].state = PROC_UNUSED;
        for (int f = 0; f < VFS_MAX_FD; f++)
            processes[i].fds[f] = -1;
    }
    current = NULL;
}

process_t *process_current(void) { return current; }
void process_set_current(process_t *p) { current = p; }
process_t *process_table(void) { return processes; }

process_t *process_get(pid_t pid)
{
    for (int i = 0; i < PROC_MAX; i++) {
        if (processes[i].state != PROC_UNUSED && processes[i].pid == pid)
            return &processes[i];
    }
    return NULL;
}

pid_t process_create(const char *name, void (*entry)(void))
{
    process_t *p = alloc_process(name);
    if (!p)
        return -1;
    p->is_user = 0;
    p->user_entry = NULL;
    setup_kstack(p, process_trampoline, entry);
    return p->pid;
}

pid_t process_create_user(const char *name, void (*entry)(void))
{
    process_t *p = alloc_process(name);
    if (!p)
        return -1;
    p->is_user = 1;
    p->user_entry = entry;
    setup_kstack(p, user_trampoline, entry);
    return p->pid;
}

pid_t process_getppid(void)
{
    return current ? current->ppid : 0;
}

pid_t process_waitpid(pid_t pid, int *status_out, int options)
{
    int found_child = 0;
    (void)options;

    if (!current)
        return -ESRCH;

    for (int i = 0; i < PROC_MAX; i++) {
        process_t *p = &processes[i];
        pid_t child_pid;

        if (p->state == PROC_UNUSED || p->ppid != current->pid)
            continue;
        if (pid > 0 && p->pid != pid)
            continue;

        found_child = 1;
        if (p->state != PROC_ZOMBIE)
            continue;

        child_pid = p->pid;
        if (status_out)
            *status_out = p->exit_code;
        process_release_fds(p);
        process_clear_slot(p);
        return child_pid;
    }

    if (!found_child)
        return -ECHILD;
    return 0;
}

void process_exit(int code)
{
    pid_t pid;
    if (!current)
        return;
    pid = current->pid;
    process_release_fds(current);
    process_free_windows(pid);
    process_free_console(pid);
    current->exit_code = code;
    if (current->ppid == 0)
        process_clear_slot(current);
    else
        current->state = PROC_ZOMBIE;
    scheduler_on_exit(current);
    schedule();
    for (;;)
        __asm__ volatile("hlt");
}

int process_kill(pid_t pid)
{
    process_t *p = process_get(pid);
    if (!p || p->state == PROC_UNUSED || p->state == PROC_ZOMBIE)
        return -ESRCH;
    if (!p->is_user)
        return -EPERM; /* kernel threads only exit themselves */
    if (p == current)
        process_exit(137);
    process_release_fds(p);
    process_free_windows(p->pid);
    process_free_console(p->pid);
    p->exit_code = 137;
    if (p->ppid == 0)
        process_clear_slot(p);
    else
        p->state = PROC_ZOMBIE;
    return 0;
}

void process_account_tick(process_t *p)
{
    if (!p || p->state == PROC_UNUSED)
        return;
    p->cpu_ticks++;
}

int process_list(proc_list_entry_t *out, size_t max_entries)
{
    uint64_t now_ticks = scheduler_tick_count();
    size_t count = 0;

    for (int i = 0; i < PROC_MAX; i++) {
        process_t *p = &processes[i];

        if (p->state == PROC_UNUSED)
            continue;
        if (out && count < max_entries) {
            proc_list_entry_t *dst = &out[count];
            memset(dst, 0, sizeof(*dst));
            dst->pid = p->pid;
            dst->ppid = p->ppid;
            dst->state = (uint32_t)p->state;
            dst->is_user = (uint32_t)p->is_user;
            dst->cpu_ticks = p->cpu_ticks;
            dst->uptime_ticks = process_uptime_ticks(p, now_ticks);
            dst->mem_bytes = process_mem_bytes(p);
            strncpy(dst->name, p->name, sizeof(dst->name) - 1);
        }
        count++;
    }

    return (int)count;
}

int process_stat(pid_t pid, proc_stat_t *out)
{
    process_t *p;
    uint64_t now_ticks = scheduler_tick_count();

    if (!out)
        return -EFAULT;

    p = pid > 0 ? process_get(pid) : process_current();
    if (!p || p->state == PROC_UNUSED)
        return -ESRCH;

    memset(out, 0, sizeof(*out));
    out->pid = p->pid;
    out->ppid = p->ppid;
    out->state = (uint32_t)p->state;
    out->is_user = (uint32_t)p->is_user;
    out->cpu_ticks = p->cpu_ticks;
    out->start_ticks = p->start_ticks;
    out->uptime_ticks = process_uptime_ticks(p, now_ticks);
    out->mem_bytes = process_mem_bytes(p);
    strncpy(out->name, p->name, sizeof(out->name) - 1);
    return 0;
}

int process_sysinfo(sys_info_t *out)
{
    if (!out)
        return -EFAULT;

    memset(out, 0, sizeof(*out));
    out->uptime_ticks = scheduler_tick_count();
    out->total_cpu_ticks = out->uptime_ticks;
    out->used_ram_bytes = (uint32_t)heap_used();
    out->free_ram_bytes = (uint32_t)heap_free();
    out->total_ram_bytes = out->used_ram_bytes + out->free_ram_bytes;

    for (int i = 0; i < PROC_MAX; i++) {
        if (processes[i].state != PROC_UNUSED)
            out->process_count++;
    }

    return 0;
}

int process_alloc_fd(process_t *p, int vfs_fd)
{
    for (int i = 0; i < VFS_MAX_FD; i++) {
        if (p->fds[i] < 0) {
            p->fds[i] = vfs_fd;
            return i;
        }
    }
    return -1;
}

int process_alloc_sock_fd(process_t *p, int sock_id)
{
    for (int i = 0; i < VFS_MAX_FD; i++) {
        if (p->fds[i] < 0) {
            p->fds[i] = PROC_FD_MAKE_SOCK(sock_id);
            return i;
        }
    }
    return -1;
}

int process_lookup_fd(process_t *p, int user_fd)
{
    if (!p || user_fd < 0 || user_fd >= VFS_MAX_FD)
        return -1;
    return p->fds[user_fd];
}

void process_free_fd(process_t *p, int user_fd)
{
    if (!p || user_fd < 0 || user_fd >= VFS_MAX_FD)
        return;
    p->fds[user_fd] = -1;
}
