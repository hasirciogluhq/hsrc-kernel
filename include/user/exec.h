#ifndef USER_EXEC_H
#define USER_EXEC_H

#include <kernel/types.h>
#include <user/dynlib.h>

#define EXEC_MAGIC      0x43455845u /* 'EXEC' */
#define EXEC_VERSION    2
#define EXEC_NAME_MAX   32
#define EXEC_NEEDED_MAX 4

typedef struct exec_header {
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t load_addr;
    uint32_t entry_off;
    uint32_t image_size;
    uint32_t bss_size;
    uint32_t stack_size;
    char     name[EXEC_NAME_MAX];
    uint32_t imports_off;
    char     needed[EXEC_NEEDED_MAX][DYNLIB_NAME_MAX];
} __attribute__((packed)) exec_header_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Every usermode ELF exports this as the ring-3 entry (CRT-less). */
void exec_main(void);

#ifdef __cplusplus
}
#endif

#endif
