/* The railway tracer (rail.h, docs/trains.md): Rail_Init 0x47fa90, Rail_TraceTrack 0x47fbe0 and the
   getters the train code uses (0x47e600, 0x47e610, 0x480bc0-0x480e10). */
#include "rail.h"
#include "game.h"
#include "lights.h"
#include "mapq.h"
#include <string.h>

RailState g_rail;

#define L g_lights

int rail_station_count(void) { return L.rail_n46b; }
const uint8_t *rail_station(int i) { return L.rail_77d180[i]; }
int rail_crossing_count(void) { return L.rail_n46a; }
const uint8_t *rail_crossing(int i) { return L.rail_77cf58[i]; }

/* The neighbours a railway block is probed for, per direction group (1-3 +x, 4-6 -x, 7-9 -y, 10-12
   +y), in the order of the original's calls: a later hit overrides an earlier one, so the block
   straight ahead wins, then ahead one layer down / up, then the sides. Each gives the next direction
   and the curve a bogie takes through the block (Train_BogieNextPiece 0x46c5d0 probes the same way).
   When nothing answers, the direction stays the reverse one (the dead end both test for). */
const RailProbe rail_probes[4][9] = {
    { { 0, 1, 1, 11, 1 }, { 0, -1, 1, 8, 2 }, { 0, 1, -1, 10, 1 }, { 0, -1, -1, 7, 2 }, { 0, -1, 0, 9, 2 },
      { 0, 1, 0, 12, 1 }, { 1, 0, -1, 1, 0 }, { 1, 0, 1, 2, 0 }, { 1, 0, 0, 3, 0 } },
    { { 0, -1, 1, 8, 1 }, { 0, 1, 1, 11, 2 }, { 0, -1, -1, 7, 1 }, { 0, 1, -1, 10, 2 }, { 0, 1, 0, 12, 2 },
      { 0, -1, 0, 9, 1 }, { -1, 0, -1, 4, 0 }, { -1, 0, 1, 5, 0 }, { -1, 0, 0, 6, 0 } },
    { { -1, 0, 1, 5, 2 }, { 1, 0, 1, 2, 1 }, { -1, 0, -1, 4, 2 }, { 1, 0, -1, 1, 1 }, { 1, 0, 0, 3, 1 },
      { -1, 0, 0, 6, 2 }, { 0, -1, -1, 7, 0 }, { 0, -1, 1, 8, 0 }, { 0, -1, 0, 9, 0 } },
    { { -1, 0, 1, 5, 1 }, { 1, 0, 1, 2, 2 }, { -1, 0, -1, 4, 1 }, { 1, 0, -1, 1, 2 }, { 1, 0, 0, 3, 2 },
      { -1, 0, 0, 6, 1 }, { 0, 1, -1, 10, 0 }, { 0, 1, 1, 11, 0 }, { 0, 1, 0, 12, 0 } },
};
const uint8_t rail_reverse_dir[4] = { 6, 3, 12, 9 };

/* The block step of a direction (Rail_TraceTrack's second switch, Train_BogieStep 0x46c460). */
void rail_step(int dir, uint8_t *x, uint8_t *y, uint8_t *z)
{
    switch (dir) {
    case 1: ++*x, --*z; break;
    case 2: ++*x, ++*z; break;
    case 3: ++*x; break;
    case 4: --*x, --*z; break;
    case 5: --*x, ++*z; break;
    case 6: --*x; break;
    case 7: --*y, --*z; break;
    case 8: --*y, ++*z; break;
    case 9: --*y; break;
    case 10: ++*y, --*z; break;
    case 11: ++*y, ++*z; break;
    case 12: ++*y; break;
    }
}

/* Rail_TraceTrack 0x47fbe0: from (x, y, z), starting in direction 3, records every block in the
   visited list and steps on until the start block comes round again (0x14) or the railway ends
   (0x1e). After each step the block's ext field decides:
   - 2, level crossing: three states. The first such block opens a record (block only), the second
     completes it with the first light of the junction crossing at its (x, y) (Lights_FindRailCrossing's
     table; 0 if none) and counts it, the third opens and counts a record that takes the previous
     record's light, and starts over;
   - 3, switch: the same three states over the switch cells, where the second looks the block up among
     the unique switches (adding it, state 0, if new) and stores that index; the third record keeps the
     index of the last lookup;
   - 6 / 7, station: a record {x, y, z, loops so far, direction, 0, track id}; 7 also adds a train start
     (at most 6) with the station's index.
   A closed loop without a station is fatal (-0x72); the original then returns 0x22. */
