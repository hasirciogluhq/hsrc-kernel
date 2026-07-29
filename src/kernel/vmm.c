#include <kernel/vmm.h>
#include <kernel/heap.h>
#include <kernel/mm.h>
#include <kernel/string.h>
#include <drivers/console/serial.h>

/*
 * Page-directory / page-table helpers (4KiB pages).
 * PDE/PTE: P=1 W=2 U=4; bits 12..31 = phys>>12.
 *
 * g_vmm_lock protects PD/PT mutations and g_kernel_as publication.
 * Contract: short sections only; no sleep; IRQ paths use irqsave.
 */

#define PTE_ADDR_MASK 0xFFFFF000u
#define PDE_INDEX(va) (((va) >> 22) & 0x3FFu)
#define PTE_INDEX(va) (((va) >> 12) & 0x3FFu)

spinlock_t g_vmm_lock = {0};

static addrspace_t g_kernel_as;
static int g_vmm_ready;

static void invlpg(uint32_t va)
{
    __asm__ volatile("invlpg (%0)" ::"r"(va) : "memory");
}

static void load_cr3(uint32_t phys)
{
    __asm__ volatile("mov %0, %%cr3" ::"r"(phys) : "memory");
}

static uint32_t read_cr0(void)
{
    uint32_t v;
    __asm__ volatile("mov %%cr0, %0" : "=r"(v));
    return v;
}

static void write_cr0(uint32_t v)
{
    __asm__ volatile("mov %0, %%cr0" ::"r"(v) : "memory");
}

static uint32_t *alloc_page_table(void)
{
    void *p = kmalloc_aligned(PAGE_SIZE, PAGE_SIZE);
    if (!p)
        return NULL;
    memset(p, 0, PAGE_SIZE);
    return (uint32_t *)p;
}

static uint32_t *pt_for_pde(uint32_t *pd, uint32_t pdi, int create)
{
    uint32_t pde = pd[pdi];
    uint32_t *pt;

    if (pde & VMM_PRESENT) {
        pt = (uint32_t *)(uintptr_t)(pde & PTE_ADDR_MASK);
        return pt;
    }
    if (!create)
        return NULL;
    pt = alloc_page_table();
    if (!pt)
        return NULL;
    /* Shareable kernel PT: present + writable, supervisor. */
    pd[pdi] = ((uint32_t)(uintptr_t)pt & PTE_ADDR_MASK) | VMM_PRESENT | VMM_WRITE;
    return pt;
}

static int map_one(uint32_t *pd, uint32_t va, uint32_t pa, uint32_t flags)
{
    uint32_t pdi = PDE_INDEX(va);
    uint32_t pti = PTE_INDEX(va);
    uint32_t *pt = pt_for_pde(pd, pdi, 1);
    uint32_t f = flags | VMM_PRESENT;

    if (!pt)
        return -1;
    /* User mappings need U on both PDE and PTE. */
    if (f & VMM_USER)
        pd[pdi] |= VMM_USER;
    pt[pti] = (pa & PTE_ADDR_MASK) | f;
    return 0;
}

static int unmap_one(uint32_t *pd, uint32_t va)
{
    uint32_t pdi = PDE_INDEX(va);
    uint32_t pti = PTE_INDEX(va);
    uint32_t *pt;
    uint32_t pde = pd[pdi];

    if (!(pde & VMM_PRESENT))
        return 0;
    pt = (uint32_t *)(uintptr_t)(pde & PTE_ADDR_MASK);
    pt[pti] = 0;
    return 0;
}

static int identity_map_range_locked(uint32_t *pd, uint32_t start, uint32_t end,
                                     uint32_t flags)
{
    uint32_t va;

    start &= PTE_ADDR_MASK;
    end = (end + PAGE_SIZE - 1u) & PTE_ADDR_MASK;
    if (end < start)
        return -1;
    for (va = start; va < end; va += PAGE_SIZE) {
        if (map_one(pd, va, va, flags) < 0)
            return -1;
    }
    return 0;
}

