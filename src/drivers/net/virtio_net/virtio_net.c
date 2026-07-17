#include <kernel/netif.h>
#include <kernel/errno.h>
#include <kernel/heap.h>
#include <kernel/string.h>
#include <drivers/driver.h>
#include <drivers/vga.h>
#include <drivers/pci.h>

#define PCI_VENDOR_VIRTIO     0x1AF4
#define PCI_DEVICE_NET_MODERN 0x1041

#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2
#define VIRTIO_PCI_CAP_ISR_CFG    3
#define VIRTIO_PCI_CAP_DEVICE_CFG 4

#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER      2
#define VIRTIO_STATUS_DRIVER_OK   4
#define VIRTIO_STATUS_FEATURES_OK 8

#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2

#define VIRTIO_NET_F_MAC 5

#define RX_BUFS 8
#define PKT_MAX 1514
#define NET_HDR_SIZE 12
#define VQ_MAX  16

typedef struct {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed)) vdesc_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} __attribute__((packed)) vavail_t;

typedef struct {
    uint32_t id;
    uint32_t len;
} __attribute__((packed)) vused_elem_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    vused_elem_t ring[];
} __attribute__((packed)) vused_t;

typedef struct {
    uint8_t  flags;
    uint8_t  gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
} __attribute__((packed)) virtio_net_hdr_t;

typedef struct {
    vdesc_t *desc;
    vavail_t *avail;
    vused_t *used;
    void *mem;
    uint16_t size;
    uint16_t free_head;
    uint16_t num_free;
    uint16_t last_used;
    uint16_t notify_off;
} vq_t;

typedef struct {
    pci_device_t pci;
    volatile uint8_t *common;
    volatile uint8_t *notify;
    volatile uint8_t *isr;
    volatile uint8_t *devcfg;
    uint32_t notify_mult;
    vq_t rxq;
    vq_t txq;
    uint8_t *rx_bufs[RX_BUFS];
    virtio_net_hdr_t tx_hdr;
    uint8_t tx_pkt[PKT_MAX];
    netif_t nif;
} vnet_t;

static vnet_t *g_vnet;

static inline void mmio_w8(volatile uint8_t *p, uint8_t v) { *p = v; }
static inline uint8_t mmio_r8(volatile uint8_t *p) { return *p; }
static inline void mmio_w16(volatile uint8_t *p, uint16_t v) { *(volatile uint16_t *)p = v; }
static inline uint16_t mmio_r16(volatile uint8_t *p) { return *(volatile uint16_t *)p; }
static inline void mmio_w32(volatile uint8_t *p, uint32_t v) { *(volatile uint32_t *)p = v; }
static inline uint32_t mmio_r32(volatile uint8_t *p) { return *(volatile uint32_t *)p; }

static volatile uint8_t *map_bar(const pci_device_t *pci, uint8_t bar, uint32_t off)
{
    uint32_t base = pci_bar_phys(pci, (int)bar);
    if (!base)
        return NULL;
    return (volatile uint8_t *)(uintptr_t)(base + off);
}

