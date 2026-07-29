#include <kernel/scheduler.h>
#include <kernel/process.h>
#include <kernel/service.h>
#include <kernel/spinlock.h>
#include <kernel/smp.h>
#include <kernel/string.h>
#include <kernel/sync.h>
#include <kernel/errno.h>
#include <arch/x86/gdt.h>
#include <arch/x86/irq.h>
#include <arch/x86/cpu.h>
#include <drivers/driver.h>

/*
 * Per-CPU round-robin over OS threads.
 *
 * Model:
 *   - cpu_t / logical id  = HW thread (core or SMT sibling): shared ALU/FPU/cache
 *   - process_t           = OS thread: private kstack + callee-saved frame
 * Scheduler maps Ready OS threads onto the calling logical CPU. Same-app
 * threads (process group) soft-pack onto one home_cpu when possible.
 */

static cpu_context_t bootstrap_ctx[CPU_MAX];
static uint8_t bootstrap_fpu[CPU_MAX][CPU_FPU_AREA_SIZE] __attribute__((aligned(16)));
static uint64_t g_switch_ticks;
static spinlock_t g_sched_lock;
static volatile int g_sched_active;
static uint32_t g_slice_left[CPU_MAX];
/* Set by timer preempt path so schedule() prefers another Ready thread. */
static int g_prefer_other[CPU_MAX];

static uint32_t g_tick_us = SCHED_TICK_US_DEFAULT;
static uint32_t g_thread_life_us = SCHED_THREAD_LIFE_US_DEFAULT;

static uint32_t life_ticks_from(uint32_t tick_us, uint32_t life_us)
{
    uint32_t t;

    if (tick_us == 0)
        tick_us = SCHED_TICK_US_DEFAULT;
    if (life_us == 0)
        life_us = SCHED_THREAD_LIFE_US_DEFAULT;
    t = life_us / tick_us;
    if (t < 1)
        t = 1;
    if (t > 100000)
        t = 100000;
    return t;
}

uint32_t scheduler_tick_us(void)
{
    return g_tick_us;
}

uint32_t scheduler_thread_life_us(void)
{
    return g_thread_life_us;
}

uint32_t scheduler_life_ticks(void)
{
    return life_ticks_from(g_tick_us, g_thread_life_us);
}

void scheduler_get_params(sched_params_t *out)
{
    if (!out)
        return;
    out->tick_us = g_tick_us;
    out->thread_life_us = g_thread_life_us;
    out->life_ticks = scheduler_life_ticks();
    out->cpu_count = (uint32_t)smp_cpu_count();
    out->threads_max = (uint32_t)process_threads_max();
}

long scheduler_set_params(uint32_t tick_us, uint32_t thread_life_us)
{
    uint32_t new_tick = g_tick_us;
    uint32_t new_life = g_thread_life_us;
    int cpu;

    if (tick_us != 0) {
        if (tick_us < 500u || tick_us > 100000u)
            return -EINVAL;
        new_tick = irq_timer_set_period_us(tick_us);
    }
    if (thread_life_us != 0) {
        if (thread_life_us < 1000u || thread_life_us > 10000000u)
            return -EINVAL;
        new_life = thread_life_us;
    }

    g_tick_us = new_tick;
    g_thread_life_us = new_life;

    for (cpu = 0; cpu < CPU_MAX; cpu++)
        g_slice_left[cpu] = scheduler_life_ticks();
    return 0;
}

static int proc_runnable(process_t *p, uint64_t now)
{
    if (!p || p->state == PROC_UNUSED || p->state == PROC_ZOMBIE)
        return 0;
    if (p->state == PROC_SUSPENDED) {
        if (p->wake_tick == ~(uint64_t)0)
            return 0;
        return p->wake_tick <= now;
    }
    return p->state == PROC_READY || p->state == PROC_RUNNING;
}

static int proc_can_run_on(process_t *p, int cpu, process_t *cur)
{
    if (!p)
        return 0;
    if (p->is_idle && p->cpu_affinity >= 0 && p->cpu_affinity != cpu)
        return 0;
    /* One OS thread runs on at most one logical CPU at a time. */
    if (p->state == PROC_RUNNING && p != cur)
        return 0;
    if (p->cpu_affinity >= 0 && p->cpu_affinity != cpu && !p->is_idle)
        return 0;
    return 1;
}

/* Logical CPUs that share a physical core (SMT siblings). */
static int same_hw_core(int cpu_a, int cpu_b)
{
    cpu_t *a, *b;

    if (cpu_a < 0 || cpu_b < 0)
        return 0;
    if (cpu_a == cpu_b)
        return 1;
    a = cpu_get(cpu_a);
    b = cpu_get(cpu_b);
    if (!a || !b)
        return 0;
    return a->package == b->package && a->core == b->core;
}

