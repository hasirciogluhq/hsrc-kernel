#include <kernel/heap.h>
#include <kernel/spinlock.h>

/*
 * Freelist + wilderness (bump) heap.
 *
 * Do NOT park the entire arena in one free-list node at init: a single
 * metadata smash after the first allocation would lose hundreds of MiB.
 * Fresh memory comes from the wilderness; the freelist only holds blocks
 * returned by kfree (address-ordered, coalesced).
 *
 * Normal allocations: header immediately before the returned pointer.
 * Stronger alignment: over-allocate, align the user pointer, tag header
 * with a stash of the real payload so kfree can recover it.
 */

typedef struct heap_block {
    size_t size; /* total bytes including this header */
    uint32_t magic;
    struct heap_block *next; /* freelist when FREE */
} heap_block_t;

#define HEAP_MAGIC_USED  0xA110C001u
#define HEAP_MAGIC_FREE  0xF4EEB10Cu
#define HEAP_MAGIC_ALIGN 0xA11CA7EDu
#define HEAP_HDR_SIZE    ((sizeof(heap_block_t) + 15u) & ~15u)
#define HEAP_MIN_BLOCK   (HEAP_HDR_SIZE + 16u)

static uint8_t *heap_base;
static uint8_t *heap_end;
static uint8_t *wilderness; /* next unused byte; grows toward heap_end */
static heap_block_t *free_list;
static size_t g_used;
static spinlock_t g_heap_lock;

static size_t align_up_sz(size_t n, size_t align)
{
    return (n + (align - 1u)) & ~(align - 1u);
}

static int block_in_heap(const heap_block_t *b)
{
    const uint8_t *p = (const uint8_t *)b;
    return p >= heap_base && (uint8_t *)b + HEAP_HDR_SIZE <= heap_end;
}

static int size_add_ok(size_t a, size_t b, size_t *out)
{
    if (a > (size_t)-1 - b)
        return 0;
    *out = a + b;
    return 1;
}

/* Merge b into wilderness if it sits immediately below the bump pointer. */
static int try_return_wilderness(heap_block_t *b)
{
    uint8_t *end;

    if (!b || b->size < HEAP_HDR_SIZE)
        return 0;
    end = (uint8_t *)b + b->size;
    if (end != wilderness)
        return 0;
    if ((uint8_t *)b < heap_base)
        return 0;
    wilderness = (uint8_t *)b;
    return 1;
}

static void freelist_insert(heap_block_t *b)
{
    heap_block_t **pp;
    heap_block_t *prev;
    heap_block_t *n;
    size_t sum;

    if (try_return_wilderness(b))
        return;

    b->magic = HEAP_MAGIC_FREE;
    b->next = NULL;

    pp = &free_list;
    while (*pp && (uintptr_t)*pp < (uintptr_t)b)
        pp = &(*pp)->next;

    b->next = *pp;
    *pp = b;

    if (b->next && (uint8_t *)b + b->size == (uint8_t *)b->next) {
        n = b->next;
        if (size_add_ok(b->size, n->size, &sum)) {
            b->size = sum;
            b->next = n->next;
        }
    }

    if (try_return_wilderness(b)) {
        /* Detach from freelist — now part of wilderness. */
        pp = &free_list;
        while (*pp) {
            if (*pp == b) {
                *pp = b->next;
                break;
            }
            pp = &(*pp)->next;
        }
        return;
    }

    prev = NULL;
    for (n = free_list; n && n != b; n = n->next)
        prev = n;
    if (prev && (uint8_t *)prev + prev->size == (uint8_t *)b) {
        if (size_add_ok(prev->size, b->size, &sum)) {
            prev->size = sum;
            prev->next = b->next;
            if (try_return_wilderness(prev)) {
                pp = &free_list;
                while (*pp) {
                    if (*pp == prev) {
                        *pp = prev->next;
                        break;
                    }
                    pp = &(*pp)->next;
                }
            }
        }
    }
}

static void freelist_remove(heap_block_t *b)
{
    heap_block_t **pp = &free_list;
    while (*pp) {
        if (*pp == b) {
            *pp = b->next;
            b->next = NULL;
            return;
        }
        pp = &(*pp)->next;
    }
}

static void split_tail(heap_block_t *b, size_t keep)
{
    heap_block_t *tail;
    size_t left;

    keep = align_up_sz(keep, 16u);
    if (keep < HEAP_HDR_SIZE)
        keep = HEAP_HDR_SIZE;
    if (b->size < keep + HEAP_MIN_BLOCK)
        return;

    left = b->size - keep;
    tail = (heap_block_t *)((uint8_t *)b + keep);
    tail->size = left;
    b->size = keep;
    freelist_insert(tail);
}

static void *bump_locked(size_t need)
{
    heap_block_t *b;
    uint8_t *next;

    need = align_up_sz(need, 16u);
    if (wilderness + need < wilderness || wilderness + need > heap_end)
        return NULL;

    b = (heap_block_t *)wilderness;
    next = wilderness + need;
    wilderness = next;

    b->size = need;
    b->magic = HEAP_MAGIC_USED;
    b->next = NULL;
    g_used += need;
    return (uint8_t *)b + HEAP_HDR_SIZE;
}

