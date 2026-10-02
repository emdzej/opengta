#include "gmath.h"
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
