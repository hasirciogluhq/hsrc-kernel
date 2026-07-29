#ifndef KERNEL_EXE_H
#define KERNEL_EXE_H

#include <kernel/types.h>
#include <multiboot.h>

#define EXE_MAGIC    0x31455845u /* 'EXE1' */
#define EXE_VERSION  1
#define EXE_NAME_MAX 32

/* On-disk usermode binary extension (PATH may omit it: `hello` → hello.exe). */
#define EXE_EXT      ".exe"
#define EXE_EXT_LEN  4

/* Minimum / maximum identity load addresses (below 128MiB QEMU default) */
#define EXE_LOAD_MIN 0x02000000u
#define EXE_LOAD_MAX 0x07000000u

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
} __attribute__((packed)) exe_header_t;

/* Validate + copy image to load_addr, zero BSS, create ring-3 process. */
int exe_spawn(const void *blob, size_t size);
int exe_spawn_flags(const void *blob, size_t size, uint32_t spawn_flags,
                    const char *const *argv, int argc);
int exe_spawn_path(const char *path);
int exe_spawn_path_flags(const char *path, uint32_t spawn_flags,
                         const char *const *argv, int argc);

/*
 * Resolve a command to an on-disk .exe path (Linux-like PATH).
 * - absolute / relative paths used as-is (optional .exe suffix tried)
 * - bare names searched in $PATH (default /system/bin:/applications)
 * - PID1 is conventional `/init` (not under /applications)
 * Accepts both `hello` and `hello.exe`.
 */
int exe_resolve(const char *in, char *out, size_t outsz);

#endif
