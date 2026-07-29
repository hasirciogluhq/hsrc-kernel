#include <kernel/exec.h>
#include <kernel/dynlib.h>
#include <kernel/argv.h>
#include <kernel/errno.h>
#include <kernel/env.h>
#include <kernel/process.h>
#include <kernel/string.h>
#include <kernel/initrd.h>
#include <kernel/initrd_store.h>
#include <kernel/vfs.h>
#include <kernel/syscall.h>
#include <drivers/console/vga.h>
#include <drivers/console/serial.h>
#include <multiboot.h>

static int name_ends_with_exec(const char *name)
{
    size_t n;
    if (!name)
        return 0;
    n = strlen(name);
    if (n < EXEC_EXT_LEN)
        return 0;
    return strcmp(name + n - EXEC_EXT_LEN, EXEC_EXT) == 0;
}

static int exec_file_readable(const char *path)
{
    int fd;

    if (!path || !path[0])
        return 0;
    fd = vfs_open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    (void)vfs_close(fd);
    return 1;
}

static int exec_try_path(const char *path, char *out, size_t outsz)
{
    char with_ext[VFS_PATH_MAX];

    if (!path || !out || outsz < 2)
        return -EINVAL;
    if (strlen(path) >= outsz)
        return -ENAMETOOLONG;
    if (exec_file_readable(path)) {
        strcpy(out, path);
        return 0;
    }
    if (name_ends_with_exec(path))
        return -ENOENT;
    {
        size_t plen = strlen(path);
        if (plen + EXEC_EXT_LEN >= sizeof(with_ext))
            return -ENAMETOOLONG;
        strcpy(with_ext, path);
        strcpy(with_ext + plen, EXEC_EXT);
    }
    if (!exec_file_readable(with_ext))
        return -ENOENT;
    if (strlen(with_ext) >= outsz)
        return -ENAMETOOLONG;
    strcpy(out, with_ext);
    return 0;
}

static int exec_path_has_slash(const char *s)
{
    if (!s)
        return 0;
    while (*s) {
        if (*s == '/')
            return 1;
        s++;
    }
    return 0;
}

int exec_resolve(const char *in, char *out, size_t outsz)
{
    process_t *p = process_current();
    char pathbuf[ENV_VAL_MAX];
    char cand[VFS_PATH_MAX];
    const char *pathenv;
    const char *start;
    const char *colon;
    size_t dirlen;
    int rc;

    if (!in || !in[0] || !out || outsz < 2)
        return -EINVAL;

    /* Absolute or relative path: try as-is, then with .exec */
    if (in[0] == '/' || exec_path_has_slash(in)) {
        if (in[0] == '/') {
            if (strlen(in) >= sizeof(cand))
                return -ENAMETOOLONG;
            strcpy(cand, in);
        } else {
            /* cwd-relative — mirror spawn resolve_path lightly */
            size_t cl, il;
            p = process_leader(p);
            if (!p)
                return -ESRCH;
            cl = strlen(p->cwd);
            il = strlen(in);
            if (strcmp(p->cwd, "/") == 0) {
                if (1 + il + 1 > sizeof(cand))
                    return -ENAMETOOLONG;
                cand[0] = '/';
                memcpy(cand + 1, in, il + 1);
            } else {
                if (cl + 1 + il + 1 > sizeof(cand))
                    return -ENAMETOOLONG;
                memcpy(cand, p->cwd, cl);
                cand[cl] = '/';
                memcpy(cand + cl + 1, in, il + 1);
            }
        }
        return exec_try_path(cand, out, outsz);
    }

    /* Bare name: search $PATH (default /system/bin:/applications) */
    pathbuf[0] = 0;
    if (env_get(p, "PATH", pathbuf, sizeof(pathbuf)) < 0 || !pathbuf[0])
        strcpy(pathbuf, "/system/bin:/applications");
    pathenv = pathbuf;
    start = pathenv;
    while (*start) {
        colon = start;
        while (*colon && *colon != ':')
            colon++;
        dirlen = (size_t)(colon - start);
        if (dirlen == 0) {
            /* empty PATH component = cwd */
            rc = exec_try_path(in, out, outsz);
            if (rc == 0)
                return 0;
        } else {
            if (dirlen + 1 + strlen(in) + 1 > sizeof(cand))
                return -ENAMETOOLONG;
            memcpy(cand, start, dirlen);
            cand[dirlen] = '/';
            strcpy(cand + dirlen + 1, in);
            rc = exec_try_path(cand, out, outsz);
            if (rc == 0)
                return 0;
        }
        if (!*colon)
            break;
        start = colon + 1;
    }
    return -ENOENT;
}

