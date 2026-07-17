#include <kernel/time.h>
#include <kernel/string.h>
#include <arch/x86/io.h>

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static time_page_t g_time_page;
static int g_ready;

static uint8_t cmos_read(uint8_t reg)
{
    outb(CMOS_ADDR, (uint8_t)(reg | 0x80)); /* NMI disable bit */
    return inb(CMOS_DATA);
}

static int cmos_update_in_progress(void)
{
    return (cmos_read(0x0A) & 0x80) != 0;
}

static uint8_t bcd_to_bin(uint8_t v)
{
    return (uint8_t)((v & 0x0F) + ((v >> 4) * 10));
}

static int is_leap(int y)
{
    return ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
}

static int days_in_month(int y, int m)
{
    static const int mdays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m < 1 || m > 12)
        return 30;
    if (m == 2 && is_leap(y))
        return 29;
    return mdays[m - 1];
}

/* Civil date → Unix seconds (UTC). */
static uint64_t civil_to_unix(int y, int mo, int d, int hh, int mm, int ss)
{
    int64_t days = 0;
    int year;
    if (y < 1970)
        y = 1970;
    for (year = 1970; year < y; year++)
        days += is_leap(year) ? 366 : 365;
    for (int m = 1; m < mo; m++)
        days += days_in_month(y, m);
    days += (d - 1);
    return (uint64_t)days * 86400ull +
           (uint64_t)hh * 3600ull +
           (uint64_t)mm * 60ull +
           (uint64_t)ss;
}

static void read_cmos_hms(int *hh, int *mm, int *ss,
                          int *d, int *mo, int *y)
{
    uint8_t s, m, h, day, mon, yr, cent, reg_b;
    int binary;

    while (cmos_update_in_progress())
        ;

    s = cmos_read(0x00);
    m = cmos_read(0x02);
    h = cmos_read(0x04);
    day = cmos_read(0x07);
    mon = cmos_read(0x08);
    yr = cmos_read(0x09);
    cent = cmos_read(0x32);
    reg_b = cmos_read(0x0B);
    binary = (reg_b & 0x04) != 0;

    if (!binary) {
        s = bcd_to_bin(s);
        m = bcd_to_bin(m);
        h = bcd_to_bin(h & 0x7F);
        day = bcd_to_bin(day);
        mon = bcd_to_bin(mon);
        yr = bcd_to_bin(yr);
        cent = bcd_to_bin(cent);
    } else {
        h = (uint8_t)(h & 0x7F);
    }

    *ss = s;
    *mm = m;
    *hh = h;
    *d = day;
    *mo = mon;
    if (cent >= 19 && cent <= 20)
        *y = cent * 100 + yr;
    else
        *y = 2000 + yr;
}

uint64_t time_rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static void seq_begin_write(void)
{
    g_time_page.seq++; /* make odd */
    __asm__ volatile("" ::: "memory");
}

static void seq_end_write(void)
{
    __asm__ volatile("" ::: "memory");
    g_time_page.seq++; /* make even */
}

static uint64_t calibrate_tsc_hz(void)
{
    int hh, mm, ss, d, mo, y;
    int ss0, ss1;
    uint64_t t0, t1;
    uint64_t hz;
    int spins;

    /* Wait for a second boundary, then measure one full second via CMOS. */
    read_cmos_hms(&hh, &mm, &ss0, &d, &mo, &y);
    spins = 0;
    for (;;) {
        read_cmos_hms(&hh, &mm, &ss, &d, &mo, &y);
        if (ss != ss0)
            break;
        if (++spins > 20000000)
            return 2000000000ull; /* QEMU fallback ~2GHz */
    }

    t0 = time_rdtsc();
    ss1 = ss;
    spins = 0;
    for (;;) {
        read_cmos_hms(&hh, &mm, &ss, &d, &mo, &y);
        if (ss != ss1)
            break;
        if (++spins > 40000000)
            return 2000000000ull;
    }
    t1 = time_rdtsc();
    if (t1 <= t0)
        return 2000000000ull;
    hz = t1 - t0;
    /* Sanity clamp for QEMU / hosts. */
    if (hz < 100000000ull)
        hz = 100000000ull;
    if (hz > 10000000000ull)
        hz = 10000000000ull;
    return hz;
}

static void publish_calib(uint64_t utc_nsec, uint64_t tsc, uint64_t hz,
                          uint64_t mono_nsec)
{
    seq_begin_write();
    g_time_page.utc_nsec = utc_nsec;
    g_time_page.tsc_at_calib = tsc;
    g_time_page.tsc_hz = hz ? hz : 2000000000ull;
    g_time_page.mono_nsec_at_calib = mono_nsec;
    seq_end_write();
}

