#include <kernel/types.h>

/*
 * Minimal unsigned 64-bit divide/modulo for i386 freestanding kmods.
 * Do not use 64-bit / or % here - GCC would emit calls back into these helpers.
 */

uint64_t __udivmoddi4(uint64_t n, uint64_t d, uint64_t *rem)
{
    uint64_t q = 0;
    uint64_t bit = 1;

    if (d == 0)
        return 0;

    while ((d & 0x8000000000000000ULL) == 0) {
        if (d > n)
            break;
        d <<= 1;
        bit <<= 1;
    }

    while (bit) {
        if (d <= n) {
            n -= d;
            q |= bit;
        }
        d >>= 1;
        bit >>= 1;
    }

    if (rem)
        *rem = n;
    return q;
}

uint64_t __udivdi3(uint64_t n, uint64_t d)
{
    return __udivmoddi4(n, d, NULL);
}

uint64_t __umoddi3(uint64_t n, uint64_t d)
{
    uint64_t r = 0;
    __udivmoddi4(n, d, &r);
    return r;
}

/* Signed 64-bit / — required by freestanding kmods (e.g. display gpu_cmd_fb). */
int64_t __divdi3(int64_t n, int64_t d)
{
    int neg = 0;
    uint64_t un, ud, uq;

    if (d == 0)
        return 0;
    if (n < 0) {
        un = (uint64_t)(-(n + 1)) + 1u; /* avoid INT64_MIN negate UB */
        neg = !neg;
    } else {
        un = (uint64_t)n;
    }
    if (d < 0) {
        ud = (uint64_t)(-(d + 1)) + 1u;
        neg = !neg;
    } else {
        ud = (uint64_t)d;
    }
    uq = __udivmoddi4(un, ud, NULL);
    return neg ? -(int64_t)uq : (int64_t)uq;
}
