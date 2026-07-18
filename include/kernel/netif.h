#ifndef MYKERNEL_KERNEL_NETIF_H
#define MYKERNEL_KERNEL_NETIF_H

#include <kernel/types.h>

#define NETIF_NAME_MAX 16
#define NETIF_MAX      4
#define ETH_ALEN       6

typedef struct netif netif_t;
typedef struct netif_info netif_info_t;

struct netif_info {
    char     name[NETIF_NAME_MAX];
    uint8_t  mac[ETH_ALEN];
    uint32_t ip;      /* host byte order */
    uint32_t netmask; /* host byte order */
    uint32_t gateway; /* host byte order */
    uint32_t dns1;    /* host byte order */
    uint32_t dns2;    /* host byte order */
    int      mtu;
    int      up;
};

struct netif {
    char     name[NETIF_NAME_MAX];
    uint8_t  mac[ETH_ALEN];
    uint32_t ip;      /* host byte order */
    uint32_t netmask; /* host byte order */
    uint32_t gateway; /* host byte order */
    uint32_t dns1;    /* host byte order */
    uint32_t dns2;    /* host byte order */
    int      mtu;
    int      up;

    /* Driver: transmit a raw Ethernet frame. */
    int (*tx)(netif_t *nif, const void *frame, size_t len);
    /* Optional: poll hardware RX into the stack. */
    void (*poll)(netif_t *nif);
    void *priv;
};

void     netif_init(void);
int      netif_register(netif_t *nif);
netif_t *netif_default(void);
netif_t *netif_by_name(const char *name);
void     netif_set_addr(netif_t *nif, uint32_t ip, uint32_t netmask, uint32_t gateway);
void     netif_set_dns(netif_t *nif, uint32_t dns1, uint32_t dns2);
int      netif_get_info(const char *name, netif_info_t *out);
int      netif_set_info(const char *name, const netif_info_t *cfg);
int      netif_dhcp_renew(const char *name);

/* Driver RX path - feeds Ethernet frames into the IP stack. */
void netif_input(netif_t *nif, const void *frame, size_t len);

/* Poll all registered interfaces (and ARP wait loops). */
void net_poll(void);

/* L2 send helper used by the IP stack. */
int netif_output(netif_t *nif, const uint8_t dst_mac[ETH_ALEN],
                 uint16_t ethertype, const void *payload, size_t len);

#endif
