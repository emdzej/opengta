#include "carinfo.h"
#include "../render/sprite.h"
#include "game.h"
#include <stdlib.h>

static uint8_t *info_copy;                  /* the converted copy of the style's car section */
static size_t info_cap;
static int info_count;                      /* 0x501570 */
static const uint8_t *info_recs[CARINFO_MAX];   /* 0x5f2ce0 (pointer 0x501574) */

int car_info_count(void) { return info_count; }
const uint8_t *car_info_record(int i) { return i >= 0 && i < info_count ? info_recs[i] : NULL; }

/* Math_FixedToFloat 0x430490: fild; fmul dword 1/65536 (exact), stored as a float */
static void fixed_to_float(uint8_t *p)
{
    int32_t v = (int32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24);
    float f = (float)((double)v * (double)(1.0f / 65536.0f));
    memcpy(p, &f, 4);
}

/* CarInfo_Setup 0x40c100 */
void car_info_setup(const Style *s)
{
    info_count = 0;
    if (!s || !s->car_info) return;
    int size = (int)s->h.car_size;
    if ((size_t)size > info_cap) {
        free(info_copy);
        info_copy = malloc((size_t)size);
        info_cap = info_copy ? (size_t)size : 0;
        if (!info_copy) game_fatal(-0x18, 0x27, size);
    }
    memcpy(info_copy, s->car_info, (size_t)size);
    /* count: the walk stops once the summed sizes reach the section size */
    int n = 0;
    for (int o = 0; o < size && o + 0xae <= size; n++) o += carinfo_s16(info_copy + o, 0xac) * 8 + 0xae;
    if (n * 4 > 0x400) game_fatal(-0x18, 0x27, n * 4 - 0x400);
    info_count = n;
    uint8_t *p = info_copy;
    for (int i = 0; i < n; i++) {
        info_recs[i] = p;
        int group;
        switch (p[0x6a]) {
        case 0: case 2: group = SPRITE_GROUP_BUS; break;
        case 1: case 4: group = SPRITE_GROUP_CAR; break;
        case 3: group = SPRITE_GROUP_BIKE; break;
        case 8: group = SPRITE_GROUP_TRAIN; break;
        case 9: group = SPRITE_GROUP_TRAM; break;
        case 0xd: group = SPRITE_GROUP_BOAT; break;
        case 0xe: group = SPRITE_GROUP_TANK; break;
        default: game_fatal(-0x23, 0x31, p[0x6a] * 0x100 + i);
        }
        int16_t spr = (int16_t)(carinfo_s16(p, 6) + sprite_group_base(group));
        p[6] = (uint8_t)spr, p[7] = (uint8_t)((uint16_t)spr >> 8);
        static const int fixed[] = { 0x7c, 0x80, 0x84, 0x88, 0x8c, 0x90, 0x94, 0x9e, 0xa2 };
        for (size_t k = 0; k < sizeof fixed / sizeof *fixed; k++) fixed_to_float(p + fixed[k]);
        if (p[0x6b] == 4) p[10] = 0x32, p[11] = 0;
        p += 0xae + carinfo_s16(p, 0xac) * 8;
    }
}
