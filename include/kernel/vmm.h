#ifndef KERNEL_VMM_H
#define KERNEL_VMM_H

#include <kernel/types.h>
#include <kernel/klock.h>

/*
 * i386 paging MVP — per-process address spaces.
 *
 * Kernel: identity-map physical RAM (and on-demand MMIO) with U=0 in a
 * shared set of page tables cloned into every addrspace.
 * User: private 4KiB mappings at canonical VAs (see USER_* below).
 *
 * Lock order: g_vmm_lock nests under g_proc_lock when both are held
 * (acquire g_proc_lock first). Never sleep while holding g_vmm_lock.
 */

#define USER_IMAGE_BASE   0x00400000u
#define USER_HEAP_BASE    0x10000000u
#define USER_STACK_TOP    0xBFC00000u /* exclusive top; grows down */

#define VMM_PRESENT  0x001u
#define VMM_WRITE    0x002u
#define VMM_USER     0x004u

typedef struct addrspace {
    uint32_t *pd;       /* page-directory VA (== PA; identity) */
    uint32_t  pd_phys;  /* CR3 value */
    int       refcnt;
} addrspace_t;

extern spinlock_t g_vmm_lock;

/* After heap_init: build kernel identity map and enable paging. */
void vmm_bootstrap(uint32_t phys_end);

addrspace_t *vmm_kernel_as(void);
addrspace_t *addrspace_create(void); /* clone kernel maps; caller owns */
void         addrspace_destroy(addrspace_t *as);
void         addrspace_switch(addrspace_t *as);

/*
 * Map npages starting at `va` → `pa` (both page-aligned).
 * flags: VMM_PRESENT | VMM_WRITE | VMM_USER as needed.
 */
int vmm_map_pages(addrspace_t *as, uint32_t va, uint32_t pa, size_t npages,
                  uint32_t flags);
int vmm_unmap_pages(addrspace_t *as, uint32_t va, size_t npages);

/* Ensure [pa, pa+len) is identity-mapped in the shared kernel tables (U=0). */
int vmm_identity_map_range(uint32_t pa, size_t len);

uint32_t vmm_va_to_pa(addrspace_t *as, uint32_t va);

/* Map kernel buffer pages into `as` at identity VA with U=1 (Reed textures). */
int vmm_map_buf_user(addrspace_t *as, void *ptr, size_t len);

#endif
