#include <user/sdk/mmap.hpp>
#include <kernel/string.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Userspace heap — freelist over anonymous mmap arenas.
 * Also exposes C malloc/free/calloc/realloc for freestanding C++ / ImGui.
 */

namespace {

struct hdr {
    size_t size;
    int free;
    hdr *next;
};

static hdr *g_free_list;
static uint8_t *g_heap_base;
static size_t g_heap_cap;
static size_t g_heap_used;

static constexpr size_t kAlign = 16;
static constexpr size_t kArena = 256u * 1024u;

static size_t align_up(size_t n)
{
    return (n + kAlign - 1u) & ~(kAlign - 1u);
}

static int grow(size_t need)
{
    size_t ask = need + sizeof(hdr) + 64;
    if (ask < kArena)
        ask = kArena;
    ask = align_up(ask);
    void *p = hsrc::sdk::mmap(nullptr, ask, PROT_READ | PROT_WRITE, MAP_PRIVATE, -1, 0);
    if (p == (void *)(intptr_t)-1 || !p)
        return -1;
    hdr *h = (hdr *)p;
    h->size = ask - sizeof(hdr);
    h->free = 1;
    h->next = g_free_list;
    g_free_list = h;
    if (!g_heap_base)
        g_heap_base = (uint8_t *)p;
    g_heap_cap += ask;
    return 0;
}

static void split(hdr *h, size_t want)
{
    size_t rem = h->size - want;
    if (rem < sizeof(hdr) + kAlign)
        return;
    hdr *n = (hdr *)((uint8_t *)(h + 1) + want);
    n->size = rem - sizeof(hdr);
    n->free = 1;
    n->next = h->next;
    h->size = want;
    h->next = n;
}

} // namespace

extern "C" {

void *malloc(size_t size)
{
    if (size == 0)
        size = 1;
    size = align_up(size);
    for (;;) {
        for (hdr *h = g_free_list; h; h = h->next) {
            if (h->free && h->size >= size) {
                split(h, size);
                h->free = 0;
                g_heap_used += h->size;
                return (void *)(h + 1);
            }
        }
        if (grow(size) < 0)
            return nullptr;
    }
}

void free(void *ptr)
{
    if (!ptr)
        return;
    hdr *h = ((hdr *)ptr) - 1;
    if (h->free)
        return;
    g_heap_used -= h->size;
    h->free = 1;
}

void *calloc(size_t n, size_t sz)
{
    size_t t = n * sz;
    void *p = malloc(t);
    if (p)
        memset(p, 0, t);
    return p;
}

void *realloc(void *ptr, size_t size)
{
    if (!ptr)
        return malloc(size);
    if (size == 0) {
        free(ptr);
        return nullptr;
    }
    hdr *h = ((hdr *)ptr) - 1;
    if (h->size >= size)
        return ptr;
    void *n = malloc(size);
    if (!n)
        return nullptr;
    memcpy(n, ptr, h->size);
    free(ptr);
    return n;
}

} /* extern "C" */

namespace hsrc::sdk::heap {

void *alloc(size_t n)
{
    return ::malloc(n);
}

void release(void *p)
{
    ::free(p);
}

void *alloc_zero(size_t n)
{
    return ::calloc(1, n);
}

size_t used_bytes(void)
{
    return g_heap_used;
}

size_t capacity_bytes(void)
{
    return g_heap_cap;
}

} // namespace hsrc::sdk::heap
