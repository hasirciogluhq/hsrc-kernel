#ifndef KERNEL_USERSPACE_BOOT_H
#define KERNEL_USERSPACE_BOOT_H

/* Hand off to /init when present. */
void userspace_boot(void);

/* Non-zero when display + disp_api + gpu provider are up. */
int gui_stack_ready(void);

#endif
