#ifndef ARCH_IRQ_H
#define ARCH_IRQ_H

#include <kernel/types.h>

void irq_init(void);
void irq_ensure_timer_unmasked(void);
void irq_dispatch(uint32_t irq);

uint64_t irq_timer_ticks(void);
uint64_t irq_idle_ticks(void);

/* Programmable PIT period (µs). Clamped; returns actual programmed period. */
uint32_t irq_timer_period_us(void);
uint32_t irq_timer_set_period_us(uint32_t us);

#endif

