#ifndef USERSPACE_SDK_LIBFS_API_H
#define USERSPACE_SDK_LIBFS_API_H

#include <kernel/vfs.h>
#include "../core/dynlib_import.h"

/*
 * Include from exactly ONE translation unit per app (normally main.cpp).
 * Other .cpp files should `extern` the function pointers they need, or use
 * <user/sdk/libfs.hpp> which only declares them.
 */
#ifdef __cplusplus
extern "C" {
#endif

DYNLIB_IMPORT("libfs", long, libfs_open,          const char *, int);
DYNLIB_IMPORT("libfs", long, libfs_close,         int);
DYNLIB_IMPORT("libfs", long, libfs_read,          int, void *, size_t);
DYNLIB_IMPORT("libfs", long, libfs_write,         int, const void *, size_t);
DYNLIB_IMPORT("libfs", long, libfs_exists,        const char *);
DYNLIB_IMPORT("libfs", long, libfs_listdir,       const char *, vfs_dirent_t *, size_t);
DYNLIB_IMPORT("libfs", long, libfs_read_file,     const char *, char *, size_t);
DYNLIB_IMPORT("libfs", long, libfs_puts,          const char *);
DYNLIB_IMPORT("libfs", long, libfs_print_listdir, const char *);

#ifdef __cplusplus
}
#endif

#endif
