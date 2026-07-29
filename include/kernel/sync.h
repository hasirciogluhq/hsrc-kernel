#ifndef KERNEL_SYNC_H
#define KERNEL_SYNC_H

#include <kernel/types.h>
#include <kernel/process.h>

/*
 * Kernel wait / wake (not locks — see <kernel/klock.h> for spin/klock).
 *
 *   kevent_*           — auto-reset events (userspace SYS_EVENT_*)
 *   process_suspend/wake
 *   input_event_*      — GUI input wait
 */

#define KEVENT_MAX 128
#define KEVENT_WAIT_FOREVER ((long)-1)

void sync_init(void);
void sync_cleanup_process(pid_t pid);

int  kevent_create(void);
int  kevent_destroy(int id);
long kevent_wait(int id, long timeout_ticks);
long kevent_signal(int id);
long kevent_broadcast(int id);

void process_suspend(uint64_t wake_tick);
void process_wake(process_t *p);

static inline void process_block(uint64_t wake_tick)
{
    process_suspend(wake_tick);
}

#define INPUT_EV_MOVE   (1u << 0)
#define INPUT_EV_BUTTON (1u << 1)
#define INPUT_EV_WHEEL  (1u << 2)
#define INPUT_EV_KEY    (1u << 3)
#define INPUT_EV_FOCUS  (1u << 4)
#define INPUT_EV_WM     (1u << 5)

uint32_t input_event_seq(void);
void     input_event_notify(uint32_t flags, int hit_id, int focus_id,
                            int prev_hit_id, int wm_id);
int      input_event_need_sched(void);
long     input_event_wait(int win_id, uint32_t last_seq, long timeout_ticks);

#endif
