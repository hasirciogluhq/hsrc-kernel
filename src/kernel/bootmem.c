#include <kernel/bootmem.h>
#include <kernel/mke.h>
#include <drivers/serial.h>

/*
 * Identity-mapped boot memory planner.
 *
 * Layout constraints:
 *   - Kernel image from 1MiB .. _kernel_end
 *   - Multiboot modules (initrd) wherever the loader placed them
 *   - .mke apps: [MKE_LOAD_MIN, MKE_LOAD_MAX + 8MiB)
 *   - Kernel heap: largest available span after the .mke window (fallback:
 *     classic 16–32MiB window below .mke when RAM is small)
 */

extern char _kernel_end[];

#define BOOTMEM_HEAP_MIN_BYTES  (1u * 1024u * 1024u)
#define BOOTMEM_MKE_END         (MKE_LOAD_MAX + 0x00800000u) /* 0x07800000 */
#define BOOTMEM_FALLBACK_PHYS   0x01000000u
#define BOOTMEM_FALLBACK_END    (MKE_LOAD_MIN - 0x10000u)
#define BOOTMEM_ADDR_MAX        0xFFFFF000u

static bootmem_layout_t g_bootmem;

static uint32_t align_up_u32(uint32_t v, uint32_t a)
{
    return (v + (a - 1u)) & ~(a - 1u);
}

static uint32_t align_down_u32(uint32_t v, uint32_t a)
{
    return v & ~(a - 1u);
}

static uint32_t clamp_u64_to_u32(uint64_t v)
{
    if (v > (uint64_t)BOOTMEM_ADDR_MAX)
        return BOOTMEM_ADDR_MAX;
    return (uint32_t)v;
}

static void bootmem_add_total(uint64_t *total, uint64_t len)
{
    uint64_t next = *total + len;
    if (next < *total)
        *total = ~0ull;
    else
        *total = next;
}

static uint32_t bootmem_modules_end(const multiboot_info_t *mbi)
{
    uint32_t end = 0;
    uint32_t i;

    if (!mbi || !(mbi->flags & MULTIBOOT_INFO_MODS) || !mbi->mods_count)
        return 0;

    {
        const multiboot_mod_list_t *mods =
            (const multiboot_mod_list_t *)(uintptr_t)mbi->mods_addr;

        for (i = 0; i < mbi->mods_count; i++) {
            if (mods[i].mod_end > end)
                end = mods[i].mod_end;
        }
    }
    return end;
}

static uint32_t bootmem_reserved_low_end(const multiboot_info_t *mbi)
{
    uint32_t end = (uint32_t)(uintptr_t)_kernel_end;
    uint32_t mod_end = bootmem_modules_end(mbi);

    if (end < 0x00100000u)
        end = 0x00100000u;
    if (mod_end > end)
        end = mod_end;
    return align_up_u32(end, 0x1000u);
}

/*
 * Clip [start, end) to the available Multiboot region; returns 1 if non-empty.
 * Without mmap, treat [1MiB, mem_top) as available (mem_upper fallback).
 */
static int bootmem_clip_available(const multiboot_info_t *mbi, uint32_t start,
                                  uint32_t end, uint32_t *out_start,
                                  uint32_t *out_end)
{
    uint32_t best_s = 0;
    uint32_t best_e = 0;
    uint32_t best_len = 0;

    if (!out_start || !out_end || end <= start)
        return 0;

    if (mbi && (mbi->flags & MULTIBOOT_INFO_MMAP) && mbi->mmap_length &&
        mbi->mmap_addr) {
        uint32_t off = 0;

        while (off < mbi->mmap_length) {
            const multiboot_mmap_entry_t *e =
                (const multiboot_mmap_entry_t *)(uintptr_t)(mbi->mmap_addr + off);
            uint32_t entry_size = e->size + sizeof(e->size);
            uint64_t a0, a1;
            uint32_t s, t, len;

            if (entry_size < sizeof(e->size) + 20u)
                entry_size = sizeof(multiboot_mmap_entry_t);

            if (e->type == MULTIBOOT_MEMORY_AVAILABLE && e->len != 0) {
                a0 = e->addr;
                a1 = e->addr + e->len;
                if (a1 > a0) {
                    if (a0 < (uint64_t)end && a1 > (uint64_t)start) {
                        s = (a0 > (uint64_t)start) ? clamp_u64_to_u32(a0) : start;
                        t = (a1 < (uint64_t)end) ? clamp_u64_to_u32(a1) : end;
                        if (t > s) {
                            len = t - s;
                            if (len > best_len) {
                                best_len = len;
                                best_s = s;
                                best_e = t;
                            }
                        }
                    }
                }
            }

            off += entry_size;
        }
    } else if (mbi && (mbi->flags & MULTIBOOT_INFO_MEMORY)) {
        uint64_t top64 = 0x100000ull + (uint64_t)mbi->mem_upper * 1024ull;
        uint32_t top = clamp_u64_to_u32(top64);
        uint32_t s = start < 0x100000u ? 0x100000u : start;
        uint32_t t = end < top ? end : top;

        if (t > s) {
            best_s = s;
            best_e = t;
            best_len = t - s;
        }
    }

    if (best_len == 0)
        return 0;
    *out_start = align_up_u32(best_s, 0x1000u);
    *out_end = align_down_u32(best_e, 0x1000u);
    return *out_end > *out_start;
}

