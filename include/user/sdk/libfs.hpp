#pragma once

/*
 * Dynamic libfs SDK — thin C++ wrappers over typed import slots.
 *
 * Include userspace/sdk/libfs/libfs_api.h from exactly ONE .cpp (normally
 * main.cpp) so DYNLIB_IMPORT defines the slots + .dynimports table entries.
 * This header only declares the pointers; it does not emit imports.
 *
 * App still needs needed={"libfs.dynlib"} in define_app (hdr->needed[]).
 */
#include <kernel/types.h>
#include <kernel/vfs.h>

extern "C" {
extern long (*libfs_open)(const char *, int);
extern long (*libfs_close)(int);
extern long (*libfs_read)(int, void *, size_t);
extern long (*libfs_write)(int, const void *, size_t);
extern long (*libfs_exists)(const char *);
extern long (*libfs_listdir)(const char *, vfs_dirent_t *, size_t);
extern long (*libfs_read_file)(const char *, char *, size_t);
extern long (*libfs_puts)(const char *);
extern long (*libfs_print_listdir)(const char *);
}

namespace hsrc::sdk::libfs {

inline long open(const char *path, int flags)
{
    return libfs_open(path, flags);
}

inline long close(int fd)
{
    return libfs_close(fd);
}

inline long read(int fd, void *buf, size_t count)
{
    return libfs_read(fd, buf, count);
}

inline long write(int fd, const void *buf, size_t count)
{
    return libfs_write(fd, buf, count);
}

inline bool exists(const char *path)
{
    return libfs_exists(path) != 0;
}

inline long listdir(const char *path, vfs_dirent_t *buf, size_t max_entries)
{
    return libfs_listdir(path, buf, max_entries);
}

inline long read_file(const char *path, char *buf, size_t max_bytes)
{
    return libfs_read_file(path, buf, max_bytes);
}

inline long puts(const char *s)
{
    return libfs_puts(s);
}

inline long print_listdir(const char *path)
{
    return libfs_print_listdir(path);
}

} // namespace hsrc::sdk::libfs