static void exec_attach_console(pid_t pid, const char *name, uint32_t spawn_flags)
{
    (void)pid;
    (void)name;
    (void)spawn_flags;
}

static const char *exec_path_basename(const char *path)
{
    const char *base = path;

    if (!path)
        return "";
    while (*path) {
        if (*path == '/')
            base = path + 1;
        path++;
    }
    return base;
}

static const uint8_t *exec_initrd_lookup(const char *path, size_t *size_out)
{
    const char *name = exec_path_basename(path);
    size_t size = 0;
    const initrd_header_t *hdr;
    size_t table_bytes;
    uint32_t i;

    if (size_out)
        *size_out = 0;
    if (!name[0])
        return NULL;

    hdr = (const initrd_header_t *)initrd_store_get(&size);
    if (!hdr || size < sizeof(uint32_t) * 2 || hdr->magic != INITRD_MAGIC ||
        hdr->count == 0 || hdr->count > INITRD_MAX_FILES)
        return NULL;

    table_bytes = sizeof(uint32_t) * 2 + (size_t)hdr->count * sizeof(initrd_file_t);
    if (size < table_bytes)
        return NULL;

    for (i = 0; i < hdr->count; i++) {
        const initrd_file_t *f = &hdr->files[i];

        if (strcmp(f->name, name) != 0)
            continue;
        if (f->size == 0 || f->offset + f->size > size)
            return NULL;
        if (size_out)
            *size_out = f->size;
        return (const uint8_t *)hdr + f->offset;
    }

    return NULL;
}

static int exec_validate_header(const exec_header_t *hdr, size_t total_size)
{
    if (!hdr || total_size < sizeof(exec_header_t)) {
        klog("[exec] spawn: bad blob\n");
        return -1;
    }
    if (hdr->magic != EXEC_MAGIC || hdr->version != EXEC_VERSION) {
        klog("[exec] spawn: bad magic/version\n");
        return -1;
    }
    if (hdr->header_size != sizeof(exec_header_t)) {
        klog("[exec] spawn: bad header_size\n");
        return -1;
    }
    if (hdr->load_addr < EXEC_LOAD_MIN || hdr->load_addr > EXEC_LOAD_MAX) {
        klog("[exec] spawn: load_addr out of range ");
        serial_print_hex(hdr->load_addr);
        klog("\n");
        return -1;
    }
    if (hdr->image_size == 0) {
        klog("[exec] spawn: empty image\n");
        return -1;
    }
    if ((size_t)hdr->header_size + (size_t)hdr->image_size > total_size) {
        klog("[exec] spawn: image exceeds blob\n");
        return -1;
    }
    if (hdr->entry_off >= hdr->image_size + hdr->bss_size) {
        klog("[exec] spawn: bad entry_off\n");
        return -1;
    }
    if (hdr->load_addr + hdr->image_size + hdr->bss_size < hdr->load_addr) {
        klog("[exec] spawn: load region wrap\n");
        return -1;
    }
    if (hdr->load_addr + hdr->image_size + hdr->bss_size > EXEC_LOAD_MAX + 0x00800000u) {
        klog("[exec] spawn: load region too large\n");
        return -1;
    }
    if (hdr->imports_off != 0 && hdr->imports_off >= hdr->image_size + hdr->bss_size) {
        klog("[exec] spawn: bad imports_off\n");
        return -1;
    }
    return 0;
}

static void exec_zero_bss(const exec_header_t *hdr)
{
    if (!hdr || hdr->bss_size == 0)
        return;
    memset((void *)(uintptr_t)(hdr->load_addr + hdr->image_size), 0, hdr->bss_size);
}

static int ranges_overlap(uint32_t a_lo, uint32_t a_hi, uint32_t b_lo, uint32_t b_hi)
{
    return a_lo < b_hi && b_lo < a_hi;
}

/*
 * Single address space: reloading an .exec at a fixed load_addr overwrites any
 * still-running instance. Kill those first so we do not corrupt live EIP/data
 * or leave orphan windows / PROC slots.
 */
