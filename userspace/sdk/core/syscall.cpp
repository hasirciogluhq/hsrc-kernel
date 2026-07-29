#include <user/sdk/syscall.hpp>

namespace hsrc::sdk {

long syscall0(long n)
{
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n) : "memory");
    return ret;
}

long syscall1(long n, long a1)
{
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "b"(a1) : "memory");
    return ret;
}

long syscall2(long n, long a1, long a2)
{
    long ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(n), "b"(a1), "c"(a2)
                     : "memory");
    return ret;
}

long syscall3(long n, long a1, long a2, long a3)
{
    long ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(n), "b"(a1), "c"(a2), "d"(a3)
                     : "memory");
    return ret;
}

long syscall4(long n, long a1, long a2, long a3, long a4)
{
    long ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(n), "b"(a1), "c"(a2), "d"(a3), "S"(a4)
                     : "memory");
    return ret;
}

} // namespace hsrc::sdk
