#include <kernel/mke.h>
#include <kernel/argv.h>
#include <kernel/errno.h>
#include <kernel/heap.h>
#include <kernel/process.h>
#include <kernel/string.h>
#include <kernel/initrd.h>
#include <kernel/vfs.h>
#include <kernel/mkdx_api.h>
#include <kernel/syscall.h>
#include <drivers/vga.h>
#include <drivers/serial.h>
#include <multiboot.h>

static int name_ends_with_mke(const char *name)
{
    size_t n;
    if (!name)
        return 0;
    n = strlen(name);
    if (n < 4)
        return 0;
    return strcmp(name + n - 4, ".mke") == 0;
}

static void mke_attach_console(pid_t pid, const char *name, uint32_t spawn_flags)
{
    const mkdx_api_t *api = mkdx_api_get();
    int visible;

    if (!api || !api->console_alloc || pid <= 0)
        return;

    visible = (spawn_flags & SPAWN_CONSOLE_VISIBLE) ? 1 : 0;
    (void)api->console_alloc((int)pid, name, visible);
}

int mke_spawn_flags(const void *blob, size_t size, uint32_t spawn_flags,
                    const char *const *argv, int argc)
{
    const mke_header_t *hdr;
    const uint8_t *img;
    uint8_t *dst;
    uint32_t i;
    void (*entry)(void);
    pid_t pid;

    if (!blob || size < sizeof(mke_header_t)) {
        klog("[mke] spawn: bad blob\n");
        return -1;
    }

    hdr = (const mke_header_t *)blob;
    if (hdr->magic != MKE_MAGIC || hdr->version != MKE_VERSION) {
        klog("[mke] spawn: bad magic/version\n");
        return -1;
    }
    if (hdr->header_size != sizeof(mke_header_t)) {
        klog("[mke] spawn: bad header_size\n");
        return -1;
    }
    if (hdr->load_addr < MKE_LOAD_MIN || hdr->load_addr > MKE_LOAD_MAX) {
        klog("[mke] spawn: load_addr out of range ");
        serial_print_hex(hdr->load_addr);
        klog("\n");
        return -1;
    }
    if (hdr->image_size == 0) {
        klog("[mke] spawn: empty image\n");
        return -1;
    }
    if ((size_t)hdr->header_size + (size_t)hdr->image_size > size) {
        klog("[mke] spawn: image exceeds blob\n");
        return -1;
    }
    if (hdr->entry_off >= hdr->image_size + hdr->bss_size) {
        klog("[mke] spawn: bad entry_off\n");
        return -1;
    }
    /* Avoid wrap of load region */
    if (hdr->load_addr + hdr->image_size + hdr->bss_size < hdr->load_addr) {
        klog("[mke] spawn: load region wrap\n");
        return -1;
    }
    if (hdr->load_addr + hdr->image_size + hdr->bss_size > MKE_LOAD_MAX + 0x00800000u) {
        klog("[mke] spawn: load region too large\n");
        return -1;
    }

    klog("[mke] loading ");
    klog(hdr->name[0] ? hdr->name : "?");
    klog(" @ ");
    serial_print_hex(hdr->load_addr);
    klog(" img=");
    serial_print_uint(hdr->image_size);
    klog(" bss=");
    serial_print_uint(hdr->bss_size);
    klog("\n");

    img = (const uint8_t *)blob + hdr->header_size;
    dst = (uint8_t *)(uintptr_t)hdr->load_addr;

    for (i = 0; i < hdr->image_size; i++)
        dst[i] = img[i];
    for (i = 0; i < hdr->bss_size; i++)
        dst[hdr->image_size + i] = 0;

    entry = (void (*)(void))(uintptr_t)(hdr->load_addr + hdr->entry_off);
    pid = process_create_user(hdr->name[0] ? hdr->name : "mke", entry);
    if (pid < 0) {
        klog("[mke] process_create_user FAILED\n");
        vga_print("mke: process_create_user failed\n");
        return -1;
    }

    if (argv && argc > 0) {
        process_t *child = process_get(pid);
        if (child && argv_proc_set(child, argv, argc) < 0) {
            (void)process_kill(pid);
            return -EINVAL;
        }
    }

    klog("[mke] spawned ");
    klog(hdr->name);
    klog(" pid=");
    serial_print_uint((uint32_t)pid);
    klog(" entry=");
    serial_print_hex((uint32_t)(uintptr_t)entry);
    klog("\n");

    mke_attach_console(pid, hdr->name[0] ? hdr->name : "mke", spawn_flags);
    return pid;
}

