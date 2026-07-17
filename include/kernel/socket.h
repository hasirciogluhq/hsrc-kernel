#ifndef MYKERNEL_KERNEL_SOCKET_H
#define MYKERNEL_KERNEL_SOCKET_H

#include <kernel/types.h>

#define AF_INET     2
#define SOCK_DGRAM  2
#define SOCK_RAW    3

#define IPPROTO_IP   0
#define IPPROTO_ICMP 1
#define IPPROTO_UDP  17

#define MSG_DONTWAIT 0x40

#define INADDR_ANY 0u

#ifndef MYKERNEL_SOCKADDR_IN_DEFINED
#define MYKERNEL_SOCKADDR_IN_DEFINED
typedef struct sockaddr_in {
    uint16_t sin_family; /* AF_INET */
    uint16_t sin_port;   /* network byte order */
    uint32_t sin_addr;   /* network byte order IPv4 */
    uint8_t  sin_zero[8];
} sockaddr_in_t;
#endif

static inline uint16_t htons(uint16_t x)
{
    return (uint16_t)((x << 8) | (x >> 8));
}
static inline uint16_t ntohs(uint16_t x) { return htons(x); }
static inline uint32_t htonl(uint32_t x)
{
    return ((x & 0xFFu) << 24) | ((x & 0xFF00u) << 8) |
           ((x & 0xFF0000u) >> 8) | ((x & 0xFF000000u) >> 24);
}
static inline uint32_t ntohl(uint32_t x) { return htonl(x); }

void socket_init(void);

/* Kernel socket IDs (not process fds). */
int     sock_create(int domain, int type, int protocol);
int     sock_bind(int sid, const sockaddr_in_t *addr);
int     sock_connect(int sid, const sockaddr_in_t *addr);
ssize_t sock_sendto(int sid, const void *buf, size_t len, int flags,
                    const sockaddr_in_t *dst);
ssize_t sock_recvfrom(int sid, void *buf, size_t len, int flags,
                      sockaddr_in_t *src);
int     sock_close(int sid);

/* Deliver demuxed datagrams from the IP stack (kernel only). */
void sock_input_udp(uint32_t src_ip, uint16_t src_port,
                    uint32_t dst_ip, uint16_t dst_port,
                    const void *payload, size_t len);
void sock_input_icmp(uint32_t src_ip, const void *payload, size_t len);

#endif
