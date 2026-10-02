/* Shape against the map and the entity grid (the collision module 0x40e350-0x412310) and the box
   queries over the grid that coll.c leaves out (0x4348c0-0x435a90). A "shape" is a CollBox (corners
   0..3, then the centre). The module keeps its state in globals the callers read back; they are in
   g_cc below. See docs/cars.md. */
#pragma once
#include "car.h"
#include "coll.h"
#include "game.h"
#include <stdbool.h>
#include <stdint.h>

/* the module's globals (0x501c58..0x501d18) */
typedef struct {
    int32_t min_x, min_y, min_gz;   /* 0x501c5c, 0x501c60, 0x501c64 (Coll_ComputeBounds / _ComputeMinZ) */
    int32_t max_x, max_y, max_gz;   /* 0x501d04, 0x501d08, 0x501d0c */
    int16_t layer_lo;               /* 0x501d18 (lowest ground z - 1) >> 22 */
    int16_t layer_z;                /* 0x501d14 (z - 1) >> 22 */
    int16_t layer_hi;               /* 0x501cac (highest ground z - 1) >> 22 */
    int16_t kind;                   /* 0x501d10 what the last test hit: -1 none, 2 solid, 3 slope, 4 wall,
                                       9 solid + wall, 10 solid + slope, 11 wall + slope, 12 all three;
                                       grid hits: 5 ped, 6 car, 7 object, 8 heavy object */
    int16_t corner_pair;            /* 0x501c58 corners touching a building (first * 16 + second) */
    int16_t nhits;                  /* 0x501d16 entries of the gathered hit list */
    int16_t u501d12;                /* 0x501d12 */
    float bounce_x, bounce_y;       /* 0x501cf4, 0x501cf8 */
    CollBox tile_box;               /* 0x501c68 a block-sized box for the special tile test */
} CarCollState;
extern CarCollState g_cc;

/* the type cache 0x55fab0 at a linear index ((z * 256 + y) * 256 + x), as the original indexes it
   (neighbours are index +-1 / +-256); 0 outside the array (the original reads its neighbours there) */
uint8_t car_cache_at(int idx);
static inline int car_cache_index(int32_t x, int32_t y, int32_t z) { return ((z >> 22) * 0x100 + (y >> 22)) * 0x100 + (x >> 22); }
static inline uint8_t car_type_cache(int32_t x, int32_t y, int32_t z) { return car_cache_at(car_cache_index(x, y, z)); }

/* ---- shape against the map ---- */
void coll_compute_bounds(const CollBox *b);         /* Coll_ComputeBounds 0x40e350 */
void coll_compute_min_z(const CollBox *b);          /* Coll_ComputeMinZ 0x40e5a0 */
bool coll_circle_vs_shape(int32_t x, int32_t y, int r, const CollBox *b);   /* Coll_CircleVsShape 0x40e5f0 */
int coll_check_special_tile(int tile, int32_t x, int32_t y, const CollBox *b);   /* Coll_CheckSpecialTile 0x40e710 (1 / -1) */
int coll_map_walls(const CollBox *b, int special);  /* Coll_MapWalls 0x40e7e0 (10 / -1) */
/* Coll_MapWallsEx 0x40ebe0: the same over the faces flagged in the 0x7711b0 table (projectiles), also
   giving the block hit (x, y and the lowest ground layer) */
int coll_map_walls_ex(const CollBox *b, int special, int *bx, int *by, int *bz);
int coll_map_solid(const CollBox *b);               /* Coll_MapSolid 0x40f060 (corner 0..3, 4 several, 10 edge, -1) */
int coll_map_slopes(const CollBox *b, int bike);    /* Coll_MapSlopes 0x40f4c0 (10 / -1) */
/* Coll_MapSlopesEx 0x40fac0: only the high sides of the ramps' tops (slope types 0x10 / 0x18 / 0x20
   / 0x28) when the box is within one layer (projectiles) */
int coll_map_slopes_ex(const CollBox *b);
int coll_map_all(const CollBox *b, int bike);       /* Coll_MapAll 0x40ffe0 (sets g_cc.kind) */

/* ---- shape against the grid ---- */
bool coll_should_collide(int kind, const void *self, int other_kind, const void *other);   /* 0x4100b0 */
int coll_gather_hits(const CollBox *b, int kind, const void *self);   /* Coll_GatherHits 0x410280 (1 / last test) */
int coll_first_hit(const CollBox *b, int kind, const void *self);     /* Coll_FirstHit 0x410470 */
int coll_any_object_at(int32_t x, int32_t y, const CollBox *b);       /* Coll_AnyObjectAt 0x411040 */
int coll_entity_vs_box(int kind, void *owner, const CollBox *b);      /* Coll_EntityVsBox 0x4348c0 (1 / -1) */

/* ---- car responses ---- */
void coll_car_car_impulse(Car *a, Car *b);          /* Coll_CarCarImpulse 0x410560 */
void coll_process_hits(int kind, Car *c);           /* Coll_ProcessHits 0x4107d0 */
void car_collide_objects(int kind, Car *c);         /* Car_CollideObjects 0x410dc0 */
void car_add_crash_deltas(Car *c, int result);      /* Car_AddCrashDeltas 0x411110 */
void car_collide_map(int kind, Car *c);             /* Car_CollideMap 0x411280 */
double car_impact_speed(const Car *a, const Car *b);   /* Car_ImpactSpeed 0x411950 */
void car_collide_car(Car *b, Car *a);               /* Car_CollideCar 0x411a20 (b the other, a the mover) */
void car_push_from_walls(Car *c);                   /* Car_PushFromWalls 0x4120a0 */
void car_update_ground(Car *c);                     /* Car_UpdateGround 0x412310 */

/* ---- the box queries (lock the result list of coll.h until coll_unlock) ---- */
/* Coll_QueryBox 0x434e80: entities of `kind` (0 any; 0x11 / 0x12 special filters) hitting box b,
   not the caller (self_kind, self_id); returns the list head */
CollHit *coll_query_box(const CollBox *b, int kind, int self_kind, int self_id);
/* Coll_QueryCarBox 0x4354b0: the same around (x, y) against the car's pending box */
CollHit *coll_query_car_box(Car *c, int32_t x, int32_t y, int kind, int exclude);
CollHit *coll_query_box_first(const CollBox *b, int kind, int self_kind, int self_id);   /* 0x435a90 */
