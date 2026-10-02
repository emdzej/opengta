/* Game maths tables (Math_InitTables 0x430400). Angles are 0..1023 for a full turn; values are 16.16
   fixed point. The tables are bss in the exe, computed at startup with the x87 (fsin / fptan), so they
   are computed here the same way rather than loaded. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* 0x511e28: sin, 0x500 entries (a full turn plus a quarter), so cos(a) = sin[a + 256] (0x512228). */
extern int32_t g_sin[0x500];
/* 0x511a28: tan for the first quarter turn, 256 entries. */
extern int32_t g_tan[0x100];

void math_init_tables(void);                 /* Math_InitTables 0x430400 */
static inline int32_t math_sin(int a) { return g_sin[a]; }
static inline int32_t math_cos(int a) { return g_sin[a + 256]; }

/* The game RNG (Math_Rand 0x489abe): a lagged additive generator over five 15-bit words at 0x4b3810,
   s0 = (s1 + s2 + s3) & 0x7fff, then the words shift up (s4 = s3, ... s1 = s0); returns s0.
   Math_Random 0x434160 also keeps the value in 0x523fe8. thunk_Math_RandomReset 0x434170 reloads the
   five words from the seed copy at 0x4b3824 (read from the exe), which Game_Init does at every level
   start, so a level's random sequence is the same every time. */
extern int32_t g_rng[5];                     /* 0x4b3810 */
extern int32_t g_rng_last;                   /* 0x523fe8 */
bool math_random_reset(void);                /* thunk_Math_RandomReset 0x434170; false without the exe */
int32_t math_rand(void);                     /* Math_Rand 0x489abe */
int32_t math_random(void);                   /* Math_Random 0x434160 */
/* MSVC rand 0x49cb27 (seed 0x4b8e9c = 1, srand is never called): s = s * 214013 + 2531011,
   returns (s >> 16) & 0x7fff. Used by the sound code and skid marks. */
extern uint32_t g_crt_rand_seed;
int32_t crt_rand(void);

/* Math_Atan2 0x4899ee: the angle 0..1023 of (dx, dy) (0 = +y, 0x100 = +x), both 16.16. Hand-written
   assembly: the integer parts decide the axes; otherwise |dy / (dx >> 16)| (a 16.16 tangent) is looked
   up in the tan table (Math_AtanSearch 0x48997c, a binary search) and put in its quadrant. */
int math_atan2(int32_t dy, int32_t dx);
int math_atan_search(int32_t v);             /* Math_AtanSearch 0x48997c over g_tan (pointer 0x4b0a7c) */