static process_t *app_leader(process_t *p)
{
    return process_leader(p);
}

/* Soft home for the process group (stored on the leader). */
static int app_home_cpu(process_t *p)
{
    process_t *lead = app_leader(p);
    if (!lead)
        return -1;
    return lead->home_cpu;
}

/* True if another OS thread of the same app is (or was) on this core. */
static int app_packed_on_cpu(process_t *p, int cpu)
{
    process_t *lead = app_leader(p);
    process_t **table;
    int home;

    if (!lead || lead->is_idle)
        return 0;
    home = lead->home_cpu;
    if (home >= 0 && same_hw_core(home, cpu))
        return 1;

    table = process_table();
    for (int i = 0; i < PROC_MAX; i++) {
        process_t *th = table[i];
        if (!th || th->is_idle || th->state == PROC_UNUSED || th->state == PROC_ZOMBIE)
            continue;
        if (app_leader(th) != lead)
            continue;
        if (th->cpu >= 0 && same_hw_core(th->cpu, cpu))
            return 1;
    }
    return 0;
}

/*
 * Pick score (higher wins): place OS thread on this logical CPU.
 *   stickiness → same-app core pack → fairness (older last_run).
 */
static int pick_score(process_t *p, int cpu)
{
    int score = 0;
    int home;

    if (p->cpu == cpu)
        score += 100;
    else if (p->cpu >= 0 && same_hw_core(p->cpu, cpu))
        score += 80;

    home = app_home_cpu(p);
    if (home < 0)
        score += 30; /* unbound app: free to claim this CPU as home */
    else if (same_hw_core(home, cpu))
        score += 60;
    else
        score -= 40; /* prefer CPUs near the app's home core */

    if (app_packed_on_cpu(p, cpu))
        score += 50;

    /* Age: fewer recent ticks → slightly higher (fair share). */
    if (p->last_run_tick < 0xFFFFFFFFu)
        score += (int)(0xFFu - (p->last_run_tick & 0xFFu));

    return score;
}

/* True if a different Ready (waiting) thread could run on this CPU. */
static int has_waiting_ready(process_t *cur, int cpu)
{
    process_t **table = process_table();
    uint64_t now = irq_timer_ticks();

    for (int i = 0; i < PROC_MAX; i++) {
        process_t *p = table[i];
        if (!p || p->is_idle || p == cur)
            continue;
        /* Ready, or timed-suspend whose wake_tick already elapsed. */
        if (p->state == PROC_READY) {
            /* ok */
        } else if (p->state == PROC_SUSPENDED &&
                   p->wake_tick != ~(uint64_t)0 && p->wake_tick <= now) {
            /* ok - will be woken on pick */
        } else {
            continue;
        }
        if (!proc_can_run_on(p, cpu, cur))
            continue;
        return 1;
    }
    return 0;
}

/*
 * Dynamic pick: soft stickiness + same-app core pack + fair last_run → idle.
 * When prefer_other is set (timeslice preempt), skip cur if another Ready waits.
 */
static process_t *pick_next(process_t *cur, int cpu, int prefer_other)
{
    process_t **table = process_table();
    uint64_t now = irq_timer_ticks();
    process_t *fallback_idle = NULL;
    process_t *best = NULL;
    int best_score = -0x7FFFFFFF;
    int start;

    start = cur && cur->slot >= 0 && cur->slot < PROC_MAX ? cur->slot : 0;

    for (int i = 0; i < PROC_MAX; i++) {
        int idx = (start + i) % PROC_MAX;
        process_t *p = table[idx];
        int score;

        if (!proc_runnable(p, now) || !proc_can_run_on(p, cpu, cur))
            continue;
        if (p->is_idle) {
            if (!fallback_idle)
                fallback_idle = p;
            continue;
        }
        if (prefer_other && p == cur)
            continue;

        score = pick_score(p, cpu);
        if (!best || score > best_score ||
            (score == best_score && p->last_run_tick < best->last_run_tick)) {
            best = p;
            best_score = score;
        }
    }

    if (best)
        return best;
    /* Timeslice expired but nobody else - keep cur (caller refreshes slice). */
    if (prefer_other && cur && proc_runnable(cur, now) &&
        proc_can_run_on(cur, cpu, cur) && !cur->is_idle)
        return cur;
    if (cur && proc_runnable(cur, now) && proc_can_run_on(cur, cpu, cur) &&
        !cur->is_idle)
        return cur;
    return fallback_idle;
}

