#include "virtio_cmd.h"
#include "virtio_virgl.h"
#include <drivers/display/gpu_cmd.h>
#include <kernel/string.h>
#include <drivers/console/vga.h>

/*
 * Virtio-gpu control protocol over the ring. CPU raster/blit/clear = yasak.
 * CLEAR/DRAW/BLIT → virtio_virgl_exec when VirGL negotiated.
 */

#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO        0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D      0x0101
#define VIRTIO_GPU_CMD_SET_SCANOUT             0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH          0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D     0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106
#define VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING 0x0107

#define VIRTIO_GPU_RESP_OK_NODATA       0x1100
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO 0x1101
#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM 1
#define VIRTIO_GPU_MAX_SCANOUTS          16
#define VIRTIO_GPU_DEFAULT_W             1920
#define VIRTIO_GPU_DEFAULT_H             1080

typedef struct virtio_gpu_ctrl_hdr {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint8_t  ring_idx;
    uint8_t  padding[3];
} __attribute__((packed)) virtio_gpu_ctrl_hdr_t;

typedef struct virtio_gpu_rect {
    uint32_t x, y, width, height;
} __attribute__((packed)) virtio_gpu_rect_t;

typedef struct virtio_gpu_resource_create_2d {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} __attribute__((packed)) virtio_gpu_resource_create_2d_t;

typedef struct virtio_gpu_mem_entry {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
} __attribute__((packed)) virtio_gpu_mem_entry_t;

typedef struct virtio_gpu_resource_attach_backing {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
} __attribute__((packed)) virtio_gpu_resource_attach_backing_t;

typedef struct virtio_gpu_resource_detach_backing {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed)) virtio_gpu_resource_detach_backing_t;

typedef struct virtio_gpu_set_scanout {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t r;
    uint32_t scanout_id;
    uint32_t resource_id;
} __attribute__((packed)) virtio_gpu_set_scanout_t;

typedef struct virtio_gpu_transfer_to_host_2d {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t r;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed)) virtio_gpu_transfer_to_host_2d_t;

typedef struct virtio_gpu_resource_flush {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t r;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed)) virtio_gpu_resource_flush_t;

typedef struct virtio_gpu_display_one {
    virtio_gpu_rect_t r;
    uint32_t enabled;
    uint32_t flags;
} __attribute__((packed)) virtio_gpu_display_one_t;

typedef struct virtio_gpu_resp_display_info {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_display_one_t pmodes[VIRTIO_GPU_MAX_SCANOUTS];
} __attribute__((packed)) virtio_gpu_resp_display_info_t;

typedef struct attach_req {
    virtio_gpu_resource_attach_backing_t ab;
    virtio_gpu_mem_entry_t entry;
} __attribute__((packed)) attach_req_t;

static void hdr_init(virtio_gpu_ctrl_hdr_t *h, uint32_t type)
{
    memset(h, 0, sizeof(*h));
    h->type = type;
}

static int ring_cmd(virtio_ring_t *ring, const void *req, uint32_t req_len,
                    void *resp, uint32_t resp_len)
{
    virtio_gpu_ctrl_hdr_t *rh;
    if (virtio_ring_submit(ring, req, req_len, resp, resp_len) < 0)
        return -1;
    rh = (virtio_gpu_ctrl_hdr_t *)resp;
    if (rh->type < VIRTIO_GPU_RESP_OK_NODATA || rh->type >= 0x1200)
        return -1;
    return 0;
}

static int ring_cmd_nodata(virtio_ring_t *ring, const void *req, uint32_t req_len)
{
    virtio_gpu_ctrl_hdr_t resp;
    memset(&resp, 0, sizeof(resp));
    if (ring_cmd(ring, req, req_len, &resp, sizeof(resp)) < 0)
        return -1;
    return (resp.type == VIRTIO_GPU_RESP_OK_NODATA) ? 0 : -1;
}

int virtio_cmd_get_display_size(virtio_ring_t *ring, uint32_t *w, uint32_t *h)
{
    virtio_gpu_ctrl_hdr_t req;
    virtio_gpu_resp_display_info_t resp;
    int i;

    hdr_init(&req, VIRTIO_GPU_CMD_GET_DISPLAY_INFO);
    memset(&resp, 0, sizeof(resp));
    if (ring_cmd(ring, &req, sizeof(req), &resp, sizeof(resp)) < 0)
        return -1;
    if (resp.hdr.type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO)
        return -1;
    for (i = 0; i < VIRTIO_GPU_MAX_SCANOUTS; i++) {
        if (resp.pmodes[i].enabled && resp.pmodes[i].r.width && resp.pmodes[i].r.height) {
            *w = resp.pmodes[i].r.width;
            *h = resp.pmodes[i].r.height;
            return 0;
        }
    }
    *w = VIRTIO_GPU_DEFAULT_W;
    *h = VIRTIO_GPU_DEFAULT_H;
    return 0;
}

static int attach_backing(virtio_scanout_t *so, void *ptr, uint32_t bytes)
{
    attach_req_t attach;
    virtio_gpu_resource_detach_backing_t det;

    if (so->attach_ptr) {
        memset(&det, 0, sizeof(det));
        hdr_init(&det.hdr, VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING);
        det.resource_id = so->resource_id;
        (void)ring_cmd_nodata(so->ring, &det, sizeof(det));
        so->attach_ptr = NULL;
    }

    memset(&attach, 0, sizeof(attach));
    hdr_init(&attach.ab.hdr, VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING);
    attach.ab.resource_id = so->resource_id;
    attach.ab.nr_entries = 1;
    attach.entry.addr = (uint64_t)(uintptr_t)ptr;
    attach.entry.length = bytes;
    if (ring_cmd_nodata(so->ring, &attach, sizeof(attach)) < 0)
        return -1;
    so->attach_ptr = ptr;
    so->attach_bytes = bytes;
    return 0;
}

