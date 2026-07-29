#include <kernel/klock.h>
#include <arch/x86/cpu.h>

klock_t klock_disp;

void klock_init(klock_t *l, const char *name)
{
    if (!l)
        return;
    spin_init(&l->lock);
    l->owner = -1;
    l->depth = 0;
    l->irq_flags = 0;
    l->name = name ? name : "?";
}

void klock_acquire(klock_t *l)
{
    int id;

    if (!l)
        return;

    id = cpu_id();
    if (l->owner == id) {
        l->depth++;
        return;
    }

    l->irq_flags = spin_lock_irqsave(&l->lock);
    l->owner = id;
    l->depth = 1;
}

void klock_release(klock_t *l)
{
    int id;

    if (!l)
        return;

    id = cpu_id();
    if (l->owner != id)
        return;
    if (l->depth > 1) {
        l->depth--;
        return;
    }
    l->depth = 0;
    l->owner = -1;
    spin_unlock_irqrestore(&l->lock, l->irq_flags);
}

int klock_held(const klock_t *l)
{
    if (!l)
        return 0;
    return l->owner == cpu_id() && l->depth > 0;
}

void klock_subsystem_init(void)
{
    klock_init(&klock_disp, "disp");
}