static void exec_kill_load_overlap(const exec_header_t *hdr)
{
    process_t **table;
    uint32_t lo, hi;
    int i;
    pid_t victims[PROC_MAX];
    int n_victims = 0;
    uint32_t irqf;

    if (!hdr)
        return;
    lo = hdr->load_addr;
    hi = hdr->load_addr + hdr->image_size + hdr->bss_size;
    if (hi < lo)
        return;

    table = process_table();
    if (!table)
        return;

    /*
     * g_procs[] / process_t::state are mutated concurrently by other CPUs
     * (scheduler, exit, other exec spawns). Snapshot matching pids under
     * g_proc_lock, then call process_kill() outside the lock — process_kill
     * takes g_proc_lock itself (no recursive spinlocks) and can send IPIs /
     * touch sync/fd state that must not run with g_proc_lock held.
     */
    irqf = process_table_lock_irqsave();
    for (i = 0; i < PROC_MAX; i++) {
        process_t *p = table[i];
        uint32_t p_lo, p_hi;

        if (!p || p->state == PROC_UNUSED || p->state == PROC_ZOMBIE)
            continue;
        if (!p->is_user)
            continue;
        if (p->load_addr) {
            /* Full image+bss range known (spawned after load_addr tracking
             * was added) — catches foreign-app slot bloat, not just self-
             * reload of the exact same binary. */
            p_lo = p->load_addr;
            p_hi = p->load_addr + p->image_bytes;
            if (!ranges_overlap(lo, hi, p_lo, p_hi))
                continue;
        } else if (p->user_entry) {
            uint32_t entry = (uint32_t)(uintptr_t)p->user_entry;
            if (entry < lo || entry >= hi)
                continue;
        } else {
            continue;
        }
        if (n_victims < PROC_MAX)
            victims[n_victims++] = p->pid;
    }
    process_table_unlock_irqrestore(irqf);

    for (i = 0; i < n_victims; i++) {
        klog("[exec] killing overlapping process pid=");
        serial_print_uint((uint32_t)victims[i]);
        klog(" to load into its slot\n");
        (void)process_kill(victims[i]);
    }
}

/*
 * Defense-in-depth: after killing same-slot occupants, any *remaining*
 * live process whose [load_addr, load_addr+image_bytes) still intersects
 * [lo,hi) is a genuine cross-binary slot collision (e.g. a binary outgrew
 * its fixed xmake load-address spacing). In this single-address-space,
 * no-MMU tree that is silent memory corruption, not a recoverable fault —
 * refuse the spawn instead of memcpy-ing over another process's live
 * code/data. See xmake/userspace.lua load_addr spacing.
 */
static int exec_check_foreign_overlap(const exec_header_t *hdr)
{
    process_t **table;
    uint32_t lo, hi;
    int i;
    int bad = 0;
    uint32_t irqf;

    if (!hdr)
        return 0;
    lo = hdr->load_addr;
    hi = hdr->load_addr + hdr->image_size + hdr->bss_size;
    if (hi < lo)
        return 0;

    table = process_table();
    if (!table)
        return 0;

    irqf = process_table_lock_irqsave();
    for (i = 0; i < PROC_MAX; i++) {
        process_t *p = table[i];
        uint32_t p_lo, p_hi;

        if (!p || p->state == PROC_UNUSED || p->state == PROC_ZOMBIE)
            continue;
        if (!p->is_user || !p->load_addr)
            continue;
        p_lo = p->load_addr;
        p_hi = p->load_addr + p->image_bytes;
        if (!ranges_overlap(lo, hi, p_lo, p_hi))
            continue;
        klog("[exec] FATAL slot collision: new image [");
        serial_print_hex(lo);
        klog("..");
        serial_print_hex(hi);
        klog(") overlaps live proc=");
        klog(p->name);
        klog(" [");
        serial_print_hex(p_lo);
        klog("..");
        serial_print_hex(p_hi);
        klog(") — refusing spawn (widen load_addr spacing in xmake/userspace.lua)\n");
        bad = 1;
    }
    process_table_unlock_irqrestore(irqf);
    return bad;
}

static int exec_bind_libs(const exec_header_t *hdr)
{
    return dynlib_bind_exec(hdr->needed, EXEC_NEEDED_MAX, hdr->load_addr,
                        hdr->imports_off);
}

static int exec_spawn_header(const exec_header_t *hdr, uint32_t spawn_flags,
                            const char *const *argv, int argc)
{
    void (*entry)(void);
    pid_t pid;

    if (exec_bind_libs(hdr) < 0) {
        klog("[exec] dynamic lib bind failed\n");
        return -ENOENT;
    }

    entry = (void (*)(void))(uintptr_t)(hdr->load_addr + hdr->entry_off);
    pid = process_create_user_stack(hdr->name[0] ? hdr->name : "exec", entry,
                                    hdr->stack_size);
    if (pid < 0) {
        klog("[exec] process_create_user FAILED\n");
        vga_print("exec: process_create_user failed\n");
        return -1;
    }

    if (argv && argc > 0) {
        process_t *child = process_get(pid);
        if (child && argv_proc_set(child, argv, argc) < 0) {
            (void)process_kill(pid);
            return -EINVAL;
        }
    }

    {
        process_t *child = process_get(pid);
        if (child) {
            child->image_bytes = hdr->image_size + hdr->bss_size;
            child->load_addr = hdr->load_addr;
        }
    }

    klog("[exec] spawned ");
    klog(hdr->name);
    klog(" pid=");
    serial_print_uint((uint32_t)pid);
    klog(" entry=");
    serial_print_hex((uint32_t)(uintptr_t)entry);
    klog(" ustack=");
    serial_print_uint(process_clamp_ustack(hdr->stack_size));
    klog("\n");

    exec_attach_console(pid, hdr->name[0] ? hdr->name : "exec", spawn_flags);
    return pid;
}

