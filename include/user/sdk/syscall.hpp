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

/*
 * Timed sleep for `ms` milliseconds (PROC_SUSPENDED). Kernel converts ms→ticks.
 * ms==0 is a no-op (no syscall, no yield). Cap is kernel-side (60s).
 */
inline void sleep(uint32_t ms)
{
    if (ms == 0)
        return;
    (void)syscall1(SYS_SLEEP, (long)ms);
}

/*
 * Cooperative reschedule only (SYS_YIELD). Prefer sleep(ms) for pacing/idle.
 * Non-zero arg is legacy: treated as milliseconds and routed to sleep().
 */
inline void yield(uint32_t ms = 0)
{
    if (ms > 0) {
        sleep(ms);
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
