#include <kernel/dhcp.h>
#include <kernel/netstack.h>
#include <kernel/errno.h>
#include <kernel/string.h>
#include <drivers/vga.h>

#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67

#define DHCP_OPT_PAD          0
#define DHCP_OPT_SUBNET_MASK  1
#define DHCP_OPT_ROUTER       3
#define DHCP_OPT_DNS          6
#define DHCP_OPT_REQ_IP       50
#define DHCP_OPT_LEASE_TIME   51
#define DHCP_OPT_MSG_TYPE     53
#define DHCP_OPT_SERVER_ID    54
#define DHCP_OPT_PARAM_REQ    55
#define DHCP_OPT_CLIENT_ID    61
#define DHCP_OPT_END          255

#define DHCP_MSG_DISCOVER 1
#define DHCP_MSG_OFFER    2
#define DHCP_MSG_REQUEST  3
#define DHCP_MSG_ACK      5
#define DHCP_MSG_NAK      6

#define DHCP_STATE_IDLE       0
#define DHCP_STATE_SELECTING  1
#define DHCP_STATE_REQUESTING 2

#define DHCP_PKT_MAX          300
#define DHCP_RETRIES          5
#define DHCP_WAIT_POLLS       20000

typedef struct {
    netif_t *nif;
    uint32_t xid;
    uint32_t offered_ip;
    uint32_t server_id;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns1;
    uint32_t dns2;
    uint8_t state;
    uint8_t active;
    uint8_t reply_type;
} dhcp_client_t;

static dhcp_client_t g_clients[NETIF_MAX];
static uint32_t      g_xid_seed;

static uint16_t bswap16(uint16_t x) { return (uint16_t)((x << 8) | (x >> 8)); }
static uint32_t bswap32(uint32_t x)
{
    return ((x & 0xFFu) << 24) | ((x & 0xFF00u) << 8) |
           ((x & 0xFF0000u) >> 8) | ((x & 0xFF000000u) >> 24);
}

static dhcp_client_t *dhcp_client_for(netif_t *nif, int create)
{
    int i, free_i = -1;
    for (i = 0; i < NETIF_MAX; i++) {
        if (g_clients[i].nif == nif)
            return &g_clients[i];
        if (create && !g_clients[i].nif && free_i < 0)
            free_i = i;
    }
    if (!create || free_i < 0)
        return NULL;
    memset(&g_clients[free_i], 0, sizeof(g_clients[free_i]));
    g_clients[free_i].nif = nif;
    return &g_clients[free_i];
}

static void put_opt_u32(uint8_t *dst, uint32_t v)
{
    uint32_t be = bswap32(v);
    memcpy(dst, &be, 4);
}

static uint32_t get_u32(const uint8_t *src)
{
    uint32_t v;
    memcpy(&v, src, 4);
    return bswap32(v);
}

static const uint8_t *dhcp_option_ptr(const uint8_t *pkt, size_t len, uint8_t code, uint8_t *out_len)
{
    size_t off = 240;
    if (out_len)
        *out_len = 0;
    while (off < len) {
        uint8_t opt = pkt[off++];
        uint8_t opt_len;
        if (opt == DHCP_OPT_PAD)
            continue;
        if (opt == DHCP_OPT_END)
            break;
        if (off >= len)
            break;
        opt_len = pkt[off++];
        if (off + opt_len > len)
            break;
        if (opt == code) {
            if (out_len)
                *out_len = opt_len;
            return pkt + off;
        }
        off += opt_len;
    }
    return NULL;
}