static int parse_caps(vnet_t *vd)
{
    uint16_t status = pci_config_read16(vd->pci.bus, vd->pci.slot, vd->pci.func, 0x06);
    uint8_t cap;
    if (!(status & 0x10))
        return -1;
    cap = pci_config_read8(vd->pci.bus, vd->pci.slot, vd->pci.func, 0x34);
    while (cap) {
        uint8_t id = pci_config_read8(vd->pci.bus, vd->pci.slot, vd->pci.func, cap);
        if (id == 0x09) {
            uint8_t cfg = pci_config_read8(vd->pci.bus, vd->pci.slot, vd->pci.func, (uint8_t)(cap + 3));
            uint8_t bar = pci_config_read8(vd->pci.bus, vd->pci.slot, vd->pci.func, (uint8_t)(cap + 4));
            uint32_t offset =
                (uint32_t)pci_config_read8(vd->pci.bus, vd->pci.slot, vd->pci.func, (uint8_t)(cap + 8)) |
                ((uint32_t)pci_config_read8(vd->pci.bus, vd->pci.slot, vd->pci.func, (uint8_t)(cap + 9)) << 8) |
                ((uint32_t)pci_config_read8(vd->pci.bus, vd->pci.slot, vd->pci.func, (uint8_t)(cap + 10)) << 16) |
                ((uint32_t)pci_config_read8(vd->pci.bus, vd->pci.slot, vd->pci.func, (uint8_t)(cap + 11)) << 24);
            volatile uint8_t *ptr;

            /* Type 5 (PCI_CFG) has no BAR — skip. Only map MMIO cap types. */
            if (cfg < VIRTIO_PCI_CAP_COMMON_CFG || cfg > VIRTIO_PCI_CAP_DEVICE_CFG)
                goto next_cap;

            ptr = map_bar(&vd->pci, bar, offset);
            if (!ptr)
                goto next_cap;

            if (cfg == VIRTIO_PCI_CAP_COMMON_CFG)
                vd->common = ptr;
            else if (cfg == VIRTIO_PCI_CAP_NOTIFY_CFG) {
                vd->notify = ptr;
                vd->notify_mult =
                    (uint32_t)pci_config_read8(vd->pci.bus, vd->pci.slot, vd->pci.func, (uint8_t)(cap + 16)) |
                    ((uint32_t)pci_config_read8(vd->pci.bus, vd->pci.slot, vd->pci.func, (uint8_t)(cap + 17)) << 8) |
                    ((uint32_t)pci_config_read8(vd->pci.bus, vd->pci.slot, vd->pci.func, (uint8_t)(cap + 18)) << 16) |
                    ((uint32_t)pci_config_read8(vd->pci.bus, vd->pci.slot, vd->pci.func, (uint8_t)(cap + 19)) << 24);
            } else if (cfg == VIRTIO_PCI_CAP_ISR_CFG)
                vd->isr = ptr;
            else if (cfg == VIRTIO_PCI_CAP_DEVICE_CFG)
                vd->devcfg = ptr;
        }
next_cap:
        cap = pci_config_read8(vd->pci.bus, vd->pci.slot, vd->pci.func, (uint8_t)(cap + 1));
    }
    return (vd->common && vd->notify) ? 0 : -1;
}

static int alloc_desc(vq_t *q)
{
    uint16_t i;
    if (!q->num_free)
        return -1;
    i = q->free_head;
    q->free_head = q->desc[i].next;
    q->num_free--;
    return (int)i;
}

static void free_desc(vq_t *q, uint16_t i)
{
    q->desc[i].next = q->free_head;
    q->free_head = i;
    q->num_free++;
}

static void free_chain(vq_t *q, uint16_t head)
{
    uint16_t cur = head;
    for (;;) {
        uint16_t next = q->desc[cur].next;
        int last = !(q->desc[cur].flags & VRING_DESC_F_NEXT);
        free_desc(q, cur);
        if (last)
            break;
        cur = next;
    }
}

static void vq_notify(vnet_t *vd, vq_t *q)
{
    uint32_t mult = vd->notify_mult ? vd->notify_mult : 1;
    mmio_w16(vd->notify + q->notify_off * mult, 0);
}

static int setup_queue(vnet_t *vd, vq_t *q, uint16_t index)
{
    uint16_t qsz, i;
    size_t desc_sz, avail_sz, used_sz, total;
    uint8_t *mem;

    mmio_w16(vd->common + 22, index);
    qsz = mmio_r16(vd->common + 24);
    if (qsz == 0)
        return -1;
    if (qsz > VQ_MAX)
        qsz = VQ_MAX;
    q->size = qsz;
    q->notify_off = mmio_r16(vd->common + 30);

    desc_sz = sizeof(vdesc_t) * qsz;
    avail_sz = sizeof(vavail_t) + sizeof(uint16_t) * qsz;
    used_sz = sizeof(vused_t) + sizeof(vused_elem_t) * qsz;
    total = desc_sz + avail_sz + 4096 + used_sz;
    mem = (uint8_t *)kmalloc_aligned(total, 4096);
    if (!mem)
        return -ENOMEM;
    memset(mem, 0, total);
    q->mem = mem;
    q->desc = (vdesc_t *)mem;
    q->avail = (vavail_t *)(mem + desc_sz);
    q->used = (vused_t *)(mem + ((desc_sz + avail_sz + 4095u) & ~4095u));
    q->free_head = 0;
    q->num_free = qsz;
    q->last_used = 0;
    for (i = 0; i < qsz - 1; i++)
        q->desc[i].next = (uint16_t)(i + 1);
    q->desc[qsz - 1].next = 0xFFFF;

    mmio_w16(vd->common + 24, qsz);
    mmio_w32(vd->common + 32, (uint32_t)(uintptr_t)q->desc);
    mmio_w32(vd->common + 36, 0);
    mmio_w32(vd->common + 40, (uint32_t)(uintptr_t)q->avail);
    mmio_w32(vd->common + 44, 0);
    mmio_w32(vd->common + 48, (uint32_t)(uintptr_t)q->used);
    mmio_w32(vd->common + 52, 0);
    mmio_w16(vd->common + 28, 1);
    return 0;
}

