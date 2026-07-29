#ifndef USER_DYNLIB_H
#define USER_DYNLIB_H

#include <kernel/types.h>

#define DYNLIB_NAME_MAX 32

/*
 * Import row embedded in a .exec. The kernel walks a NUL-lib-terminated array
 * of these (symbol __dynlib_imports) after loading needed dynlibs and writes
 * each resolved symbol address into *slot_addr.
 */
typedef struct dynlib_import {
    char     lib[32];
    char     sym[32];
    uint32_t slot_addr;
} __attribute__((packed, aligned(1))) dynlib_import_t;

#endif