/* Allocate `size` payload bytes. Blocks stay 16-aligned so payloads are too. */
static void *alloc_raw_locked(size_t size)
{
    heap_block_t *b;
    size_t need;
    void *data;

    size = align_up_sz(size, 16u);
    need = HEAP_HDR_SIZE + size;
    need = align_up_sz(need, 16u);

    for (b = free_list; b; b = b->next) {
        if (b->magic != HEAP_MAGIC_FREE || !block_in_heap(b))
            continue;
        if (b->size < need)
            continue;
        if (((uintptr_t)b) & 15u)
            continue;
        if ((uint8_t *)b + b->size > heap_end ||
            (uint8_t *)b + b->size < (uint8_t *)b)
            continue;

        freelist_remove(b);
        split_tail(b, need);
        b->magic = HEAP_MAGIC_USED;
        b->next = NULL;
        g_used += b->size;
        data = (uint8_t *)b + HEAP_HDR_SIZE;
        return data;
    }

    return bump_locked(need);
}

static void free_raw_locked(void *ptr)
{
    heap_block_t *blk;

    if (!ptr)
        return;

    blk = (heap_block_t *)((uint8_t *)ptr - HEAP_HDR_SIZE);
    if (!block_in_heap(blk) || blk->magic != HEAP_MAGIC_USED ||
        blk->size < HEAP_HDR_SIZE ||
        (uint8_t *)blk + blk->size > heap_end ||
        (uint8_t *)blk + blk->size < (uint8_t *)blk)
        return;

    if (g_used >= blk->size)
        g_used -= blk->size;
    else
        g_used = 0;

    freelist_insert(blk);
}

void heap_init(void *start, size_t size)
{
    heap_base = (uint8_t *)start;
    heap_end = heap_base + size;
    wilderness = heap_base;
    free_list = NULL;
    g_used = 0;
    spin_init(&g_heap_lock);

    if (size < HEAP_MIN_BLOCK || heap_end < heap_base) {
        heap_end = heap_base;
        wilderness = heap_base;
    }
}

void *kmalloc(size_t size)
{
    return kmalloc_aligned(size, 8);
}

void *kmalloc_aligned(size_t size, size_t align)
{
    void *raw;
    uintptr_t aligned;
    heap_block_t *tag;
    void *out;

    if (size == 0 || align == 0)
        return NULL;
    if (align < 4)
        align = 4;
    if (align & (align - 1u))
        return NULL;

    spin_lock(&g_heap_lock);

    /*
     * Heap base is 16-aligned and headers are 16 bytes, so raw payloads are
     * naturally 16-aligned — covers kmalloc and all surface buffers.
     */
    if (align <= 16) {
        out = alloc_raw_locked(size);
        spin_unlock(&g_heap_lock);
        return out;
    }

    /*
     * Stronger alignment (AHCI/NVMe/page): over-allocate, align user pointer,
     * tag header immediately before it with stash of the real payload.
     */
    raw = alloc_raw_locked(size + align + sizeof(void *) + HEAP_HDR_SIZE);
    if (!raw) {
        spin_unlock(&g_heap_lock);
        return NULL;
    }

    aligned = ((uintptr_t)raw + sizeof(void *) + HEAP_HDR_SIZE + (align - 1u)) &
              ~(uintptr_t)(align - 1u);
    tag = (heap_block_t *)(aligned - HEAP_HDR_SIZE);
    tag->size = 0;
    tag->magic = HEAP_MAGIC_ALIGN;
    tag->next = (heap_block_t *)raw;

    spin_unlock(&g_heap_lock);
    return (void *)aligned;
}

void kfree(void *ptr)
{
    heap_block_t *blk;
    void *raw;

    if (!ptr)
        return;

    spin_lock(&g_heap_lock);
    blk = (heap_block_t *)((uint8_t *)ptr - HEAP_HDR_SIZE);
    if (!block_in_heap(blk)) {
        spin_unlock(&g_heap_lock);
        return;
    }

    if (blk->magic == HEAP_MAGIC_ALIGN) {
        raw = (void *)blk->next;
        free_raw_locked(raw);
        spin_unlock(&g_heap_lock);
        return;
    }

    if (blk->magic == HEAP_MAGIC_USED)
        free_raw_locked(ptr);

    spin_unlock(&g_heap_lock);
}

size_t heap_used(void)
{
    size_t n;
    spin_lock(&g_heap_lock);
    n = g_used;
    spin_unlock(&g_heap_lock);
    return n;
}

size_t heap_free(void)
{
    size_t n;
    heap_block_t *b;

    spin_lock(&g_heap_lock);
    n = 0;
    if (heap_end > wilderness)
        n += (size_t)(heap_end - wilderness);
    for (b = free_list; b; b = b->next) {
        if (b->magic == HEAP_MAGIC_FREE && b->size > HEAP_HDR_SIZE)
            n += b->size - HEAP_HDR_SIZE;
    }
    spin_unlock(&g_heap_lock);
    return n;
}
