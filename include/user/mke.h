#ifndef MYKERNEL_USER_MKE_H
#define MYKERNEL_USER_MKE_H

#include <kernel/types.h>

#define MKE_MAGIC    0x31454B4Du /* 'MKE1' */
#define MKE_VERSION  1
#define MKE_NAME_MAX 32

typedef struct mke_header {
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t load_addr;
    uint32_t entry_off;
    uint32_t image_size;
    uint32_t bss_size;
    uint32_t stack_size;
    char     name[MKE_NAME_MAX];
} __attribute__((packed)) mke_header_t;

/* Every .mke exports this as the ring-3 entry (CRT-less). */
void mke_main(void);

#endif
