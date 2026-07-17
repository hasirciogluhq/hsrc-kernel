#include <kernel/mkdx_api.h>

static const mkdx_api_t *g_mkdx;

void mkdx_api_register(const mkdx_api_t *api)
{
    g_mkdx = api;
}

const mkdx_api_t *mkdx_api_get(void)
{
    return g_mkdx;
}
