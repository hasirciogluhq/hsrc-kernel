#include <kernel/epoll.h>
#include <kernel/socket.h>
#include <kernel/process.h>
#include <kernel/errno.h>
#include <kernel/string.h>
#include <kernel/netif.h>
#include <kernel/scheduler.h>
#include <kernel/sync.h>
#include <arch/x86/irq.h>

#define EPOLL_MAX       16
#define EPOLL_SLOTS     32

typedef struct {
    int          used;
    int          user_fd;   /* process-visible fd being watched */
    int          sock_sid;  /* >=0 if target is a socket */
    uint32_t     events;    /* interest mask */
    epoll_data_t data;
} epoll_item_t;

typedef struct {
    int          used;
    epoll_item_t items[EPOLL_SLOTS];
    int          nitems;
    pid_t        waiter;
} epoll_t;

static epoll_t g_epolls[EPOLL_MAX];

void epoll_init(void)
{
    memset(g_epolls, 0, sizeof(g_epolls));
}

static epoll_t *epoll_get(int eid)
{
    if (eid < 0 || eid >= EPOLL_MAX || !g_epolls[eid].used)
        return NULL;
    return &g_epolls[eid];
}

static void epoll_wake(epoll_t *e)
{
    process_t *p;
    if (!e || e->waiter <= 0)
        return;
    p = process_by_tid(e->waiter);
    e->waiter = -1;
    if (p)
        process_wake(p);
}

static int epoll_find_item(epoll_t *e, int user_fd)
{
    int i;
    for (i = 0; i < EPOLL_SLOTS; i++) {
        if (e->items[i].used && e->items[i].user_fd == user_fd)
            return i;
    }
    return -1;
}

static uint32_t item_ready(const epoll_item_t *it)
{
    uint32_t ready;
    if (!it || !it->used)
        return 0;
    if (it->sock_sid < 0)
        return 0; /* VFS fds: no poll yet */
    ready = sock_poll_events(it->sock_sid);
    return ready & (it->events | EPOLLERR | EPOLLHUP);
}

int epoll_create_inst(int flags)
{
    int i;
    (void)flags;
    for (i = 0; i < EPOLL_MAX; i++) {
        if (!g_epolls[i].used) {
            memset(&g_epolls[i], 0, sizeof(g_epolls[i]));
            g_epolls[i].used = 1;
            g_epolls[i].waiter = -1;
            return i;
        }
    }
    return -EMFILE;
}

int epoll_ctl_inst(int eid, int op, int user_fd, uint32_t events, epoll_data_t data)
{
    epoll_t *e = epoll_get(eid);
    process_t *p = process_current();
    int enc, idx, sid = -1;
    int i;

    if (!e || !p || user_fd < 0)
        return -EINVAL;

    enc = process_lookup_fd(p, user_fd);
    if (enc < 0)
        return -EBADF;
    if (PROC_FD_IS_EPOLL(enc))
        return -EINVAL; /* no nested epoll */
    if (PROC_FD_IS_SOCK(enc))
        sid = PROC_FD_SOCK_ID(enc);
    else
        return -EPERM; /* only sockets for now */

    idx = epoll_find_item(e, user_fd);

    if (op == EPOLL_CTL_ADD) {
        if (idx >= 0)
            return -EEXIST;
        for (i = 0; i < EPOLL_SLOTS; i++) {
            if (!e->items[i].used) {
                e->items[i].used = 1;
                e->items[i].user_fd = user_fd;
                e->items[i].sock_sid = sid;
                e->items[i].events = events;
                e->items[i].data = data;
                e->nitems++;
                if (item_ready(&e->items[i]))
                    epoll_wake(e);
                return 0;
            }
        }
        return -ENOSPC;
    }

    if (op == EPOLL_CTL_MOD) {
        if (idx < 0)
            return -ENOENT;
        e->items[idx].events = events;
        e->items[idx].data = data;
        e->items[idx].sock_sid = sid;
        if (item_ready(&e->items[idx]))
            epoll_wake(e);
        return 0;
    }

    if (op == EPOLL_CTL_DEL) {
        if (idx < 0)
            return -ENOENT;
        memset(&e->items[idx], 0, sizeof(e->items[idx]));
        if (e->nitems > 0)
            e->nitems--;
        return 0;
    }

    return -EINVAL;
}

static int epoll_collect(epoll_t *e, epoll_event_t *out, int maxevents)
{
    int i, n = 0;
    if (!e || !out || maxevents <= 0)
        return 0;
    for (i = 0; i < EPOLL_SLOTS && n < maxevents; i++) {
        uint32_t ready;
        if (!e->items[i].used)
            continue;
        ready = item_ready(&e->items[i]);
        if (!ready)
            continue;
        out[n].events = ready;
        out[n].data = e->items[i].data;
        n++;
    }
    return n;
}

static uint32_t ms_to_deadline_ticks(int timeout_ms)
{
    uint32_t tick_us, ms_per_tick, ticks;
    if (timeout_ms < 0)
        return 0; /* forever: caller uses 0 sentinel differently */
    if (timeout_ms == 0)
        return irq_timer_ticks(); /* already expired → one poll pass */
    tick_us = scheduler_tick_us();
    if (tick_us == 0)
        tick_us = 3500u;
    ms_per_tick = (tick_us + 999u) / 1000u;
    if (ms_per_tick == 0)
        ms_per_tick = 1;
    ticks = (uint32_t)timeout_ms / ms_per_tick;
    if (ticks == 0)
        ticks = 1;
    return irq_timer_ticks() + ticks;
}

int epoll_wait_inst(int eid, epoll_event_t *events, int maxevents, int timeout_ms)
{
    epoll_t *e = epoll_get(eid);
    process_t *cur;
    uint32_t deadline = 0;
    int forever;

    if (!e || !events || maxevents <= 0)
        return -EINVAL;
    if (maxevents > EPOLL_SLOTS)
        maxevents = EPOLL_SLOTS;

    forever = (timeout_ms < 0);
    if (!forever)
        deadline = ms_to_deadline_ticks(timeout_ms);

    for (;;) {
        int n;
        net_poll();
        n = epoll_collect(e, events, maxevents);
        if (n > 0)
            return n;
        if (!forever && irq_timer_ticks() >= deadline)
            return 0;

        cur = process_current();
        if (!cur)
            return -ESRCH;
        e->waiter = cur->tid > 0 ? cur->tid : cur->pid;
        if (forever)
            process_block(irq_timer_ticks() + 2);
        else {
            uint32_t left = deadline - irq_timer_ticks();
            if ((int32_t)left <= 0)
                return 0;
            if (left > 2)
                left = 2;
            process_block(irq_timer_ticks() + left);
        }
        schedule();
    }
}

int epoll_close_inst(int eid)
{
    epoll_t *e = epoll_get(eid);
    if (!e)
        return -EBADF;
    epoll_wake(e);
    memset(e, 0, sizeof(*e));
    return 0;
}

void epoll_sock_notify(int sid)
{
    int i, j;
    if (sid < 0)
        return;
    for (i = 0; i < EPOLL_MAX; i++) {
        epoll_t *e = &g_epolls[i];
        if (!e->used)
            continue;
        for (j = 0; j < EPOLL_SLOTS; j++) {
            if (!e->items[j].used || e->items[j].sock_sid != sid)
                continue;
            if (item_ready(&e->items[j])) {
                epoll_wake(e);
                break;
            }
        }
    }
}
