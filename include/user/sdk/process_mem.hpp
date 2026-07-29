#pragma once

#include <kernel/proc_mem.h>
#include <kernel/syscall.h>
#include <kernel/types.h>
#include <user/sdk/syscall.hpp>

/*
 * Userspace Windows-like process memory API.
 * Permissions checked in kernel; drivers may deny OpenProcess via hooks.
 */

namespace hsrc::sdk::procmem {

inline int open_process(pid_t pid, uint32_t access)
{
    return (int)syscall2(SYS_OPEN_PROCESS, (long)pid, (long)access);
}

inline int close_handle(int handle)
{
    return (int)syscall1(SYS_CLOSE_HANDLE, handle);
}

inline long read_memory(int handle, uint32_t addr, void *buf, size_t len)
{
    return syscall4(SYS_READ_PROCESS_MEMORY, handle, (long)addr, (long)buf,
                    (long)len);
}

inline long write_memory(int handle, uint32_t addr, const void *buf, size_t len)
{
    return syscall4(SYS_WRITE_PROCESS_MEMORY, handle, (long)addr, (long)buf,
                    (long)len);
}

inline void *virtual_alloc(void *addr, size_t size, uint32_t type, uint32_t prot)
{
    long r = syscall4(SYS_VIRTUAL_ALLOC, (long)addr, (long)size, (long)type,
                      (long)prot);
    if (r < 0)
        return nullptr;
    return (void *)(uintptr_t)(uint32_t)r;
}

inline int virtual_free(void *addr, size_t size, uint32_t type)
{
    return (int)syscall3(SYS_VIRTUAL_FREE, (long)addr, (long)size, (long)type);
}

inline void *virtual_alloc_ex(int handle, void *addr, size_t size, uint32_t type,
                              uint32_t prot)
{
    long r = syscall5(SYS_VIRTUAL_ALLOC_EX, handle, (long)addr, (long)size,
                      (long)type, (long)prot);
    if (r < 0)
        return nullptr;
    return (void *)(uintptr_t)(uint32_t)r;
}

inline int virtual_free_ex(int handle, void *addr, size_t size, uint32_t type)
{
    return (int)syscall4(SYS_VIRTUAL_FREE_EX, handle, (long)addr, (long)size,
                         (long)type);
}

struct QueryInfo {
    pid_t pid;
    pid_t ppid;
    uint32_t access;
    uint32_t state;
    uint32_t mem_bytes;
    uint32_t stack_bytes;
    uint32_t image_bytes;
    char name[32];
};

inline long query(int handle, QueryInfo *out)
{
    if (!out)
        return -1;
    return syscall3(SYS_QUERY_PROCESS, handle, (long)out, (long)sizeof(*out));
}

inline long query_vm(int handle, proc_vm_region_t *out, size_t nbytes)
{
    return syscall3(SYS_QUERY_PROCESS_VM, handle, (long)out, (long)nbytes);
}

/* Own process helpers (pseudo-handle; no OpenProcess). */
inline int current_handle(void)
{
    return PROCESS_HANDLE_CURRENT;
}

inline void *heap_alloc(size_t n)
{
    return virtual_alloc(nullptr, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

inline int heap_free(void *p, size_t n)
{
    return virtual_free(p, n, MEM_RELEASE);
}

} // namespace hsrc::sdk::procmem
