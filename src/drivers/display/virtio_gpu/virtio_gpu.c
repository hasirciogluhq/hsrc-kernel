#include "virtio_gpu.h"
#include <drivers/display.h>
#include <drivers/driver.h>
#include <drivers/pci.h>
#include <kernel/string.h>
#include <drivers/vga.h>

/* VirtIO vendor / GPU device (modern 1.0) */
#define PCI_VENDOR_VIRTIO     0x1AF4
#define PCI_DEVICE_VIRTIO_GPU 0x1050
/* Legacy transitional GPU often 0x1000 + virtio id; keep modern probe first */

static display_mode_t g_mode;
static int g_ready;
static display_ops_t g_ops;
static pci_device_t g_dev;

/*
 * Skeleton: detect virtio-gpu PCI device and register ops.
 * Full VirGL / resource create / scanout comes later.
 * Until scanout is wired, probe succeeds only if we can claim the device
 * and expose a software-backed placeholder mode via BAR0 (not used yet).
 * For now: if device is present we register with higher priority but
 * present returns -1 so BGA remains preferred unless gpu_submit path used.
 *
 * Practical boot: without working scanout we do NOT steal the active display
 * from BGA — register only after a future complete init. This kmod still
 * loads and reports presence for MKDX gpu_submit queries.
 */

static int g_device_present;

static int virtio_get_mode(display_mode_t *out)
{
    if (!g_ready || !out)
        return -1;
    *out = g_mode;
    return 0;
}

static int virtio_present(const uint32_t *src, uint32_t src_stride_px)
{
    (void)src;
    (void)src_stride_px;
    /* Scanout not implemented — fail hard per MKDX contract */
    return -1;
}

static int virtio_present_rect(const uint32_t *src, uint32_t src_stride_px,
                               uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    (void)src;
    (void)src_stride_px;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    return -1;
}

static int virtio_gpu_submit(const void *cmd, uint32_t size)
{
    (void)cmd;
    (void)size;
    if (!g_device_present)
        return -1;
    /* Command ring submit — stub */
    return -1;
}

static int virtio_drv_probe(driver_t *drv, void *ctx)
{
    (void)drv;
    (void)ctx;
    if (pci_find(PCI_VENDOR_VIRTIO, PCI_DEVICE_VIRTIO_GPU, &g_dev) == 0) {
        g_device_present = 1;
        return 0;
    }
    g_device_present = 0;
    /* No device: soft-success so boot continues; do not register display */
    return 0;
}

static int virtio_drv_init(driver_t *drv, void *ctx)
{
    (void)drv;
    (void)ctx;

    memset(&g_ops, 0, sizeof(g_ops));
    g_ops.name = "virtio_gpu";
    g_ops.get_mode = virtio_get_mode;
    g_ops.present = virtio_present;
    g_ops.present_rect = virtio_present_rect;
    g_ops.gpu_submit = virtio_gpu_submit;

    if (!g_device_present) {
        vga_print("virtio-gpu: not present\n");
        return 0;
    }

    /*
     * Device found — do not register as active display until scanout works.
     * Expose gpu_submit via a side registry later; for now log and keep BGA.
     */
    vga_print("virtio-gpu: present (scanout stub)\n");
    g_ready = 0;
    (void)DISPLAY_PRIO_VIRTIO;
    return 0;
}

int kmod_init(void)
{
    driver_t d;
    memset(&d, 0, sizeof(d));
    strncpy(d.name, "display_virtio", DRIVER_NAME_MAX - 1);
    strncpy(d.version, "0.1", DRIVER_VERSION_MAX - 1);
    d.kind = DRIVER_KIND_CUSTOM;
    d.class = DRIVER_CLASS_DISPLAY;
    d.flags = 0;
    d.priority = 20;
    d.probe = virtio_drv_probe;
    d.init = virtio_drv_init;

    if (driver_register(&d) < 0)
        return -1;
    if (driver_load("display_virtio", NULL) < 0)
        return -1;
    return 0;
}
