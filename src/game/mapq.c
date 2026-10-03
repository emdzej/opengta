/* Map queries of the game on the loaded level (mapq.h). */
#include "mapq.h"
#include "../map.h"
#include "game.h"

/* Map_GetLidBelow 0x4387b0 */
int map_get_lid_below(int32_t x, int32_t y, int32_t z)
{
    const Map *m = g_game.map;
    const int16_t *col = map_column(m, x >> 22 & 0xff, y >> 22 & 0xff);
    int l = (z >> 22) + 1;
    if (col[0] > l) return 0;
    return m->blocks[col[l - col[0] + 1]].lid;
}

/* Map_TestBlockAttr 0x44b310 */
int map_test_block_attr(int what, int bx, int by, int bz)
{
    static int16_t cx = -1, cy = -1, cz = -1;   /* 0x4b1de4.. */
    static uint32_t t;                          /* 0x6b3ea8 */
    bx = (int16_t)bx, by = (int16_t)by, bz = (int16_t)bz;
    if (bx < 0 || bx > 0xff || by < 0 || by > 0xff || bz < 0 || bz > 5) return 0;
    if (bx != cx || by != cy || bz != cz) {
        t = map_get_type_map(g_game.map, bx, by, bz);
        cx = (int16_t)bx, cy = (int16_t)by, cz = (int16_t)bz;
    }
    int k;
    switch ((int16_t)what) {
    case 1: return (t & 0x800000) != 0;
    case 2: return (t >> 16 & 7) == 1;
    case 3: k = t >> 4 & 7; return k == 2 || k == 6 || k == 7;
    case 4: return (int)(t & 0xffffff0f);
    case 5: return (int)(t >> 8 & 0xffff3f);
    case 6: return (int)(t >> 16 & 7);
    case 7: k = t >> 16 & 7; return k == 5 || k == 4 ? k : 0;
    case 8: return (t >> 16 & 7) == 2;
    case 9: return (t >> 4 & 7) == 3;
    }
    return 0;
}
