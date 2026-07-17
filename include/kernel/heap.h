#ifndef MYKERNEL_HEAP_H
#define MYKERNEL_HEAP_H

#include <kernel/types.h>

void  heap_init(void *start, size_t size);
void *kmalloc(size_t size);
void *kmalloc_aligned(size_t size, size_t align);
void  kfree(void *ptr);
size_t heap_used(void);
size_t heap_free(void);

#endif
