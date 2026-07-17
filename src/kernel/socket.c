#include <kernel/socket.h>
#include <kernel/netif.h>
#include <kernel/netstack.h>
#include <kernel/errno.h>
#include <kernel/string.h>

#define SOCK_MAX       16
#define SOCK_RX_SLOTS  8
#define SOCK_RX_BYTES  1472

typedef struct {
    uint32_t src_ip;   /* host order */
    uint16_t src_port; /* host order */
    uint16_t len;
    uint8_t  data[SOCK_RX_BYTES];
} sock_pkt_t;

typedef struct {
    int      used;
    int      domain;
    int      type;
    int      proto;
    int      bound;
    int      connected;
    uint16_t lport; /* host */
    uint32_t laddr; /* host */
    uint16_t rport; /* host */
    uint32_t raddr; /* host */
    sock_pkt_t rx[SOCK_RX_SLOTS];
    int      rx_r;
    int      rx_w;
    int      rx_n;
} socket_t;

static socket_t g_socks[SOCK_MAX];
static uint16_t g_ephemeral = 40000;

void socket_init(void)
{
    memset(g_socks, 0, sizeof(g_socks));
    g_ephemeral = 40000;
}

static socket_t *sock_get(int sid)
{
    if (sid < 0 || sid >= SOCK_MAX || !g_socks[sid].used)
        return NULL;
    return &g_socks[sid];
}

static int port_in_use(int type, int proto, uint16_t port, uint32_t laddr)
{
    int i;
    for (i = 0; i < SOCK_MAX; i++) {
        socket_t *s = &g_socks[i];
        if (!s->used || !s->bound)
            continue;
        if (s->type != type || s->proto != proto)
            continue;
        if (s->lport == port && (s->laddr == 0 || laddr == 0 || s->laddr == laddr))
            return 1;
    }
    return 0;
}

static uint16_t alloc_ephemeral(int type, int proto)
{
    int n;
    for (n = 0; n < 20000; n++) {
        uint16_t p = g_ephemeral++;
        if (g_ephemeral < 40000)
            g_ephemeral = 40000;
        if (!port_in_use(type, proto, p, 0))
            return p;
    }
    return 0;
}

static int rx_push(socket_t *s, uint32_t src_ip, uint16_t src_port,
                   const void *data, size_t len)
{
    sock_pkt_t *p;
    if (s->rx_n >= SOCK_RX_SLOTS)
        return -1;
    if (len > SOCK_RX_BYTES)
        len = SOCK_RX_BYTES;
    p = &s->rx[s->rx_w];
    p->src_ip = src_ip;
    p->src_port = src_port;
    p->len = (uint16_t)len;
    memcpy(p->data, data, len);
    s->rx_w = (s->rx_w + 1) % SOCK_RX_SLOTS;
    s->rx_n++;
    return 0;
}

static int rx_pop(socket_t *s, sock_pkt_t *out)
{
    if (s->rx_n == 0)
        return -1;
    *out = s->rx[s->rx_r];
    s->rx_r = (s->rx_r + 1) % SOCK_RX_SLOTS;
    s->rx_n--;
    return 0;
}

int sock_create(int domain, int type, int protocol)
{
    int i;
    if (domain != AF_INET)
        return -EAFNOSUPPORT;
    if (type == SOCK_DGRAM) {
        if (protocol == 0)
            protocol = IPPROTO_UDP;
        if (protocol != IPPROTO_UDP)
            return -EPROTONOSUPPORT;
    } else if (type == SOCK_RAW) {
        if (protocol == 0)
            protocol = IPPROTO_ICMP;
        if (protocol != IPPROTO_ICMP)
            return -EPROTONOSUPPORT;
    } else {
        return -EPROTONOSUPPORT;
    }

    for (i = 0; i < SOCK_MAX; i++) {
        if (!g_socks[i].used) {
            memset(&g_socks[i], 0, sizeof(g_socks[i]));
            g_socks[i].used = 1;
            g_socks[i].domain = domain;
            g_socks[i].type = type;
            g_socks[i].proto = protocol;
            return i;
        }
    }
    return -EMFILE;
}

int sock_bind(int sid, const sockaddr_in_t *addr)
{
    socket_t *s = sock_get(sid);
    uint16_t port;
    uint32_t lip;
    if (!s || !addr)
        return -EINVAL;
    if (addr->sin_family != AF_INET)
        return -EAFNOSUPPORT;
    port = ntohs(addr->sin_port);
    lip = ntohl(addr->sin_addr);
    if (port == 0) {
        port = alloc_ephemeral(s->type, s->proto);
        if (!port)
            return -EADDRINUSE;
    } else if (port_in_use(s->type, s->proto, port, lip)) {
        return -EADDRINUSE;
    }
    s->lport = port;
    s->laddr = lip;
    s->bound = 1;
    return 0;
}

