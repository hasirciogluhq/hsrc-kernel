#include <kernel/netstack.h>
#include <kernel/dhcp.h>
#include <kernel/socket.h>
#include <kernel/netif.h>
#include <kernel/errno.h>
#include <kernel/string.h>

#define ARP_CACHE_MAX 16
#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP  6
#define IP_PROTO_UDP  17

typedef struct {
    uint32_t ip;
    uint8_t  mac[ETH_ALEN];
    int      valid;
} arp_entry_t;

static arp_entry_t g_arp[ARP_CACHE_MAX];
static uint16_t    g_ip_id;

static uint16_t bswap16(uint16_t x) { return (uint16_t)((x << 8) | (x >> 8)); }
static uint32_t bswap32(uint32_t x)
{
    return ((x & 0xFFu) << 24) | ((x & 0xFF00u) << 8) |
           ((x & 0xFF0000u) >> 8) | ((x & 0xFF000000u) >> 24);
}

static int ipv4_is_broadcast(const netif_t *nif, uint32_t ip)
{
    uint32_t bcast;
    if (!nif)
        return 0;
    if (ip == 0xFFFFFFFFu)
        return 1;
    bcast = (nif->ip & nif->netmask) | ~nif->netmask;
    return ip == bcast;
}

uint16_t ip_checksum(const void *data, size_t len)
{
    const uint8_t *b = (const uint8_t *)data;
    uint32_t sum = 0;
    size_t i;
    for (i = 0; i + 1 < len; i += 2)
        sum += (uint32_t)b[i] << 8 | b[i + 1];
    if (i < len)
        sum += (uint32_t)b[i] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)~sum;
}

static uint32_t checksum_add(uint32_t sum, const void *data, size_t len)
{
    const uint8_t *b = (const uint8_t *)data;
    size_t i;
    for (i = 0; i + 1 < len; i += 2)
        sum += (uint32_t)b[i] << 8 | b[i + 1];
    if (i < len)
        sum += (uint32_t)b[i] << 8;
    return sum;
}

static uint16_t checksum_finish(uint32_t sum)
{
    while (sum >> 16)
        sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)~sum;
}

void netstack_init(void)
{
    memset(g_arp, 0, sizeof(g_arp));
    g_ip_id = 1;
    dhcp_init();
}

void netstack_reset_arp(void)
{
    memset(g_arp, 0, sizeof(g_arp));
}

static void arp_cache_put(uint32_t ip, const uint8_t mac[ETH_ALEN])
{
    int i, free_i = -1;
    for (i = 0; i < ARP_CACHE_MAX; i++) {
        if (g_arp[i].valid && g_arp[i].ip == ip) {
            memcpy(g_arp[i].mac, mac, ETH_ALEN);
            return;
        }
        if (!g_arp[i].valid && free_i < 0)
            free_i = i;
    }
    if (free_i < 0)
        free_i = 0;
    g_arp[free_i].ip = ip;
    memcpy(g_arp[free_i].mac, mac, ETH_ALEN);
    g_arp[free_i].valid = 1;
}

static int arp_cache_get(uint32_t ip, uint8_t out[ETH_ALEN])
{
    int i;
    for (i = 0; i < ARP_CACHE_MAX; i++) {
        if (g_arp[i].valid && g_arp[i].ip == ip) {
            memcpy(out, g_arp[i].mac, ETH_ALEN);
            return 0;
        }
    }
    return -1;
}

