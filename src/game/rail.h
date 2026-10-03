/* The railway tracer (the rail part of 0x47dcf0-0x480e10: Rail_*). Lights_Init 0x47dcf0 ends in
   Rail_Init, which follows every railway (blocks with bit 7 of the type map ext byte, Map_TestBlockAttr
   1) as a closed loop and records what lies on it, from the ext field (bits 16-18 of the type map):
   2 level crossings, 3 switches (single-track sections), 6 stations, 7 stations where a train starts;
   4 / 5 mark the two ends of a curve (the train code reads them, the tracer steps over them).

   The tables the traffic lights read too (stations 0x77d180, crossings 0x77cf58 and their counts
   0x77d46b / 0x77d46a, and the junction crossings 0x77d2b8 that Lights_FindRailCrossing fills) live in
   lights.c's LightsState; the rest is here. See docs/trains.md. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum {
    RAIL_VISITED_MAX = 0x370,   /* 0x775588: 880 blocks (fatal -0x86 past them) */
    RAIL_TABLE_MAX = 44,        /* the room of each static table up to the next global */
    RAIL_SPAWNS = 20,           /* rows of the summary block (6 used: fatal -0xa4 at the 7th) */
    RAIL_SPAWNS_MAX = 6,
    RAIL_OK = 0x14,             /* traced a closed loop */
    RAIL_DEAD_END = 0x1e,       /* the railway ends (Rail_Init then stops: no trains) */
    RAIL_NO_STATION = 0x22,     /* a loop without a station (after fatal -0x72) */
};

/* Directions the tracer and the bogies step in (the tracer's local 0x1c, a bogie's +0x61): 1-3 +x,
   4-6 -x, 7-9 -y, 10-12 +y, each as (up a layer, down a layer, level). z = 0 is the top layer, so
   "up" is z - 1. */

/* A neighbour probe: offset, the direction it gives and the curve a bogie takes (0 straight, 1 / 2 a
   corner). rail_probes[(dir - 1) / 3] in the original's call order; rail_reverse_dir[] is what stays
   when no probe answers (a dead end). */
typedef struct { int8_t dx, dy, dz; uint8_t dir, curve; } RailProbe;
extern const RailProbe rail_probes[4][9];
extern const uint8_t rail_reverse_dir[4];
void rail_step(int dir, uint8_t *x, uint8_t *y, uint8_t *z);   /* the block step of a direction */

/* A unique switch (0x77d010, 4 bytes): its block and whether a train holds it. */
typedef struct { uint8_t x, y, z, state; } RailSwitch;
/* A switch cell met on a track (0x77d0c8, 4 bytes): entry and exit records alternate; sw is the
   unique switch. */
typedef struct { uint8_t x, y, z, sw; } RailSwitchCell;

/* The summary block 0x77d368 (Rail_GetInfo): the number of trains, then five arrays of 20: the
   direction the track runs at the start station, its block and the station's index. */
typedef struct {
    uint8_t count;              /* 0x77d368 */
    uint8_t dir[RAIL_SPAWNS];   /* 0x77d369 */
    uint8_t x[RAIL_SPAWNS];     /* 0x77d37d */
    uint8_t y[RAIL_SPAWNS];     /* 0x77d391 */
    uint8_t z[RAIL_SPAWNS];     /* 0x77d3a5 */
    uint8_t station[RAIL_SPAWNS];   /* 0x77d3b9 */
} RailInfo;
_Static_assert(sizeof(RailInfo) == 0x65, "rail summary layout");

typedef struct {
    uint8_t visited[RAIL_VISITED_MAX][3];   /* 0x775588 {x, y, z} */
    uint16_t nvisited;          /* 0x77d468 */
    uint8_t ntracks;            /* 0x77d46c loops traced */
    uint8_t nspawns;            /* 0x77d46d start stations (copied to info.count at the end) */
    uint8_t nswitches;          /* 0x77d46e unique switches */
    uint8_t nswitch_cells;      /* 0x77d46f */
    uint8_t track_id;           /* 0x77d471 (counts with 0x77d46c) */
    RailSwitch switches[RAIL_TABLE_MAX];         /* 0x77d010 */
    RailSwitchCell switch_cells[RAIL_TABLE_MAX]; /* 0x77d0c8 */
    RailInfo info;              /* 0x77d368 (count) .. 0x77d3cc */
} RailState;
extern RailState g_rail;

/* A station record (7 bytes, g_lights.rail_77d180): {x, y, z, track, direction, occupied, track id}. */
enum { RAIL_ST_X, RAIL_ST_Y, RAIL_ST_Z, RAIL_ST_TRACK, RAIL_ST_DIR, RAIL_ST_FLAG, RAIL_ST_TRACK_ID };
int rail_station_count(void);                /* 0x77d46b */
const uint8_t *rail_station(int i);          /* 0x77d180 + 7 i */
int rail_crossing_count(void);               /* 0x77d46a */
const uint8_t *rail_crossing(int i);         /* 0x77cf58 + 4 i: {x, y, z, light} */

int rail_init(void);                         /* Rail_Init 0x47fa90 (0x14 when every railway is a loop) */
int rail_trace_track(int x, int y, int z);   /* Rail_TraceTrack 0x47fbe0 */
const RailInfo *rail_get_info(void);         /* Rail_GetInfo 0x47e600 */
int rail_toggle_crossing(int x, int y);      /* Rail_ToggleCrossing 0x47e610 (0x14 found, 0x21 not) */
bool rail_find_station(int x, int y);        /* Rail_FindStation 0x480bc0 */
int rail_find_switch(int x, int y);          /* Rail_FindSwitch 0x480c20 (0xff none) */
bool rail_is_switch_set(int i);              /* Rail_IsSwitchSet 0x480c90 */
bool rail_toggle_switch(int i);              /* Rail_ToggleSwitch 0x480cc0 */
int rail_get_station_flag(int i);            /* Rail_GetStationFlag 0x480d10 */
bool rail_toggle_station(int i);             /* Rail_ToggleStation 0x480d40 */
int rail_next_station(int i);                /* Rail_NextStation 0x480d90 */
