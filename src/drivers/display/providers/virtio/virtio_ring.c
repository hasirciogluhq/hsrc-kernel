#include "virtio_ring.h"
#include <kernel/heap.h>
#include <kernel/string.h>
#include <kernel/vmm.h>
#include <drivers/console/vga.h>

/*
 * Virtio modern PCI + virtqueue — complex transport only.
 * No pixels. Only MMIO + ring descriptors to the device.
 */

#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2
#define VIRTIO_PCI_CAP_ISR_CFG    3
#define VIRTIO_PCI_CAP_DEVICE_CFG 4
#define VIRTIO_F_VERSION_1        32
#define VIRTIO_GPU_F_VIRGL        0 /* device feature bit 0 */
#define VIRTIO_MSI_NO_VECTOR      0xFFFF
#define VRING_DESC_F_NEXT         1
#define VRING_DESC_F_WRITE        2

#define PCI_VENDOR_VIRTIO     0x1AF4
#define PCI_DEVICE_GPU_MODERN 0x1050
#define PCI_DEVICE_GPU_TRANS  0x1010

static uint32_t g_gpu_features_lo; /* negotiated device features (lo) */

typedef struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed)) virtq_desc_t;

typedef struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} __attribute__((packed)) virtq_avail_t;

typedef struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed)) virtq_used_elem_t;

typedef struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[];
} __attribute__((packed)) virtq_used_t;

static inline void mmio_barrier(void)
{
    __asm__ volatile("" ::: "memory");
}

static inline uint8_t mmio_r8(volatile uint8_t *p) { return *p; }
static inline void mmio_w8(volatile uint8_t *p, uint8_t v)
{
    *p = v;
    mmio_barrier();
}
static inline uint16_t mmio_r16(volatile uint8_t *p)
{
    return *(volatile uint16_t *)p;
}
static inline void mmio_w16(volatile uint8_t *p, uint16_t v)
{
    *(volatile uint16_t *)p = v;
    mmio_barrier();
}
static inline uint32_t mmio_r32(volatile uint8_t *p)
{
    return *(volatile uint32_t *)p;
}
static inline void mmio_w32(volatile uint8_t *p, uint32_t v)
{
    *(volatile uint32_t *)p = v;
    mmio_barrier();
}

static volatile uint8_t *map_cap(const pci_device_t *pci, uint8_t bar, uint32_t offset)
{
    uint32_t base = pci_bar_phys(pci, (int)bar);
    if (!base)
        return NULL;
    (void)vmm_identity_map_range(base, 0x10000u);
    return (volatile uint8_t *)(uintptr_t)(base + offset);
}

static int parse_caps(virtio_ring_t *vr)
{
    uint16_t status;
    uint8_t cap;

    status = pci_config_read16(vr->pci.bus, vr->pci.slot, vr->pci.func, PCI_STATUS);
    if (!(status & PCI_STATUS_CAP_LIST))
        return -1;

    cap = pci_config_read8(vr->pci.bus, vr->pci.slot, vr->pci.func, PCI_CAPABILITY_LIST);
    while (cap) {
        uint8_t id = pci_config_read8(vr->pci.bus, vr->pci.slot, vr->pci.func, cap);
        if (id == PCI_CAP_ID_VNDR) {
            uint8_t cfg_type = pci_config_read8(vr->pci.bus, vr->pci.slot, vr->pci.func,
                                                (uint8_t)(cap + 3));
            uint8_t bar = pci_config_read8(vr->pci.bus, vr->pci.slot, vr->pci.func,
                                           (uint8_t)(cap + 4));
            uint32_t offset =
                (uint32_t)pci_config_read8(vr->pci.bus, vr->pci.slot, vr->pci.func, (uint8_t)(cap + 8)) |
                ((uint32_t)pci_config_read8(vr->pci.bus, vr->pci.slot, vr->pci.func, (uint8_t)(cap + 9)) << 8) |
                ((uint32_t)pci_config_read8(vr->pci.bus, vr->pci.slot, vr->pci.func, (uint8_t)(cap + 10)) << 16) |
                ((uint32_t)pci_config_read8(vr->pci.bus, vr->pci.slot, vr->pci.func, (uint8_t)(cap + 11)) << 24);
            volatile uint8_t *ptr;

            if (cfg_type < VIRTIO_PCI_CAP_COMMON_CFG || cfg_type > VIRTIO_PCI_CAP_DEVICE_CFG)
                goto next_cap;

            ptr = map_cap(&vr->pci, bar, offset);
            if (!ptr)
                goto next_cap;

            switch (cfg_type) {
            case VIRTIO_PCI_CAP_COMMON_CFG:
                vr->common = ptr;
                break;
            case VIRTIO_PCI_CAP_NOTIFY_CFG: {
                uint32_t mult =
                    (uint32_t)pci_config_read8(vr->pci.bus, vr->pci.slot, vr->pci.func, (uint8_t)(cap + 16)) |
                    ((uint32_t)pci_config_read8(vr->pci.bus, vr->pci.slot, vr->pci.func, (uint8_t)(cap + 17)) << 8) |
                    ((uint32_t)pci_config_read8(vr->pci.bus, vr->pci.slot, vr->pci.func, (uint8_t)(cap + 18)) << 16) |
                    ((uint32_t)pci_config_read8(vr->pci.bus, vr->pci.slot, vr->pci.func, (uint8_t)(cap + 19)) << 24);
                vr->notify = ptr;
                vr->notify_off_multiplier = mult;
                break;
            }
            case VIRTIO_PCI_CAP_ISR_CFG:
                vr->isr = ptr;
                break;
            case VIRTIO_PCI_CAP_DEVICE_CFG:
                vr->device_cfg = ptr;
                break;
            default:
                break;
            }
        }
next_cap:
        cap = pci_config_read8(vr->pci.bus, vr->pci.slot, vr->pci.func, (uint8_t)(cap + 1));
    }
    return (vr->common && vr->notify) ? 0 : -1;
}

