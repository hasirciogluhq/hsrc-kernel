#ifndef HSRC_KERNEL_HXE_H
#define HSRC_KERNEL_HXE_H

#include <kernel/types.h>
#include <multiboot.h>

#define HXE_MAGIC    0x31455848u /* 'HXE1' — hsrc Executable */
#define HXE_VERSION  1
#define HXE_NAME_MAX 32

/* On-disk usermode binary extension (PATH may omit it: `hello` → hello.hxe). */
#define HXE_EXT      ".hxe"
#define HXE_EXT_LEN  4

/* Minimum / maximum identity load addresses (below 128MiB QEMU default) */
#define HXE_LOAD_MIN 0x02000000u
#define HXE_LOAD_MAX 0x07000000u

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

/* Validate + copy image to load_addr, zero BSS, create ring-3 process. */
int hxe_spawn(const void *blob, size_t size);
int hxe_spawn_flags(const void *blob, size_t size, uint32_t spawn_flags,
                    const char *const *argv, int argc);
int hxe_spawn_path(const char *path);
int hxe_spawn_path_flags(const char *path, uint32_t spawn_flags,
                         const char *const *argv, int argc);

/*
 * Resolve a command to an on-disk .hxe path (Linux-like PATH).
 * - absolute / relative paths used as-is (optional .hxe suffix tried)
 * - bare names searched in $PATH (default /system/bin:/applications)
 * - PID1 is conventional `/init` (not under /applications)
 * Accepts both `hello` and `hello.hxe`.
 */
int exe_resolve(const char *in, char *out, size_t outsz);

#endif
