#pragma once

#include <kernel/time_abi.h>
#include <kernel/types.h>

namespace hsrc::sdk::time {

struct DateTime {
    int year = 1970;
    int month = 1;   /* 1..12 */
    int day = 1;     /* 1..31 */
    int hour = 0;    /* 0..23 */
    int minute = 0;
    int second = 0;
    int wday = 4;    /* 0=Sun .. 6=Sat; 1970-01-01 was Thursday */
};

/* Map shared time page once (SYS_TIME_MAP). Safe to call repeatedly. */
bool init();

/* Hot path: read shared page + RDTSC interpolate — no syscall. */
uint64_t unix_ns();
uint64_t unix_sec();
uint64_t mono_ns();

/* Broken-down wall clock. */
DateTime utc_now();
DateTime local_now();

/* Timezone (also written into shared page via syscall). */
int  timezone_offset_sec();
const char *timezone_name();
bool set_timezone(int offset_sec, const char *name);

/* Clock display preference published on the time page. */
bool hour12();
bool set_hour12(bool enabled);

/* Formatting helpers (no heap). */
void format_clock(char *out, size_t out_sz);          /* "Fri 14:32" / "Fri 2:32 PM" */
void format_clock_hm(char *out, size_t out_sz);       /* "14:32" / "2:32 PM" */
void format_iso_local(char *out, size_t out_sz);      /* "2026-07-17T14:32:05+03:00" */
void format_iso_utc(char *out, size_t out_sz);        /* "2026-07-17T11:32:05Z" */

/* Rare explicit snapshot via SYS_TIME_GET (not for per-frame use). */
bool snapshot(time_snapshot_t &out);

/* Parse settings strings like "UTC+3", "UTC", "UTC-5". */
bool parse_timezone_label(const char *label, int *offset_sec_out);

} // namespace hsrc::sdk::time
