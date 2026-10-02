/* The CMP sections after the objects that map.c keeps raw: routes (Route_LoadCmp 0x471970), the
   service locations that follow them (0x6c bytes, counted by Sentinel_InitAll 0x41abd0) and the nav
   zones (Area_SetNavData 0x44b590).

   Routes: records {u8 n, u8 type, n x (u8 x, u8 y, u8 z)}. Types 0xfe and 0xff go to the path slots
   from 50 on (each slot 0xff bytes: 85 block triples ended by 0, 0, 0); any other type is the
   roadblock vertex set of that number (pointer and count in 0x5f2b50[type], vertices pooled at
   0x5ce3c8, 300 at most). At most 100 0xfe / 0xff routes. */
#pragma once
#include <stdint.h>

enum {
    PATH_SLOTS = 150, PATH_SLOT_SIZE = 0xff, PATH_ROUTE_FIRST = 50, ROUTE_MAX = 100,
    ROADBLOCK_VERTS_MAX = 300, ROADBLOCK_SETS = 256,
    LOCATION_KINDS = 6, LOCATION_N = 6,
    NAV_SIZE = 35, NAV_MAX = 40,
};
/* location kinds, in the order of the section */
enum { LOC_POLICE = 0, LOC_HOSPITAL = 1, LOC_UNUSED2, LOC_UNUSED3, LOC_FIRE = 4, LOC_UNUSED5 };

typedef struct { uint8_t x, y, z; } BlockXYZ;
typedef struct { const BlockXYZ *v; uint8_t n; } RoadblockSet;
/* nav zone (35 bytes) */
typedef struct { uint8_t x, y, w, h, sample; char name[30]; } NavZone;
_Static_assert(sizeof(NavZone) == NAV_SIZE, "nav record");

extern uint8_t g_path_slots[PATH_SLOTS][PATH_SLOT_SIZE];   /* 0x7537d0 */
extern RoadblockSet g_roadblock_sets[ROADBLOCK_SETS];     /* 0x5f2b50 */
extern BlockXYZ g_roadblock_verts[ROADBLOCK_VERTS_MAX];   /* 0x5ce3c8 */
extern int g_route_fe, g_route_ff, g_route_paths;         /* 0x7705e8, 0x7705f8, 0x7705f0 */
extern int g_roadblock_nverts;                            /* 0x7705f4 */
extern const NavZone *g_nav;                               /* 0x6b3eb4 */
extern int g_nav_count;                                    /* 0x6b3eb0 */

/* the location table (0x50589c) and the counts Sentinel_InitAll takes from it */
extern const BlockXYZ *g_locations;                       /* [kind * 6 + i] */
extern int g_police_stations, g_hospitals, g_fire_stations;   /* 0x50caa6, 0x50caa4, 0x50f28c */
extern BlockXYZ g_hospital_block;                          /* 0x505898.. the first hospital */
extern BlockXYZ g_fire_engine_bases[4];                    /* 0x511960 */

/* Route_LoadCmp 0x471970 on the route section (size bytes; the 0x6c location bytes follow it). */
void route_load_cmp(const uint8_t *data, int size);
/* Route_GetCounts 0x471b90 */
void route_get_counts(int *fe, int *ff, const uint8_t **end);
/* The location part of Sentinel_InitAll 0x41abd0: the table pointer, the first police station as the
   respawn block (0x74f850), the counts (non-zero x), the first hospital and the first 4 fire stations. */
void route_init_locations(void);
void area_set_nav_data(const uint8_t *data, int size);    /* Area_SetNavData 0x44b590 */
