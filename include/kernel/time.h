#ifndef MYKERNEL_KERNEL_TIME_H
#define MYKERNEL_KERNEL_TIME_H

#include <kernel/time_abi.h>

void time_init(void);

/* Pointer to the shared page (kernel + identity-mapped userspace). */
time_page_t *time_page_get(void);

/* Refresh calibration from CMOS + TSC (throttled ok). */
void time_recalibrate(void);

/* Set wall-clock UTC (nanoseconds since Unix epoch). */
int time_set_utc_nsec(uint64_t utc_nsec);

/* Timezone: offset seconds east of UTC, optional short name. */
int time_set_timezone(int32_t offset_sec, const char *name);

/* Clock display flags (e.g. 12-hour). */
int time_set_flags(uint32_t flags);

/* Kernel helpers. */
uint64_t time_utc_nsec_now(void);
uint64_t time_mono_nsec_now(void);
uint64_t time_rdtsc(void);

#endif
