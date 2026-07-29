#ifndef HSRC_USER_HXE_H
#define HSRC_USER_HXE_H

#include <kernel/types.h>

#define HXE_MAGIC    0x31455848u /* 'HXE1' */
#define HXE_VERSION  1
#define HXE_NAME_MAX 32

typedef struct hxe_header {
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t load_addr;
    uint32_t entry_off;
    uint32_t image_size;
    uint32_t bss_size;
    uint32_t stack_size;
    char     name[HXE_NAME_MAX];
} __attribute__((packed)) hxe_header_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Every .hxe exports this as the ring-3 entry (CRT-less). */
void hxe_main(void);

#ifdef __cplusplus
}
#endif

#endif