int rail_trace_track(int x0, int y0, int z0)
{
    int result = RAIL_OK;
    uint8_t dir = 3;
    uint8_t fx = 0xff, fy = 0xff, fz = 0xff;    /* the first block (0xff: none yet) */
    bool have_first = false, station = false, spawn = false;
    uint8_t cross_state = 1, switch_state = 1;
    uint8_t sw_index = 0;                       /* local 0x13 */
    uint8_t x = (uint8_t)x0, y = (uint8_t)y0, z = (uint8_t)z0;
    for (;;) {
        if (fx == x && fy == y && fz == z) break;
        if (!have_first) have_first = true, fx = x, fy = y, fz = z;
        if (g_rail.nvisited >= RAIL_VISITED_MAX) game_fatal(-0x86, 0x8e, 0);
        uint8_t *v = g_rail.visited[g_rail.nvisited++];
        v[0] = x, v[1] = y, v[2] = z;
        int g = (dir - 1) / 3;
        const RailProbe *p = rail_probes[g];
        uint8_t rev = rail_reverse_dir[g];
        dir = rev;
        for (int k = 0; k < 9; k++)
            if (map_test_block_attr(1, x + p[k].dx, y + p[k].dy, z + p[k].dz)) dir = p[k].dir;
        if (dir == rev) { result = RAIL_DEAD_END; break; }
        rail_step(dir, &x, &y, &z);
        switch (map_test_block_attr(6, x, y, z) & 0xff) {
        case 2: {
            uint8_t *c = L.rail_77cf58[L.rail_n46a % RAIL_TABLE_MAX];
            if (cross_state == 1) {
                cross_state = 2;
                c[0] = x, c[1] = y, c[2] = z;
            } else if (cross_state == 2) {
                uint8_t light = 0;
                for (int i = 0; i < L.ncross && i < LIGHTS_RAIL_MAX; i++)
                    if (L.cross[i][0] == x && L.cross[i][1] == y) { light = L.cross[i][3]; break; }
                c[3] = light;
                L.rail_n46a++;
                cross_state = 3;
            } else {
                cross_state = 1;
                c[0] = x, c[1] = y, c[2] = z;
                /* (the previous record's light: for record 0 the byte before the table) */
                c[3] = L.rail_n46a ? L.rail_77cf58[(L.rail_n46a - 1) % RAIL_TABLE_MAX][3] : 0;
                L.rail_n46a++;
            }
            break;
        }
        case 3: {
            RailSwitchCell *c = &g_rail.switch_cells[g_rail.nswitch_cells % RAIL_TABLE_MAX];
            if (switch_state == 1) {
                switch_state = 2;
                c->x = x, c->y = y, c->z = z;
            } else if (switch_state == 2) {
                int found = -1;
                for (int i = 0; i < g_rail.nswitches; i++)
                    if (g_rail.switches[i].x == x && g_rail.switches[i].y == y) { found = i; break; }
                if (found < 0) {
                    if (g_rail.nswitches >= RAIL_TABLE_MAX) game_fatal(-0x86, 0x8e, 1);   /* (port: the table's room) */
                    found = g_rail.nswitches++;
                    g_rail.switches[found] = (RailSwitch){ x, y, z, 0 };
                }
                sw_index = (uint8_t)found;
                c->sw = sw_index;
                g_rail.nswitch_cells++;
                switch_state = 3;
            } else {
                switch_state = 1;
                c->x = x, c->y = y, c->z = z, c->sw = sw_index;
                g_rail.nswitch_cells++;
            }
            break;
        }
        case 7:
            spawn = true;
            /* fall through */
        case 6: {
            if (L.rail_n46b >= RAIL_TABLE_MAX) game_fatal(-0x86, 0x8e, 2);   /* (port: the table's room) */
            uint8_t *s = L.rail_77d180[L.rail_n46b];
            s[RAIL_ST_X] = x, s[RAIL_ST_Y] = y, s[RAIL_ST_Z] = z;
            s[RAIL_ST_TRACK] = g_rail.ntracks;
            s[RAIL_ST_DIR] = dir;
            s[RAIL_ST_FLAG] = 0;
            s[RAIL_ST_TRACK_ID] = g_rail.track_id;
            if (spawn) {
                if (g_rail.nspawns >= RAIL_SPAWNS_MAX) game_fatal(-0xa4, 0x8e, 0);
                RailInfo *in = &g_rail.info;
                int n = g_rail.nspawns;
                spawn = false;
                in->dir[n] = dir, in->x[n] = x, in->y[n] = y, in->z[n] = z;
                in->station[n] = L.rail_n46b;
                g_rail.nspawns++;
            }
            station = true;
            L.rail_n46b++;
            break;
        }
        default: break;   /* 0, 1 and the curve marks 4 / 5 */
        }
    }
    if (!station && result == RAIL_OK) {
        game_fatal(-0x72, 0, 0);   /* "No Station found on Track" */
        result = RAIL_NO_STATION;
    }
    return result;
}

/* Rail_Init 0x47fa90: rows outer, then columns, then layers: a railway block not in the visited list
   starts a trace (and ends the column's layers). A loop counts a track; anything else stops the
   scan. The summary's train count is the starts found when the last trace closed its loop, else 0
   (also 0 when the map has no railway: the last result is then the initial 0x1e). The station and
   crossing counts are not reset here: Lights_Init clears them. */
