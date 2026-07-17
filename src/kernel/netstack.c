#include <kernel/netstack.h>
#include <kernel/socket.h>
#include <kernel/netif.h>
#include <kernel/errno.h>
#include <kernel/string.h>

#define ARP_CACHE_MAX 16
#define IP_PROTO_ICMP 1
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

void netstack_init(void)
{
    memset(g_arp, 0, sizeof(g_arp));
    g_ip_id = 1;
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
    tip = ((dst_ip & nif->netmask) == (nif->ip & nif->netmask)) ? dst_ip : nif->gateway;
    if (arp_cache_get(tip, out_mac) == 0)
        return 0;

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

int ipv4_output(netif_t *nif, uint32_t dst_ip, uint8_t proto,
                const void *payload, size_t len)
{
    uint8_t pkt[20 + 1480];
    uint8_t *ip = pkt;
    uint8_t mac[ETH_ALEN];
    uint16_t tot, id, csum;
    uint32_t sip, dip;
    int rc;

    if (!nif || !payload || len > 1480)
        return -EINVAL;
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
    sip = bswap32(nif->ip);
    dip = bswap32(dst_ip);
    memcpy(ip + 12, &sip, 4);
    memcpy(ip + 16, &dip, 4);
    csum = ip_checksum(ip, 20);
    ip[10] = (uint8_t)(csum >> 8);
    ip[11] = (uint8_t)(csum & 0xFF);
    memcpy(ip + 20, payload, len);
    return netif_output(nif, mac, ETH_P_IP, pkt, 20 + len);
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
                       const uint8_t *udp, size_t len)
{
    uint16_t sport, dport, ulen;
    if (len < 8)
        return;
    sport = (uint16_t)((udp[0] << 8) | udp[1]);
    dport = (uint16_t)((udp[2] << 8) | udp[3]);
    ulen = (uint16_t)((udp[4] << 8) | udp[5]);
    if (ulen < 8 || ulen > len)
        ulen = (uint16_t)len;
    sock_input_udp(src_ip, sport, dst_ip, dport, udp + 8, ulen - 8);
}

static void handle_ip(netif_t *nif, const uint8_t *pkt, size_t len)
{
    uint8_t ihl, proto;
    uint16_t tot;
    uint32_t src, dst;

    if (len < 20)
        return;
    ihl = (uint8_t)((pkt[0] & 0x0F) * 4);
    if (ihl < 20 || len < ihl)
        return;
    tot = (uint16_t)((pkt[2] << 8) | pkt[3]);
    if (tot < ihl || tot > len)
        tot = (uint16_t)len;
    proto = pkt[9];
    memcpy(&src, pkt + 12, 4);
    memcpy(&dst, pkt + 16, 4);
    src = bswap32(src);
    dst = bswap32(dst);

    /* Accept unicast to us or local/subnet broadcast. */
    {
        uint32_t bcast = (nif->ip & nif->netmask) | ~nif->netmask;
        if (dst != nif->ip && dst != 0xFFFFFFFFu && dst != bcast)
            return;
    }

    if (proto == IP_PROTO_ICMP)
        handle_icmp(nif, src, pkt + ihl, tot - ihl);
    else if (proto == IP_PROTO_UDP)
        handle_udp(src, dst, pkt + ihl, tot - ihl);
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
