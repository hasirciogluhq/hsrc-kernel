#include "virtio_ring.h"
#include "virtio_cmd.h"
#include "virtio_virgl.h"
#include <drivers/display/display.h>
#include <drivers/display/gpu.h>
#include <drivers/driver.h>
#include <drivers/console/vga.h>
#include <kernel/heap.h>
#include <kernel/string.h>

/*
 * display_virtio — thin GpuProvider.
 * Simple bring-up + present/submit wiring. Transport in virtio_ring.c,
 * device commands in virtio_cmd.c, 3D in virtio_virgl.c. No CPU raster.
 */

#define VIRTIO_GPU_DEFAULT_W 1920
#define VIRTIO_GPU_DEFAULT_H 1080

static virtio_ring_t    g_ring;
static virtio_scanout_t g_scan;
static display_mode_t   g_mode;
static display_ops_t    g_ops;
static uint32_t        *g_fb;
static uint32_t         g_fb_bytes;
static int              g_ready;
static int              g_device_present;
static int              g_virgl_ok;
static pci_device_t     g_pci;

static int virtio_get_mode(display_mode_t *out)
{
    if (!g_ready || !out)
        return -1;
    *out = g_mode;
    return 0;
}

static int virtio_present(const uint32_t *src, uint32_t src_stride_px)
{
    if (!g_ready || !src)
        return -1;
    return virtio_cmd_present(&g_scan, (void *)src, g_mode.width, g_mode.height,
                              src_stride_px * 4u, 0, 0, 0, 0);
}

static int virtio_present_rect(const uint32_t *src, uint32_t src_stride_px,
                               uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (!g_ready || !src || w == 0 || h == 0)
        return -1;
    return virtio_cmd_present(&g_scan, (void *)src, g_mode.width, g_mode.height,
                              src_stride_px * 4u, x, y, w, h);
}

static int virtio_present_rects(const uint32_t *src, uint32_t src_stride_px,
                                const display_rect_t *rects, uint32_t n)
{
    uint32_t i, bx = 0, by = 0, bx2 = 0, by2 = 0;
    int have = 0;

    if (!g_ready || !src || !rects || n == 0)
        return -1;
    if (src_stride_px != g_mode.width)
        return -1;

    /* One present of bbox — device TRANSFER/FLUSH only. */
    for (i = 0; i < n; i++) {
        uint32_t x = rects[i].x, y = rects[i].y, w = rects[i].w, h = rects[i].h;
        if (w == 0 || h == 0)
            continue;
        if (x >= g_mode.width || y >= g_mode.height)
            continue;
        if (x + w > g_mode.width)
            w = g_mode.width - x;
        if (y + h > g_mode.height)
            h = g_mode.height - y;
        if (!have) {
            bx = x;
            by = y;
            bx2 = x + w;
            by2 = y + h;
            have = 1;
        } else {
            if (x < bx)
                bx = x;
            if (y < by)
                by = y;
            if (x + w > bx2)
                bx2 = x + w;
            if (y + h > by2)
                by2 = y + h;
        }
    }
    if (!have)
        return 0;
    return virtio_cmd_present(&g_scan, (void *)src, g_mode.width, g_mode.height,
                              src_stride_px * 4u, bx, by, bx2 - bx, by2 - by);
}

static int virtio_gpu_submit(const void *cmd, uint32_t size)
{
    if (!g_ready || !cmd || size == 0)
        return -1;
    return virtio_cmd_submit_gpu(&g_scan, cmd, size);
}

static int bringup(void)
{
    uint32_t width, height;

    if (virtio_ring_init(&g_ring, &g_pci) < 0)
        return -1;
    if (virtio_ring_setup_queue(&g_ring, VIRTIO_GPU_QUEUE_CONTROL, 64) < 0) {
        vga_print("virtio: controlq failed\n");
        return -1;
    }
    virtio_ring_set_status(&g_ring,
                           (uint8_t)(virtio_ring_get_status(&g_ring) | VIRTIO_STATUS_DRIVER_OK));

    if (virtio_cmd_get_display_size(&g_ring, &width, &height) < 0) {
        width = VIRTIO_GPU_DEFAULT_W;
        height = VIRTIO_GPU_DEFAULT_H;
    }
    width = VIRTIO_GPU_DEFAULT_W;
    height = VIRTIO_GPU_DEFAULT_H;

    g_fb_bytes = width * height * 4u;
    g_fb = (uint32_t *)kmalloc_aligned(g_fb_bytes, 4096);
    if (!g_fb)
        return -1;
    memset(g_fb, 0, g_fb_bytes);

    memset(&g_scan, 0, sizeof(g_scan));
    g_scan.ring = &g_ring;
    if (virtio_cmd_setup_scanout(&g_scan, g_fb, g_fb_bytes, width, height) < 0)
        return -1;

    g_virgl_ok = 0;
    if (virtio_virgl_init(&g_ring, &g_scan) == 0)
        g_virgl_ok = 1;
    else
        vga_print("virtio: VirGL init skipped/failed (2D present only)\n");

    memset(&g_mode, 0, sizeof(g_mode));
    g_mode.addr = (uint8_t *)g_fb;
    g_mode.width = width;
    g_mode.height = height;
    g_mode.bpp = 32;
    g_mode.bytes_per_pixel = 4;
    g_mode.pitch = width * 4;
    g_ready = 1;
    vga_print(g_virgl_ok ? "virtio: scanout+VirGL ready\n" : "virtio: scanout ready (2D)\n");
    return 0;
}

static int virtio_drv_probe(driver_t *drv, void *ctx)
{
    (void)drv;
    (void)ctx;
    if (virtio_ring_probe_gpu(&g_pci) == 0) {
        g_device_present = 1;
        return 0;
    }
    g_device_present = 0;
    return 0;
}

static int virtio_drv_init(driver_t *drv, void *ctx)
{
    (void)drv;
    (void)ctx;

    memset(&g_ops, 0, sizeof(g_ops));
    g_ops.name = "virtio_gpu";
    g_ops.gpu_caps = GPU_CAP_SCANOUT | GPU_CAP_PRESENT_RECT | GPU_CAP_PRESENT_RECTS |
                     GPU_CAP_SUBMIT;
    g_ops.get_mode = virtio_get_mode;
    g_ops.present = virtio_present;
    g_ops.present_rect = virtio_present_rect;
    g_ops.present_rects = virtio_present_rects;
    g_ops.gpu_submit = virtio_gpu_submit;

    if (!g_device_present) {
        vga_print("virtio: not present\n");
        return 0;
    }
    if (bringup() < 0) {
        vga_print("virtio: bringup failed\n");
        g_ready = 0;
        return 0;
    }
    if (g_virgl_ok)
        g_ops.gpu_caps |= GPU_CAP_HW_SUBMIT;
    return display_register(&g_ops, DISPLAY_PRIO_VIRTIO);
}

int kmod_init(void)
{
    driver_t d;
    memset(&d, 0, sizeof(d));
    strncpy(d.name, "display_virtio", DRIVER_NAME_MAX - 1);
    strncpy(d.version, "1.0", DRIVER_VERSION_MAX - 1);
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
