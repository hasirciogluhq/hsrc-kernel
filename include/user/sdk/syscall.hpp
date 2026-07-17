#pragma once

#include <kernel/types.h>
#include <kernel/syscall.h>

namespace hsrc::sdk {

long syscall0(long n);
long syscall1(long n, long a1);
long syscall2(long n, long a1, long a2);
long syscall3(long n, long a1, long a2, long a3);

inline void yield()
{
    (void)syscall1(SYS_YIELD, 0);
}

[[noreturn]] inline void exit(int code)
{
    (void)syscall1(SYS_EXIT, code);
    for (;;)
        ;
}

} // namespace hsrc::sdk
