#include <kernel/scheduler.h>
#include <kernel/process.h>
#include <kernel/service.h>
#include <arch/x86/gdt.h>
#include <arch/x86/irq.h>
#include <drivers/driver.h>

static uint32_t *bootstrap_esp;
static uint64_t g_switch_ticks;
static process_t *g_idle_proc;

static int proc_runnable(process_t *p, uint64_t now)
{
    if (!p || p->state == PROC_UNUSED || p->state == PROC_ZOMBIE)
        return 0;
    if (p->state == PROC_BLOCKED)
        return p->wake_tick <= now;
    return p->state == PROC_READY || p->state == PROC_RUNNING;
}

static process_t *pick_next(process_t *cur)
{
    process_t **table = process_table();
    uint64_t now = irq_timer_ticks();
    process_t *fallback_idle = NULL;
    int start;

    if (!cur) {
        for (int i = 0; i < PROC_MAX; i++) {
            process_t *p = table[i];
            if (!proc_runnable(p, now)) {
                continue;
            }
            if (p->is_idle) {
                fallback_idle = p;
                continue;
            }
            return p;
        }
        return fallback_idle;
    }

    start = cur->slot;
    if (start < 0 || start >= PROC_MAX)
        start = 0;

    for (int i = 1; i <= PROC_MAX; i++) {
        int idx = (start + i) % PROC_MAX;
        process_t *p = table[idx];
        if (!proc_runnable(p, now)) {
            continue;
        }
        if (p->is_idle) {
            if (!fallback_idle)
                fallback_idle = p;
            continue;
        }
        return p;
    }

    if (proc_runnable(cur, now) && !cur->is_idle)
        return cur;

    return fallback_idle;
}

static void idle_halt(void)
{
    for (;;) {
        __asm__ volatile("sti; hlt" ::: "memory");
        drivers_poll();
        /* pick_next() returns the idle thread when nothing else is runnable —
         * that is not "work". Only break when a real app can run. */
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

void scheduler_init(void)
{
    pid_t pid;

    bootstrap_esp = NULL;
    g_switch_ticks = 0;
    g_idle_proc = NULL;

    pid = process_create("idle", idle_thread);
    if (pid > 0) {
        g_idle_proc = process_get(pid);
        if (g_idle_proc)
            g_idle_proc->is_idle = 1;
    }
}

void schedule(void)
{
    process_t *cur = process_current();
    process_t *next;

    process_reap_graveyard();
    service_reap_dead();

    if (cur && !cur->is_idle && cur->state != PROC_UNUSED && cur->state != PROC_ZOMBIE) {
        process_account_tick(cur);
        g_switch_ticks++;
    }

    next = pick_next(cur);

    /* Halt for timer IRQ when every app is blocked (yield sleep). Without
     * sti+hlt the CPU spins with IF=0 and wake_sleepers never runs. */
    if (!next || (next->is_idle && !scheduler_has_runnable_apps())) {
        idle_halt();
        next = pick_next(cur);
    }

    if (!next)
        return;

    if (cur && cur->state == PROC_RUNNING)
        cur->state = PROC_READY;

    next->state = PROC_RUNNING;

    if (cur == next) {
        process_set_current(next);
        gdt_set_kernel_stack(next->kstack_top);
        return;
    }

    process_set_current(next);
    gdt_set_kernel_stack(next->kstack_top);

    if (cur)
        context_switch(&cur->esp, next->esp);
    else
        context_switch(&bootstrap_esp, next->esp);
}

void scheduler_on_exit(process_t *p)
{
    (void)p;
}

void scheduler_on_timer(void)
{
    /* Timer IRQ path — wake_sleepers runs in irq_dispatch. */
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

    for (int i = 0; i < PROC_MAX; i++) {
        process_t *p = table[i];
        if (!p || p->is_idle)
            continue;
        if (proc_runnable(p, now))
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
    process_set_current(NULL);
    schedule();
    for (;;)
        idle_halt();
}
