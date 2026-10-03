/* Area names (area module 0x44b4a0-0x44b7a0): the CMP's nav zones named through the FXT, with a
   compass prefix for the part of the zone a block is in. The HUD shows them in the street sign
   (zone text type 2). Area_SetNavData 0x44b590 is route.c's; Area_GetSample 0x44b7b0 gives the police
   radio its zone sample and compass part. */
#include "hud.h"
#include "hud_internal.h"
#include "../exe.h"
#include "../game/game.h"
#include "../game/route.h"
#include <stdio.h>
#include <string.h>

enum { AREA_DIRS = 10, AREA_DIR_LEN = 0x16 };
static char dir_prefix[AREA_DIRS][AREA_DIR_LEN];   /* 0x6b3eb8: [0] stays empty (no prefix) */
static bool dir_loaded;                             /* 0x6b3f94 */

/* Area_LoadDirPrefixes 0x44b510: the FXT texts of the 9 keys at 0x4b1e00 (s, n, e, w, c, se, ne, sw,
   nw) followed by a space ("%s " 0x4b1e58) into slots 1..9; a text over 20 bytes is fatal -0x87.
   WinMain calls it once at start-up; the port calls it from HUD_Init (it does nothing the second time). */
void area_load_dir_prefixes(void)
{
    if (dir_loaded) return;
    for (int i = 0; i < AREA_DIRS - 1; i++) {
        const char *t = hud_text(exe_str(exe_u32(0x4b1e00 + 4u * (uint32_t)i)));
        if (strlen(t) > 0x14) hud_fatal(-0x87, 0x8f, 0);
        snprintf(dir_prefix[i + 1], AREA_DIR_LEN, exe_str(0x4b1e58), t);
    }
    dir_loaded = true;
}

/* Area_LocalizeNames 0x44b4a0: each zone's name becomes the FXT text "%03darea%03d" (style, sample).
   The original strcpys over the 30-byte name (a longer text would run into the next record); the port
   cuts it at 29 bytes. */
void area_localize_names(void)
{
    if (!g_game.map) return;
    NavZone *z = (NavZone *)g_game.map->nav;   /* what g_nav points at */
    for (int i = 0; i < g_nav_count; i++) {
        char key[16];
        snprintf(key, sizeof key, exe_str(0x4b1e48), style_requested(), z[i].sample);
        snprintf(z[i].name, sizeof z[i].name, "%s", hud_text(key));
    }
}

/* Area_SubDirection 0x44b6d0: which ninth of a w x h zone (dx, dy) is in. Rows give 2 (top), 3
   (middle), 1 (bottom); columns 8 (left), 12 (middle), 4 (right). A side up to 6 blocks isn't divided
   (0), up to 12 only halved (no middle); the thirds are w / 3 and 2w / 3, rounded down. */
int area_sub_direction(uint8_t dx, uint8_t dy, uint8_t w, uint8_t h)
{
    int r, c;
    if (h <= 6) r = 0;
    else if (h <= 12) r = (dy < (h >> 1)) + 1;
    else if (dy < h / 3) r = 2;
    else r = (dy < (2 * h) / 3) * 2 + 1;
    if (w <= 6) c = 0;
    else if (w <= 12) c = (dx < (w >> 1)) * 4 + 4;
    else if (dx < w / 3) c = 8;
    else c = (dx < (2 * w) / 3) * 8 + 4;
    return (uint8_t)(c + r);
}

/* Area_GetName 0x44b5c0: the first zone containing the block, except zones with sample 1 in style 1
   and sample 11 in style 3; "%s%s" (0x4b1e5c) of the direction prefix (table 0x4b1dec maps the
   sub-direction to it) and the name. */
int area_get_name(uint8_t x, uint8_t y, char *out)
{
    int style = style_requested();   /* Style_GetNumber 0x47cec0 */
    const uint8_t *map = exe_data(0x4b1dec, 16);
    for (int i = 0; i < g_nav_count; i++) {
        const NavZone *z = &g_nav[i];
        if (z->x > x || z->y > y || x >= z->x + z->w || y >= z->y + z->h) continue;
        if ((style == 1 && z->sample == 1) || (style == 3 && z->sample == 0xb)) continue;
        int d = area_sub_direction((uint8_t)(x - z->x), (uint8_t)(y - z->y), z->w, z->h);
        int p = map ? map[d & 0xf] : 0;
        if (p >= AREA_DIRS) p = 0;
        snprintf(out, 64, exe_str(0x4b1e5c), dir_prefix[p], z->name);
        return i * 0x100 + p;
    }
    snprintf(out, 64, "%s", exe_str(0x4b1e64));   /* "unknown area" */
    return -1;
}

/* Area_GetSample 0x44b7b0: the first nav zone with a sample (non-zero) containing block (x, y), with
   the exceptions of Area_GetName: its sample and the compass part of the zone the block is in (the
   table 0x4b1dec); 0, 0 outside every zone. */
void area_get_sample(uint8_t x, uint8_t y, uint8_t *area, uint8_t *dir)
{
    int style = style_requested();
    const uint8_t *map = exe_data(0x4b1dec, 16);
    for (int i = 0; i < g_nav_count; i++) {
        const NavZone *z = &g_nav[i];
        if (z->sample == 0 || x < z->x || y < z->y || x >= z->x + z->w || y >= z->y + z->h) continue;
        if ((style == 1 && z->sample == 1) || (style == 3 && z->sample == 0xb)) continue;
        int d = area_sub_direction((uint8_t)(x - z->x), (uint8_t)(y - z->y), z->w, z->h);
        *dir = map ? map[d & 0xf] : 0;
        *area = z->sample;
        return;
    }
    *dir = 0;
    *area = 0;
}
