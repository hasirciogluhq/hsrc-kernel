#ifndef MYKERNEL_KERNEL_DHCP_H
#define MYKERNEL_KERNEL_DHCP_H

#include <kernel/netif.h>
#include <kernel/types.h>

void dhcp_init(void);
int  dhcp_configure(netif_t *nif);
int  dhcp_input(netif_t *nif, uint32_t src_ip, uint32_t dst_ip,
                uint16_t src_port, uint16_t dst_port,
                const void *payload, size_t len);

#endif
