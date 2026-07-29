#include <kernel/disp_api.h>
#include <kernel/klock.h>

static const disp_api_t *g_raw;
static disp_api_t        g_wrap;
static int               g_wrap_ready;

static long w_call(uint32_t op, void *arg, uint32_t owner_pid)
{
    long r;
    if (!g_raw || !g_raw->call)
        return -1;
    klock_acquire(&klock_disp);
    r = g_raw->call(op, arg, owner_pid);
    klock_release(&klock_disp);
    return r;
}

static void w_cleanup_pid(uint32_t pid)
{
    if (!g_raw || !g_raw->cleanup_pid)
        return;
    klock_acquire(&klock_disp);
    g_raw->cleanup_pid(pid);
    klock_release(&klock_disp);
}

void disp_api_register(const disp_api_t *api)
{
    g_raw = api;
    g_wrap.call = w_call;
    g_wrap.cleanup_pid = w_cleanup_pid;
    g_wrap_ready = api ? 1 : 0;
}

const disp_api_t *disp_api_get(void)
{
    return g_wrap_ready ? &g_wrap : NULL;
}
