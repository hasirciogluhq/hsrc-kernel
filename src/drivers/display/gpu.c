#include <drivers/display/gpu.h>
#include <kernel/string.h>

/*
 * GpuProvider registry — priority select (same model as display_ops).
 * Ownership: BSP write-once at provider kmod init; readers lock-free after ready.
 */

static gpu_provider_ops_t *g_active;
static int                 g_priority = -1;

#define GPU_DISP_BRIDGE_MAX 4
static gpu_provider_ops_t g_bridge[GPU_DISP_BRIDGE_MAX];
static int                g_bridge_n;

void gpu_framework_init(void)
{
    g_active = NULL;
    g_priority = -1;
    g_bridge_n = 0;
    memset(g_bridge, 0, sizeof(g_bridge));
}

int gpu_provider_register(gpu_provider_ops_t *ops, int priority)
{
    if (!ops || !ops->get_mode || !ops->present)
        return -1;
    if (!(ops->caps & GPU_CAP_SCANOUT))
        ops->caps |= GPU_CAP_SCANOUT;

    if (!g_active || priority > g_priority) {
        g_active = ops;
        g_priority = priority;
    }
    return 0;
}

void gpu_provider_unregister(gpu_provider_ops_t *ops)
{
    if (ops && g_active == ops) {
        g_active = NULL;
        g_priority = -1;
    }
}

gpu_provider_ops_t *gpu_provider_active(void)
{
    return g_active;
}

int gpu_get_screen_size(uint32_t *w, uint32_t *h, uint32_t *bpp)
{
    display_mode_t mode;
    gpu_provider_ops_t *ops = gpu_provider_active();

    if (!ops || !ops->get_mode)
        return -1;
    if (ops->get_mode(ops, &mode) < 0)
        return -1;
    if (w)
        *w = mode.width;
    if (h)
        *h = mode.height;
    if (bpp)
        *bpp = mode.bpp;
    return 0;
}

static display_ops_t *bridge_disp(gpu_provider_ops_t *self)
{
    return self ? (display_ops_t *)self->priv : NULL;
}

static int bridge_get_mode(gpu_provider_ops_t *self, display_mode_t *out)
{
    display_ops_t *d = bridge_disp(self);
    if (!d || !d->get_mode)
        return -1;
    return d->get_mode(out);
}

static int bridge_present(gpu_provider_ops_t *self,
                          const uint32_t *src, uint32_t src_stride_px)
{
    display_ops_t *d = bridge_disp(self);
    if (!d || !d->present)
        return -1;
    return d->present(src, src_stride_px);
}

static int bridge_present_rect(gpu_provider_ops_t *self,
                               const uint32_t *src, uint32_t src_stride_px,
                               uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    display_ops_t *d = bridge_disp(self);
    if (!d || !d->present_rect)
        return -1;
    return d->present_rect(src, src_stride_px, x, y, w, h);
}

static int bridge_present_rects(gpu_provider_ops_t *self,
                                const uint32_t *src, uint32_t src_stride_px,
                                const display_rect_t *rects, uint32_t n)
{
    display_ops_t *d = bridge_disp(self);
    if (!d || !d->present_rects)
        return -1;
    return d->present_rects(src, src_stride_px, rects, n);
}

static int bridge_gpu_submit(gpu_provider_ops_t *self,
                             const void *cmd, uint32_t size)
{
    display_ops_t *d = bridge_disp(self);
    if (!d || !d->gpu_submit)
        return -1;
    return d->gpu_submit(cmd, size);
}

int gpu_provider_register_display(display_ops_t *ops, int priority,
                                  gpu_provider_ops_t *out_slot)
{
    gpu_provider_ops_t *slot;
    uint32_t caps;

    if (!ops || !ops->get_mode || !ops->present)
        return -1;
    if (g_bridge_n >= GPU_DISP_BRIDGE_MAX)
        return -1;

    slot = &g_bridge[g_bridge_n++];
    caps = GPU_CAP_SCANOUT;
    if (ops->present_rect)
        caps |= GPU_CAP_PRESENT_RECT;
    if (ops->present_rects)
        caps |= GPU_CAP_PRESENT_RECTS;
    if (ops->gpu_submit)
        caps |= GPU_CAP_SUBMIT;

    memset(slot, 0, sizeof(*slot));
    slot->name = ops->name ? ops->name : "display";
    slot->caps = caps;
    slot->priv = ops;
    slot->get_mode = bridge_get_mode;
    slot->present = bridge_present;
    slot->present_rect = ops->present_rect ? bridge_present_rect : NULL;
    slot->present_rects = ops->present_rects ? bridge_present_rects : NULL;
    slot->gpu_submit = ops->gpu_submit ? bridge_gpu_submit : NULL;

    if (out_slot)
        *out_slot = *slot;

    return gpu_provider_register(slot, priority);
}
