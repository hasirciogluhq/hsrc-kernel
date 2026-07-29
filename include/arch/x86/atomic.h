#ifndef ARCH_X86_ATOMIC_H
#define ARCH_X86_ATOMIC_H

#include <kernel/types.h>

/* CPU-level atomics — implemented in atomic.asm (LOCK / XCHG). No __sync. */

void     atomic_inc32(volatile uint32_t *counter);
uint32_t atomic_fetch_add32(volatile uint32_t *ptr, uint32_t val);
int      atomic_cas32(volatile uint32_t *ptr, uint32_t expected, uint32_t desired);

#endif
