#ifndef MYKERNEL_KERNEL_MKE_H
#define MYKERNEL_KERNEL_MKE_H

#include <kernel/types.h>
#include <multiboot.h>

#define MKE_MAGIC    0x31454B4Du /* 'MKE1' — MyKernel Executable */
#define MKE_VERSION  1
#define MKE_NAME_MAX 32

/* On-disk usermode binary extension (PATH may omit it: `hello` → hello.mke). */
#define MKE_EXT      ".mke"
#define MKE_EXT_LEN  4

/* Minimum / maximum identity load addresses (below 128MiB QEMU default) */
#define MKE_LOAD_MIN 0x02000000u
#define MKE_LOAD_MAX 0x07000000u

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

/* Validate + copy image to load_addr, zero BSS, create ring-3 process. */
int mke_spawn(const void *blob, size_t size);
int mke_spawn_flags(const void *blob, size_t size, uint32_t spawn_flags,
                    const char *const *argv, int argc);
int mke_spawn_path(const char *path);
int mke_spawn_path_flags(const char *path, uint32_t spawn_flags,
                         const char *const *argv, int argc);

/*
 * Resolve a command to an on-disk .mke path (Linux-like PATH).
 * - absolute / relative paths used as-is (optional .mke suffix tried)
 * - bare names searched in $PATH (default /applications:/usr/bin)
 * - `/init` also tries `/applications/init` (+ .mke)
 * Accepts both `hello` and `hello.mke`.
 */
int exe_resolve(const char *in, char *out, size_t outsz);

#endif
