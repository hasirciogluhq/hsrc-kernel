#ifndef KERNEL_KLOCK_H
#define KERNEL_KLOCK_H

#include <kernel/types.h>

/*
 * Kernel locking (single header).
 *
 *   spinlock_t / spin_* / kspin_*  — busy-wait; use irqsave in IRQ paths
 *   klock_t                        — reentrant + irqsave (nested OK on same CPU)
 *   klock_gfx                      — DX + PS2 drivers_poll big-lock
 *
 * Waiting / events live in <kernel/sync.h> (not locks).
 */

/* ---- spinlock (busy-wait) ---- */

typedef struct spinlock {
    volatile uint32_t locked;
} spinlock_t;

static inline void spin_init(spinlock_t *l)
{
    if (l)
        l->locked = 0;
}

static inline void spin_lock(spinlock_t *l)
{
    if (!l)
        return;
    for (;;) {
        if (__sync_bool_compare_and_swap(&l->locked, 0, 1))
            break;
        __asm__ volatile("pause" ::: "memory");
    }
}

static inline void spin_unlock(spinlock_t *l)
{
    if (!l)
        return;
    __asm__ volatile("" ::: "memory");
    l->locked = 0;
}

/* Acquire with interrupts disabled on this CPU. Returns previous FLAGS. */
static inline uint32_t spin_lock_irqsave(spinlock_t *l)
{
    uint32_t flags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(flags) :: "memory");
    spin_lock(l);
    return flags;
}

static inline void spin_unlock_irqrestore(spinlock_t *l, uint32_t flags)
{
    spin_unlock(l);
    if (flags & 0x200)
        __asm__ volatile("sti" ::: "memory");
}

static inline void kspin_init(spinlock_t *l) { spin_init(l); }
static inline void kspin_lock(spinlock_t *l) { spin_lock(l); }
static inline void kspin_unlock(spinlock_t *l) { spin_unlock(l); }
static inline uint32_t kspin_lock_irqsave(spinlock_t *l)
{
    return spin_lock_irqsave(l);
}
static inline void kspin_unlock_irqrestore(spinlock_t *l, uint32_t flags)
{
    spin_unlock_irqrestore(l, flags);
}

/* ---- klock (reentrant irqsave) ---- */

typedef struct klock {
    spinlock_t    lock;
    volatile int  owner;     /* cpu_id holding, or -1 */
    volatile int  depth;
    uint32_t      irq_flags; /* FLAGS saved at depth==1 acquire */
    const char   *name;
} klock_t;

void klock_init(klock_t *l, const char *name);
void klock_acquire(klock_t *l);
void klock_release(klock_t *l);
int  klock_held(const klock_t *l);

extern klock_t klock_gfx; /* DX present/input + drivers_poll (PS/2) */

void klock_subsystem_init(void);

#endif
