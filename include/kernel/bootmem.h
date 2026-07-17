#ifndef MYKERNEL_BOOTMEM_H
#define MYKERNEL_BOOTMEM_H

#include <kernel/types.h>
#include <multiboot.h>

typedef struct bootmem_layout {
    uint32_t total_ram_bytes; /* available RAM from Multiboot (≤4GiB) */
    uint32_t heap_phys;
    uint32_t heap_size;
    uint32_t phys_end;        /* exclusive end of chosen heap span */
} bootmem_layout_t;

/*
 * Parse Multiboot memory info and pick a heap region that does not collide
 * with the kernel, initrd modules, or the fixed .mke load window.
 */
int bootmem_init(const multiboot_info_t *mbi, bootmem_layout_t *out);

uint32_t bootmem_total_ram(void);
uint32_t bootmem_heap_phys(void);
uint32_t bootmem_heap_size(void);

#endif