static void assign_home_cpu(process_t *p, int cpu)
{
    process_t *lead = app_leader(p);

    if (!lead || lead->is_idle)
        return;
    if (lead->home_cpu < 0)
        lead->home_cpu = cpu;
}

static void idle_halt(void)
{
    for (;;) {
        __asm__ volatile("sti; hlt" ::: "memory");
        if (cpu_id() == 0)
            drivers_poll();
        if (scheduler_has_runnable_apps())
            break;
    }
}

static void idle_thread(void)
{
    for (;;) {
        if (scheduler_has_runnable_apps()) {
            schedule();
            continue;
        }
        idle_halt();
        schedule();
    }
}

int scheduler_create_idle_for_cpu(cpu_t *c)
{
    pid_t pid;
    process_t *idle;
    char name[12];

    if (!c)
        return -1;

    name[0] = 'i';
    name[1] = 'd';
    name[2] = 'l';
    name[3] = 'e';
    name[4] = '-';
    name[5] = (char)('0' + (c->id % 10));
    name[6] = '\0';

    pid = process_create(name, idle_thread);
    if (pid <= 0)
        return -1;
    idle = process_get(pid);
    if (!idle)
        return -1;
    idle->is_idle = 1;
    idle->cpu_affinity = c->id;
    c->idle = idle;
    return 0;
}

void scheduler_init(void)
{
    cpu_t *bsp = cpu_get(0);
    uint32_t life;
    uint32_t cr3;
    int i;

    spin_init(&g_sched_lock);
    memset(bootstrap_ctx, 0, sizeof(bootstrap_ctx));
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    for (i = 0; i < CPU_MAX; i++) {
        memset(bootstrap_fpu[i], 0, CPU_FPU_AREA_SIZE);
        __asm__ volatile("fninit; fxsave %0"
                         : "=m"(bootstrap_fpu[i])
                         :
                         : "memory");
        bootstrap_ctx[i].fpu_area = bootstrap_fpu[i];
        bootstrap_ctx[i].cr3 = cr3;
    }
    g_switch_ticks = 0;
    g_sched_active = 0;
    g_tick_us = irq_timer_period_us();
    if (g_tick_us == 0)
        g_tick_us = SCHED_TICK_US_DEFAULT;
    g_thread_life_us = SCHED_THREAD_LIFE_US_DEFAULT;
    life = scheduler_life_ticks();
    for (i = 0; i < CPU_MAX; i++) {
        g_slice_left[i] = life;
        g_prefer_other[i] = 0;
    }

    if (!bsp)
        bsp = cpu_current();
    (void)scheduler_create_idle_for_cpu(bsp);
}

void scheduler_unlock_new_thread(void)
{
    spin_unlock(&g_sched_lock);
    __asm__ volatile("sti" ::: "memory");
}

void scheduler_wake_sleepers(uint64_t now)
{
    process_t **table = process_table();
    uint32_t flags = spin_lock_irqsave(&g_sched_lock);

    for (int i = 0; i < PROC_MAX; i++) {
        process_t *p = table[i];
        if (!p || p->state != PROC_SUSPENDED)
            continue;
        if (p->wake_tick == ~(uint64_t)0)
            continue;
        if (p->wake_tick <= now)
            process_wake(p);
    }

    spin_unlock_irqrestore(&g_sched_lock, flags);
}

