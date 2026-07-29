#include <kernel/userspace_boot.h>
#include <kernel/mke.h>
#include <kernel/vfs.h>
#include <kernel/string.h>
#include <drivers/serial.h>
#include <drivers/vga.h>

/* Conventional first userspace process path. OS/user places the binary on disk;
 * typically /applications/init.mke (resolved via exe_resolve). */
#define USERSPACE_INIT_PATH "/init"

void userspace_boot(void)
{
    char resolved[VFS_PATH_MAX];
    int pid;
    int rc;

    klog("[boot] starting userspace: ");
    klog(USERSPACE_INIT_PATH);
    klog("\n");

    rc = exe_resolve(USERSPACE_INIT_PATH, resolved, sizeof(resolved));
    if (rc < 0) {
        klog("[boot] /init not found on disk\n");
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
