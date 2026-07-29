#include <kernel/userspace_boot.h>
#include <kernel/mke.h>
#include <kernel/vfs.h>
#include <kernel/initrd.h>
#include <kernel/initrd_store.h>
#include <kernel/string.h>
#include <drivers/serial.h>
#include <drivers/vga.h>

/* Conventional first userspace process path (Linux-like). */
#define USERSPACE_INIT_PATH "/init"

/*
 * Install PID1 at /init on the rootfs from the initrd entry named "init".
 * Custom images may already provide /init; we do not overwrite it.
 */
static int install_init_from_initrd(void)
{
    const initrd_header_t *hdr;
    size_t size = 0;
    size_t table_bytes;
    uint32_t i;
    const uint8_t *blob = NULL;
    size_t blob_sz = 0;
    int fd;
    size_t off;
    int probe;

    probe = vfs_open(USERSPACE_INIT_PATH, O_RDONLY);
    if (probe >= 0) {
        (void)vfs_close(probe);
        return 0;
    }

    hdr = (const initrd_header_t *)initrd_store_get(&size);
    if (!hdr || size < sizeof(uint32_t) * 2 || hdr->magic != INITRD_MAGIC ||
        hdr->count == 0 || hdr->count > INITRD_MAX_FILES)
        return -1;

    table_bytes = sizeof(uint32_t) * 2 + (size_t)hdr->count * sizeof(initrd_file_t);
    if (size < table_bytes)
        return -1;

    for (i = 0; i < hdr->count; i++) {
        const initrd_file_t *f = &hdr->files[i];
        if (strcmp(f->name, "init") != 0)
            continue;
        if (f->size == 0 || f->offset + f->size > size)
            return -1;
        blob = (const uint8_t *)hdr + f->offset;
        blob_sz = f->size;
        break;
    }
    if (!blob)
        return -1;

    fd = vfs_open(USERSPACE_INIT_PATH, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0)
        return fd;

    off = 0;
    while (off < blob_sz) {
        ssize_t n = vfs_write(fd, blob + off, blob_sz - off);
        if (n <= 0) {
            (void)vfs_close(fd);
            return n < 0 ? (int)n : -1;
        }
        off += (size_t)n;
    }
    (void)vfs_close(fd);
    klog("[boot] installed /init from initrd\n");
    return 0;
}

void userspace_boot(void)
{
    char resolved[VFS_PATH_MAX];
    int pid;
    int rc;

    klog("[boot] starting userspace: ");
    klog(USERSPACE_INIT_PATH);
    klog("\n");

    if (install_init_from_initrd() < 0)
        klog("[boot] warning: could not install /init from initrd\n");

    rc = exe_resolve(USERSPACE_INIT_PATH, resolved, sizeof(resolved));
    if (rc < 0) {
        klog("[boot] /init not found\n");
        vga_print("userspace boot failed (/init)\n");
        return;
    }

    pid = mke_spawn_path(resolved);
    if (pid < 0) {
        klog("[boot] userspace_boot FAILED\n");
        vga_print("userspace boot failed (/init)\n");
        return;
    }

    klog("[boot] /init pid=");
    serial_print_uint((uint32_t)pid);
    klog(" path=");
    klog(resolved);
    klog("\n");
}