int virtio_ring_probe_gpu(pci_device_t *out)
{
    if (!out)
        return -1;
    if (pci_find(PCI_VENDOR_VIRTIO, PCI_DEVICE_GPU_MODERN, out) == 0)
        return 0;
    if (pci_find(PCI_VENDOR_VIRTIO, PCI_DEVICE_GPU_TRANS, out) == 0)
        return 0;
    return -1;
}

void virtio_ring_set_status(virtio_ring_t *vr, uint8_t status)
{
    mmio_w8(vr->common + 20, status);
}

uint8_t virtio_ring_get_status(virtio_ring_t *vr)
{
    return mmio_r8(vr->common + 20);
}

static int negotiate_features(virtio_ring_t *vr)
{
    uint32_t want_hi = (1u << (VIRTIO_F_VERSION_1 - 32));
    uint32_t want_lo = (1u << VIRTIO_GPU_F_VIRGL);
    uint32_t feats_lo, feats_hi;

    mmio_w32(vr->common + 0, 0);
    feats_lo = mmio_r32(vr->common + 4);
    mmio_w32(vr->common + 0, 1);
    feats_hi = mmio_r32(vr->common + 4);
    if (!(feats_hi & want_hi)) {
        vga_print("virtio: no VERSION_1\n");
        return -1;
    }
    /* Always request VIRGL for ring SUBMIT_3D (host must offer it). */
    g_gpu_features_lo = feats_lo & want_lo;
    mmio_w32(vr->common + 8, 0);
    mmio_w32(vr->common + 12, g_gpu_features_lo);
    mmio_w32(vr->common + 8, 1);
    mmio_w32(vr->common + 12, want_hi);
    virtio_ring_set_status(vr, (uint8_t)(virtio_ring_get_status(vr) | VIRTIO_STATUS_FEATURES_OK));
    if (!(virtio_ring_get_status(vr) & VIRTIO_STATUS_FEATURES_OK)) {
        vga_print("virtio: FEATURES_OK rejected\n");
        return -1;
    }
    if (g_gpu_features_lo & (1u << VIRTIO_GPU_F_VIRGL))
        vga_print("virtio: VIRGL feature ok\n");
    else
        vga_print("virtio: VIRGL feature missing (need virtio-gpu-gl + virglrenderer)\n");
    return 0;
}

int virtio_ring_has_virgl(void)
{
    return (g_gpu_features_lo & (1u << VIRTIO_GPU_F_VIRGL)) ? 1 : 0;
}

int virtio_ring_init(virtio_ring_t *vr, const pci_device_t *pci)
{
    if (!vr || !pci)
        return -1;
    memset(vr, 0, sizeof(*vr));
    vr->pci = *pci;
    if (pci_enable_bus_master(&vr->pci) < 0)
        return -1;
    if (parse_caps(vr) < 0) {
        vga_print("virtio: modern caps missing\n");
        return -1;
    }
    virtio_ring_set_status(vr, 0);
    virtio_ring_set_status(vr, VIRTIO_STATUS_ACKNOWLEDGE);
    virtio_ring_set_status(vr, (uint8_t)(VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER));
    if (negotiate_features(vr) < 0) {
        virtio_ring_set_status(vr, VIRTIO_STATUS_FAILED);
        return -1;
    }
    mmio_w16(vr->common + 16, VIRTIO_MSI_NO_VECTOR);
    return 0;
}