void time_init(void)
{
    int hh, mm, ss, d, mo, y;
    uint64_t hz, tsc, utc;

    memset(&g_time_page, 0, sizeof(g_time_page));
    g_time_page.magic = TIME_PAGE_MAGIC;
    g_time_page.seq = 0;
    g_time_page.tz_offset_sec = 3 * 3600; /* default UTC+3 */
    g_time_page.flags = 0;
    strncpy(g_time_page.tz_name, "UTC+3", sizeof(g_time_page.tz_name) - 1);

    hz = calibrate_tsc_hz();
    read_cmos_hms(&hh, &mm, &ss, &d, &mo, &y);
    utc = civil_to_unix(y, mo, d, hh, mm, ss) * 1000000000ull;
    tsc = time_rdtsc();
    publish_calib(utc, tsc, hz, 0);
    g_ready = 1;
}

time_page_t *time_page_get(void)
{
    return &g_time_page;
}

void time_recalibrate(void)
{
    int hh, mm, ss, d, mo, y;
    uint64_t tsc, utc, mono;
    uint64_t hz;

    if (!g_ready)
        return;

    hz = g_time_page.tsc_hz ? g_time_page.tsc_hz : 2000000000ull;
    read_cmos_hms(&hh, &mm, &ss, &d, &mo, &y);
    utc = civil_to_unix(y, mo, d, hh, mm, ss) * 1000000000ull;
    tsc = time_rdtsc();
    /* Keep monotonic continuous across recalibration. */
    if (hz) {
        uint64_t dt = tsc - g_time_page.tsc_at_calib;
        mono = g_time_page.mono_nsec_at_calib + (dt * 1000000000ull) / hz;
    } else {
        mono = g_time_page.mono_nsec_at_calib;
    }
    publish_calib(utc, tsc, hz, mono);
}

static uint64_t interpolate_ns(uint64_t base_ns, uint64_t tsc_base, uint64_t hz)
{
    uint64_t now = time_rdtsc();
    uint64_t dt;
    if (!hz)
        return base_ns;
    if (now >= tsc_base)
        dt = now - tsc_base;
    else
        dt = 0;
    return base_ns + (dt * 1000000000ull) / hz;
}

uint64_t time_utc_nsec_now(void)
{
    uint32_t s1, s2;
    uint64_t utc, tsc, hz;
    if (!g_ready)
        return 0;
    do {
        s1 = g_time_page.seq;
        __asm__ volatile("" ::: "memory");
        utc = g_time_page.utc_nsec;
        tsc = g_time_page.tsc_at_calib;
        hz = g_time_page.tsc_hz;
        __asm__ volatile("" ::: "memory");
        s2 = g_time_page.seq;
    } while ((s1 & 1u) || s1 != s2);
    return interpolate_ns(utc, tsc, hz);
}

uint64_t time_mono_nsec_now(void)
{
    uint32_t s1, s2;
    uint64_t mono, tsc, hz;
    if (!g_ready)
        return 0;
    do {
        s1 = g_time_page.seq;
        __asm__ volatile("" ::: "memory");
        mono = g_time_page.mono_nsec_at_calib;
        tsc = g_time_page.tsc_at_calib;
        hz = g_time_page.tsc_hz;
        __asm__ volatile("" ::: "memory");
        s2 = g_time_page.seq;
    } while ((s1 & 1u) || s1 != s2);
    return interpolate_ns(mono, tsc, hz);
}

int time_set_utc_nsec(uint64_t utc_nsec)
{
    uint64_t tsc, mono, hz;
    if (!g_ready)
        return -1;
    hz = g_time_page.tsc_hz ? g_time_page.tsc_hz : 2000000000ull;
    tsc = time_rdtsc();
    mono = time_mono_nsec_now();
    publish_calib(utc_nsec, tsc, hz, mono);
    return 0;
}

int time_set_timezone(int32_t offset_sec, const char *name)
{
    if (!g_ready)
        return -1;
    if (offset_sec < -14 * 3600 || offset_sec > 14 * 3600)
        return -1;
    seq_begin_write();
    g_time_page.tz_offset_sec = offset_sec;
    memset(g_time_page.tz_name, 0, sizeof(g_time_page.tz_name));
    if (name && name[0])
        strncpy(g_time_page.tz_name, name, sizeof(g_time_page.tz_name) - 1);
    else
        strncpy(g_time_page.tz_name, "UTC", sizeof(g_time_page.tz_name) - 1);
    seq_end_write();
    return 0;
}

int time_set_flags(uint32_t flags)
{
    if (!g_ready)
        return -1;
    seq_begin_write();
    g_time_page.flags = flags;
    seq_end_write();
    return 0;
}