static void tx_drain(vnet_t *vd)
{
    vq_t *q = &vd->txq;
    while (q->last_used != q->used->idx) {
        free_chain(q, (uint16_t)q->used->ring[q->last_used % q->size].id);
        q->last_used++;
    }
    if (vd->isr)
        (void)mmio_r8(vd->isr);
}

static int rx_post(vnet_t *vd, int slot)
{
    vq_t *q = &vd->rxq;
    int d = alloc_desc(q);
    if (d < 0)
        return -1;
    q->desc[d].addr = (uint64_t)(uintptr_t)vd->rx_bufs[slot];
    q->desc[d].len = NET_HDR_SIZE + PKT_MAX;
    q->desc[d].flags = VRING_DESC_F_WRITE;
    q->desc[d].next = 0;
    q->avail->ring[q->avail->idx % q->size] = (uint16_t)d;
    __asm__ volatile("" ::: "memory");
    q->avail->idx++;
    __asm__ volatile("" ::: "memory");
    vq_notify(vd, q);
    return 0;
}

static int vnet_tx(netif_t *nif, const void *frame, size_t len)
{
    vnet_t *vd = (vnet_t *)nif->priv;
    vq_t *q;
    int h0, h1;
    int spins = 0;

    if (!vd || !frame || len == 0 || len > PKT_MAX)
        return -EINVAL;
    q = &vd->txq;
    tx_drain(vd);
    while (q->num_free < 2 && spins++ < 10000)
        tx_drain(vd);
    h0 = alloc_desc(q);
    h1 = alloc_desc(q);
    if (h0 < 0 || h1 < 0)
        return -EAGAIN;

    memset(&vd->tx_hdr, 0, sizeof(vd->tx_hdr));
    memcpy(vd->tx_pkt, frame, len);

    q->desc[h0].addr = (uint64_t)(uintptr_t)&vd->tx_hdr;
    q->desc[h0].len = sizeof(vd->tx_hdr);
    q->desc[h0].flags = VRING_DESC_F_NEXT;
    q->desc[h0].next = (uint16_t)h1;
    q->desc[h1].addr = (uint64_t)(uintptr_t)vd->tx_pkt;
    q->desc[h1].len = (uint32_t)len;
    q->desc[h1].flags = 0;
    q->desc[h1].next = 0;

    q->avail->ring[q->avail->idx % q->size] = (uint16_t)h0;
    __asm__ volatile("" ::: "memory");
    q->avail->idx++;
    __asm__ volatile("" ::: "memory");
    vq_notify(vd, q);

    spins = 0;
    while (q->last_used == q->used->idx && spins++ < 20000)
        ;
    tx_drain(vd);
    return 0;
}

static void rx_poll(vnet_t *vd)
{
    vq_t *q = &vd->rxq;
    while (q->last_used != q->used->idx) {
        vused_elem_t *ue = &q->used->ring[q->last_used % q->size];
        uint16_t id = (uint16_t)ue->id;
        uint8_t *buf = (uint8_t *)(uintptr_t)q->desc[id].addr;
        int slot = -1, i;
        if (ue->len > NET_HDR_SIZE)
            netif_input(&vd->nif, buf + NET_HDR_SIZE, ue->len - NET_HDR_SIZE);
        free_desc(q, id);
        for (i = 0; i < RX_BUFS; i++) {
            if (vd->rx_bufs[i] == buf) {
                slot = i;
                break;
            }
        }
        q->last_used++;
        if (slot >= 0)
            (void)rx_post(vd, slot);
    }
    if (vd->isr)
        (void)mmio_r8(vd->isr);
}

static void vnet_poll(netif_t *nif)
{
    vnet_t *vd = (vnet_t *)nif->priv;
    if (!vd)
        return;
    tx_drain(vd);
    rx_poll(vd);
}

