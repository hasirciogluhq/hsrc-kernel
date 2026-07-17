#ifndef MYKERNEL_ARCH_IRQ_H
#define MYKERNEL_ARCH_IRQ_H

#include <kernel/types.h>

void irq_init(void);
void irq_ensure_timer_unmasked(void);
void irq_dispatch(uint32_t irq);

uint64_t irq_timer_ticks(void);
uint64_t irq_idle_ticks(void);

#endif
