#pragma once

#include <kernel/types.h>
#include <kernel/mm.h>
#include <kernel/syscall.h>
#include <user/sdk/syscall.hpp>

namespace hsrc::sdk {

inline void *mmap(void *addr, size_t len, int prot, int flags, int fd, long off)
{
    (void)off;
    long r = syscall5(SYS_MMAP, (long)addr, (long)len, prot, flags, fd);
    if (r < 0)
        return (void *)(uintptr_t)-1u;
    return (void *)(uintptr_t)(uint32_t)r;
}

inline int munmap(void *addr, size_t len)
{
    return (int)syscall2(SYS_MUNMAP, (long)addr, (long)len);
}

inline int msync(void *addr, size_t len, int flags)
{
    return (int)syscall3(SYS_MSYNC, (long)addr, (long)len, flags);
}

} // namespace hsrc::sdk
