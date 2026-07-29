#ifndef USER_MKL_H
#define USER_MKL_H

#include <kernel/types.h>

#define MKL_NAME_MAX 32

/*
 * Import row embedded in a .exe. The kernel walks a NUL-lib-terminated array
 * of these (symbol __mkl_imports) after loading DT_NEEDED-style libs and
 * writes each resolved symbol address into *slot_addr.
 */
typedef struct mkl_import {
    char     lib[32];
    char     sym[32];
    uint32_t slot_addr;
} __attribute__((packed)) mkl_import_t;

#endif