int exec_spawn_flags(const void *blob, size_t size, uint32_t spawn_flags,
                    const char *const *argv, int argc)
{
    const exec_header_t *hdr;
    const uint8_t *img;
    uint8_t *dst;

    if (exec_validate_header((const exec_header_t *)blob, size) < 0)
        return -1;

    hdr = (const exec_header_t *)blob;

    klog("[exec] loading ");
    klog(hdr->name[0] ? hdr->name : "?");
    klog(" @ ");
    serial_print_hex(hdr->load_addr);
    klog(" img=");
    serial_print_uint(hdr->image_size);
    klog(" bss=");
    serial_print_uint(hdr->bss_size);
    klog("\n");

    exec_kill_load_overlap(hdr);
    if (exec_check_foreign_overlap(hdr)) {
        klog("[exec] spawn refused: slot collision\n");
        return -1;
    }

    img = (const uint8_t *)blob + hdr->header_size;
    dst = (uint8_t *)(uintptr_t)hdr->load_addr;
    memcpy(dst, img, hdr->image_size);
    exec_zero_bss(hdr);

    return exec_spawn_header(hdr, spawn_flags, argv, argc);
}

int exec_spawn(const void *blob, size_t size)
{
    return exec_spawn_flags(blob, size, SPAWN_CONSOLE_HIDDEN, NULL, 0);
}

int exec_spawn_path_flags(const char *path, uint32_t spawn_flags,
                         const char *const *argv, int argc)
{
    const uint8_t *initrd_blob;
    size_t initrd_size = 0;
    exec_header_t hdr;
    int fd;
    off_t end;
    ssize_t n;
    size_t loaded;
    uint8_t *dst;

    if (!path || !path[0])
        return -EINVAL;

    initrd_blob = exec_initrd_lookup(path, &initrd_size);
    if (initrd_blob)
        return exec_spawn_flags(initrd_blob, initrd_size, spawn_flags, argv, argc);

    fd = vfs_open(path, O_RDONLY);
    if (fd < 0)
        return fd;

    end = vfs_lseek(fd, 0, SEEK_END);
    if (end <= 0) {
        (void)vfs_close(fd);
        return end < 0 ? (int)end : -ENOEXEC;
    }
    if (vfs_lseek(fd, 0, SEEK_SET) < 0) {
        (void)vfs_close(fd);
        return -EIO;
    }

    n = vfs_read(fd, &hdr, sizeof(hdr));
    if (n < 0) {
        (void)vfs_close(fd);
        return (int)n;
    }
    if ((size_t)n != sizeof(hdr)) {
        (void)vfs_close(fd);
        return -ENOEXEC;
    }
    if (exec_validate_header(&hdr, (size_t)end) < 0) {
        (void)vfs_close(fd);
        return -ENOEXEC;
    }

    klog("[exec] loading ");
    klog(hdr.name[0] ? hdr.name : "?");
    klog(" @ ");
    serial_print_hex(hdr.load_addr);
    klog(" img=");
    serial_print_uint(hdr.image_size);
    klog(" bss=");
    serial_print_uint(hdr.bss_size);
    klog("\n");

    exec_kill_load_overlap(&hdr);
    if (exec_check_foreign_overlap(&hdr)) {
        klog("[exec] spawn refused: slot collision\n");
        (void)vfs_close(fd);
        return -1;
    }

    dst = (uint8_t *)(uintptr_t)hdr.load_addr;
    if (vfs_lseek(fd, (off_t)hdr.header_size, SEEK_SET) < 0) {
        (void)vfs_close(fd);
        return -EIO;
    }

    loaded = 0;
    while (loaded < hdr.image_size) {
        size_t chunk = hdr.image_size - loaded;
        n = vfs_read(fd, dst + loaded, chunk);
        if (n < 0) {
            (void)vfs_close(fd);
            return (int)n;
        }
        if (n == 0) {
            (void)vfs_close(fd);
            return -EIO;
        }
        loaded += (size_t)n;
    }

    (void)vfs_close(fd);
    exec_zero_bss(&hdr);
    return exec_spawn_header(&hdr, spawn_flags, argv, argc);
}

int exec_spawn_path(const char *path)
{
    return exec_spawn_path_flags(path, SPAWN_CONSOLE_HIDDEN, NULL, 0);
}
