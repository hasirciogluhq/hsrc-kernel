#ifndef MYKERNEL_USERSPACE_BOOT_H
#define MYKERNEL_USERSPACE_BOOT_H

/*
 * Start the first userspace process (systemd) from disk.
 * Kernel main must not enumerate or spawn session apps.
 */
void userspace_boot(void);

#endif
