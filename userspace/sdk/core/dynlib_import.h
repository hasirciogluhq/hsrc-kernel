#ifndef USERSPACE_SDK_DYNLIB_IMPORT_H
#define USERSPACE_SDK_DYNLIB_IMPORT_H

#include <user/dynlib.h>

/*
 * DYNLIB_IMPORT(lib, ret, name, ...)
 *
 * Declares a function-pointer slot `name` AND registers it into the
 * .dynimports linker section in one line. The kernel (dynlib_bind_exec)
 * walks this section at exec time and patches each slot with the real
 * runtime address resolved from the loaded .dynlib's export table —
 * this is the exact equivalent of Windows' IAT patching.
 *
 * IMPORTANT: include this header (and call this macro) from exactly ONE
 * translation unit per app (normally main.cpp). If you need to call the
 * imported function from another .cpp in the same app, declare it there
 * as `extern ret (*name)(...)` instead of re-invoking DYNLIB_IMPORT —
 * otherwise you get duplicate-symbol link errors, since each invocation
 * both defines storage for the pointer AND emits a table entry.
 */
/* aligned(1): packed alone does not force object alignment; GCC may pad
 * each _dynimp_* to 32B (stride 96) while dynlib_bind_exec does imp++ by
 * sizeof=68 — only the first import would bind. */
#define DYNLIB_IMPORT(lib, ret, name, ...)                         \
    ret (*name)(__VA_ARGS__) = 0;                                  \
    __attribute__((section(".dynimports"), used, aligned(1)))      \
    static const dynlib_import_t _dynimp_##name = {                 \
        lib,                                                       \
        #name,                                                     \
        (uint32_t)(uintptr_t)&name                                 \
    }

#endif
