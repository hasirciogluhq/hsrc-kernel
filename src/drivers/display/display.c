#include <drivers/display.h>

static display_ops_t *g_active;
static int            g_priority = -1;

void display_framework_init(void)
{
    g_active = NULL;
    g_priority = -1;
}

int display_register(display_ops_t *ops, int priority)
{
    if (!ops || !ops->get_mode || !ops->present)
        return -1;

    if (!g_active || priority > g_priority) {
        g_active = ops;
        g_priority = priority;
    }
    return 0;
}

void display_unregister(display_ops_t *ops)
{
    if (ops && g_active == ops) {
        g_active = NULL;
        g_priority = -1;
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
