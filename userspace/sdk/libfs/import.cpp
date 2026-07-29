/*
 * App-side import stubs for libfs.dynlib.
 * Linked into the .exec (not the .dynlib). Kernel fills the void* slots on spawn.
 */
#include <user/dynlib.h>
#include <kernel/types.h>
#include <kernel/vfs.h>

extern "C" {

void *g_libfs_open;
void *g_libfs_close;
void *g_libfs_read;
void *g_libfs_write;
void *g_libfs_exists;
void *g_libfs_listdir;
void *g_libfs_read_file;
void *g_libfs_puts;
void *g_libfs_print_listdir;

__attribute__((used, section(".data")))
const dynlib_import_t __dynlib_imports[] = {
    { "libfs.dynlib", "libfs_open",           (uint32_t)(uintptr_t)&g_libfs_open },
    { "libfs.dynlib", "libfs_close",          (uint32_t)(uintptr_t)&g_libfs_close },
    { "libfs.dynlib", "libfs_read",           (uint32_t)(uintptr_t)&g_libfs_read },
    { "libfs.dynlib", "libfs_write",          (uint32_t)(uintptr_t)&g_libfs_write },
    { "libfs.dynlib", "libfs_exists",         (uint32_t)(uintptr_t)&g_libfs_exists },
    { "libfs.dynlib", "libfs_listdir",        (uint32_t)(uintptr_t)&g_libfs_listdir },
    { "libfs.dynlib", "libfs_read_file",      (uint32_t)(uintptr_t)&g_libfs_read_file },
    { "libfs.dynlib", "libfs_puts",           (uint32_t)(uintptr_t)&g_libfs_puts },
    { "libfs.dynlib", "libfs_print_listdir",  (uint32_t)(uintptr_t)&g_libfs_print_listdir },
    { "", "", 0 },
};

} // extern "C"
