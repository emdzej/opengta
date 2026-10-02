#include "route.h"
#include "game.h"
#include "player.h"
#include <string.h>

uint8_t g_path_slots[PATH_SLOTS][PATH_SLOT_SIZE] = { 0 };
RoadblockSet g_roadblock_sets[ROADBLOCK_SETS];
BlockXYZ g_roadblock_verts[ROADBLOCK_VERTS_MAX];
int g_route_fe, g_route_ff, g_route_paths, g_roadblock_nverts;
static const uint8_t *route_end;             /* 0x7705ec: the location data follows the routes */
const NavZone *g_nav;
int g_nav_count;
const BlockXYZ *g_locations;
int g_police_stations, g_hospitals, g_fire_stations;
BlockXYZ g_hospital_block;
BlockXYZ g_fire_engine_bases[4];

/* A byte of a path slot by triple index: the terminator of an 85-node route lands in the next slot,
   as in the original (the slots are one array). */
static uint8_t *path_byte(int slot, int triple, int k)
{
    size_t off = (size_t)slot * PATH_SLOT_SIZE + (size_t)triple * 3 + (size_t)k;
    if (off >= sizeof g_path_slots) game_fatal(-0xac, 0x131, slot);   /* (past the array in the original) */
    return &g_path_slots[0][0] + off;
}

/* Route_LoadCmp 0x471970 */
void route_load_cmp(const uint8_t *data, int size)
{
    g_roadblock_nverts = 0;
    g_route_paths = 0;
    g_route_ff = 0;
    g_route_fe = 0;
    if (size < 1) {
        route_end = data + size;
        return;
    }
    int off = 0;
    for (;;) {
        int n = data[off], type = data[off + 1];
        if (g_route_fe + g_route_ff > 99) game_fatal(-0xac, 0x131, g_route_fe + g_route_ff);
        if (type == 0xfe || type == 0xff) {
            if (type == 0xfe) g_route_fe++;
            else g_route_ff++;
            int slot = g_route_paths + PATH_ROUTE_FIRST;
            for (int i = 0; i < n; i++)
                for (int k = 0; k < 3; k++) *path_byte(slot, i, k) = data[off + 2 + 3 * i + k];
            g_route_paths++;
            for (int k = 0; k < 3; k++) *path_byte(slot, n, k) = 0;
        } else {
            if (g_roadblock_nverts + n > ROADBLOCK_VERTS_MAX) game_fatal(-0x36, 0x2e, 0);
            g_roadblock_sets[type].v = &g_roadblock_verts[g_roadblock_nverts];
            g_roadblock_sets[type].n = (uint8_t)n;
            memcpy(&g_roadblock_verts[g_roadblock_nverts], data + off + 2, (size_t)n * 3);
            g_roadblock_nverts += n;
        }
        off += n * 3 + 2;
        if (off >= size) {
            route_end = data + size;
            return;
        }
    }
}

/* Route_GetCounts 0x471b90 */
void route_get_counts(int *fe, int *ff, const uint8_t **end)
{
    *fe = g_route_fe;
    *ff = g_route_ff;
    *end = route_end;
}

void route_init_locations(void)
{
    int fe, ff;
    const uint8_t *loc;
    route_get_counts(&fe, &ff, &loc);
    g_locations = (const BlockXYZ *)loc;
    g_player_respawn_block = (g_player_respawn_block & 0xff000000u) | (uint32_t)loc[0] << 8 | loc[1] | (uint32_t)loc[2] << 16;
    g_fire_stations = g_police_stations = g_hospitals = 0;
    for (int i = 0; i < LOCATION_N; i++) {
        if (loc[0x12 + 3 * i]) g_hospitals++;
        if (loc[3 * i]) g_police_stations++;
        if (loc[0x48 + 3 * i]) g_fire_stations++;
    }
    g_hospital_block = (BlockXYZ){ loc[0x12], loc[0x13], loc[0x14] };
    for (int i = 0; i < 4; i++) g_fire_engine_bases[i] = (BlockXYZ){ loc[0x48 + 3 * i], loc[0x49 + 3 * i], loc[0x4a + 3 * i] };
}

/* Area_SetNavData 0x44b590 */
void area_set_nav_data(const uint8_t *data, int size)
{
    g_nav = (const NavZone *)data;
    g_nav_count = size / NAV_SIZE;
}
