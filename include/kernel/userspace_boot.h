#ifndef KERNEL_USERSPACE_BOOT_H
#define KERNEL_USERSPACE_BOOT_H

/*
 * Hand off to /init when present. Kernel stays usable without GUI:
 * missing /init or failed spawn → kshell (console mode).
 */
void userspace_boot(void);

/* Non-zero when display driver + dx are available for a GUI session. */
int gui_stack_ready(void);

#endif