void schedule(void)
{
    process_t *cur;
    process_t *next;
    int cpu;
    int prefer;
    cpu_context_t *old_ctx;
    uint32_t flags;
    uint64_t now;

    if (!g_sched_active)
        return;

    cpu = cpu_id();

    process_reap_graveyard();
    service_reap_dead();

    cur = process_current();
    if (cur && cur->kill_pending && !cur->is_idle) {
        if (cur->group)
            process_thread_exit(cur->exit_code ? cur->exit_code : 137);
        else
            process_exit(137);
        return;
    }

    flags = spin_lock_irqsave(&g_sched_lock);
    cur = process_current();

    prefer = 0;
    if (cpu >= 0 && cpu < CPU_MAX) {
        prefer = g_prefer_other[cpu];
        g_prefer_other[cpu] = 0;
    }

    next = pick_next(cur, cpu, prefer);

    if (!next || (next->is_idle && !scheduler_has_runnable_apps())) {
        spin_unlock_irqrestore(&g_sched_lock, flags);
        idle_halt();
        flags = spin_lock_irqsave(&g_sched_lock);
        cur = process_current();
        next = pick_next(cur, cpu, 0);
    }

    if (!next) {
        spin_unlock_irqrestore(&g_sched_lock, flags);
        return;
    }

    /* Timed suspend may be picked before wake_sleepers; clear wait metadata. */
    if (next->state == PROC_SUSPENDED)
        process_wake(next);

    if (cur && cur->state == PROC_RUNNING)
        cur->state = PROC_READY;

    now = irq_timer_ticks();
    next->state = PROC_RUNNING;
    next->cpu = cpu;
    next->last_run_tick = now;
    assign_home_cpu(next, cpu);

    if (cpu >= 0 && cpu < CPU_MAX && next != cur)
        g_slice_left[cpu] = scheduler_life_ticks();

    if (cur == next) {
        process_set_current(next);
        gdt_set_kernel_stack(next->kstack_top);
        spin_unlock_irqrestore(&g_sched_lock, flags);
        return;
    }

    if (cur && !cur->is_idle)
        g_switch_ticks++;

    process_set_current(next);
    gdt_set_kernel_stack(next->kstack_top);

    old_ctx = (cur) ? &cur->ctx : &bootstrap_ctx[cpu];

    /*
     * Kick idle APs when Ready OS threads can run elsewhere (no hard pin to
     * this CPU). Same-app soft home still allows other CPUs as fallback.
     */
    {
        process_t **table = process_table();
        int kick = 0;
        for (int i = 0; i < PROC_MAX; i++) {
            process_t *p = table[i];
            if (!p || p->is_idle || p->state != PROC_READY)
                continue;
            if (p->cpu_affinity < 0 || p->cpu_affinity != cpu) {
                kick = 1;
                break;
            }
        }
        if (kick)
            smp_kick_idle_cpus();
    }

    /* PRE: g_sched_lock + IRQs off — playbook contract. */
    context_switch_locked(old_ctx, &next->ctx);
    spin_unlock_irqrestore(&g_sched_lock, flags);
}

void scheduler_on_exit(process_t *p)
{
    (void)p;
}

void scheduler_on_timer(void)
{
    process_t *cur;
    int cpu;
    uint32_t life;

    if (!g_sched_active)
        return;

    cur = process_current();
    cpu = cpu_id();
    life = scheduler_life_ticks();

    if (cur && !cur->is_idle && cur->state == PROC_RUNNING)
        process_account_tick(cur);

    if (cpu < 0 || cpu >= CPU_MAX)
        return;

    /*
     * Idle CPU with Ready work → pick it up. Do not preempt a Running
     * thread just because input arrived or an interrupt fired.
     */
    if (!cur || cur->is_idle || cur->state != PROC_RUNNING) {
        if (scheduler_has_runnable_apps())
            schedule();
        return;
    }

    /* Ensure slice is armed (e.g. after first switch onto this CPU). */
    if (g_slice_left[cpu] == 0)
        g_slice_left[cpu] = life;

    if (g_slice_left[cpu] > 0)
        g_slice_left[cpu]--;

    if (g_slice_left[cpu] > 0)
        return;

    /*
     * Life expired. Switch only if another Ready thread is waiting;
     * otherwise refresh slice and keep the same thread untouched.
     */
    if (has_waiting_ready(cur, cpu)) {
        g_slice_left[cpu] = life;
        g_prefer_other[cpu] = 1;
        schedule();
        return;
    }

    g_slice_left[cpu] = life;
}

int scheduler_current_is_idle(void)
{
    process_t *cur = process_current();
    return cur && cur->is_idle;
}

int scheduler_has_runnable_apps(void)
{
    process_t **table = process_table();
    uint64_t now = irq_timer_ticks();
    int cpu = cpu_id();

    for (int i = 0; i < PROC_MAX; i++) {
        process_t *p = table[i];
        if (!p || p->is_idle)
            continue;
        if (proc_runnable(p, now) && proc_can_run_on(p, cpu, process_current()))
            return 1;
    }
    return 0;
}

uint64_t scheduler_tick_count(void)
{
    return irq_timer_ticks();
}

uint64_t scheduler_idle_ticks(void)
{
    return irq_idle_ticks();
}

void scheduler_start(void)
{
    uint32_t life;
    int i;

    __asm__ volatile("" ::: "memory");
    g_tick_us = irq_timer_period_us();
    if (g_tick_us == 0)
        g_tick_us = SCHED_TICK_US_DEFAULT;
    life = scheduler_life_ticks();
    for (i = 0; i < CPU_MAX; i++)
        g_slice_left[i] = life;
    g_sched_active = 1;
    smp_start_scheduling();
    process_set_current(NULL);
    schedule();
    for (;;)
        idle_halt();
}