static size_t dhcp_build_packet(netif_t *nif, dhcp_client_t *cli,
                                uint8_t msg_type, uint8_t *pkt, size_t pkt_cap)
{
    uint8_t *opt;
    uint32_t xid;
    uint16_t flags;

    if (!nif || !cli || !pkt || pkt_cap < DHCP_PKT_MAX)
        return 0;

    memset(pkt, 0, pkt_cap);
    pkt[0] = 1;
    pkt[1] = 1;
    pkt[2] = 6;

    xid = bswap32(cli->xid);
    memcpy(pkt + 4, &xid, 4);
    flags = bswap16(0x8000u);
    memcpy(pkt + 10, &flags, 2);
    memcpy(pkt + 28, nif->mac, ETH_ALEN);

    pkt[236] = 99;
    pkt[237] = 130;
    pkt[238] = 83;
    pkt[239] = 99;

    opt = pkt + 240;
    *opt++ = DHCP_OPT_MSG_TYPE;
    *opt++ = 1;
    *opt++ = msg_type;

    *opt++ = DHCP_OPT_CLIENT_ID;
    *opt++ = 7;
    *opt++ = 1;
    memcpy(opt, nif->mac, ETH_ALEN);
    opt += ETH_ALEN;

    *opt++ = DHCP_OPT_PARAM_REQ;
    *opt++ = 5;
    *opt++ = DHCP_OPT_SUBNET_MASK;
    *opt++ = DHCP_OPT_ROUTER;
    *opt++ = DHCP_OPT_DNS;
    *opt++ = DHCP_OPT_LEASE_TIME;
    *opt++ = DHCP_OPT_SERVER_ID;

    if (msg_type == DHCP_MSG_REQUEST) {
        *opt++ = DHCP_OPT_REQ_IP;
        *opt++ = 4;
        put_opt_u32(opt, cli->offered_ip);
        opt += 4;

        if (cli->server_id != 0) {
            *opt++ = DHCP_OPT_SERVER_ID;
            *opt++ = 4;
            put_opt_u32(opt, cli->server_id);
            opt += 4;
        }
    }

    *opt++ = DHCP_OPT_END;
    return (size_t)(opt - pkt);
}

static int dhcp_send(netif_t *nif, dhcp_client_t *cli, uint8_t msg_type)
{
    uint8_t pkt[DHCP_PKT_MAX];
    size_t len = dhcp_build_packet(nif, cli, msg_type, pkt, sizeof(pkt));
    if (len == 0)
        return -EINVAL;
    return udp_output(nif, 0, DHCP_CLIENT_PORT, 0xFFFFFFFFu, DHCP_SERVER_PORT, pkt, len);
}

static int dhcp_wait_reply(netif_t *nif, dhcp_client_t *cli, uint8_t want)
{
    int attempt, spin;
    uint8_t req = (want == DHCP_MSG_OFFER) ? DHCP_MSG_DISCOVER : DHCP_MSG_REQUEST;

    for (attempt = 0; attempt < DHCP_RETRIES; attempt++) {
        cli->reply_type = 0;
        if (dhcp_send(nif, cli, req) < 0)
            continue;
        for (spin = 0; spin < DHCP_WAIT_POLLS; spin++) {
            net_poll();
            if (cli->reply_type == want)
                return 0;
            if (cli->reply_type == DHCP_MSG_NAK)
                return -EHOSTUNREACH;
        }
    }
    return -EAGAIN;
}

void dhcp_init(void)
{
    memset(g_clients, 0, sizeof(g_clients));
    g_xid_seed = 0x4D4B0000u;
}

