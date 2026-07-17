#pragma once

#include <kernel/socket.h>
#include <kernel/syscall.h>
#include <user/sdk/syscall.hpp>

namespace hsrc::sdk {

inline long socket(int domain, int type, int protocol)
{
    return syscall3(SYS_SOCKET, domain, type, protocol);
}

inline long bind(int fd, const sockaddr_in_t *addr)
{
    return syscall2(SYS_BIND, fd, (long)addr);
}

inline long connect(int fd, const sockaddr_in_t *addr)
{
    return syscall2(SYS_CONNECT, fd, (long)addr);
}

/* dest may be null if the socket is connected. */
inline long sendto(int fd, const void *buf, size_t len, const sockaddr_in_t *dest)
{
    return syscall4(SYS_SENDTO, fd, (long)buf, (long)len, (long)dest);
}

/* src may be null if peer address is not needed. */
inline long recvfrom(int fd, void *buf, size_t len, sockaddr_in_t *src)
{
    return syscall4(SYS_RECVFROM, fd, (long)buf, (long)len, (long)src);
}

inline int inet_aton(const char *s, uint32_t *out_host)
{
    uint32_t o[4] = { 0, 0, 0, 0 };
    int oi = 0;
    const char *p = s;
    if (!s || !out_host)
        return -1;
    for (;;) {
        if (*p >= '0' && *p <= '9') {
            o[oi] = o[oi] * 10u + (uint32_t)(*p - '0');
            if (o[oi] > 255)
                return -1;
            p++;
            continue;
        }
        if (*p == '.') {
            if (oi >= 3)
                return -1;
            oi++;
            p++;
            continue;
        }
        break;
    }
    if (oi != 3 || (*p && *p != ' ' && *p != '\t' && *p != '\0'))
        return -1;
    *out_host = (o[0] << 24) | (o[1] << 16) | (o[2] << 8) | o[3];
    return 0;
}

} // namespace hsrc::sdk
