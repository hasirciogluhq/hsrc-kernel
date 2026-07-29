#pragma once

#include <kernel/types.h>
#include <kernel/syscall.h>

namespace hsrc::sdk {

long syscall0(long n);
long syscall1(long n, long a1);
long syscall2(long n, long a1, long a2);
long syscall3(long n, long a1, long a2, long a3);
long syscall4(long n, long a1, long a2, long a3, long a4);
long syscall5(long n, long a1, long a2, long a3, long a4, long a5);

/* Timed suspend for `ticks` scheduler ticks (PROC_SUSPENDED). ticks==0 → coop yield. */
inline void sleep_ticks(uint32_t ticks)
{
    (void)syscall1(SYS_SLEEP, (long)ticks);
}

/* Cooperative reschedule. ticks>0 is accepted for compat and routes to SYS_SLEEP. */
inline void yield(uint32_t ticks = 0)
{
    if (ticks > 0) {
        sleep_ticks(ticks);
        return;
    }
    (void)syscall1(SYS_YIELD, 0);
}

[[noreturn]] inline void exit(int code)
{
    (void)syscall1(SYS_EXIT, code);
    for (;;)
        ;
}

} // namespace hsrc::sdk
