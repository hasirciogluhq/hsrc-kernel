#include <user/sdk/time.hpp>
#include <user/sdk/syscall.hpp>
#include <user/string.h>

namespace hsrc::sdk::time {
namespace {

const time_page_t *g_page = nullptr;

static uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static uint64_t interpolate(uint64_t base_ns, uint64_t tsc_base, uint64_t hz)
{
    if (!hz)
        return base_ns;
    uint64_t now = rdtsc();
    uint64_t dt = (now >= tsc_base) ? (now - tsc_base) : 0;
    return base_ns + (dt * 1000000000ull) / hz;
}

static bool read_fields(uint64_t *utc_nsec, uint64_t *mono_nsec,
                        int32_t *tz_off, uint32_t *flags, char *tz_name)
{
    if (!init() || !g_page)
        return false;

    uint32_t s1, s2;
    uint64_t utc, tsc, hz, mono;
    int32_t off;
    uint32_t fl;
    char name[16];

    do {
        s1 = g_page->seq;
        __asm__ volatile("" ::: "memory");
        utc = g_page->utc_nsec;
        tsc = g_page->tsc_at_calib;
        hz = g_page->tsc_hz;
        mono = g_page->mono_nsec_at_calib;
        off = g_page->tz_offset_sec;
        fl = g_page->flags;
        memcpy(name, g_page->tz_name, sizeof(name));
        __asm__ volatile("" ::: "memory");
        s2 = g_page->seq;
    } while ((s1 & 1u) || s1 != s2);

    if (utc_nsec)
        *utc_nsec = interpolate(utc, tsc, hz);
    if (mono_nsec)
        *mono_nsec = interpolate(mono, tsc, hz);
    if (tz_off)
        *tz_off = off;
    if (flags)
        *flags = fl;
    if (tz_name) {
        memcpy(tz_name, name, 16);
        tz_name[15] = 0;
    }
    return true;
}

static int is_leap(int y)
{
    return ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
}

static void unix_to_civil(uint64_t sec, DateTime &dt)
{
    static const int mdays_n[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int64_t days = (int64_t)(sec / 86400ull);
    int tod = (int)(sec % 86400ull);
    dt.hour = tod / 3600;
    dt.minute = (tod % 3600) / 60;
    dt.second = tod % 60;
    /* 1970-01-01 = Thursday = 4 */
    dt.wday = (int)((days + 4) % 7);
    if (dt.wday < 0)
        dt.wday += 7;

    int y = 1970;
    for (;;) {
        int diy = is_leap(y) ? 366 : 365;
        if (days < diy)
            break;
        days -= diy;
        y++;
    }
    dt.year = y;
    int m = 1;
    for (; m <= 12; m++) {
        int dim = mdays_n[m - 1];
        if (m == 2 && is_leap(y))
            dim = 29;
        if (days < dim)
            break;
        days -= dim;
    }
    dt.month = m;
    dt.day = (int)days + 1;
}

static void append_ch(char *out, size_t out_sz, size_t *n, char c)
{
    if (!out || !n || *n + 1 >= out_sz)
        return;
    out[(*n)++] = c;
    out[*n] = 0;
}

static void append_str(char *out, size_t out_sz, size_t *n, const char *s)
{
    if (!s)
        return;
    while (*s)
        append_ch(out, out_sz, n, *s++);
}

static void append_2(char *out, size_t out_sz, size_t *n, int v)
{
    if (v < 0)
        v = 0;
    if (v > 99)
        v = 99;
    append_ch(out, out_sz, n, (char)('0' + v / 10));
    append_ch(out, out_sz, n, (char)('0' + v % 10));
}

static void append_4(char *out, size_t out_sz, size_t *n, int v)
{
    if (v < 0)
        v = 0;
    append_ch(out, out_sz, n, (char)('0' + (v / 1000) % 10));
    append_ch(out, out_sz, n, (char)('0' + (v / 100) % 10));
    append_ch(out, out_sz, n, (char)('0' + (v / 10) % 10));
    append_ch(out, out_sz, n, (char)('0' + v % 10));
}

static const char *wday_name(int w)
{
    static const char *names[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    if (w < 0 || w > 6)
        return "???";
    return names[w];
}

} // namespace

bool init()
{
    if (g_page && g_page->magic == TIME_PAGE_MAGIC)
        return true;
    long p = syscall0(SYS_TIME_MAP);
    if (p <= 0)
        return false;
    const time_page_t *tp = (const time_page_t *)(uintptr_t)p;
    if (!tp || tp->magic != TIME_PAGE_MAGIC)
        return false;
    g_page = tp;
    return true;
}

uint64_t unix_ns()
{
    uint64_t utc = 0;
    (void)read_fields(&utc, nullptr, nullptr, nullptr, nullptr);
    return utc;
}

uint64_t unix_sec()
{
    return unix_ns() / 1000000000ull;
}

uint64_t mono_ns()
{
    uint64_t mono = 0;
    (void)read_fields(nullptr, &mono, nullptr, nullptr, nullptr);
    return mono;
}

DateTime utc_now()
{
    DateTime dt;
    unix_to_civil(unix_sec(), dt);
    return dt;
}

DateTime local_now()
{
    DateTime dt;
    int32_t off = 0;
    uint64_t utc = 0;
    (void)read_fields(&utc, nullptr, &off, nullptr, nullptr);
    int64_t local = (int64_t)(utc / 1000000000ull) + (int64_t)off;
    if (local < 0)
        local = 0;
    unix_to_civil((uint64_t)local, dt);
    return dt;
}

int timezone_offset_sec()
{
    int32_t off = 0;
    (void)read_fields(nullptr, nullptr, &off, nullptr, nullptr);
    return (int)off;
}

const char *timezone_name()
{
    static char name[16];
    name[0] = 0;
    (void)read_fields(nullptr, nullptr, nullptr, nullptr, name);
    return name;
}

bool set_timezone(int offset_sec, const char *name)
{
    long rc = syscall2(SYS_TIME_SETTZ, (long)offset_sec, (long)name);
    return rc == 0;
}

bool hour12()
{
    uint32_t fl = 0;
    (void)read_fields(nullptr, nullptr, nullptr, &fl, nullptr);
    return (fl & TIME_FLAG_HOUR12) != 0;
}

bool set_hour12(bool enabled)
{
    uint32_t fl = 0;
    (void)read_fields(nullptr, nullptr, nullptr, &fl, nullptr);
    if (enabled)
        fl |= TIME_FLAG_HOUR12;
    else
        fl &= ~TIME_FLAG_HOUR12;
    return syscall1(SYS_TIME_SETFLAGS, (long)fl) == 0;
}

void format_clock_hm(char *out, size_t out_sz)
{
    if (!out || out_sz == 0)
        return;
    out[0] = 0;
    size_t n = 0;
    DateTime dt = local_now();
    if (hour12()) {
        int h = dt.hour % 12;
        if (h == 0)
            h = 12;
        if (h >= 10)
            append_2(out, out_sz, &n, h);
        else
            append_ch(out, out_sz, &n, (char)('0' + h));
        append_ch(out, out_sz, &n, ':');
        append_2(out, out_sz, &n, dt.minute);
        append_ch(out, out_sz, &n, ' ');
        append_str(out, out_sz, &n, dt.hour >= 12 ? "PM" : "AM");
    } else {
        append_2(out, out_sz, &n, dt.hour);
        append_ch(out, out_sz, &n, ':');
        append_2(out, out_sz, &n, dt.minute);
    }
}

void format_clock(char *out, size_t out_sz)
{
    if (!out || out_sz == 0)
        return;
    out[0] = 0;
    size_t n = 0;
    DateTime dt = local_now();
    append_str(out, out_sz, &n, wday_name(dt.wday));
    append_ch(out, out_sz, &n, ' ');
    char hm[16];
    format_clock_hm(hm, sizeof(hm));
    append_str(out, out_sz, &n, hm);
}

void format_iso_local(char *out, size_t out_sz)
{
    if (!out || out_sz == 0)
        return;
    out[0] = 0;
    size_t n = 0;
    DateTime dt = local_now();
    int off = timezone_offset_sec();
    append_4(out, out_sz, &n, dt.year);
    append_ch(out, out_sz, &n, '-');
    append_2(out, out_sz, &n, dt.month);
    append_ch(out, out_sz, &n, '-');
    append_2(out, out_sz, &n, dt.day);
    append_ch(out, out_sz, &n, 'T');
    append_2(out, out_sz, &n, dt.hour);
    append_ch(out, out_sz, &n, ':');
    append_2(out, out_sz, &n, dt.minute);
    append_ch(out, out_sz, &n, ':');
    append_2(out, out_sz, &n, dt.second);
    if (off < 0) {
        append_ch(out, out_sz, &n, '-');
        off = -off;
    } else {
        append_ch(out, out_sz, &n, '+');
    }
    append_2(out, out_sz, &n, off / 3600);
    append_ch(out, out_sz, &n, ':');
    append_2(out, out_sz, &n, (off % 3600) / 60);
}

void format_iso_utc(char *out, size_t out_sz)
{
    if (!out || out_sz == 0)
        return;
    out[0] = 0;
    size_t n = 0;
    DateTime dt = utc_now();
    append_4(out, out_sz, &n, dt.year);
    append_ch(out, out_sz, &n, '-');
    append_2(out, out_sz, &n, dt.month);
    append_ch(out, out_sz, &n, '-');
    append_2(out, out_sz, &n, dt.day);
    append_ch(out, out_sz, &n, 'T');
    append_2(out, out_sz, &n, dt.hour);
    append_ch(out, out_sz, &n, ':');
    append_2(out, out_sz, &n, dt.minute);
    append_ch(out, out_sz, &n, ':');
    append_2(out, out_sz, &n, dt.second);
    append_ch(out, out_sz, &n, 'Z');
}

bool snapshot(time_snapshot_t &out)
{
    memset(&out, 0, sizeof(out));
    return syscall1(SYS_TIME_GET, (long)&out) == 0;
}

bool parse_timezone_label(const char *label, int *offset_sec_out)
{
    if (!offset_sec_out)
        return false;
    *offset_sec_out = 0;
    if (!label || !label[0])
        return false;
    if (strcmp(label, "UTC") == 0 || strcmp(label, "GMT") == 0) {
        *offset_sec_out = 0;
        return true;
    }
    /* UTC+3 / UTC-5 / GMT+1 */
    const char *p = label;
    if (strncmp(p, "UTC", 3) == 0)
        p += 3;
    else if (strncmp(p, "GMT", 3) == 0)
        p += 3;
    else
        return false;
    if (!*p) {
        *offset_sec_out = 0;
        return true;
    }
    int sign = 1;
    if (*p == '+') {
        p++;
    } else if (*p == '-') {
        sign = -1;
        p++;
    } else {
        return false;
    }
    int hours = 0;
    if (*p < '0' || *p > '9')
        return false;
    while (*p >= '0' && *p <= '9') {
        hours = hours * 10 + (*p - '0');
        p++;
    }
    int mins = 0;
    if (*p == ':' && p[1] >= '0' && p[1] <= '9') {
        p++;
        while (*p >= '0' && *p <= '9') {
            mins = mins * 10 + (*p - '0');
            p++;
        }
    }
    if (*p != 0)
        return false;
    if (hours > 14 || mins > 59)
        return false;
    *offset_sec_out = sign * (hours * 3600 + mins * 60);
    return true;
}

} // namespace hsrc::sdk::time
