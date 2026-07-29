#ifndef KERNEL_EXEC_H
#define KERNEL_EXEC_H

#include <kernel/types.h>
#include <kernel/dynlib.h>
#include <multiboot.h>

#define EXEC_MAGIC    0x43455845u /* 'EXEC' */
#define EXEC_VERSION  2
#define EXEC_NAME_MAX 32

/* On-disk userspace binary (PATH may omit suffix: `hello` → hello.exec). */
#define EXEC_EXT      ".exec"
#define EXEC_EXT_LEN  5

#define EXEC_NEEDED_MAX 4

/* Identity load window for position-dependent userspace images */
#define EXEC_LOAD_MIN 0x02000000u
#define EXEC_LOAD_MAX 0x07000000u

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
    /* v2: dynamic libraries required by this executable */
    uint32_t imports_off; /* load_addr-relative offset of dynlib_import_t[] */
    char     needed[EXEC_NEEDED_MAX][DYNLIB_NAME_MAX];
} __attribute__((packed)) exec_header_t;

int exec_spawn(const void *blob, size_t size);
int exec_spawn_flags(const void *blob, size_t size, uint32_t spawn_flags,
                     const char *const *argv, int argc);
int exec_spawn_path(const char *path);
int exec_spawn_path_flags(const char *path, uint32_t spawn_flags,
                          const char *const *argv, int argc);

/*
 * Resolve a command to an on-disk .exec path.
 * - absolute / relative paths used as-is (optional .exec suffix tried)
 * - bare names searched in $PATH (default /system/bin:/applications)
 */
int exec_resolve(const char *in, char *out, size_t outsz);

#endif