int mke_spawn(const void *blob, size_t size)
{
    return mke_spawn_flags(blob, size, SPAWN_CONSOLE_HIDDEN, NULL, 0);
}

int mke_spawn_path_flags(const char *path, uint32_t spawn_flags,
                         const char *const *argv, int argc)
{
    int fd;
    off_t end;
    ssize_t total = 0;
    int rc;
    uint8_t *blob;

    if (!path || !path[0])
        return -EINVAL;

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

    blob = (uint8_t *)kmalloc((size_t)end);
    if (!blob) {
        (void)vfs_close(fd);
        return -ENOMEM;
    }

    while (total < end) {
        ssize_t n = vfs_read(fd, blob + total, (size_t)(end - total));
        if (n < 0) {
            rc = (int)n;
            (void)vfs_close(fd);
            kfree(blob);
            return rc;
        }
        if (n == 0)
            break;
        total += n;
    }

    (void)vfs_close(fd);
    if (total != end) {
        kfree(blob);
        return -EIO;
    }

    rc = mke_spawn_flags(blob, (size_t)end, spawn_flags, argv, argc);
    kfree(blob);
    return rc;
}

int mke_spawn_path(const char *path)
{
    return mke_spawn_path_flags(path, SPAWN_CONSOLE_HIDDEN, NULL, 0);
}

int mke_spawn_from_initrd(const void *data, size_t size)
{
    const initrd_header_t *hdr;
    uint32_t i;
    int spawned = 0;
    size_t table_bytes;

    if (!data || size < sizeof(uint32_t) * 2)
        return -1;

    hdr = (const initrd_header_t *)data;
    if (hdr->magic != INITRD_MAGIC || hdr->count == 0 ||
        hdr->count > INITRD_MAX_FILES)
        return -1;

    table_bytes = sizeof(uint32_t) * 2 + (size_t)hdr->count * sizeof(initrd_file_t);
    if (size < table_bytes)
        return -1;

    for (i = 0; i < hdr->count; i++) {
        const initrd_file_t *f = &hdr->files[i];
        const uint8_t *blob;
        const mke_header_t *mh;

        if (f->size == 0 || f->offset + f->size > size)
            return -1;
        blob = (const uint8_t *)data + f->offset;
        if (f->size < sizeof(mke_header_t))
            continue;
        mh = (const mke_header_t *)blob;
        if (mh->magic != MKE_MAGIC && !name_ends_with_mke(f->name))
            continue;
        klog("[mke] initrd file ");
        klog(f->name);
        klog(" size=");
        serial_print_uint(f->size);
        klog("\n");
        /* Soft-fail one app so others (and the scheduler) can still start. */
        if (mke_spawn(blob, f->size) < 0) {
            klog("[mke] spawn failed, skipping ");
            klog(f->name);
            klog("\n");
            continue;
        }
        spawned++;
    }

    klog_uint("[mke] initrd spawned count=", (uint32_t)spawned);
    return spawned > 0 ? 0 : -1;
}

int mke_spawn_from_mbi(multiboot_info_t *mbi)
{
    multiboot_mod_list_t *mods;
    uint32_t i;
    int any = 0;

    if (!mbi || !(mbi->flags & MULTIBOOT_INFO_MODS) || mbi->mods_count == 0)
        return -1;

    mods = (multiboot_mod_list_t *)(uintptr_t)mbi->mods_addr;
    for (i = 0; i < mbi->mods_count; i++) {
        const void *start = (const void *)(uintptr_t)mods[i].mod_start;
        size_t sz = (size_t)(mods[i].mod_end - mods[i].mod_start);
        const initrd_header_t *hdr;
        const mke_header_t *mh;

        if (sz < 4)
            continue;

        hdr = (const initrd_header_t *)start;
        if (hdr->magic == INITRD_MAGIC) {
            if (mke_spawn_from_initrd(start, sz) == 0)
                any = 1;
            continue;
        }

        mh = (const mke_header_t *)start;
        if (mh->magic == MKE_MAGIC) {
            if (mke_spawn(start, sz) < 0)
                return -1;
            any = 1;
        }
    }

    return any ? 0 : -1;
}
