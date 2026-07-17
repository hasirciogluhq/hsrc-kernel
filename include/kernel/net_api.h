#ifndef MYKERNEL_KERNEL_NET_API_H
#define MYKERNEL_KERNEL_NET_API_H

/*
 * Networking surface for kernel modules and the rest of the kernel.
 * Drivers register a netif; apps use BSD-style sockets (see socket.h).
 */
#include <kernel/netif.h>
#include <kernel/socket.h>

#endif
