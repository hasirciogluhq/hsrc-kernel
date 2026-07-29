#ifndef HSRC_KERNEL_KSHELL_H
#define HSRC_KERNEL_KSHELL_H

/* Kernel console shell — used when GUI stack is unavailable. */
void kshell_run(void);
void kshell_start(void); /* spawn as kernel process */

#endif
