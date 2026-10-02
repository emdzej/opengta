/* The collision grid (0x434180-0x437000): every entity that can be hit or drawn is linked into one of
   128 x 128 cells of 2 x 2 blocks (cell = coordinate >> 23), with per-kind node pools, plus the
   rotated hitboxes the queries test against. Coll_Init 0x436ad0 clears it at level start.

   Kinds (the first byte of a node): 1 ped, 3 object, 6 car, 7, 8, 10, 0xe, 0x13 (only active with the
   trains + lights switch 0x502f48), 0xc explosion, 0xd, 0x1e (the heli). Kinds 1, 3, 6, 8 and 10
   count in the per-cell entity counter 0x537908. */
#pragma once
#include "../map.h"
#include <stdbool.h>
#include <stdint.h>

enum { COLL_GRID = 128 };

enum {
    COLL_PED = 1, COLL_OBJECT = 3, COLL_CAR = 6, COLL_KIND7 = 7, COLL_KIND8 = 8, COLL_KIND10 = 10,
    COLL_EXPLOSION = 0xc, COLL_KIND13 = 0xd, COLL_KIND14 = 0xe, COLL_KIND19 = 0x13, COLL_HELI = 0x1e,
};

/* A grid node (16 bytes in the original: u8 kind +0, s16 id +2, u8 in use +4, owner +8, next +0xc). */
typedef struct CollNode {
    uint8_t kind;
    int16_t id;
    uint8_t used;
    void *owner;                /* the entity record (car, ped, object...) */
    struct CollNode *next;
} CollNode;

/* A hitbox (0x44 bytes; Coll_BuildBox 0x434300). Corners 0..3 then the centre. */
typedef struct {
    int32_t x[4], cx;           /* +0x00, +0x10 */
    int32_t y[4], cy;           /* +0x14, +0x24 */
    int32_t gz[4];              /* +0x28: ground z under each corner, minus 1 */
    int32_t z;                  /* +0x38 */
    int16_t angle;              /* +0x3c */
    int16_t id;                 /* +0x3e */
    int32_t z2;                 /* +0x40 */
} CollBox;
_Static_assert(sizeof(CollBox) == 0x44, "hitbox layout");

/* 0x5278f8: the cell lists, [cy][cx] */
extern CollNode *g_coll_grid[COLL_GRID][COLL_GRID];
extern int16_t g_coll_count[COLL_GRID][COLL_GRID];      /* 0x537908: kinds 1, 3, 6, 8, 10 */
extern int16_t g_coll_obj_count[COLL_GRID][COLL_GRID];  /* 0x513890: objects of type 10 */
extern int16_t g_coll_ped_count[COLL_GRID][COLL_GRID];  /* 0x51be58: objects of types 0xb, 0xc, 0x11 */
extern bool g_coll_locked;                              /* 0x527018: the query list is in use */

void coll_init_node_pools(void);                        /* Coll_InitNodePools 0x434180 */
void coll_init(void);                                   /* Coll_Init 0x436ad0 */
static inline void coll_unlock(void) { g_coll_locked = false; }   /* Coll_Unlock 0x436b20 */
/* Coll_Insert 0x436c30: links entity `id` of `kind` (owner = its record) into the cell of (x, y) and
   records x, y in the sprite (grid_xy: the sprite's +0x20 / +0x24). */
void coll_insert(int kind, int id, void *owner, int32_t grid_xy[2], int32_t x, int32_t y);
/* Coll_Remove 0x436e30: unlinks the node whose owner is `owner` from the cell recorded in grid_xy. */
void coll_remove(void *owner, int32_t grid_xy[2]);
int coll_get_ped_count(int32_t x, int32_t y);           /* Coll_GetPedCount 0x436b30 */
void coll_inc_ped_count(int32_t x, int32_t y);          /* Coll_IncPedCount 0x436b50 */
void coll_dec_ped_count(int32_t x, int32_t y);          /* Coll_DecPedCount 0x436b80 */
int coll_get_obj_count(int32_t x, int32_t y);           /* Coll_GetObjCount 0x436bb0 */
void coll_inc_obj_count(int32_t x, int32_t y);          /* Coll_IncObjCount 0x436bd0 */
void coll_dec_obj_count(int32_t x, int32_t y);          /* Coll_DecObjCount 0x436c00 */
/* Coll_BuildBox 0x434300: the box of an entity at (x, y, z) with half extents hw, hh (pixels), angle
   0..1023 and id, into *out (returned). Needs the map (ground z of the corners). */
