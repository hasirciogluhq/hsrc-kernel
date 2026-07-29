#include <kernel/mm.h>
#include <kernel/heap.h>
#include <kernel/process.h>
#include <kernel/vfs.h>
#include <kernel/errno.h>
#include <kernel/string.h>
#include <kernel/vmm.h>

/*
 * process_t::vmas[] is shared mutable state: the owning process can call
 * mmap/munmap/msync concurrently with other processes/CPUs poking the same
 * table via ReadProcessMemory/WriteProcessMemory/VirtualAlloc(Ex) (see
 * proc_mem.c). All find-slot / publish / clear sequences below run under
 * g_proc_lock (process_table_lock_irqsave) — never sleep while holding it;
 * file I/O and page alloc/free happen outside the critical section.
 *
 * Backing pages are physical (kmalloc_aligned); user VA is mapped through
 * the process addrspace. Kernel accesses backing via the physical pointer.
 */

void mm_init(void)
{
}

void *mm_alloc_pages(size_t npages)
{
    if (npages == 0)
        return NULL;
    return kmalloc_aligned(npages * PAGE_SIZE, PAGE_SIZE);
}

void mm_free_pages(void *ptr, size_t npages)
{
    (void)npages;
    kfree(ptr);
}

static vma_t *proc_vmas(process_t *p)
{
    p = process_leader(p);
    return p ? p->vmas : NULL;
}

static uint32_t mm_pick_va(process_t *lead, size_t bytes)
{
    uint32_t cand = USER_HEAP_BASE;
    size_t i;

    /* First-fit above USER_HEAP_BASE, avoiding existing VMAs. */
    for (;;) {
        int clash = 0;
        uint32_t end = cand + (uint32_t)bytes;

        if (end < cand || end >= USER_STACK_TOP - (8u * 1024u * 1024u))
            return 0;
        for (i = 0; i < VMA_MAX; i++) {
            if (!lead->vmas[i].used)
                continue;
            if (cand < lead->vmas[i].end && end > lead->vmas[i].start) {
                cand = lead->vmas[i].end;
                clash = 1;
                break;
            }
        }
        if (!clash)
            return cand;
    }
}

long mm_mmap(process_t *p, uint32_t addr, size_t len, int prot, int flags,
             int vfs_fd, off_t off)
{
    vma_t *vmas;
    process_t *lead;
    size_t npages, i;
    void *pages;
    uint32_t start;
    uint32_t pa;
    int slot = -1;
    uint32_t map_flags = VMM_USER;

    if (!p || len == 0)
        return -EINVAL;
    lead = process_leader(p);
    vmas = proc_vmas(p);
    if (!vmas || !lead || !lead->as)
        return -ENOMEM;

    npages = (len + PAGE_SIZE - 1) / PAGE_SIZE;
    pages = mm_alloc_pages(npages);
    if (!pages)
        return -ENOMEM;
    memset(pages, 0, npages * PAGE_SIZE);

    if (vfs_fd >= 0) {
        size_t got = 0;
        if (vfs_lseek(vfs_fd, off, SEEK_SET) < 0) {
            mm_free_pages(pages, npages);
            return -EINVAL;
        }
        while (got < len) {
            ssize_t n = vfs_read(vfs_fd, (uint8_t *)pages + got, len - got);
            if (n < 0) {
                mm_free_pages(pages, npages);
                return n;
            }
            if (n == 0)
                break;
            got += (size_t)n;
        }
    }

    if ((flags & MAP_FIXED) && addr) {
        start = addr & ~(PAGE_SIZE - 1u);
    } else {
        start = mm_pick_va(lead, npages * PAGE_SIZE);
        if (!start) {
            mm_free_pages(pages, npages);
            return -ENOMEM;
        }
    }

    if (prot & PROT_WRITE)
        map_flags |= VMM_WRITE;

    pa = (uint32_t)(uintptr_t)pages;
    if (vmm_map_pages(lead->as, start, pa, npages, map_flags) < 0) {
        mm_free_pages(pages, npages);
        return -ENOMEM;
    }

    {
        uint32_t irqf = process_table_lock_irqsave();
        for (i = 0; i < VMA_MAX; i++) {
            if (!vmas[i].used) {
                slot = (int)i;
                break;
            }
        }
        if (slot < 0) {
            process_table_unlock_irqrestore(irqf);
            vmm_unmap_pages(lead->as, start, npages);
            mm_free_pages(pages, npages);
            return -ENOMEM;
        }

        vmas[slot].start = start;
        vmas[slot].end = start + (uint32_t)(npages * PAGE_SIZE);
        vmas[slot].offset = (uint32_t)off;
        vmas[slot].prot = prot;
        vmas[slot].flags = flags;
        vmas[slot].fd = vfs_fd;
        vmas[slot].pages = pages;
        vmas[slot].npages = npages;
        vmas[slot].used = 1;
        process_table_unlock_irqrestore(irqf);
    }

    return (long)start;
}

int mm_munmap(process_t *p, uint32_t addr, size_t len)
{
    vma_t *vmas;
    process_t *lead;
    vma_t snap;
    size_t i;
    int found = 0;
    uint32_t irqf;
    (void)len;
    if (!p)
        return -EINVAL;
    lead = process_leader(p);
    vmas = proc_vmas(p);
    if (!vmas || !lead)
        return -ENOMEM;

    irqf = process_table_lock_irqsave();
    for (i = 0; i < VMA_MAX; i++) {
        if (!vmas[i].used)
            continue;
        if (vmas[i].start == addr) {
            snap = vmas[i];
            memset(&vmas[i], 0, sizeof(vmas[i]));
            found = 1;
            break;
        }
    }
    process_table_unlock_irqrestore(irqf);

    if (!found)
        return -EINVAL;

    if (lead->as)
        vmm_unmap_pages(lead->as, snap.start, snap.npages);

    if ((snap.prot & PROT_WRITE) && snap.fd >= 0) {
        vfs_lseek(snap.fd, (off_t)snap.offset, SEEK_SET);
        vfs_write(snap.fd, snap.pages, snap.npages * PAGE_SIZE);
    }
    mm_free_pages(snap.pages, snap.npages);
    return 0;
}

int mm_msync(process_t *p, uint32_t addr, size_t len, int flags)
{
    vma_t *vmas;
    vma_t snap;
    size_t i;
    int found = 0;
    uint32_t irqf;
    (void)flags;
    if (!p)
        return -EINVAL;
    vmas = proc_vmas(p);
    if (!vmas)
        return -ENOMEM;

    irqf = process_table_lock_irqsave();
    for (i = 0; i < VMA_MAX; i++) {
        if (!vmas[i].used)
            continue;
        if (addr >= vmas[i].start && addr < vmas[i].end) {
            snap = vmas[i];
            found = 1;
            break;
        }
    }
    process_table_unlock_irqrestore(irqf);

    if (!found)
        return -EINVAL;

    if ((snap.prot & PROT_WRITE) && snap.fd >= 0) {
        size_t n = len ? len : (snap.npages * PAGE_SIZE);
        vfs_lseek(snap.fd, (off_t)snap.offset, SEEK_SET);
        vfs_write(snap.fd, snap.pages, n);
    }
    return 0;
}
