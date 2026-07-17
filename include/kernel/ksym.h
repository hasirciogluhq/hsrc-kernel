#ifndef MYKERNEL_KERNEL_KSYM_H
#define MYKERNEL_KERNEL_KSYM_H

#include <kernel/types.h>

typedef struct ksym {
    const char *name;
    void       *addr;
} ksym_t;

void  ksym_init(void);
void *ksym_lookup(const char *name);

#endif
