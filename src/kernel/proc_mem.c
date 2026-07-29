#include <kernel/proc_mem.h>
#include <kernel/process.h>
#include <kernel/mm.h>
#include <kernel/vmm.h>
#include <kernel/heap.h>
#include <kernel/errno.h>
#include <kernel/string.h>
#include <kernel/klock.h>

#define PROC_HANDLE_MAX 128
#define PROC_HOOK_MAX   8

static proc_handle_t g_handles[PROC_HANDLE_MAX];
static int g_next_handle = 1;
static spinlock_t g_pm_lock;

typedef struct {
    proc_open_hook_fn fn;
    void *ctx;
} proc_hook_t;

static proc_hook_t g_hooks[PROC_HOOK_MAX];
static int g_hook_n;

void proc_mem_init(void)
{
    memset(g_handles, 0, sizeof(g_handles));
    memset(g_hooks, 0, sizeof(g_hooks));
    g_hook_n = 0;
    g_next_handle = 1;
    spin_init(&g_pm_lock);
}

int proc_register_open_hook(proc_open_hook_fn fn, void *ctx)
{
    uint32_t f;
    if (!fn)
        return -EINVAL;
    f = spin_lock_irqsave(&g_pm_lock);
    if (g_hook_n >= PROC_HOOK_MAX) {
        spin_unlock_irqrestore(&g_pm_lock, f);
        return -ENOMEM;
    }
    g_hooks[g_hook_n].fn = fn;
    g_hooks[g_hook_n].ctx = ctx;
    g_hook_n++;
    spin_unlock_irqrestore(&g_pm_lock, f);
    return 0;
}

void proc_unregister_open_hook(proc_open_hook_fn fn)
{
    uint32_t f;
    int i;
    f = spin_lock_irqsave(&g_pm_lock);
    for (i = 0; i < g_hook_n; i++) {
        if (g_hooks[i].fn == fn) {
            g_hooks[i] = g_hooks[g_hook_n - 1];
            g_hook_n--;
            break;
        }
    }
    spin_unlock_irqrestore(&g_pm_lock, f);
}

static int run_open_hooks(pid_t req, pid_t target, uint32_t access)
{
    int i;
    for (i = 0; i < g_hook_n; i++) {
        int rc = g_hooks[i].fn(req, target, access, g_hooks[i].ctx);
        if (rc < 0)
            return rc;
    }
    return 0;
}

static proc_handle_t *lookup_handle(int id)
{
    int i;
    for (i = 0; i < PROC_HANDLE_MAX; i++) {
        if (g_handles[i].used && g_handles[i].id == id)
            return &g_handles[i];
    }
    return NULL;
}

static int prot_from_page(uint32_t prot)
{
    int p = 0;
    if (prot & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ |
                PAGE_EXECUTE_READWRITE))
        p |= PROT_READ;
    if (prot & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))
        p |= PROT_WRITE;
    if (prot & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))
        p |= PROT_EXEC;
    if (!p)
        p = PROT_READ | PROT_WRITE;
    return p;
}

/* Caller must hold process_table_lock_irqsave() — reads lead->vmas[] /
 * ustack_base / kstack_base, which mm_mmap()/mm_munmap() mutate under the
 * same lock from other CPUs. */
static int resolve_addr_locked(process_t *target, uint32_t addr, size_t len,
                               void **out_ptr, size_t *out_max)
{
    process_t *lead = process_leader(target);
    size_t i;
    if (!lead || !out_ptr)
        return -EINVAL;

    /* User stack (canonical VA → physical backing) */
    if (lead->ustack_base && lead->ustack_size) {
        uint32_t b = USER_STACK_TOP - lead->ustack_size;
        uint32_t e = USER_STACK_TOP;
        if (addr >= b && addr + len <= e) {
            *out_ptr = (uint8_t *)lead->ustack_base + (addr - b);
            if (out_max)
                *out_max = (size_t)(e - addr);
            return 0;
        }
    }

    /* Kernel stack (usually not for userspace RPM; allow with VM_READ) */
    if (lead->kstack_base) {
        uint32_t b = (uint32_t)(uintptr_t)lead->kstack_base;
        uint32_t e = b + PROC_KSTACK_SIZE;
        if (addr >= b && addr + len <= e) {
            *out_ptr = (uint8_t *)lead->kstack_base + (addr - b);
            if (out_max)
                *out_max = (size_t)(e - addr);
            return 0;
        }
    }

    for (i = 0; i < VMA_MAX; i++) {
        vma_t *v = &lead->vmas[i];
        if (!v->used || !v->pages)
            continue;
        if (addr >= v->start && addr < v->end) {
            size_t max = (size_t)(v->end - addr);
            if (len > max)
                return -EFAULT;
            *out_ptr = (uint8_t *)v->pages + (addr - v->start);
            if (out_max)
                *out_max = max;
            return 0;
        }
    }

    /* Mapped .exec image */
    if (lead->image_pages && lead->image_bytes && lead->load_addr) {
        uint32_t b = lead->load_addr;
        uint32_t e = b + lead->image_bytes;
        if (addr >= b && addr + len <= e) {
            *out_ptr = (uint8_t *)lead->image_pages + (addr - b);
            if (out_max)
                *out_max = (size_t)(e - addr);
            return 0;
        }
    }
    return -EFAULT;
}

