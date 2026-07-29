#include <drivers/display/display.h>
#include <drivers/display/gpu.h>
#include <drivers/bus/pci.h>
#include <drivers/console/serial.h>

/*
 * Display ops registry + boot-time video debug dump.
 * Ownership: BSP register at kmod init; readers lock-free after ready.
 */

#define DISPLAY_REG_MAX 8

typedef struct display_reg {
    display_ops_t *ops;
    int            priority;
} display_reg_t;

static display_ops_t *g_active;
static int            g_priority = -1;
static display_reg_t  g_regs[DISPLAY_REG_MAX];
static int            g_reg_n;

void display_framework_init(void)
{
    g_active = NULL;
    g_priority = -1;
    g_reg_n = 0;
}

int display_register(display_ops_t *ops, int priority)
{
    int i;
    int became_active = 0;

    if (!ops || !ops->get_mode || !ops->present)
        return -1;

    for (i = 0; i < g_reg_n; i++) {
        if (g_regs[i].ops == ops) {
            g_regs[i].priority = priority;
            break;
        }
    }
    if (i == g_reg_n) {
        if (g_reg_n >= DISPLAY_REG_MAX)
            return -1;
        g_regs[g_reg_n].ops = ops;
        g_regs[g_reg_n].priority = priority;
        g_reg_n++;
    }

    if (!g_active || priority > g_priority) {
        g_active = ops;
        g_priority = priority;
        became_active = 1;
    }

    klog("[video] register display=");
    klog(ops->name ? ops->name : "?");
    klog(" prio=");
    serial_print_uint((uint32_t)priority);
    if (became_active)
        klog(" (active)");
    klog("\n");

    /* Also publish as GpuProvider for display.kmod / Reed. */
    (void)gpu_provider_register_display(ops, priority, NULL);
    return 0;
}

void display_unregister(display_ops_t *ops)
{
    int i;

    if (!ops)
        return;

    for (i = 0; i < g_reg_n; i++) {
        if (g_regs[i].ops == ops) {
            g_regs[i] = g_regs[g_reg_n - 1];
            g_reg_n--;
            break;
        }
    }

    if (g_active == ops) {
        display_ops_t *best = NULL;
        int best_prio = -1;
        for (i = 0; i < g_reg_n; i++) {
            if (g_regs[i].priority > best_prio) {
                best_prio = g_regs[i].priority;
                best = g_regs[i].ops;
            }
        }
        g_active = best;
        g_priority = best_prio;
    }
}

display_ops_t *display_active(void)
{
    return g_active;
}

int display_get_screen_size(uint32_t *w, uint32_t *h, uint32_t *bpp)
{
    display_mode_t mode;
    display_ops_t *ops = display_active();

    if (!ops || !ops->get_mode)
        return -1;
    if (ops->get_mode(&mode) < 0)
        return -1;

    if (w)
        *w = mode.width;
    if (h)
        *h = mode.height;
    if (bpp)
        *bpp = mode.bpp;
    return 0;
}

static const char *pci_display_subclass_name(uint8_t subclass)
{
    switch (subclass) {
    case 0x00: return "vga";
    case 0x01: return "xga";
    case 0x02: return "3d";
    case 0x80: return "other";
    default:   return "display";
    }
}

static int video_pci_gpu_cb(const pci_device_t *dev, void *ctx)
{
    int *n = (int *)ctx;
    uint32_t bar0;

    if (!dev || dev->class_code != 0x03)
        return 0;

    (*n)++;
    bar0 = pci_bar_phys(dev, 0);

    klog("[video]   pci ");
    serial_print_uint(dev->bus);
    klog(":");
    serial_print_uint(dev->slot);
    klog(".");
    serial_print_uint(dev->func);
    klog(" vend=");
    serial_print_hex(dev->vendor);
    klog(" dev=");
    serial_print_hex(dev->device);
    klog(" class=");
    serial_print_hex(dev->class_code);
    klog(":");
    serial_print_hex(dev->subclass);
    klog(" (");
    klog(pci_display_subclass_name(dev->subclass));
    klog(") if=");
    serial_print_hex(dev->prog_if);
    klog(" rev=");
    serial_print_hex(dev->revision);
    klog(" bar0=");
    serial_print_hex(bar0);
    klog("\n");
    return 0;
}

void display_boot_log(void)
{
    display_ops_t *ops;
    display_mode_t mode;
    gpu_provider_ops_t *gpu;
    int i;
    int pci_n = 0;

    klog("[video] -------- display dump --------\n");

    ops = display_active();
    if (ops) {
        klog("[video] active display=");
        klog(ops->name ? ops->name : "?");
        klog(" prio=");
        serial_print_uint((uint32_t)(g_priority < 0 ? 0 : g_priority));
        klog("\n");

        if (ops->get_mode && ops->get_mode(&mode) == 0) {
            klog("[video] screen ");
            serial_print_uint(mode.width);
            klog("x");
            serial_print_uint(mode.height);
            klog(" bpp=");
            serial_print_uint(mode.bpp);
            klog(" pitch=");
            serial_print_uint(mode.pitch);
            klog(" bppix=");
            serial_print_uint(mode.bytes_per_pixel);
            klog(" lfb=");
            serial_print_hex((uint32_t)(unsigned long)mode.addr);
            klog("\n");
        } else {
            klog("[video] screen: get_mode failed\n");
        }
    } else {
        klog("[video] active display=<none>\n");
        klog("[video] screen: n/a\n");
    }

    gpu = gpu_provider_active();
    if (gpu) {
        klog("[video] active gpu=");
        klog(gpu->name ? gpu->name : "?");
        klog(" caps=");
        serial_print_hex(gpu->caps);
        klog("\n");
        klog("[video]   caps detail:");
        if (gpu->caps & GPU_CAP_SCANOUT)
            klog(" scanout");
        if (gpu->caps & GPU_CAP_PRESENT_RECT)
            klog(" present_rect");
        if (gpu->caps & GPU_CAP_PRESENT_RECTS)
            klog(" present_rects");
        if (gpu->caps & GPU_CAP_HW_SUBMIT)
            klog(" hw_submit");
        klog("\n");
    } else {
        klog("[video] active gpu=<none>\n");
    }

    klog("[video] registered displays=");
    serial_print_uint((uint32_t)g_reg_n);
    klog("\n");
    for (i = 0; i < g_reg_n; i++) {
        display_ops_t *d = g_regs[i].ops;
        klog("[video]   [");
        serial_print_uint((uint32_t)i);
        klog("] ");
        klog(d && d->name ? d->name : "?");
        klog(" prio=");
        serial_print_uint((uint32_t)g_regs[i].priority);
        if (d == g_active)
            klog(" *active*");
        klog(" present=");
        klog(d && d->present ? "y" : "n");
        klog(" rect=");
        klog(d && d->present_rect ? "y" : "n");
        klog(" rects=");
        klog(d && d->present_rects ? "y" : "n");
        klog(" submit=");
        klog(d && d->gpu_submit ? "y" : "n");
        klog("\n");
    }

    klog("[video] PCI display class (0x03):\n");
    (void)pci_enumerate(video_pci_gpu_cb, &pci_n);
    if (pci_n == 0)
        klog("[video]   (none)\n");
    else {
        klog("[video]   count=");
        serial_print_uint((uint32_t)pci_n);
        klog("\n");
    }

    klog("[video] --------------------------------\n");
}
