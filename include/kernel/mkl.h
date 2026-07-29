#ifndef KERNEL_MKL_H
#define KERNEL_MKL_H

#include <kernel/types.h>

#define MKL_NAME_MAX     32
#define MKL_SYMS_MAX     128
#define MKL_MAX          16
#define MKL_DIR          "/system/lib"
#define MKL_EXT          ".mkl"
#define MKL_EXT_LEN      4

/*
 * Userspace dynamic library (.mkl): ELF32 ET_REL (same shape as .kmod).
 * Loaded once into kernel heap, relocated (R_386_*), shared across processes
 * via the single identity-mapped address space + refcount.
 */
typedef struct mkl {
    char     name[MKL_NAME_MAX];
    void    *base;
    size_t   size;
    int      loaded;
    int      refcount;
    int      nsyms;
    struct {
        char  name[48];
        void *addr;
    } syms[MKL_SYMS_MAX];
} mkl_t;

/* Load from VFS path or ensure by basename ("libfs.mkl" / "libfs"). */
int   mkl_load_path(const char *path);
int   mkl_ensure(const char *name);
void *mkl_lookup(const char *sym);
mkl_t *mkl_find(const char *name);

/*
 * App import table (lives in .exe image). slot_addr is the absolute VA of a
 * void* function-pointer slot that the loader fills after relocation.
 */
typedef struct mkl_import {
    char     lib[32];
    char     sym[32];
    uint32_t slot_addr;
} __attribute__((packed)) mkl_import_t;

/* Ensure each needed lib, then bind import slots. */
int mkl_bind_exe(const char needed[][MKL_NAME_MAX], int needed_count,
                 uint32_t load_addr, uint32_t imports_off);

#endif
