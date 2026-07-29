#ifndef USERSPACE_SDK_SYSCALL_ABI_H
#define USERSPACE_SDK_SYSCALL_ABI_H

/*
 * Freestanding int $0x80 helpers for .dynlib objects (no link against sdk-core).
 * Keep in sync with userspace/sdk/core/syscall.cpp register ABI (eax/ebx/ecx/edx/esi/edi).
 */
#include <kernel/types.h>

static inline long sc0(long n)
{
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n) : "memory");
    return ret;
}

static inline long sc1(long n, long a1)
{
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "b"(a1) : "memory");
    return ret;
}

static inline long sc2(long n, long a1, long a2)
{
    long ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(n), "b"(a1), "c"(a2)
                     : "memory");
    return ret;
}

static inline long sc3(long n, long a1, long a2, long a3)
{
    long ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(n), "b"(a1), "c"(a2), "d"(a3)
                     : "memory");
    return ret;
}

static inline long sc4(long n, long a1, long a2, long a3, long a4)
{
    long ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(n), "b"(a1), "c"(a2), "d"(a3), "S"(a4)
                     : "memory");
    return ret;
}

static inline long sc5(long n, long a1, long a2, long a3, long a4, long a5)
{
    long ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(n), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5)
                     : "memory");
    return ret;
}

#endif
