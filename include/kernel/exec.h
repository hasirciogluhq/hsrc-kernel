#ifndef KERNEL_EXEC_H
#define KERNEL_EXEC_H

#include <kernel/types.h>
#include <kernel/dynlib.h>
#include <kernel/vmm.h>

#define EXEC_MAGIC    0x43455845u /* 'EXEC' — legacy container */
#define EXEC_VERSION  3
#define EXEC_NAME_MAX 32

/* Preferred on-disk suffix; plain ELF bytes inside. .elf still accepted. */
#define EXEC_EXT          ".exec"
#define EXEC_EXT_LEN      5
#define EXEC_EXT_ELF      ".elf"
#define EXEC_EXT_ELF_LEN  4

#define EXEC_NEEDED_MAX 4

/* Canonical link / map base (ld/user.ld). */
#define EXEC_IMAGE_BASE USER_IMAGE_BASE

/* Legacy packed container (still loadable). */
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

int exec_spawn(const void *blob, size_t size);
int exec_spawn_flags(const void *blob, size_t size, uint32_t spawn_flags,
                     const char *const *argv, int argc);
int exec_spawn_path(const char *path);
int exec_spawn_path_flags(const char *path, uint32_t spawn_flags,
                          const char *const *argv, int argc);

/*
 * Resolve a command to an on-disk binary path.
 * Tries as-is, then .exec, then .elf. Bare names search $PATH.
 */
int exec_resolve(const char *in, char *out, size_t outsz);

#endif
