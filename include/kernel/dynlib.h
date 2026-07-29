#ifndef KERNEL_DYNLIB_H
#define KERNEL_DYNLIB_H

#include <kernel/types.h>

#define DYNLIB_NAME_MAX     32
#define DYNLIB_SYMS_MAX     128
#define DYNLIB_MAX          16
#define DYNLIB_DIR          "/system/lib"
#define DYNLIB_EXT          ".dynlib"
#define DYNLIB_EXT_LEN      7

/*
 * Userspace dynamic library (.dynlib): ELF32 ET_REL (same shape as .kmod).
 * Loaded once into kernel heap, relocated (R_386_*), shared across processes
 * via the single identity-mapped address space + refcount.
 */
typedef struct dynlib {
    char     name[DYNLIB_NAME_MAX];
    void    *base;
    size_t   size;
    int      loaded;
    int      refcount;
    int      nsyms;
    struct {
        char  name[48];
        void *addr;
    } syms[DYNLIB_SYMS_MAX];
} dynlib_t;

int   dynlib_load_path(const char *path);
int   dynlib_ensure(const char *name);
void *dynlib_lookup(const char *sym);
dynlib_t *dynlib_find(const char *name);

/*
 * App import table (lives in .exec image). slot_addr is the absolute VA of a
 * void* function-pointer slot that the loader fills after relocation.
 */
typedef struct dynlib_import {
    char     lib[32];
    char     sym[32];
    uint32_t slot_addr;
} __attribute__((packed)) dynlib_import_t;

/* Ensure each needed dynlib, then bind import slots. */
int dynlib_bind_exec(const char needed[][DYNLIB_NAME_MAX], int needed_count,
                     uint32_t load_addr, uint32_t imports_off);

#endif
