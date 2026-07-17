#ifndef MYKERNEL_KERNEL_NETSTACK_H
#define MYKERNEL_KERNEL_NETSTACK_H

#include <kernel/netif.h>
#include <kernel/types.h>

#define ETH_P_IP  0x0800
#define ETH_P_ARP 0x0806

void netstack_init(void);

/* Called from netif_input for ethertype demux. */
void netstack_rx_ethernet(netif_t *nif, const uint8_t *frame, size_t len);

/* IPv4 TX: payload is after IP header (UDP/ICMP body with L4 already built). */
int ipv4_output(netif_t *nif, uint32_t dst_ip, uint8_t proto,
                const void *payload, size_t len);
int udp_output(netif_t *nif, uint32_t src_ip, uint16_t src_port,
               uint32_t dst_ip, uint16_t dst_port,
               const void *payload, size_t len);
int tcp_output(netif_t *nif, uint32_t src_ip, uint16_t src_port,
               uint32_t dst_ip, uint16_t dst_port,
               uint32_t seq, uint32_t ack, uint8_t flags, uint16_t window,
               const void *payload, size_t len);

/* Resolve next-hop MAC (ARP). Blocks via net_poll. */
int arp_resolve(netif_t *nif, uint32_t dst_ip, uint8_t out_mac[ETH_ALEN]);
void netstack_reset_arp(void);

uint16_t ip_checksum(const void *data, size_t len);

#endif
