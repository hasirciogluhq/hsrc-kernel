#include <kernel/userspace_boot.h>
#include <kernel/kshell.h>
#include <kernel/exec.h>
#include <kernel/vfs.h>
#include <kernel/initrd.h>
#include <kernel/initrd_store.h>
#include <kernel/string.h>
#include <kernel/disp_api.h>
#include <drivers/console/serial.h>
#include <drivers/console/vga.h>
#include <drivers/display/display.h>
#include <drivers/display/gpu.h>

#define USERSPACE_INIT_PATH "/init"

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

int gui_stack_ready(void)
{
    gpu_provider_ops_t *gpu = gpu_provider_active();
    /* display + disp_api + a provider that can scanout/submit. Unsupported
     * cmds (e.g. 3D without VirGL) fail at submit time — no boot gate. */
    return (display_active() && disp_api_get() && gpu) ? 1 : 0;
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

    rc = exec_resolve(USERSPACE_INIT_PATH, resolved, sizeof(resolved));
    if (rc < 0) {
        klog("[boot] /init not found — falling back to kshell\n");
        vga_print("no /init — console mode\n");
        kshell_start();
        return;
    }

    pid = exec_spawn_path(resolved);
    if (pid < 0) {
        klog("[boot] userspace_boot FAILED — kshell\n");
        vga_print("init spawn failed — console mode\n");
        kshell_start();
        return;
    }

    klog("[boot] /init pid=");
    serial_print_uint((uint32_t)pid);
    klog(" path=");
    klog(resolved);
    klog("\n");
}
