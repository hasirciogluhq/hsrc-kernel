#ifndef KERNEL_SMP_H
#define KERNEL_SMP_H

#include <kernel/types.h>

/* Probe LAPIC, start APs, create per-CPU idle threads. Call after scheduler_init. */
void smp_init(void);

/* Let APs leave the boot barrier and enter the scheduler. */
void smp_start_scheduling(void);

/* Wake other CPUs from HLT so they can schedule runnable work. */
void smp_reschedule_others(void);

/* IPI only idle remote CPUs (orphan Ready work). */
void smp_kick_idle_cpus(void);

int smp_cpu_count(void);

#endif