static int transfer_flush(virtio_scanout_t *so, uint32_t x, uint32_t y,
                          uint32_t w, uint32_t h)
{
    virtio_gpu_transfer_to_host_2d_t xfer;
    virtio_gpu_resource_flush_t flush;

    if (w == 0 || h == 0)
        return 0;

    memset(&xfer, 0, sizeof(xfer));
    hdr_init(&xfer.hdr, VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D);
    xfer.r.x = x;
    xfer.r.y = y;
    xfer.r.width = w;
    xfer.r.height = h;
    xfer.offset = ((uint64_t)y * so->width + x) * 4u;
    xfer.resource_id = so->resource_id;
    if (ring_cmd_nodata(so->ring, &xfer, sizeof(xfer)) < 0)
        return -1;

    memset(&flush, 0, sizeof(flush));
    hdr_init(&flush.hdr, VIRTIO_GPU_CMD_RESOURCE_FLUSH);
    flush.r.x = x;
    flush.r.y = y;
    flush.r.width = w;
    flush.r.height = h;
    flush.resource_id = so->resource_id;
    return ring_cmd_nodata(so->ring, &flush, sizeof(flush));
}

int virtio_cmd_setup_scanout(virtio_scanout_t *so, void *fb, uint32_t bytes,
                             uint32_t width, uint32_t height)
{
    virtio_gpu_resource_create_2d_t create;
    virtio_gpu_set_scanout_t scan;

    if (!so || !so->ring || !fb)
        return -1;

    so->resource_id = 1;
    so->width = width;
    so->height = height;
    so->attach_ptr = NULL;

    hdr_init(&create.hdr, VIRTIO_GPU_CMD_RESOURCE_CREATE_2D);
    create.resource_id = so->resource_id;
    create.format = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
    create.width = width;
    create.height = height;
    if (ring_cmd_nodata(so->ring, &create, sizeof(create)) < 0) {
        vga_print("virtio: CREATE_2D failed\n");
        return -1;
    }

    if (attach_backing(so, fb, bytes) < 0) {
        vga_print("virtio: ATTACH_BACKING failed\n");
        return -1;
    }

    memset(&scan, 0, sizeof(scan));
    hdr_init(&scan.hdr, VIRTIO_GPU_CMD_SET_SCANOUT);
    scan.r.x = 0;
    scan.r.y = 0;
    scan.r.width = width;
    scan.r.height = height;
    scan.scanout_id = 0;
    scan.resource_id = so->resource_id;
    if (ring_cmd_nodata(so->ring, &scan, sizeof(scan)) < 0) {
        vga_print("virtio: SET_SCANOUT failed\n");
        return -1;
    }
    return transfer_flush(so, 0, 0, width, height);
}

int virtio_cmd_present(virtio_scanout_t *so, void *data, uint32_t width,
                       uint32_t height, uint32_t stride_bytes,
                       uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    uint32_t bytes;

    if (!so || !so->ring || !data)
        return -1;
    if (width != so->width || height != so->height)
        return -1;
    if (stride_bytes != so->width * 4u)
        return -1; /* no CPU pitch fixup */

    bytes = width * height * 4u;
    if (data != so->attach_ptr) {
        if (attach_backing(so, data, bytes) < 0)
            return -1;
    }

    if (w == 0 || h == 0) {
        x = 0;
        y = 0;
        w = width;
        h = height;
    }
    if (x >= width || y >= height)
        return -1;
    if (x + w > width)
        w = width - x;
    if (y + h > height)
        h = height - y;
    return transfer_flush(so, x, y, w, h);
}

int virtio_cmd_submit_gpu(virtio_scanout_t *so, const void *gpu_cmds, uint32_t size)
{
    const uint8_t *p = (const uint8_t *)gpu_cmds;
    const uint8_t *end;
    const uint8_t *batch;
    uint32_t batch_size;

    if (!so || !gpu_cmds || size < sizeof(gpu_cmd_hdr_t))
        return -1;
    end = p + size;

    while (p + sizeof(gpu_cmd_hdr_t) <= end) {
        const gpu_cmd_hdr_t *h = (const gpu_cmd_hdr_t *)p;
        uint32_t psz;

        if (h->size < sizeof(gpu_cmd_hdr_t) || (h->size & 3u))
            return -1;
        psz = h->size;
        if (p + psz > end)
            return -1;

        if (h->op == GPU_CMD_PRESENT) {
            const gpu_cmd_present_t *c = (const gpu_cmd_present_t *)p;
            if (psz < sizeof(*c) || !c->color.data)
                return -1;
            if (virtio_cmd_present(so, c->color.data, c->color.width, c->color.height,
                                   c->color.stride, c->x, c->y, c->w, c->h) < 0)
                return -1;
            p += psz;
            continue;
        }

        /* Collect a contiguous non-PRESENT run for VirGL. */
        batch = p;
        batch_size = 0;
        while (p + sizeof(gpu_cmd_hdr_t) <= end) {
            const gpu_cmd_hdr_t *hh = (const gpu_cmd_hdr_t *)p;
            uint32_t psz2;
            if (hh->size < sizeof(gpu_cmd_hdr_t) || (hh->size & 3u))
                return -1;
            psz2 = hh->size;
            if (p + psz2 > end)
                return -1;
            if (hh->op == GPU_CMD_PRESENT)
                break;
            batch_size += psz2;
            p += psz2;
        }
        if (batch_size == 0)
            return -1;
        if (!virtio_virgl_ready())
            return -1;
        if (virtio_virgl_exec(batch, batch_size) < 0)
            return -1;
    }
    return 0;
}
