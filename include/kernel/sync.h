#ifndef MYKERNEL_SYNC_H
#define MYKERNEL_SYNC_H

#include <kernel/types.h>
#include <kernel/process.h>

/* Kernel auto-reset events — waiters leave the Ready queue until signal/timeout. */
#define KEVENT_MAX 128

/* timeout_ticks: <0 = forever; 0 = try (non-blocking); >0 = max wait. */
#define KEVENT_WAIT_FOREVER ((long)-1)

void sync_init(void);
void sync_cleanup_process(pid_t pid);

/* Returns event id (>=0) or -errno. */
int  kevent_create(void);
int  kevent_destroy(int id);
/* 0 = signaled; -ETIMEDOUT; -errno. */
long kevent_wait(int id, long timeout_ticks);
/* Wake one waiter (or sticky-signal if none). */
long kevent_signal(int id);
/* Wake every waiter. */
long kevent_broadcast(int id);

/* Block current thread until wake_tick (or forever if wake_tick == ~0ULL). */
void process_block(uint64_t wake_tick);
/* Move BLOCKED → READY; safe from IRQ / other CPU contexts. */
void process_wake(process_t *p);

#endif
