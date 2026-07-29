#ifndef USERSPACE_SDK_LIBFS_API_H
#define USERSPACE_SDK_LIBFS_API_H

#include <kernel/vfs.h>
#include "../core/dynlib_import.h"

/*
 * In exactly ONE TU (normally main.cpp):
 *   #define LIBFS_API_DEFINE_SLOTS
 *   #include ".../libfs_api.h"
 * Other TUs include without the define (extern declarations only).
 */
#ifdef __cplusplus
extern "C" {
#endif

#ifdef LIBFS_API_DEFINE_SLOTS

DYNLIB_IMPORT("libfs", long, libfs_open,          const char *, int);
DYNLIB_IMPORT("libfs", long, libfs_close,         int);
DYNLIB_IMPORT("libfs", long, libfs_read,          int, void *, size_t);
DYNLIB_IMPORT("libfs", long, libfs_write,         int, const void *, size_t);
DYNLIB_IMPORT("libfs", long, libfs_exists,        const char *);
DYNLIB_IMPORT("libfs", long, libfs_listdir,       const char *, vfs_dirent_t *, size_t);
DYNLIB_IMPORT("libfs", long, libfs_read_file,     const char *, char *, size_t);
DYNLIB_IMPORT("libfs", long, libfs_puts,          const char *);
DYNLIB_IMPORT("libfs", long, libfs_print_listdir, const char *);

#else

extern long (*libfs_open)(const char *, int);
extern long (*libfs_close)(int);
extern long (*libfs_read)(int, void *, size_t);
extern long (*libfs_write)(int, const void *, size_t);
extern long (*libfs_exists)(const char *);
extern long (*libfs_listdir)(const char *, vfs_dirent_t *, size_t);
extern long (*libfs_read_file)(const char *, char *, size_t);
extern long (*libfs_puts)(const char *);
extern long (*libfs_print_listdir)(const char *);

#endif /* LIBFS_API_DEFINE_SLOTS */

#ifdef __cplusplus
}
#endif

#endif
