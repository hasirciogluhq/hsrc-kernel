#pragma once

#include <stddef.h>

/* Userspace process heap (mmap-backed freelist). */
namespace hsrc::sdk::heap {

void *alloc(size_t n);
void release(void *p);
void *alloc_zero(size_t n);
size_t used_bytes(void);
size_t capacity_bytes(void);

} // namespace hsrc::sdk::heap

extern "C" {
void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t n, size_t sz);
void *realloc(void *ptr, size_t size);
}