void vmm_bootstrap(uint32_t phys_end)
{
    uint32_t *pd;
    uint32_t map_end;
    uint32_t cr0;

    if (g_vmm_ready)
        return;

    if (phys_end < 0x01000000u)
        phys_end = 0x01000000u;
    /* Cover RAM; round up to page. Leave headroom for late MMIO via
     * vmm_identity_map_range. */
    map_end = (phys_end + PAGE_SIZE - 1u) & PTE_ADDR_MASK;
    if (map_end < phys_end)
        map_end = 0xFFFFF000u;

    pd = alloc_page_table();
    if (!pd) {
        klog("[vmm] FATAL: no page for PD\n");
        for (;;)
            __asm__ volatile("hlt");
    }

    if (identity_map_range_locked(pd, 0, map_end, VMM_WRITE) < 0) {
        klog("[vmm] FATAL: identity map failed\n");
        for (;;)
            __asm__ volatile("hlt");
    }

    g_kernel_as.pd = pd;
    g_kernel_as.pd_phys = (uint32_t)(uintptr_t)pd;
    g_kernel_as.refcnt = 1;

    load_cr3(g_kernel_as.pd_phys);
    cr0 = read_cr0();
    cr0 |= 0x80000000u; /* PG */
    write_cr0(cr0);

    g_vmm_ready = 1;
    klog("[vmm] paging on identity_end=");
    serial_print_hex(map_end);
    klog("\n");
}

addrspace_t *vmm_kernel_as(void)
{
    return g_vmm_ready ? &g_kernel_as : NULL;
}

addrspace_t *addrspace_create(void)
{
    addrspace_t *as;
    uint32_t *pd;
    uint32_t irqf;
    int i;

    if (!g_vmm_ready)
        return NULL;

    as = (addrspace_t *)kmalloc(sizeof(*as));
    pd = alloc_page_table();
    if (!as || !pd) {
        kfree(as);
        kfree(pd);
        return NULL;
    }
    memset(as, 0, sizeof(*as));
    memset(pd, 0, PAGE_SIZE);

    irqf = spin_lock_irqsave(&g_vmm_lock);
    /*
     * Deep-copy kernel PTs so later U=1 upgrades (dynlib) and user PDE
     * replacements cannot mutate the shared kernel page tables.
     */
    for (i = 0; i < 1024; i++) {
        uint32_t kpde = g_kernel_as.pd[i];
        uint32_t *kpt, *pt;
        int j;

        if (!(kpde & VMM_PRESENT))
            continue;
        kpt = (uint32_t *)(uintptr_t)(kpde & PTE_ADDR_MASK);
        pt = alloc_page_table();
        if (!pt) {
            spin_unlock_irqrestore(&g_vmm_lock, irqf);
            /* unwind */
            for (j = 0; j < i; j++) {
                if (pd[j] & VMM_PRESENT)
                    kfree((void *)(uintptr_t)(pd[j] & PTE_ADDR_MASK));
            }
            kfree(pd);
            kfree(as);
            return NULL;
        }
        memcpy(pt, kpt, PAGE_SIZE);
        pd[i] = ((uint32_t)(uintptr_t)pt & PTE_ADDR_MASK) |
                (kpde & (VMM_PRESENT | VMM_WRITE | VMM_USER));
    }
    as->pd = pd;
    as->pd_phys = (uint32_t)(uintptr_t)pd;
    as->refcnt = 1;
    spin_unlock_irqrestore(&g_vmm_lock, irqf);
    return as;
}

void addrspace_destroy(addrspace_t *as)
{
    uint32_t irqf;
    int i;

    if (!as || as == &g_kernel_as)
        return;

    irqf = spin_lock_irqsave(&g_vmm_lock);
    for (i = 0; i < 1024; i++) {
        uint32_t pde = as->pd[i];
        uint32_t *pt;

        if (!(pde & VMM_PRESENT))
            continue;
        pt = (uint32_t *)(uintptr_t)(pde & PTE_ADDR_MASK);
        as->pd[i] = 0;
        kfree(pt);
    }
    kfree(as->pd);
    as->pd = NULL;
    spin_unlock_irqrestore(&g_vmm_lock, irqf);
    kfree(as);
}