int rail_init(void)
{
    int last = RAIL_DEAD_END;
    g_rail.nvisited = 0;
    g_rail.ntracks = g_rail.nspawns = 0;
    g_rail.nswitches = g_rail.nswitch_cells = 0;
    g_rail.track_id = 0;
    for (int y = 0; y < 0x100; y++)
        for (int x = 0; x < 0x100; x++)
            for (int z = 0; z < 6; z++) {
                if (!map_test_block_attr(1, x, y, z)) continue;
                bool seen = false;
                for (int i = 0; i < g_rail.nvisited && !seen; i++) {
                    const uint8_t *v = g_rail.visited[i];
                    seen = v[0] == x && v[1] == y && v[2] == z;
                }
                if (seen) continue;
                last = rail_trace_track(x, y, z);
                if (last != RAIL_OK) {
                    g_rail.info.count = 0;
                    return last;
                }
                g_rail.ntracks++;
                g_rail.track_id++;
                break;   /* the rest of this column's layers */
            }
    g_rail.info.count = last == RAIL_OK ? g_rail.nspawns : 0;
    return last;
}

/* Rail_GetInfo 0x47e600 */
const RailInfo *rail_get_info(void) { return &g_rail.info; }

/* Rail_ToggleCrossing 0x47e610: the last crossing record at (x, y) switches its light and the next one
   between phase 0 and 3 (other phases stay) and redraws both. 0x14 if there is one, else 0x21. */
int rail_toggle_crossing(int bx, int by)
{
    uint8_t x = (uint8_t)bx, y = (uint8_t)by;
    int found = -1;
    for (int i = 0; i < L.rail_n46a && i < RAIL_TABLE_MAX; i++)
        if (L.rail_77cf58[i][0] == x && L.rail_77cf58[i][1] == y) found = i;
    if (found < 0) return 0x21;
    int li = L.rail_77cf58[found][3];
    if (li + 1 >= LIGHTS_MAX + 2) return 0x14;   /* (port: past the light table) */
    Light *a = &L.lights[li], *b = &L.lights[li + 1];
    if (a->phase == 0) a->phase = 3, b->phase = 3;
    else if (a->phase == 3) a->phase = 0, b->phase = 0;
    lights_update_sprite(li);
    lights_update_sprite(li + 1);
    return 0x14;
}

/* Rail_FindStation 0x480bc0: whether a station record is at (x, y) (in AL; the decompiler shows no
   result, Train_Update tests it) */
bool rail_find_station(int bx, int by)
{
    for (int i = 0; i < L.rail_n46b && i < RAIL_TABLE_MAX; i++)
        if (L.rail_77d180[i][RAIL_ST_X] == (uint8_t)bx && L.rail_77d180[i][RAIL_ST_Y] == (uint8_t)by) return true;
    return false;
}

/* Rail_FindSwitch 0x480c20: the first switch cell at (x, y), 0xff if none */
int rail_find_switch(int bx, int by)
{
    for (int i = 0; i < g_rail.nswitch_cells && i < RAIL_TABLE_MAX; i++)
        if (g_rail.switch_cells[i].x == (uint8_t)bx && g_rail.switch_cells[i].y == (uint8_t)by) return i;
    return 0xff;
}

/* Rail_IsSwitchSet 0x480c90: the unique switch of cell i is held */
bool rail_is_switch_set(int i)
{
    return g_rail.switches[g_rail.switch_cells[(uint8_t)i % RAIL_TABLE_MAX].sw % RAIL_TABLE_MAX].state == 1;
}

/* Rail_ToggleSwitch 0x480cc0: the unique switch of cell i flips between 0 and 1 (if it exists) */
bool rail_toggle_switch(int i)
{
    uint8_t s = g_rail.switch_cells[(uint8_t)i % RAIL_TABLE_MAX].sw;
    if (s >= g_rail.nswitches) return false;
    RailSwitch *w = &g_rail.switches[s];
    if (w->state == 0) w->state = 1;
    else if (w->state == 1) w->state = 0;
    return true;
}

/* Rail_GetStationFlag 0x480d10: station i is occupied (past the count the original returns what AL
   held: 0 here) */
int rail_get_station_flag(int i)
{
    if ((uint8_t)i >= L.rail_n46b) return 0;
    return L.rail_77d180[(uint8_t)i][RAIL_ST_FLAG];
}

/* Rail_ToggleStation 0x480d40: station i's occupied flag flips between 0 and 1 */
bool rail_toggle_station(int i)
{
    if ((uint8_t)i >= L.rail_n46b) return false;
    uint8_t *f = &L.rail_77d180[(uint8_t)i][RAIL_ST_FLAG];
    if (*f == 0) *f = 1;
    else if (*f == 1) *f = 0;
    return true;
}

/* Rail_NextStation 0x480d90: the next station on the same track (the stations of a track are
   consecutive records), or from the last one back to the track's first. The test reads record i + 1
   before checking that it exists. */
int rail_next_station(int i)
{
    uint8_t s = (uint8_t)i;
    if (L.rail_77d180[s][RAIL_ST_TRACK_ID] == L.rail_77d180[(s + 1) % RAIL_TABLE_MAX][RAIL_ST_TRACK_ID] &&
        s + 1 != L.rail_n46b)
        return (uint8_t)(s + 1);
    while (s != 0 && L.rail_77d180[s][RAIL_ST_TRACK_ID] == L.rail_77d180[s - 1][RAIL_ST_TRACK_ID]) s--;
    return s;
}
