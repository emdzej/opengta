#include "gmath.h"
#include "../exe.h"
#include <math.h>

int32_t g_sin[0x500];
int32_t g_tan[0x100];

/* Math_InitTables 0x430400: entry i = ftol(f(i * (1/1024) * 6.283185308) * 65536), f = fsin / fptan.
   The constants are the exe's doubles at 0x4a8b70 (1/1024), 0x4a8b68 (6.283185308, slightly more than
   2 pi) and 0x4a8b60 (65536); __ftol truncates. MSVC runs the x87 at 53-bit precision, which C doubles
   match. */
void math_init_tables(void)
{
    for (int i = 0; i < 0x500; i++) g_sin[i] = (int32_t)(sin((double)i * 0.0009765625 * 6.283185308) * 65536.0);
    for (int i = 0; i < 0x100; i++) g_tan[i] = (int32_t)(tan((double)i * 0.0009765625 * 6.283185308) * 65536.0);
}

/* ---- random numbers ---- */

int32_t g_rng[5];
int32_t g_rng_last;
uint32_t g_crt_rand_seed = 1;

/* thunk_Math_RandomReset 0x434170: copies the five seed words 0x4b3824..0x4b3834 over the state. */
bool math_random_reset(void)
{
    const uint8_t *p = exe_data(0x4b3824, 20);
    if (!p) return false;
    for (int i = 0; i < 5; i++) g_rng[i] = (int32_t)(p[4 * i] | p[4 * i + 1] << 8 | p[4 * i + 2] << 16 | (uint32_t)p[4 * i + 3] << 24);
    return true;
}

/* Math_Rand 0x489abe */
int32_t math_rand(void)
{
    g_rng[0] = (g_rng[1] + g_rng[2] + g_rng[3]) & 0x7fff;
    for (int i = 4; i > 0; i--) g_rng[i] = g_rng[i - 1];
    return g_rng[0];
}

/* Math_Random 0x434160 */
int32_t math_random(void)
{
    return g_rng_last = math_rand();
}

/* MSVC rand 0x49cb27 */
int32_t crt_rand(void)
{
    g_crt_rand_seed = g_crt_rand_seed * 0x343fd + 0x269ec3;
    return (int32_t)(g_crt_rand_seed >> 16 & 0x7fff);
}

/* Math_AtanSearch 0x48997c: binary search over the 256 tangents with unsigned bounds; the index where
   the search stopped, or 0 below tan[1] / 0xff above tan[0xff] at the ends. */
int math_atan_search(int32_t v)
{
    uint32_t lo = 0, hi = 0xff, mid = 0;
    do {
        mid = (lo + hi) >> 1;
        if (mid > 0xff) return (int)lo;   /* (a negative v walks off the table in the original) */
        int32_t t = g_tan[mid];
        if (v < t) hi = mid - 1;
        else if (v == t) return (int)mid;
        else lo = mid + 1;
    } while (lo <= hi);
    if (mid == 1 && v < g_tan[1]) return 0;
    if (mid == 0xff && (uint32_t)v > (uint32_t)g_tan[0xff]) return 0xff;
    return (int)lo;
}

/* Math_Atan2 0x4899ee */
int math_atan2(int32_t dy, int32_t dx)
{
    int32_t iy = dy >> 16, ix = dx >> 16;
    if (ix == 0) return dy >= 0 ? 0 : 0x200;
    if (iy == 0) return ix >= 0 ? 0x100 : 0x300;
    int32_t q = (ix == -1 && dy == INT32_MIN) ? dy : dy / ix;   /* (IDIV: INT_MIN / -1 faults in the original) */
    if (q < 0) q = -q;
    int t = math_atan_search(q);
    if (iy >= 0) {
        if (ix >= 0) return 0x100 - t;
        return t == 0x100 ? 0 : t + 0x300;
    }
    return ix >= 0 ? t + 0x100 : 0x300 - t;
}