long sys_open_process(pid_t target, uint32_t access)
{
    process_t *cur = process_current();
    process_t *t;
    pid_t req;
    uint32_t f;
    int i;
    int rc;
    int slot = -1;

    if (!cur || target <= 0)
        return -EINVAL;
    req = process_leader(cur)->pid;
    t = process_get(target);
    if (!t || t->state == PROC_UNUSED || t->state == PROC_ZOMBIE)
        return -ESRCH;

    rc = run_open_hooks(req, target, access);
    if (rc < 0)
        return rc; /* driver blocked — Windows-like ACCESS_DENIED */

    f = spin_lock_irqsave(&g_pm_lock);
    for (i = 0; i < PROC_HANDLE_MAX; i++) {
        if (!g_handles[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        spin_unlock_irqrestore(&g_pm_lock, f);
        return -EMFILE;
    }
    g_handles[slot].used = 1;
    g_handles[slot].id = g_next_handle++;
    g_handles[slot].owner_pid = req;
    g_handles[slot].target_pid = target;
    g_handles[slot].access = access ? access : PROCESS_QUERY_INFORMATION;
    rc = g_handles[slot].id;
    spin_unlock_irqrestore(&g_pm_lock, f);
    return rc;
}

long sys_close_handle(int handle)
{
    process_t *cur = process_current();
    proc_handle_t *h;
    uint32_t f;
    pid_t req;

    if (!cur || handle <= 0)
        return -EINVAL;
    req = process_leader(cur)->pid;
    f = spin_lock_irqsave(&g_pm_lock);
    h = lookup_handle(handle);
    if (!h || h->owner_pid != req) {
        spin_unlock_irqrestore(&g_pm_lock, f);
        return -EBADF;
    }
    memset(h, 0, sizeof(*h));
    spin_unlock_irqrestore(&g_pm_lock, f);
    return 0;
}

static long copy_with_access(int handle, uint32_t need_access, uint32_t addr,
                             void *buf, size_t len, int writing,
                             size_t *out_n)
{
    process_t *cur = process_current();
    process_t *target;
    proc_handle_t *h;
    pid_t req, tid;
    uint32_t f;
    void *ptr = NULL;
    size_t max = 0;
    int rc;

    if (!cur || !buf || len == 0)
        return -EINVAL;
    req = process_leader(cur)->pid;

    if (handle == (int)PROCESS_HANDLE_CURRENT) {
        tid = req;
    } else {
        f = spin_lock_irqsave(&g_pm_lock);
        h = lookup_handle(handle);
        if (!h || h->owner_pid != req) {
            spin_unlock_irqrestore(&g_pm_lock, f);
            return -EBADF;
        }
        if ((h->access & need_access) == 0) {
            spin_unlock_irqrestore(&g_pm_lock, f);
            return -EACCES;
        }
        tid = h->target_pid;
        spin_unlock_irqrestore(&g_pm_lock, f);
    }

    target = process_get(tid);
    if (!target)
        return -ESRCH;

    /*
     * Hold g_proc_lock across resolve + copy: resolve_addr_locked() returns
     * a raw pointer into the target's vma/stack backing. Without the lock a
     * concurrent munmap() on another CPU could free that memory between
     * resolve and memcpy (use-after-free). Critical section is bounded by
     * `len` (capped by the syscall's out_bytes) — no sleeping inside.
     */
    f = process_table_lock_irqsave();
    rc = resolve_addr_locked(target, addr, len, &ptr, &max);
    if (rc < 0) {
        process_table_unlock_irqrestore(f);
        return rc;
    }
    if (len > max)
        len = max;
    if (writing)
        memcpy(ptr, buf, len);
    else
        memcpy(buf, ptr, len);
    process_table_unlock_irqrestore(f);

    if (out_n)
        *out_n = len;
    return (long)len;
}

long sys_read_process_memory(int handle, uint32_t addr, void *buf, size_t len,
                             size_t *out_read)
{
    return copy_with_access(handle, PROCESS_VM_READ, addr, buf, len, 0, out_read);
}

long sys_write_process_memory(int handle, uint32_t addr, const void *buf,
                              size_t len, size_t *out_written)
{
    return copy_with_access(handle, PROCESS_VM_WRITE, addr, (void *)buf, len, 1,
                            out_written);
}

long sys_virtual_alloc(uint32_t addr, size_t size, uint32_t type, uint32_t prot)
{
    process_t *p = process_current();
    int pprot;
    (void)type;
    if (!p || size == 0)
        return -EINVAL;
    pprot = prot_from_page(prot);
    return mm_mmap(p, addr, size, pprot, MAP_PRIVATE, -1, 0);
}

long sys_virtual_free(uint32_t addr, size_t size, uint32_t type)
{
    (void)type;
    return mm_munmap(process_current(), addr, size);
}

long sys_virtual_alloc_ex(int handle, uint32_t addr, size_t size, uint32_t type,
                          uint32_t prot)
{
    process_t *cur = process_current();
    process_t *target;
    proc_handle_t *h;
    pid_t req, tid;
    uint32_t f;
    int pprot;
    (void)type;

    if (!cur || size == 0)
        return -EINVAL;
    req = process_leader(cur)->pid;

    if (handle == (int)PROCESS_HANDLE_CURRENT) {
        tid = req;
    } else {
        f = spin_lock_irqsave(&g_pm_lock);
        h = lookup_handle(handle);
        if (!h || h->owner_pid != req) {
            spin_unlock_irqrestore(&g_pm_lock, f);
            return -EBADF;
        }
        if ((h->access & PROCESS_VM_OPERATION) == 0) {
            spin_unlock_irqrestore(&g_pm_lock, f);
            return -EACCES;
        }
        tid = h->target_pid;
        spin_unlock_irqrestore(&g_pm_lock, f);
    }
    target = process_get(tid);
    if (!target)
        return -ESRCH;
    pprot = prot_from_page(prot);
    return mm_mmap(target, addr, size, pprot, MAP_PRIVATE, -1, 0);
}

long sys_virtual_free_ex(int handle, uint32_t addr, size_t size, uint32_t type)
{
    process_t *cur = process_current();
    process_t *target;
    proc_handle_t *h;
    pid_t req, tid;
    uint32_t f;
    (void)type;

    if (!cur)
        return -EINVAL;
    req = process_leader(cur)->pid;

    if (handle == (int)PROCESS_HANDLE_CURRENT) {
        tid = req;
    } else {
        f = spin_lock_irqsave(&g_pm_lock);
        h = lookup_handle(handle);
        if (!h || h->owner_pid != req) {
            spin_unlock_irqrestore(&g_pm_lock, f);
            return -EBADF;
        }
        if ((h->access & PROCESS_VM_OPERATION) == 0) {
            spin_unlock_irqrestore(&g_pm_lock, f);
            return -EACCES;
        }
        tid = h->target_pid;
        spin_unlock_irqrestore(&g_pm_lock, f);
    }
    target = process_get(tid);
    if (!target)
        return -ESRCH;
    return mm_munmap(target, addr, size);
}

typedef struct proc_query_info {
    pid_t pid;
    pid_t ppid;
    uint32_t access;
    uint32_t state;
    uint32_t mem_bytes;
    uint32_t stack_bytes;
    uint32_t image_bytes;
    char name[32];
} proc_query_info_t;

long sys_query_process(int handle, void *out, size_t out_size)
{
    process_t *cur = process_current();
    process_t *target;
    proc_handle_t *h;
    proc_query_info_t info;
    pid_t req, tid;
    uint32_t f;
    uint32_t h_access;

    if (!cur || !out || out_size < sizeof(info))
        return -EINVAL;
    req = process_leader(cur)->pid;

    if (handle == (int)PROCESS_HANDLE_CURRENT) {
        tid = req;
        h_access = PROCESS_ALL_ACCESS;
    } else {
        f = spin_lock_irqsave(&g_pm_lock);
        h = lookup_handle(handle);
        if (!h || h->owner_pid != req) {
            spin_unlock_irqrestore(&g_pm_lock, f);
            return -EBADF;
        }
        if ((h->access & PROCESS_QUERY_INFORMATION) == 0) {
            spin_unlock_irqrestore(&g_pm_lock, f);
            return -EACCES;
        }
        tid = h->target_pid;
        h_access = h->access;
        spin_unlock_irqrestore(&g_pm_lock, f);
    }

    target = process_get(tid);
    if (!target)
        return -ESRCH;
    memset(&info, 0, sizeof(info));
    info.pid = target->pid;
    info.ppid = target->ppid;
    info.state = (uint32_t)target->state;
    info.image_bytes = target->image_bytes;
    info.stack_bytes = target->ustack_size + PROC_KSTACK_SIZE;
    info.access = h_access;
    strncpy(info.name, target->name, sizeof(info.name) - 1);
    memcpy(out, &info, sizeof(info));
    return (long)sizeof(info);
}

long sys_query_process_vm(int handle, void *out, size_t out_bytes)
{
    process_t *cur = process_current();
    process_t *target;
    process_t *lead;
    proc_handle_t *h;
    proc_vm_region_t *dst;
    pid_t req, tid;
    uint32_t f;
    size_t max_n, n = 0;
    size_t i;

    if (!cur || !out || out_bytes < sizeof(proc_vm_region_t))
        return -EINVAL;
    max_n = out_bytes / sizeof(proc_vm_region_t);
    req = process_leader(cur)->pid;

    if (handle == (int)PROCESS_HANDLE_CURRENT) {
        tid = req;
    } else {
        f = spin_lock_irqsave(&g_pm_lock);
        h = lookup_handle(handle);
        if (!h || h->owner_pid != req) {
            spin_unlock_irqrestore(&g_pm_lock, f);
            return -EBADF;
        }
        if ((h->access & (PROCESS_QUERY_INFORMATION | PROCESS_VM_READ)) == 0) {
            spin_unlock_irqrestore(&g_pm_lock, f);
            return -EACCES;
        }
        tid = h->target_pid;
        spin_unlock_irqrestore(&g_pm_lock, f);
    }

    target = process_get(tid);
    if (!target)
        return -ESRCH;
    lead = process_leader(target);
    dst = (proc_vm_region_t *)out;

    /* lead->vmas[] is mutated by mm_mmap()/mm_munmap() under g_proc_lock
     * from any CPU; take the same lock while walking it. */
    f = process_table_lock_irqsave();
    if (lead->ustack_base && lead->ustack_size && n < max_n) {
        dst[n].base = (uint32_t)(uintptr_t)lead->ustack_base;
        dst[n].size = lead->ustack_size;
        dst[n].prot = PROT_READ | PROT_WRITE;
        dst[n].kind = 1; /* stack */
        n++;
    }
    for (i = 0; i < VMA_MAX && n < max_n; i++) {
        vma_t *v = &lead->vmas[i];
        if (!v->used || !v->pages)
            continue;
        dst[n].base = (uint32_t)(uintptr_t)v->pages;
        dst[n].size = (uint32_t)(v->npages * PAGE_SIZE);
        dst[n].prot = (uint32_t)v->prot;
        dst[n].kind = 2; /* heap/vma */
        n++;
    }
    process_table_unlock_irqrestore(f);
    return (long)n;
}

/* ---- driver mirror ---- */

void *drv_heap_alloc(size_t n)
{
    return kmalloc(n);
}

void drv_heap_free(void *p)
{
    kfree(p);
}

void *drv_vm_alloc_pages(size_t npages)
{
    return mm_alloc_pages(npages);
}

void drv_vm_free_pages(void *p, size_t npages)
{
    mm_free_pages(p, npages);
}

long drv_open_process(pid_t target, uint32_t access)
{
    return sys_open_process(target, access);
}

long drv_close_handle(int handle)
{
    return sys_close_handle(handle);
}

long drv_read_process_memory(int handle, uint32_t addr, void *buf, size_t len)
{
    return sys_read_process_memory(handle, addr, buf, len, NULL);
}

long drv_write_process_memory(int handle, uint32_t addr, const void *buf,
                              size_t len)
{
    return sys_write_process_memory(handle, addr, buf, len, NULL);
}

long drv_virtual_alloc(uint32_t addr, size_t size, uint32_t type, uint32_t prot)
{
    return sys_virtual_alloc(addr, size, type, prot);
}

long drv_virtual_free(uint32_t addr, size_t size, uint32_t type)
{
    return sys_virtual_free(addr, size, type);
}

long drv_virtual_alloc_ex(int handle, uint32_t addr, size_t size, uint32_t type,
                          uint32_t prot)
{
    return sys_virtual_alloc_ex(handle, addr, size, type, prot);
}

long drv_virtual_free_ex(int handle, uint32_t addr, size_t size, uint32_t type)
{
    return sys_virtual_free_ex(handle, addr, size, type);
}