static void bootmem_sum_available(const multiboot_info_t *mbi, uint64_t *total)
{
    *total = 0;

    if (mbi && (mbi->flags & MULTIBOOT_INFO_MMAP) && mbi->mmap_length &&
        mbi->mmap_addr) {
        uint32_t off = 0;

        while (off < mbi->mmap_length) {
            const multiboot_mmap_entry_t *e =
                (const multiboot_mmap_entry_t *)(uintptr_t)(mbi->mmap_addr + off);
            uint32_t entry_size = e->size + sizeof(e->size);

            if (entry_size < sizeof(e->size) + 20u)
                entry_size = sizeof(multiboot_mmap_entry_t);

            if (e->type == MULTIBOOT_MEMORY_AVAILABLE)
                bootmem_add_total(total, e->len);

            off += entry_size;
        }
        return;
    }

    if (mbi && (mbi->flags & MULTIBOOT_INFO_MEMORY)) {
        bootmem_add_total(total, (uint64_t)mbi->mem_lower * 1024ull);
        bootmem_add_total(total, (uint64_t)mbi->mem_upper * 1024ull);
    }
}

int bootmem_init(const multiboot_info_t *mbi, bootmem_layout_t *out)
{
    bootmem_layout_t layout;
    uint64_t total64 = 0;
    uint32_t reserved_low;
    uint32_t hs, he;
    uint32_t prefer_start;

    layout.total_ram_bytes = 0;
    layout.heap_phys = 0;
    layout.heap_size = 0;
    layout.phys_end = 0;

    bootmem_sum_available(mbi, &total64);
    if (total64 > (uint64_t)BOOTMEM_ADDR_MAX)
        layout.total_ram_bytes = BOOTMEM_ADDR_MAX;
    else
        layout.total_ram_bytes = (uint32_t)total64;

    reserved_low = bootmem_reserved_low_end(mbi);
    prefer_start = BOOTMEM_MKE_END;
    if (reserved_low > prefer_start)
        prefer_start = reserved_low;

    /* Prefer heap above the fixed .mke load window so apps keep their VA. */
    if (bootmem_clip_available(mbi, prefer_start, BOOTMEM_ADDR_MAX, &hs, &he) &&
        (he - hs) >= BOOTMEM_HEAP_MIN_BYTES) {
        layout.heap_phys = hs;
        layout.heap_size = he - hs;
        layout.phys_end = he;
    } else {
        uint32_t fb_s = BOOTMEM_FALLBACK_PHYS;
        uint32_t fb_e = BOOTMEM_FALLBACK_END;

        if (reserved_low > fb_s)
            fb_s = reserved_low;
        if (bootmem_clip_available(mbi, fb_s, fb_e, &hs, &he) && he > hs) {
            layout.heap_phys = hs;
            layout.heap_size = he - hs;
            layout.phys_end = he;
        }
    }

    if (layout.heap_size == 0) {
        /* Last resort: classic ~16MiB window (may be short on tiny VMs). */
        layout.heap_phys = BOOTMEM_FALLBACK_PHYS;
        layout.heap_size = BOOTMEM_FALLBACK_END - BOOTMEM_FALLBACK_PHYS;
        layout.phys_end = BOOTMEM_FALLBACK_END;
        if (layout.total_ram_bytes == 0)
            layout.total_ram_bytes = layout.heap_size;
    }

    if (layout.total_ram_bytes < layout.heap_size)
        layout.total_ram_bytes = layout.heap_size;

    g_bootmem = layout;
    if (out)
        *out = layout;

    klog("[bootmem] total_ram=");
    serial_print_uint(layout.total_ram_bytes);
    klog(" heap_phys=");
    serial_print_hex(layout.heap_phys);
    klog(" heap_size=");
    serial_print_uint(layout.heap_size);
    klog("\n");

    return layout.heap_size > 0 ? 0 : -1;
}

uint32_t bootmem_total_ram(void)
{
    return g_bootmem.total_ram_bytes;
}

uint32_t bootmem_heap_phys(void)
{
    return g_bootmem.heap_phys;
}

uint32_t bootmem_heap_size(void)
{
    return g_bootmem.heap_size;
}
