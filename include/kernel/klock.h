#ifndef KERNEL_KLOCK_H
#define KERNEL_KLOCK_H

#include <kernel/types.h>

/*
 * Kernel locking — CPU-level only (spinlock.asm / atomic.asm).
 * No __sync / soft CAS as the source of truth. Playbook law.
 *
 *   spinlock_t     — XCHG + PAUSE test-and-test-and-spin
 *   ticketlock_t   — fair LOCK XADD ticket
 *   klock_t        — reentrant + irqsave wrapper over spinlock
 *   klock_gfx      — DX + PS2 drivers_poll big-lock
 *
 * Waiting / events: <kernel/sync.h>
 */

typedef struct spinlock {
    volatile uint32_t locked; /* 0=free, 1=held — XCHG target */
} spinlock_t;

typedef struct ticketlock {
    volatile uint32_t next_ticket;
    volatile uint32_t now_serving;
} ticketlock_t;

/* ---- CPU asm (src/arch/x86/spinlock.asm) ---- */
void spinlock_acquire(spinlock_t *lock);
void spinlock_release(spinlock_t *lock);
int  spinlock_try_acquire(spinlock_t *lock);
void ticketlock_acquire(ticketlock_t *lock);
void ticketlock_release(ticketlock_t *lock);

static inline void spin_init(spinlock_t *l)
{
    if (l)
        l->locked = 0;
}

static inline void spin_lock(spinlock_t *l)
{
    if (l)
        spinlock_acquire(l);
}

static inline void spin_unlock(spinlock_t *l)
{
    if (l)
        spinlock_release(l);
}

static inline int spin_trylock(spinlock_t *l)
{
    if (!l)
        return 0;
    return spinlock_try_acquire(l);
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

static inline void ticket_init(ticketlock_t *l)
{
    if (!l)
        return;
    l->next_ticket = 0;
    l->now_serving = 0;
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

extern klock_t klock_gfx;

void klock_subsystem_init(void);

#endif