CollBox *coll_build_box(int32_t x, int32_t y, int32_t z, int hw, int hh, int angle, int id, CollBox *out);
/* Coll_BoxVsBox 0x434690: true (1) if a corner or the centre of one box lies inside the other (seen
   from corners 1 and 2 of the other box, angles relative to its heading in the ranges of 0x4b0c32..),
   when their z2 are within half a block; the contact point (pixels) goes to g_coll_contact. -1 if not. */
int coll_box_vs_box(const CollBox *a, const CollBox *b);
extern float g_coll_contact[2];             /* 0x501cfc, 0x501d00 */

/* The query result list (0x523ff0: 149 entries of 16 bytes, newest first from the head 0x5278a0,
   count 0x524956). A query locks it (0x527018; a nested query is fatal -0xa8) until coll_unlock. */
enum { COLL_HITS = 0x95 };
typedef struct CollHit {
    uint8_t kind;
    int16_t id;
    void *owner;
    struct CollHit *next;
} CollHit;
extern CollHit g_coll_hits[COLL_HITS];
extern CollHit *g_coll_hit_head;
extern int g_coll_hit_count;

/* Coll_BoxTouchesBlock 0x435e00: the box's first ground z - 1 is in layer z (16.16) and a corner,
   or the midpoint of corners 0 and 3, is in block (bx, by). */
bool coll_box_touches_block(const CollBox *b, int bx, int by, int32_t z);
/* Coll_QueryCellBlock 0x435e80: entities of `kind` (0 = any) in the cell of (cx, cy) whose box touches
   the block of (x, y) at layer z (mode 4: every entity of the kind), except id `exclude` (peds, cars). */
void coll_query_cell_block(int32_t cx, int32_t cy, int32_t x, int32_t y, int32_t z, int kind, int mode, int exclude);
/* Coll_QueryBlock 0x4363c0 / Coll_QueryBlockAll 0x436650: the same over the 3 x 3 cells around (x, y)
   that hold entities; returns the list head. */
CollHit *coll_query_block(int32_t x, int32_t y, int32_t z, int kind, int exclude);
CollHit *coll_query_block_all(int32_t x, int32_t y, int32_t z, int kind);
/* Coll_QueryCellCars 0x436280 / Coll_QueryCars 0x4368d0: the cars (not model 0x2f) except `exclude`. */
void coll_query_cell_cars(int32_t x, int32_t y, int exclude);
CollHit *coll_query_cars(int32_t x, int32_t y, int exclude);
/* Coll_GetFiresInBlock 0x435040: fire objects (type 0x12) in the block of (x, y). Returns the first
   entry found (not the head: callers see one fire), NULL if none. Locks the list. */
CollHit *coll_get_fires_in_block(int32_t x, int32_t y);
/* Coll_IsTrafficObjectNear 0x434580: an object of type 0xf, 0x3f or 0x40 in the cell of (x, y) within
   8 pixels in x / y and z. */
bool coll_is_traffic_object_near(int32_t x, int32_t y, int32_t z);

/* Coll_FindCarAt 0x436350: a car of `model` at exactly (x, y) in the cell of (x, y). */
bool coll_find_car_at(int32_t x, int32_t y, int model);
/* Nodes in use, for checks */
int coll_count_nodes(int kind);

/* Map_GetGroundZ 0x4544e0: the height an entity at (x, y) starting at layer z >> 22 stands on (the
   first non-air layer at or below it, slopes interpolated), 16.16. Reads the type cache of g_game.map. */
int32_t map_get_ground_z(const Map *m, int32_t x, int32_t y, int32_t z);
