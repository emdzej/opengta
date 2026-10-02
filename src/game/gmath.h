/* Game maths tables (Math_InitTables 0x430400). Angles are 0..1023 for a full turn; values are 16.16
   fixed point. The tables are bss in the exe, computed at startup with the x87 (fsin / fptan), so they
   are computed here the same way rather than loaded. */
#pragma once
#include <stdint.h>

/* 0x511e28: sin, 0x500 entries (a full turn plus a quarter), so cos(a) = sin[a + 256] (0x512228). */
extern int32_t g_sin[0x500];
/* 0x511a28: tan for the first quarter turn, 256 entries. */
extern int32_t g_tan[0x100];

void math_init_tables(void);                 /* Math_InitTables 0x430400 */
static inline int32_t math_sin(int a) { return g_sin[a]; }
static inline int32_t math_cos(int a) { return g_sin[a + 256]; }