int dhcp_configure(netif_t *nif)
{
    dhcp_client_t *cli;
    uint32_t prev_ip, prev_mask, prev_gw, prev_dns1, prev_dns2;
    int rc;

    if (!nif)
        return -EINVAL;

    cli = dhcp_client_for(nif, 1);
    if (!cli)
        return -ENOMEM;

    prev_ip = nif->ip;
    prev_mask = nif->netmask;
    prev_gw = nif->gateway;
    prev_dns1 = nif->dns1;
    prev_dns2 = nif->dns2;

    memset(cli, 0, sizeof(*cli));
    cli->nif = nif;
    cli->xid = ++g_xid_seed ^ ((uint32_t)nif->mac[4] << 8) ^ nif->mac[5];
    cli->active = 1;
    cli->state = DHCP_STATE_SELECTING;

    rc = dhcp_wait_reply(nif, cli, DHCP_MSG_OFFER);
    if (rc < 0)
        goto fallback;

    cli->state = DHCP_STATE_REQUESTING;
    rc = dhcp_wait_reply(nif, cli, DHCP_MSG_ACK);
    if (rc < 0)
        goto fallback;

    if (cli->offered_ip == 0)
        goto fallback;

    netif_set_addr(nif,
                   cli->offered_ip,
                   cli->netmask ? cli->netmask : prev_mask,
                   cli->gateway ? cli->gateway : prev_gw);
    netif_set_dns(nif,
                  cli->dns1 ? cli->dns1 : prev_dns1,
                  cli->dns2 ? cli->dns2 : prev_dns2);
    netstack_reset_arp();
    cli->active = 0;
    cli->state = DHCP_STATE_IDLE;
    vga_print("dhcp: lease acquired\n");
    return 0;

fallback:
    netif_set_addr(nif, prev_ip, prev_mask, prev_gw);
    netif_set_dns(nif, prev_dns1, prev_dns2);
    netstack_reset_arp();
    cli->active = 0;
    cli->state = DHCP_STATE_IDLE;
    vga_print("dhcp: fallback static config\n");
    return rc;
}

int dhcp_input(netif_t *nif, uint32_t src_ip, uint32_t dst_ip,
               uint16_t src_port, uint16_t dst_port,
               const void *payload, size_t len)
{
    const uint8_t *pkt = (const uint8_t *)payload;
    dhcp_client_t *cli;
    uint8_t opt_len;
    const uint8_t *msg_type;
    const uint8_t *opt;
    uint32_t xid;

    (void)dst_ip;
    if (!nif || !pkt || src_port != DHCP_SERVER_PORT || dst_port != DHCP_CLIENT_PORT)
        return 0;

    cli = dhcp_client_for(nif, 0);
    if (!cli || !cli->active || len < 240)
        return 0;
    if (pkt[0] != 2 || pkt[1] != 1 || pkt[2] != 6)
        return 0;

    xid = get_u32(pkt + 4);
    if (xid != cli->xid)
        return 0;
    if (memcmp(pkt + 28, nif->mac, ETH_ALEN) != 0)
        return 0;
    if (pkt[236] != 99 || pkt[237] != 130 || pkt[238] != 83 || pkt[239] != 99)
        return 0;

    msg_type = dhcp_option_ptr(pkt, len, DHCP_OPT_MSG_TYPE, &opt_len);
    if (!msg_type || opt_len != 1)
        return 1;

    cli->offered_ip = get_u32(pkt + 16);

    opt = dhcp_option_ptr(pkt, len, DHCP_OPT_SERVER_ID, &opt_len);
    cli->server_id = (opt && opt_len == 4) ? get_u32(opt) : src_ip;

    opt = dhcp_option_ptr(pkt, len, DHCP_OPT_SUBNET_MASK, &opt_len);
    if (opt && opt_len == 4)
        cli->netmask = get_u32(opt);

    opt = dhcp_option_ptr(pkt, len, DHCP_OPT_ROUTER, &opt_len);
    if (opt && opt_len >= 4)
        cli->gateway = get_u32(opt);

    opt = dhcp_option_ptr(pkt, len, DHCP_OPT_DNS, &opt_len);
    if (opt && opt_len >= 4) {
        cli->dns1 = get_u32(opt);
        if (opt_len >= 8)
            cli->dns2 = get_u32(opt + 4);
    }

    if (*msg_type == DHCP_MSG_OFFER && cli->state == DHCP_STATE_SELECTING)
        cli->reply_type = DHCP_MSG_OFFER;
    else if (*msg_type == DHCP_MSG_ACK && cli->state == DHCP_STATE_REQUESTING)
        cli->reply_type = DHCP_MSG_ACK;
    else if (*msg_type == DHCP_MSG_NAK)
        cli->reply_type = DHCP_MSG_NAK;

    return 1;
}