static int vnet_init_device(pci_device_t *pci)
{
    vnet_t *vd;
    uint32_t feats;
    int i;

    vd = (vnet_t *)kmalloc(sizeof(*vd));
    if (!vd)
        return -ENOMEM;
    memset(vd, 0, sizeof(*vd));
    vd->pci = *pci;
    if (pci_enable_bus_master(pci) < 0)
        return -EIO;
    if (parse_caps(vd) < 0) {
        vga_print("virtio-net: no modern caps\n");
        return -ENODEV;
    }

    mmio_w8(vd->common + 20, 0);
    mmio_w8(vd->common + 20, VIRTIO_STATUS_ACKNOWLEDGE);
    mmio_w8(vd->common + 20, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    mmio_w32(vd->common + 0, 0);
    feats = mmio_r32(vd->common + 4);
    mmio_w32(vd->common + 8, 0);
    mmio_w32(vd->common + 12, feats & (1u << VIRTIO_NET_F_MAC));
    mmio_w8(vd->common + 20,
            VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);
    if (!(mmio_r8(vd->common + 20) & VIRTIO_STATUS_FEATURES_OK))
        return -EIO;

    strcpy(vd->nif.name, "eth0");
    if (vd->devcfg && (feats & (1u << VIRTIO_NET_F_MAC))) {
        for (i = 0; i < 6; i++)
            vd->nif.mac[i] = mmio_r8(vd->devcfg + i);
    } else {
        vd->nif.mac[0] = 0x52;
        vd->nif.mac[1] = 0x54;
        vd->nif.mac[2] = 0x00;
        vd->nif.mac[3] = 0x12;
        vd->nif.mac[4] = 0x34;
        vd->nif.mac[5] = 0x56;
    }
    vd->nif.ip = (10u << 24) | (2u << 8) | 15u;
    vd->nif.netmask = 0xFFFFFF00u;
    vd->nif.gateway = (10u << 24) | (2u << 8) | 2u;
    vd->nif.mtu = 1500;
    vd->nif.tx = vnet_tx;
    vd->nif.poll = vnet_poll;
    vd->nif.priv = vd;

    if (setup_queue(vd, &vd->rxq, 0) < 0 || setup_queue(vd, &vd->txq, 1) < 0)
        return -ENOMEM;

    for (i = 0; i < RX_BUFS; i++) {
        vd->rx_bufs[i] = (uint8_t *)kmalloc_aligned(NET_HDR_SIZE + PKT_MAX, 16);
        if (!vd->rx_bufs[i])
            return -ENOMEM;
        memset(vd->rx_bufs[i], 0, NET_HDR_SIZE + PKT_MAX);
        if (rx_post(vd, i) < 0)
            return -EIO;
    }

    mmio_w8(vd->common + 20,
            VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);

    if (netif_register(&vd->nif) < 0)
        return -EIO;

    g_vnet = vd;
    vga_print("virtio-net: eth0 10.0.2.15 up\n");
    return 0;
}

static int net_scan_cb(const pci_device_t *dev, void *ctx)
{
    int *found = (int *)ctx;
    if (dev->vendor == PCI_VENDOR_VIRTIO && dev->device == PCI_DEVICE_NET_MODERN) {
        pci_device_t d = *dev;
        if (vnet_init_device(&d) == 0)
            *found = 1;
    }
    return 0;
}

static void vnet_drv_poll(driver_t *drv)
{
    (void)drv;
    if (g_vnet)
        vnet_poll(&g_vnet->nif);
}

static int vnet_drv_init(driver_t *drv, void *ctx)
{
    int found = 0;
    (void)drv;
    (void)ctx;
    pci_enumerate(net_scan_cb, &found);
    if (!found)
        vga_print("virtio-net: no device\n");
    return 0;
}

int kmod_init(void)
{
    driver_t d;
    memset(&d, 0, sizeof(d));
    strncpy(d.name, "virtio_net", DRIVER_NAME_MAX - 1);
    strncpy(d.version, "1.0", DRIVER_VERSION_MAX - 1);
    d.kind = DRIVER_KIND_CUSTOM;
    d.class = DRIVER_CLASS_MISC;
    d.flags = DRIVER_FLAG_POLL;
    d.priority = 70;
    d.init = vnet_drv_init;
    d.poll = vnet_drv_poll;
    if (driver_register(&d) < 0)
        return -1;
    if (driver_load("virtio_net", NULL) < 0)
        return -1;
    return 0;
}