static void send_arp_request(netif_t *nif, uint32_t tip)
{
    uint8_t p[28];
    uint32_t sip = bswap32(nif->ip);
    uint32_t t = bswap32(tip);
    memset(p, 0, sizeof(p));
    p[0] = 0x00;
    p[1] = 0x01;
    p[2] = 0x08;
    p[3] = 0x00;
    p[4] = 6;
    p[5] = 4;
    p[6] = 0x00;
    p[7] = 0x01;
    memcpy(p + 8, nif->mac, 6);
    memcpy(p + 14, &sip, 4);
    memset(p + 18, 0, 6);
    memcpy(p + 24, &t, 4);
    {
        uint8_t bcast[ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
        (void)netif_output(nif, bcast, ETH_P_ARP, p, sizeof(p));
    }
}

static void send_arp_reply(netif_t *nif, const uint8_t *tha, uint32_t tip)
{
    uint8_t p[28];
    uint32_t sip = bswap32(nif->ip);
    uint32_t t = bswap32(tip);
    p[0] = 0x00;
    p[1] = 0x01;
    p[2] = 0x08;
    p[3] = 0x00;
    p[4] = 6;
    p[5] = 4;
    p[6] = 0x00;
    p[7] = 0x02;
    memcpy(p + 8, nif->mac, 6);
    memcpy(p + 14, &sip, 4);
    memcpy(p + 18, tha, 6);
    memcpy(p + 24, &t, 4);
    (void)netif_output(nif, tha, ETH_P_ARP, p, sizeof(p));
}

static void handle_arp(netif_t *nif, const uint8_t *p, size_t len)
{
    uint16_t op;
    uint32_t tip, sip;
    if (len < 28)
        return;
    if (p[0] != 0x00 || p[1] != 0x01 || p[2] != 0x08 || p[3] != 0x00 ||
        p[4] != 6 || p[5] != 4)
        return;
    op = (uint16_t)((p[6] << 8) | p[7]);
    memcpy(&sip, p + 14, 4);
    sip = bswap32(sip);
    memcpy(&tip, p + 24, 4);
    tip = bswap32(tip);
    arp_cache_put(sip, p + 8);
    if (op == 1 && tip == nif->ip)
        send_arp_reply(nif, p + 8, sip);
}

int arp_resolve(netif_t *nif, uint32_t dst_ip, uint8_t out_mac[ETH_ALEN])
{
    uint32_t tip;
    int t, i;

    if (!nif || !out_mac)
        return -EINVAL;
    if (!nif->up)
        return -ENETDOWN;
    if (dst_ip == 0)
        return -EINVAL;
    if (ipv4_is_broadcast(nif, dst_ip)) {
        memset(out_mac, 0xFF, ETH_ALEN);
        return 0;
    }
    if (((dst_ip & nif->netmask) == (nif->ip & nif->netmask)))
        tip = dst_ip;
    else if (nif->gateway != 0)
        tip = nif->gateway;
    else
        return -EHOSTUNREACH;
    if (tip == nif->ip) {
        memcpy(out_mac, nif->mac, ETH_ALEN);
        return 0;
    }
    if (arp_cache_get(tip, out_mac) == 0)
        return 0;

    /* UDP sendto() exits through this ARP/gateway path; recvfrom() succeeds once
       vnet_poll() feeds RX Ethernet frames back through netif_input(). */
    for (t = 0; t < 40; t++) {
        send_arp_request(nif, tip);
        for (i = 0; i < 4000; i++) {
            net_poll();
            if (arp_cache_get(tip, out_mac) == 0)
                return 0;
        }
    }
    return -EHOSTUNREACH;
}

static int ipv4_output_ex(netif_t *nif, uint32_t src_ip, uint32_t dst_ip, uint8_t proto,
                          const void *payload, size_t len)
{
    uint8_t pkt[20 + 1480];
    uint8_t *ip = pkt;
    uint8_t mac[ETH_ALEN];
    uint16_t tot, id, csum;
    uint32_t sip, dip;
    int rc;

    if (!nif || !payload)
        return -EINVAL;
    if (!nif->up)
        return -ENETDOWN;
    if (nif->mtu <= 20 || len > (size_t)(nif->mtu - 20) || len > 1480)
        return -EMSGSIZE;
    rc = arp_resolve(nif, dst_ip, mac);
    if (rc < 0)
        return rc;

    memset(ip, 0, 20);
    ip[0] = 0x45;
    tot = bswap16((uint16_t)(20 + len));
    memcpy(ip + 2, &tot, 2);
    id = bswap16(g_ip_id++);
    memcpy(ip + 4, &id, 2);
    ip[8] = 64;
    ip[9] = proto;
    sip = bswap32(src_ip);
    dip = bswap32(dst_ip);
    memcpy(ip + 12, &sip, 4);
    memcpy(ip + 16, &dip, 4);
    csum = ip_checksum(ip, 20);
    ip[10] = (uint8_t)(csum >> 8);
    ip[11] = (uint8_t)(csum & 0xFF);
    memcpy(ip + 20, payload, len);
    return netif_output(nif, mac, ETH_P_IP, pkt, 20 + len);
}

int ipv4_output(netif_t *nif, uint32_t dst_ip, uint8_t proto,
                const void *payload, size_t len)
{
    if (!nif)
        return -EINVAL;
    return ipv4_output_ex(nif, nif->ip, dst_ip, proto, payload, len);
}

int udp_output(netif_t *nif, uint32_t src_ip, uint16_t src_port,
               uint32_t dst_ip, uint16_t dst_port,
               const void *payload, size_t len)
{
    uint8_t udp[8 + 1480];
    uint16_t ulen;

    if (!nif || !payload)
        return -EINVAL;
    if (len > 1480)
        return -EMSGSIZE;

    ulen = (uint16_t)(8 + len);
    udp[0] = (uint8_t)(src_port >> 8);
    udp[1] = (uint8_t)(src_port & 0xFF);
    udp[2] = (uint8_t)(dst_port >> 8);
    udp[3] = (uint8_t)(dst_port & 0xFF);
    udp[4] = (uint8_t)(ulen >> 8);
    udp[5] = (uint8_t)(ulen & 0xFF);
    udp[6] = 0;
    udp[7] = 0;
    memcpy(udp + 8, payload, len);
    return ipv4_output_ex(nif, src_ip, dst_ip, IP_PROTO_UDP, udp, ulen);
}

int tcp_output(netif_t *nif, uint32_t src_ip, uint16_t src_port,
               uint32_t dst_ip, uint16_t dst_port,
               uint32_t seq, uint32_t ack, uint8_t flags, uint16_t window,
               const void *payload, size_t len)
{
    uint8_t seg[20 + 1460];
    uint8_t pseudo[4];
    uint32_t sum = 0;
    uint16_t tcp_len;
    uint32_t be32;
    uint16_t be16, csum;

    if (!nif)
        return -EINVAL;
    if ((payload == NULL && len != 0) || len > 1460)
        return -EMSGSIZE;

    memset(seg, 0, sizeof(seg));
    seg[0] = (uint8_t)(src_port >> 8);
    seg[1] = (uint8_t)(src_port & 0xFF);
    seg[2] = (uint8_t)(dst_port >> 8);
    seg[3] = (uint8_t)(dst_port & 0xFF);
    be32 = bswap32(seq);
    memcpy(seg + 4, &be32, 4);
    be32 = bswap32(ack);
    memcpy(seg + 8, &be32, 4);
    seg[12] = 0x50;
    seg[13] = flags;
    be16 = bswap16(window);
    memcpy(seg + 14, &be16, 2);
    if (len)
        memcpy(seg + 20, payload, len);

    tcp_len = (uint16_t)(20 + len);
    be32 = bswap32(src_ip);
    sum = checksum_add(sum, &be32, 4);
    be32 = bswap32(dst_ip);
    sum = checksum_add(sum, &be32, 4);
    pseudo[0] = 0;
    pseudo[1] = IP_PROTO_TCP;
    pseudo[2] = (uint8_t)(tcp_len >> 8);
    pseudo[3] = (uint8_t)(tcp_len & 0xFF);
    sum = checksum_add(sum, pseudo, sizeof(pseudo));
    sum = checksum_add(sum, seg, tcp_len);
    csum = checksum_finish(sum);
    seg[16] = (uint8_t)(csum >> 8);
    seg[17] = (uint8_t)(csum & 0xFF);

    return ipv4_output_ex(nif, src_ip, dst_ip, IP_PROTO_TCP, seg, tcp_len);
}

static void send_icmp_echo_reply(netif_t *nif, uint32_t dst_ip,
                                 const uint8_t *req, size_t len)
{
    uint8_t buf[1480];
    uint16_t csum;
    if (len < 8 || len > sizeof(buf))
        return;
    memcpy(buf, req, len);
    buf[0] = 0; /* echo reply */
    buf[1] = 0;
    buf[2] = 0;
    buf[3] = 0;
    csum = ip_checksum(buf, len);
    buf[2] = (uint8_t)(csum >> 8);
    buf[3] = (uint8_t)(csum & 0xFF);
    (void)ipv4_output(nif, dst_ip, IP_PROTO_ICMP, buf, len);
}

static void handle_icmp(netif_t *nif, uint32_t src_ip, const uint8_t *icmp, size_t len)
{
    if (len < 8)
        return;
    if (icmp[0] == 8) /* echo request */
        send_icmp_echo_reply(nif, src_ip, icmp, len);
    sock_input_icmp(src_ip, icmp, len);
}

static void handle_udp(uint32_t src_ip, uint32_t dst_ip,
                       netif_t *nif, const uint8_t *udp, size_t len)
{
    uint16_t sport, dport, ulen;
    if (len < 8)
        return;
    sport = (uint16_t)((udp[0] << 8) | udp[1]);
    dport = (uint16_t)((udp[2] << 8) | udp[3]);
    ulen = (uint16_t)((udp[4] << 8) | udp[5]);
    if (ulen < 8 || ulen > len)
        ulen = (uint16_t)len;
    if (dhcp_input(nif, src_ip, dst_ip, sport, dport, udp + 8, ulen - 8))
        return;
    sock_input_udp(src_ip, sport, dst_ip, dport, udp + 8, ulen - 8);
}

static void handle_tcp(netif_t *nif, uint32_t src_ip, uint32_t dst_ip,
                       const uint8_t *tcp, size_t len)
{
    if (len < 20)
        return;
    sock_input_tcp(nif, src_ip, dst_ip, tcp, len);
}

static void handle_ip(netif_t *nif, const uint8_t *pkt, size_t len)
{
    uint8_t ihl, proto;
    uint16_t tot, frag;
    uint32_t src, dst;

    if (len < 20)
        return;
    if ((pkt[0] >> 4) != 4)
        return;
    ihl = (uint8_t)((pkt[0] & 0x0F) * 4);
    if (ihl < 20 || len < ihl)
        return;
    if (ip_checksum(pkt, ihl) != 0)
        return;
    tot = (uint16_t)((pkt[2] << 8) | pkt[3]);
    if (tot < ihl || tot > len)
        return;
    frag = (uint16_t)(((uint16_t)pkt[6] << 8) | pkt[7]);
    if (frag & 0x3FFFu)
        return; /* Fragment reassembly is not implemented yet. */
    proto = pkt[9];
    memcpy(&src, pkt + 12, 4);
    memcpy(&dst, pkt + 16, 4);
    src = bswap32(src);
    dst = bswap32(dst);

    /* Accept unicast to us or local/subnet broadcast. */
    {
        if (dst != nif->ip && !ipv4_is_broadcast(nif, dst))
            return;
    }

    if (proto == IP_PROTO_ICMP)
        handle_icmp(nif, src, pkt + ihl, tot - ihl);
    else if (proto == IP_PROTO_UDP)
        handle_udp(src, dst, nif, pkt + ihl, tot - ihl);
    else if (proto == IP_PROTO_TCP)
        handle_tcp(nif, src, dst, pkt + ihl, tot - ihl);
}

void netstack_rx_ethernet(netif_t *nif, const uint8_t *frame, size_t len)
{
    uint16_t type;
    if (!nif || !frame || len < 14)
        return;
    type = (uint16_t)((frame[12] << 8) | frame[13]);
    if (type == ETH_P_ARP)
        handle_arp(nif, frame + 14, len - 14);
    else if (type == ETH_P_IP)
        handle_ip(nif, frame + 14, len - 14);
}
