#include <kernel/netif.h>
#include <kernel/dhcp.h>
#include <kernel/netstack.h>
#include <kernel/errno.h>
#include <kernel/string.h>

static netif_t *g_ifs[NETIF_MAX];
static int      g_nifs;

static netif_t *netif_lookup(const char *name)
{
    if (name && name[0])
        return netif_by_name(name);
    return netif_default();
}

void netif_init(void)
{
    g_nifs = 0;
    memset(g_ifs, 0, sizeof(g_ifs));
    netstack_init();
}

int netif_register(netif_t *nif)
{
    int rc;
    if (!nif || !nif->tx || g_nifs >= NETIF_MAX)
        return -EINVAL;
    if (!nif->name[0])
        return -EINVAL;
    if (nif->mtu <= 0)
        nif->mtu = 1500;
    nif->up = 1;
    g_ifs[g_nifs++] = nif;
    rc = dhcp_configure(nif);
    (void)rc;
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

void netif_set_dns(netif_t *nif, uint32_t dns1, uint32_t dns2)
{
    if (!nif)
        return;
    nif->dns1 = dns1;
    nif->dns2 = dns2;
}

int netif_get_info(const char *name, netif_info_t *out)
{
    netif_t *nif = netif_lookup(name);
    if (!out)
        return -EINVAL;
    if (!nif)
        return -ENODEV;
    memset(out, 0, sizeof(*out));
    strncpy(out->name, nif->name, sizeof(out->name) - 1);
    memcpy(out->mac, nif->mac, ETH_ALEN);
    out->ip = nif->ip;
    out->netmask = nif->netmask;
    out->gateway = nif->gateway;
    out->dns1 = nif->dns1;
    out->dns2 = nif->dns2;
    out->mtu = nif->mtu;
    out->up = nif->up;
    return 0;
}

int netif_set_info(const char *name, const netif_info_t *cfg)
{
    netif_t *nif = netif_lookup(name);
    if (!cfg)
        return -EINVAL;
    if (!nif)
        return -ENODEV;
    netif_set_addr(nif, cfg->ip, cfg->netmask, cfg->gateway);
    netif_set_dns(nif, cfg->dns1, cfg->dns2);
    if (cfg->mtu > 0)
        nif->mtu = cfg->mtu;
    nif->up = cfg->up ? 1 : 0;
    netstack_reset_arp();
    return 0;
}

int netif_dhcp_renew(const char *name)
{
    netif_t *nif = netif_lookup(name);
    if (!nif)
        return -ENODEV;
    return dhcp_configure(nif);
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
