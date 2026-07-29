#include <kernel/dx_api.h>

static const dx_api_t *g_dx;

void dx_api_register(const dx_api_t *api)
{
    g_dx = api;
}

const dx_api_t *dx_api_get(void)
{
    return g_dx;
}
