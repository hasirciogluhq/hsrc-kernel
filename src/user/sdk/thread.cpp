#include <user/sdk/thread.hpp>

namespace hsrc::sdk {
namespace thread {

struct StartPack {
    void (*fn)(void *);
    void *arg;
    int used;
};

/* One pack per in-flight create until the child thread starts. */
static StartPack g_packs[256];
static volatile long g_last_create_err;

long Thread::last_error()
{
    return g_last_create_err;
}

static StartPack *pack_alloc(void (*fn)(void *), void *arg)
{
    for (int i = 0; i < 256; i++) {
        if (__sync_bool_compare_and_swap(&g_packs[i].used, 0, 1)) {
            g_packs[i].fn = fn;
            g_packs[i].arg = arg;
            return &g_packs[i];
        }
    }
    return nullptr;
}

static void pack_free(StartPack *p)
{
    if (!p)
        return;
    p->fn = nullptr;
    p->arg = nullptr;
    __sync_lock_release(&p->used);
}

static void bootstrap(void *raw)
{
    StartPack *p = (StartPack *)raw;
    void (*fn)(void *) = p ? p->fn : nullptr;
    void *arg = p ? p->arg : nullptr;
    pack_free(p);
    if (fn)
        fn(arg);
    this_thread::exit(0);
}

Thread Thread::create(void (*fn)(void *), void *arg)
{
    Thread t;
    StartPack *pack;

    g_last_create_err = 0;
    if (!fn) {
        g_last_create_err = -22; /* EINVAL */
        return t;
    }

    pack = pack_alloc(fn, arg);
    if (!pack) {
        g_last_create_err = -11; /* EAGAIN */
        return t;
    }

    long r = syscall2(SYS_THREAD_CREATE, (long)(uintptr_t)bootstrap,
                      (long)(uintptr_t)pack);
    if (r > 0) {
        t.tid_ = (tid_t)r;
        t.joinable_ = true;
        g_last_create_err = 0;
    } else {
        pack_free(pack);
        g_last_create_err = r; /* -EAGAIN / -ENOMEM / -EPERM / … */
    }
    return t;
}

bool Thread::join(int *status_out)
{
    if (!joinable_ || tid_ <= 0)
        return false;
    long r = syscall2(SYS_THREAD_JOIN, (long)tid_,
                      (long)(uintptr_t)status_out);
    joinable_ = false;
    tid_ = 0;
    return r == 0;
}

bool Thread::detach()
{
    if (!joinable_ || tid_ <= 0)
        return false;
    long r = syscall1(SYS_THREAD_DETACH, (long)tid_);
    joinable_ = false;
    tid_ = 0;
    return r == 0;
}

} // namespace thread
} // namespace hsrc::sdk
