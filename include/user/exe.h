#ifndef USER_EXE_H
#define USER_EXE_H

#include <kernel/types.h>
#include <user/mkl.h>

#define EXE_MAGIC      0x31455845u /* 'EXE1' */
#define EXE_VERSION    2
#define EXE_NAME_MAX   32
#define EXE_NEEDED_MAX 4
#define MKL_NAME_MAX   32

typedef struct exe_header {
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t load_addr;
    uint32_t entry_off;
    uint32_t image_size;
    uint32_t bss_size;
    uint32_t stack_size;
    char     name[EXE_NAME_MAX];
    uint32_t imports_off;
    char     needed[EXE_NEEDED_MAX][MKL_NAME_MAX];
} __attribute__((packed)) exe_header_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Every .exe exports this as the ring-3 entry (CRT-less). */
void exe_main(void);

#ifdef __cplusplus
}
#endif

#endif