int sock_connect(int sid, const sockaddr_in_t *addr)
{
    socket_t *s = sock_get(sid);
    if (!s || !addr)
        return -EINVAL;
    if (addr->sin_family != AF_INET)
        return -EAFNOSUPPORT;
    if (!s->bound) {
        sockaddr_in_t any;
        memset(&any, 0, sizeof(any));
        any.sin_family = AF_INET;
        any.sin_port = 0;
        any.sin_addr = htonl(INADDR_ANY);
        if (sock_bind(sid, &any) < 0)
            return -EADDRINUSE;
    }
    s->raddr = ntohl(addr->sin_addr);
    s->rport = ntohs(addr->sin_port);
    s->connected = 1;
    return 0;
}

static int ensure_bound(socket_t *s, int sid)
{
    sockaddr_in_t any;
    if (s->bound)
        return 0;
    memset(&any, 0, sizeof(any));
    any.sin_family = AF_INET;
    return sock_bind(sid, &any);
}

ssize_t sock_sendto(int sid, const void *buf, size_t len, int flags,
                    const sockaddr_in_t *dst)
{
    socket_t *s = sock_get(sid);
    netif_t *nif = netif_default();
    uint32_t dip;
    uint16_t dport;
    (void)flags;

    if (!s || !buf)
        return -EINVAL;
    if (!nif || !nif->up)
        return -ENETDOWN;
    if (ensure_bound(s, sid) < 0)
        return -EADDRINUSE;

    if (dst) {
        if (dst->sin_family != AF_INET)
            return -EAFNOSUPPORT;
        dip = ntohl(dst->sin_addr);
        dport = ntohs(dst->sin_port);
    } else if (s->connected) {
        dip = s->raddr;
        dport = s->rport;
    } else {
        return -EDESTADDRREQ;
    }

    if (s->type == SOCK_DGRAM) {
        uint8_t udp[8 + SOCK_RX_BYTES];
        uint16_t ulen, csum;
        if (len > SOCK_RX_BYTES)
            return -EMSGSIZE;
        ulen = (uint16_t)(8 + len);
        udp[0] = (uint8_t)(s->lport >> 8);
        udp[1] = (uint8_t)(s->lport & 0xFF);
        udp[2] = (uint8_t)(dport >> 8);
        udp[3] = (uint8_t)(dport & 0xFF);
        udp[4] = (uint8_t)(ulen >> 8);
        udp[5] = (uint8_t)(ulen & 0xFF);
        udp[6] = 0;
        udp[7] = 0;
        memcpy(udp + 8, buf, len);
        /* optional checksum 0 = unused */
        (void)csum;
        if (ipv4_output(nif, dip, IPPROTO_UDP, udp, ulen) < 0)
            return -EHOSTUNREACH;
        return (ssize_t)len;
    }

    if (s->type == SOCK_RAW) {
        if (len > SOCK_RX_BYTES)
            return -EMSGSIZE;
        if (ipv4_output(nif, dip, (uint8_t)s->proto, buf, len) < 0)
            return -EHOSTUNREACH;
        return (ssize_t)len;
    }
    return -EPROTONOSUPPORT;
}

ssize_t sock_recvfrom(int sid, void *buf, size_t len, int flags,
                      sockaddr_in_t *src)
{
    socket_t *s = sock_get(sid);
    sock_pkt_t pkt;
    int spins = 0;
    int wait = !(flags & MSG_DONTWAIT);

    if (!s || !buf)
        return -EINVAL;
    if (ensure_bound(s, sid) < 0)
        return -EADDRINUSE;

    while (rx_pop(s, &pkt) < 0) {
        if (!wait)
            return -EAGAIN;
        net_poll();
        if (++spins > 80000)
            return -EAGAIN;
    }

    if (len > pkt.len)
        len = pkt.len;
    memcpy(buf, pkt.data, len);
    if (src) {
        memset(src, 0, sizeof(*src));
        src->sin_family = AF_INET;
        src->sin_port = htons(pkt.src_port);
        src->sin_addr = htonl(pkt.src_ip);
    }
    return (ssize_t)len;
}

int sock_close(int sid)
{
    socket_t *s = sock_get(sid);
    if (!s)
        return -EBADF;
    memset(s, 0, sizeof(*s));
    return 0;
}

void sock_input_udp(uint32_t src_ip, uint16_t src_port,
                    uint32_t dst_ip, uint16_t dst_port,
                    const void *payload, size_t len)
{
    int i;
    (void)dst_ip;
    for (i = 0; i < SOCK_MAX; i++) {
        socket_t *s = &g_socks[i];
        if (!s->used || s->type != SOCK_DGRAM || !s->bound)
            continue;
        if (s->lport != dst_port)
            continue;
        if (s->laddr != 0 && s->laddr != dst_ip)
            continue;
        if (s->connected && (s->raddr != src_ip ||
                             (s->rport && s->rport != src_port)))
            continue;
        (void)rx_push(s, src_ip, src_port, payload, len);
        return;
    }
}

void sock_input_icmp(uint32_t src_ip, const void *payload, size_t len)
{
    int i;
    for (i = 0; i < SOCK_MAX; i++) {
        socket_t *s = &g_socks[i];
        if (!s->used || s->type != SOCK_RAW || s->proto != IPPROTO_ICMP)
            continue;
        if (s->connected && s->raddr != src_ip)
            continue;
        (void)rx_push(s, src_ip, 0, payload, len);
    }
}
