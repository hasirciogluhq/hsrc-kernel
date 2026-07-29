/*
 * Dynamic userspace filesystem helper library (.dynlib).
 * Freestanding C — syscalls via int 0x80; relocated by kernel/dynlib.c.
 */
#include <kernel/types.h>
#include <kernel/syscall.h>
#include <kernel/vfs.h>
#include "../core/syscall_abi.h"

#ifndef S_IFMT
#define S_IFMT  0170000
#define S_IFREG 0100000
#define S_IFDIR 0040000
#endif

static size_t u_strlen(const char *s)
{
    size_t n = 0;
    if (!s)
        return 0;
    while (s[n])
        n++;
    return n;
}

/* ---- public exports -------------------------------------------------- */

long libfs_open(const char *path, int flags)
{
    return sc2(SYS_OPEN, (long)path, flags);
}

long libfs_close(int fd)
{
    return sc1(SYS_CLOSE, fd);
}

long libfs_read(int fd, void *buf, size_t count)
{
    return sc3(SYS_READ, fd, (long)buf, (long)count);
}

long libfs_write(int fd, const void *buf, size_t count)
{
    return sc3(SYS_WRITE, fd, (long)buf, (long)count);
}

long libfs_exists(const char *path)
{
    long fd = libfs_open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    (void)libfs_close((int)fd);
    return 1;
}

/*
 * Read up to max_entries dirents from path into buf.
 * Returns entry count (>=0) or -errno.
 */
long libfs_listdir(const char *path, vfs_dirent_t *buf, size_t max_entries)
{
    long fd;
    long n;

    if (!path || !buf || max_entries == 0)
        return -1;

    fd = libfs_open(path, O_RDONLY | O_DIRECTORY);
    if (fd < 0)
        fd = libfs_open(path, O_RDONLY);
    if (fd < 0)
        return fd;

    n = sc3(SYS_GETDENTS, fd, (long)buf, (long)max_entries);
    (void)libfs_close((int)fd);
    return n;
}

/*
 * Read entire file (or first max_bytes) into buf; NUL-terminates if room.
 * Returns bytes read (>=0) or -errno.
 */
long libfs_read_file(const char *path, char *buf, size_t max_bytes)
{
    long fd;
    long total = 0;

    if (!path || !buf || max_bytes == 0)
        return -1;

    fd = libfs_open(path, O_RDONLY);
    if (fd < 0)
        return fd;

    while ((size_t)total + 1 < max_bytes) {
        long n = libfs_read((int)fd, buf + total, max_bytes - 1 - (size_t)total);
        if (n < 0) {
            (void)libfs_close((int)fd);
            return n;
        }
        if (n == 0)
            break;
        total += n;
    }
    (void)libfs_close((int)fd);
    buf[total] = 0;
    return total;
}

/* Write a C string to fd 1 (console/stdout when attached). */
long libfs_puts(const char *s)
{
    size_t n;
    if (!s)
        return 0;
    n = u_strlen(s);
    return libfs_write(1, s, n);
}

long libfs_print_listdir(const char *path)
{
    vfs_dirent_t ents[64];
    long n;
    long i;

    libfs_puts("[libfs] listing ");
    libfs_puts(path ? path : "?");
    libfs_puts("\n");

    n = libfs_listdir(path, ents, 64);
    if (n < 0) {
        libfs_puts("[libfs] listdir failed\n");
        return n;
    }

    for (i = 0; i < n; i++) {
        libfs_puts("  ");
        libfs_puts(ents[i].name);
        if ((ents[i].type & S_IFMT) == S_IFDIR)
            libfs_puts("/");
        libfs_puts("\n");
    }
    return n;
}

/* Keep a tiny exported marker so nm always sees a stable symbol. */
const char libfs_version[] = "libfs-1";
