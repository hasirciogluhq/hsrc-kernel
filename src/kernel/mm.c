#include <kernel/mm.h>
#include <kernel/heap.h>
#include <kernel/process.h>
#include <kernel/vfs.h>
#include <kernel/errno.h>
#include <kernel/string.h>

/*
 * process_t::vmas[] is shared mutable state: the owning process can call
 * mmap/munmap/msync concurrently with other processes/CPUs poking the same
 * table via ReadProcessMemory/WriteProcessMemory/VirtualAlloc(Ex) (see
 * proc_mem.c). All find-slot / publish / clear sequences below run under
 * g_proc_lock (process_table_lock_irqsave) — never sleep while holding it;
 * file I/O and page alloc/free happen outside the critical section.
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

long mm_mmap(process_t *p, uint32_t addr, size_t len, int prot, int flags,
             int vfs_fd, off_t off)
{
    vma_t *vmas;
    size_t npages, i;
    void *pages;
    uint32_t start;
    int slot = -1;

    (void)flags;
    if (!p || len == 0)
        return -EINVAL;
    vmas = proc_vmas(p);
    if (!vmas)
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

    /*
     * Identity AS: VA == backing pointer. Ignore MAP_FIXED hints that cannot
     * be honored without a real page table; always publish pages as start.
     */
    (void)addr;
    start = (uint32_t)(uintptr_t)pages;

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
        /* Publish last: readers gate on `used` (release semantics on x86
         * TSO — a plain store here is ordered after the field writes above
         * because stores are not reordered with later stores). */
        vmas[slot].used = 1;
        process_table_unlock_irqrestore(irqf);
    }

    return (long)(uintptr_t)pages;
}

int mm_munmap(process_t *p, uint32_t addr, size_t len)
{
    vma_t *vmas;
    vma_t snap;
    size_t i;
    int found = 0;
    uint32_t irqf;
    (void)len;
    if (!p)
        return -EINVAL;
    vmas = proc_vmas(p);
    if (!vmas)
        return -ENOMEM;

    irqf = process_table_lock_irqsave();
    for (i = 0; i < VMA_MAX; i++) {
        if (!vmas[i].used)
            continue;
        if ((uint32_t)(uintptr_t)vmas[i].pages == addr || vmas[i].start == addr) {
            snap = vmas[i];
            memset(&vmas[i], 0, sizeof(vmas[i]));
            found = 1;
            break;
        }
    }
    process_table_unlock_irqrestore(irqf);

    if (!found)
        return -EINVAL;

    /* Slot already unpublished; file I/O and free happen lock-free. */
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
        if ((uint32_t)(uintptr_t)vmas[i].pages == addr ||
            (addr >= vmas[i].start && addr < vmas[i].end)) {
            snap = vmas[i];
            found = 1;
            break;
        }
    }
    process_table_unlock_irqrestore(irqf);

    if (!found)
        return -EINVAL;

    /*
     * Best-effort write-back below the lock: another CPU could munmap the
     * same VMA concurrently and free snap.pages out from under us. This
     * mirrors the original (already racy) behavior; a fully safe version
     * would need per-VMA refcounting, tracked separately.
     */
    if ((snap.prot & PROT_WRITE) && snap.fd >= 0) {
        size_t n = len ? len : (snap.npages * PAGE_SIZE);
        vfs_lseek(snap.fd, (off_t)snap.offset, SEEK_SET);
        vfs_write(snap.fd, snap.pages, n);
    }
    return 0;
}
