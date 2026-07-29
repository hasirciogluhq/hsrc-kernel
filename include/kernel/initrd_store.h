#ifndef HSRC_INITRD_STORE_H
#define HSRC_INITRD_STORE_H

#include <kernel/types.h>

void        initrd_store_set(const void *data, size_t size);
const void *initrd_store_get(size_t *size_out);

#endif
