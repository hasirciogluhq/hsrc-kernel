#ifndef MYKERNEL_KERNEL_TIME_ABI_H
#define MYKERNEL_KERNEL_TIME_ABI_H

#include <kernel/types.h>

/* Shared kernel↔userspace time page (identity-mapped; lock-free seqlock). */
#define TIME_PAGE_MAGIC 0x454D4954u /* 'TIME' little-endian */

#define TIME_FLAG_HOUR12  (1u << 0)

typedef struct time_page {
    uint32_t magic;
    volatile uint32_t seq;       /* seqlock: odd while writer updates */

    uint64_t utc_nsec;           /* Unix epoch UTC ns at calibration */
    uint64_t tsc_at_calib;       /* RDTSC at calibration */
    uint64_t tsc_hz;             /* TSC ticks per second */
    uint64_t mono_nsec_at_calib; /* monotonic ns at calibration */

    int32_t  tz_offset_sec;      /* local = UTC + offset */
    uint32_t flags;              /* TIME_FLAG_* */
    char     tz_name[16];        /* e.g. "UTC+3" */
    uint32_t reserved0;
} time_page_t;

/* SYS_TIME_GET / snapshot for rare explicit queries. */
typedef struct time_snapshot {
    uint64_t utc_nsec;
    uint64_t mono_nsec;
    int32_t  tz_offset_sec;
    uint32_t flags;
    char     tz_name[16];
} time_snapshot_t;

#endif