int virtio_ring_setup_queue(virtio_ring_t *vr, uint16_t qindex, uint16_t max_size)
{
    uint16_t qsz, i;
    size_t desc_sz, avail_sz, used_sz, total;
    uint8_t *mem;
    uintptr_t used_addr;
    virtq_desc_t *desc;

    mmio_w16(vr->common + 22, qindex);
    qsz = mmio_r16(vr->common + 24);
    if (qsz == 0)
        return -1;
    if (qsz > max_size)
        qsz = max_size;
    while (qsz & (qsz - 1))
        qsz--;
    if (qsz < 4)
        return -1;
    mmio_w16(vr->common + 24, qsz);

    desc_sz = (size_t)qsz * sizeof(virtq_desc_t);
    avail_sz = 4u + (size_t)qsz * 2u + 2u;
    used_sz = 4u + (size_t)qsz * sizeof(virtq_used_elem_t);
    total = desc_sz + avail_sz;
    total = (total + 3u) & ~3u;
    total += used_sz;
    total = (total + 4095u) & ~4095u;

    mem = (uint8_t *)kmalloc_aligned(total, 4096);
    if (!mem)
        return -1;
    memset(mem, 0, total);

    vr->queue_mem = mem;
    desc = (virtq_desc_t *)mem;
    vr->desc = desc;
    vr->avail = mem + desc_sz;
    used_addr = ((uintptr_t)mem + desc_sz + avail_sz + 3u) & ~3u;
    vr->used = (void *)used_addr;
    vr->qsize = qsz;
    vr->last_used_idx = 0;
    vr->free_head = 0;
    vr->num_free = qsz;
    for (i = 0; i < qsz - 1; i++)
        desc[i].next = (uint16_t)(i + 1);
    desc[qsz - 1].next = 0;

    mmio_w32(vr->common + 32, (uint32_t)(uintptr_t)desc);
    mmio_w32(vr->common + 36, 0);
    mmio_w32(vr->common + 40, (uint32_t)(uintptr_t)vr->avail);
    mmio_w32(vr->common + 44, 0);
    mmio_w32(vr->common + 48, (uint32_t)used_addr);
    mmio_w32(vr->common + 52, 0);
    mmio_w16(vr->common + 26, VIRTIO_MSI_NO_VECTOR);
    vr->queue_notify_off = mmio_r16(vr->common + 30);
    mmio_w16(vr->common + 28, 1);
    return 0;
}

static int alloc_desc_chain(virtio_ring_t *vr, uint16_t n, uint16_t *head_out)
{
    virtq_desc_t *desc = (virtq_desc_t *)vr->desc;
    uint16_t head, i, cur;
    if (n == 0 || vr->num_free < n)
        return -1;
    head = vr->free_head;
    cur = head;
    for (i = 0; i < (uint16_t)(n - 1); i++) {
        desc[cur].flags = VRING_DESC_F_NEXT;
        cur = desc[cur].next;
    }
    vr->free_head = desc[cur].next;
    desc[cur].flags = 0;
    desc[cur].next = 0;
    vr->num_free = (uint16_t)(vr->num_free - n);
    *head_out = head;
    return 0;
}

static void free_desc_chain(virtio_ring_t *vr, uint16_t head)
{
    virtq_desc_t *desc = (virtq_desc_t *)vr->desc;
    uint16_t cur = head;
    for (;;) {
        uint16_t flags = desc[cur].flags;
        uint16_t next = desc[cur].next;
        vr->num_free++;
        if (!(flags & VRING_DESC_F_NEXT)) {
            desc[cur].next = vr->free_head;
            vr->free_head = head;
            break;
        }
        cur = next;
    }
}

static void notify_queue(virtio_ring_t *vr)
{
    uint32_t off = (uint32_t)vr->queue_notify_off * vr->notify_off_multiplier;
    mmio_w16(vr->notify + off, VIRTIO_GPU_QUEUE_CONTROL);
}

int virtio_ring_submit(virtio_ring_t *vr,
                       const void *out, uint32_t out_len,
                       void *in, uint32_t in_len)
{
    virtq_desc_t *desc;
    virtq_avail_t *avail;
    virtq_used_t *used;
    uint16_t head, d0, d1, avail_idx;
    uint32_t spins;

    if (!vr || !out || !out_len || !in || !in_len)
        return -1;
    if (alloc_desc_chain(vr, 2, &head) < 0)
        return -1;

    desc = (virtq_desc_t *)vr->desc;
    avail = (virtq_avail_t *)vr->avail;
    used = (virtq_used_t *)vr->used;
    d0 = head;
    d1 = desc[d0].next;

    desc[d0].addr = (uint64_t)(uintptr_t)out;
    desc[d0].len = out_len;
    desc[d0].flags = VRING_DESC_F_NEXT;
    desc[d0].next = d1;

    desc[d1].addr = (uint64_t)(uintptr_t)in;
    desc[d1].len = in_len;
    desc[d1].flags = VRING_DESC_F_WRITE;
    desc[d1].next = 0;

    avail_idx = avail->idx;
    avail->ring[avail_idx % vr->qsize] = head;
    mmio_barrier();
    avail->idx = (uint16_t)(avail_idx + 1);
    mmio_barrier();
    notify_queue(vr);

    for (spins = 0; spins < 50000000u; spins++) {
        mmio_barrier();
        if ((uint16_t)(used->idx - vr->last_used_idx) != 0) {
            uint16_t u = (uint16_t)(vr->last_used_idx % vr->qsize);
            uint16_t id = (uint16_t)used->ring[u].id;
            vr->last_used_idx++;
            free_desc_chain(vr, id);
            if (vr->isr)
                (void)mmio_r8(vr->isr);
            return 0;
        }
    }
    vga_print("virtio: queue timeout\n");
    free_desc_chain(vr, head);
    return -1;
}
