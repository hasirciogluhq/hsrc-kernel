#include <kernel/netif.h>
#include <kernel/netstack.h>
#include <kernel/errno.h>
#include <kernel/string.h>

static netif_t *g_ifs[NETIF_MAX];
static int      g_nifs;

void netif_init(void)
{
    g_nifs = 0;
    memset(g_ifs, 0, sizeof(g_ifs));
    netstack_init();
}

int netif_register(netif_t *nif)
{
    if (!nif || !nif->tx || g_nifs >= NETIF_MAX)
        return -EINVAL;
    if (!nif->name[0])
        return -EINVAL;
    if (nif->mtu <= 0)
        nif->mtu = 1500;
    nif->up = 1;
    g_ifs[g_nifs++] = nif;
    return 0;
}

netif_t *netif_default(void)
{
    return g_nifs > 0 ? g_ifs[0] : NULL;
}

netif_t *netif_by_name(const char *name)
{
    int i;
    if (!name)
        return NULL;
    for (i = 0; i < g_nifs; i++) {
        if (strcmp(g_ifs[i]->name, name) == 0)
            return g_ifs[i];
    }
    return NULL;
}

void netif_set_addr(netif_t *nif, uint32_t ip, uint32_t netmask, uint32_t gateway)
{
    if (!nif)
        return;
    nif->ip = ip;
    nif->netmask = netmask;
    nif->gateway = gateway;
}

void netif_input(netif_t *nif, const void *frame, size_t len)
{
    if (!nif || !frame || len < 14)
        return;
    netstack_rx_ethernet(nif, (const uint8_t *)frame, len);
}

void net_poll(void)
{
    int i;
    for (i = 0; i < g_nifs; i++) {
        if (g_ifs[i]->poll)
            g_ifs[i]->poll(g_ifs[i]);
    }
}

int netif_output(netif_t *nif, const uint8_t dst_mac[ETH_ALEN],
                 uint16_t ethertype, const void *payload, size_t len)
{
    uint8_t frame[1514];
    if (!nif || !nif->tx || !dst_mac || !payload)
        return -EINVAL;
    if (len + 14 > sizeof(frame))
        return -EINVAL;
    memcpy(frame, dst_mac, ETH_ALEN);
    memcpy(frame + 6, nif->mac, ETH_ALEN);
    frame[12] = (uint8_t)(ethertype >> 8);
    frame[13] = (uint8_t)(ethertype & 0xFF);
    memcpy(frame + 14, payload, len);
    return nif->tx(nif, frame, len + 14);
}
