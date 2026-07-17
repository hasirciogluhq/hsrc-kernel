#ifndef MYKERNEL_KERNEL_NETIF_H
#define MYKERNEL_KERNEL_NETIF_H

#include <kernel/types.h>

#define NETIF_NAME_MAX 16
#define NETIF_MAX      4
#define ETH_ALEN       6

typedef struct netif netif_t;

struct netif {
    char     name[NETIF_NAME_MAX];
    uint8_t  mac[ETH_ALEN];
    uint32_t ip;      /* host byte order */
    uint32_t netmask; /* host byte order */
    uint32_t gateway; /* host byte order */
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

/* Driver RX path — feeds Ethernet frames into the IP stack. */
void netif_input(netif_t *nif, const void *frame, size_t len);

/* Poll all registered interfaces (and ARP wait loops). */
void net_poll(void);

/* L2 send helper used by the IP stack. */
int netif_output(netif_t *nif, const uint8_t dst_mac[ETH_ALEN],
                 uint16_t ethertype, const void *payload, size_t len);

#endif
