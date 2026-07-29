#pragma once

/*
 * Dynamic libfs SDK — calls go through kernel-bound import slots filled from
 * /system/lib/libfs.dynlib at spawn time. Link app with libfs-import + needed=libfs.dynlib.
 */
#include <kernel/types.h>
#include <kernel/vfs.h>
#include <user/dynlib.h>

extern "C" {
extern void *g_libfs_open;
extern void *g_libfs_close;
extern void *g_libfs_read;
extern void *g_libfs_write;
extern void *g_libfs_exists;
extern void *g_libfs_listdir;
extern void *g_libfs_read_file;
extern void *g_libfs_puts;
extern void *g_libfs_print_listdir;
extern const dynlib_import_t __dynlib_imports[];
}

namespace hsrc::sdk::libfs {

using open_fn = long (*)(const char *, int);
using close_fn = long (*)(int);
using rw_fn = long (*)(int, void *, size_t);
using wr_fn = long (*)(int, const void *, size_t);
using exists_fn = long (*)(const char *);
using listdir_fn = long (*)(const char *, vfs_dirent_t *, size_t);
using read_file_fn = long (*)(const char *, char *, size_t);
using puts_fn = long (*)(const char *);
using print_listdir_fn = long (*)(const char *);

inline long open(const char *path, int flags)
{
    return ((open_fn)g_libfs_open)(path, flags);
}

inline long close(int fd)
{
    return ((close_fn)g_libfs_close)(fd);
}

inline long read(int fd, void *buf, size_t count)
{
    return ((rw_fn)g_libfs_read)(fd, buf, count);
}

inline long write(int fd, const void *buf, size_t count)
{
    return ((wr_fn)g_libfs_write)(fd, buf, count);
}

inline bool exists(const char *path)
{
    return ((exists_fn)g_libfs_exists)(path) != 0;
}

inline long listdir(const char *path, vfs_dirent_t *buf, size_t max_entries)
{
    return ((listdir_fn)g_libfs_listdir)(path, buf, max_entries);
}

inline long read_file(const char *path, char *buf, size_t max_bytes)
{
    return ((read_file_fn)g_libfs_read_file)(path, buf, max_bytes);
}

inline long puts(const char *s)
{
    return ((puts_fn)g_libfs_puts)(s);
}

inline long print_listdir(const char *path)
{
    return ((print_listdir_fn)g_libfs_print_listdir)(path);
}

} // namespace hsrc::sdk::libfs