void addrspace_switch(addrspace_t *as)
{
    if (!as || !as->pd_phys)
        return;
    load_cr3(as->pd_phys);
}

int vmm_map_pages(addrspace_t *as, uint32_t va, uint32_t pa, size_t npages,
                  uint32_t flags)
{
    uint32_t irqf;
    size_t i;
    int rc = 0;

    if (!as || !as->pd || npages == 0)
        return -1;
    if ((va | pa) & (PAGE_SIZE - 1u))
        return -1;

    irqf = spin_lock_irqsave(&g_vmm_lock);
    for (i = 0; i < npages; i++) {
        uint32_t v = va + (uint32_t)i * PAGE_SIZE;
        uint32_t p = pa + (uint32_t)i * PAGE_SIZE;
        if (map_one(as->pd, v, p, flags) < 0) {
            rc = -1;
            break;
        }
        invlpg(v);
    }
    spin_unlock_irqrestore(&g_vmm_lock, irqf);
    return rc;
}

int vmm_unmap_pages(addrspace_t *as, uint32_t va, size_t npages)
{
    uint32_t irqf;
    size_t i;

    if (!as || !as->pd || npages == 0)
        return -1;
    if (va & (PAGE_SIZE - 1u))
        return -1;

    irqf = spin_lock_irqsave(&g_vmm_lock);
    for (i = 0; i < npages; i++) {
        uint32_t v = va + (uint32_t)i * PAGE_SIZE;
        unmap_one(as->pd, v);
        invlpg(v);
    }
    spin_unlock_irqrestore(&g_vmm_lock, irqf);
    return 0;
}

int vmm_identity_map_range(uint32_t pa, size_t len)
{
    uint32_t irqf;
    uint32_t start, end;
    int rc;

    if (!g_vmm_ready || len == 0)
        return -1;
    start = pa & PTE_ADDR_MASK;
    end = (pa + (uint32_t)len + PAGE_SIZE - 1u) & PTE_ADDR_MASK;
    if (end < start)
        return -1;

    irqf = spin_lock_irqsave(&g_vmm_lock);
    rc = identity_map_range_locked(g_kernel_as.pd, start, end, VMM_WRITE);
    /*
     * Existing process ASes cloned earlier won't see new kernel PDEs unless
     * we also patch them. Walk is expensive; for MVP map into kernel PD and
     * rely on late BAR maps happening before heavy user spawn, plus patch
     * empty PDE slots in note — callers that need live AS update should
     * recreate or we copy new PDEs into all (skipped for MVP if drivers
     * init before /init).
     */
    spin_unlock_irqrestore(&g_vmm_lock, irqf);
    return rc;
}

uint32_t vmm_va_to_pa(addrspace_t *as, uint32_t va)
{
    uint32_t irqf;
    uint32_t pde, pte, *pt, pa = 0;

    if (!as || !as->pd)
        return 0;
    irqf = spin_lock_irqsave(&g_vmm_lock);
    pde = as->pd[PDE_INDEX(va)];
    if (pde & VMM_PRESENT) {
        pt = (uint32_t *)(uintptr_t)(pde & PTE_ADDR_MASK);
        pte = pt[PTE_INDEX(va)];
        if (pte & VMM_PRESENT)
            pa = (pte & PTE_ADDR_MASK) | (va & (PAGE_SIZE - 1u));
    }
    spin_unlock_irqrestore(&g_vmm_lock, irqf);
    return pa;
}

int vmm_map_buf_user(addrspace_t *as, void *ptr, size_t len)
{
    uint32_t start, end, pa;
    size_t npages;

    if (!as || !ptr || len == 0)
        return -1;
    start = (uint32_t)(uintptr_t)ptr & PTE_ADDR_MASK;
    end = ((uint32_t)(uintptr_t)ptr + (uint32_t)len + PAGE_SIZE - 1u) &
          PTE_ADDR_MASK;
    if (end <= start)
        return -1;
    npages = (end - start) / PAGE_SIZE;
    pa = start;
    return vmm_map_pages(as, start, pa, npages, VMM_WRITE | VMM_USER);
}
