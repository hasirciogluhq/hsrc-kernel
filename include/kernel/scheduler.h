#ifndef KERNEL_SCHEDULER_H
#define KERNEL_SCHEDULER_H

#include <kernel/types.h>
#include <kernel/process.h>

struct cpu;

/* Defaults: 3.5ms tick, 140ms thread life (≈40 ticks). Override via SYS_SCHED_SET. */
#define SCHED_TICK_US_DEFAULT       3500u
#define SCHED_THREAD_LIFE_US_DEFAULT 140000u

typedef struct sched_params {
    uint32_t tick_us;         /* interrupt period (µs) */
    uint32_t thread_life_us;  /* max continuous run before preempt (µs) */
    uint32_t life_ticks;      /* derived: life_us / tick_us */
    uint32_t cpu_count;       /* online HW logical CPUs (cores/SMT) */
    uint32_t threads_max;     /* per-process thread cap */
} sched_params_t;

void scheduler_init(void);
void scheduler_start(void);   /* never returns - run ready queue forever */
void schedule(void);          /* pick next ready thread (coop or timer preempt) */
void scheduler_unlock_new_thread(void); /* first entry after context_switch */
void scheduler_wake_sleepers(uint64_t now);
void scheduler_on_exit(process_t *p);
void scheduler_on_timer(void);
int  scheduler_current_is_idle(void);
int  scheduler_has_runnable_apps(void);
int  scheduler_create_idle_for_cpu(struct cpu *c);
uint64_t scheduler_tick_count(void);
uint64_t scheduler_idle_ticks(void);

/* Timing knobs (also exposed via SYS_SCHED_GET / SYS_SCHED_SET). */
uint32_t scheduler_tick_us(void);
uint32_t scheduler_thread_life_us(void);
uint32_t scheduler_life_ticks(void);
void     scheduler_get_params(sched_params_t *out);
/* 0 = leave field unchanged. Returns 0 or -errno. */
long     scheduler_set_params(uint32_t tick_us, uint32_t thread_life_us);

#endif
