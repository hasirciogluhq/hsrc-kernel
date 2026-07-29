#ifndef VIRTIO_RING_H
#define VIRTIO_RING_H

#include <drivers/bus/pci.h>
#include <kernel/types.h>

#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER      2
#define VIRTIO_STATUS_DRIVER_OK   4
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_FAILED      0x80

#define VIRTIO_GPU_QUEUE_CONTROL 0

typedef struct virtio_ring {
    pci_device_t pci;
    volatile uint8_t *common;
    volatile uint8_t *notify;
    volatile uint8_t *isr;
    volatile uint8_t *device_cfg;
    uint32_t notify_off_multiplier;
    uint16_t queue_notify_off;
    uint16_t qsize;
    void    *queue_mem;
    void    *desc;
    void    *avail;
    void    *used;
    uint16_t free_head;
    uint16_t num_free;
    uint16_t last_used_idx;
} virtio_ring_t;

int  virtio_ring_probe_gpu(pci_device_t *out);
int  virtio_ring_init(virtio_ring_t *vr, const pci_device_t *pci);
int  virtio_ring_setup_queue(virtio_ring_t *vr, uint16_t qindex, uint16_t max_size);
void virtio_ring_set_status(virtio_ring_t *vr, uint8_t status);
uint8_t virtio_ring_get_status(virtio_ring_t *vr);

/* Push request/response descriptors to the device controlq; wait for used. */
int  virtio_ring_submit(virtio_ring_t *vr,
                        const void *out, uint32_t out_len,
                        void *in, uint32_t in_len);

/* 1 if VIRTIO_GPU_F_VIRGL was negotiated. */
int  virtio_ring_has_virgl(void);

#endif
