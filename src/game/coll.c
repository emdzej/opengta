#include "coll.h"
#include "../exe.h"
#include "car.h"
#include "obj.h"
#include "ped.h"
#include "game.h"
#include "gmath.h"
#include <string.h>

CollNode *g_coll_grid[COLL_GRID][COLL_GRID] = { 0 };
int16_t g_coll_count[COLL_GRID][COLL_GRID] = { 0 };
int16_t g_coll_obj_count[COLL_GRID][COLL_GRID] = { 0 };
int16_t g_coll_ped_count[COLL_GRID][COLL_GRID] = { 0 };
bool g_coll_locked;

/* The node pools: one node per entity slot of the kind (pool addresses in the original). */
static CollNode pool_ped[620];               /* 0x524958 */
static CollNode pool_obj[3500];              /* 0x5417d0 */
static CollNode pool_car[400];               /* 0x53f908 */
static CollNode pool_7[88];                  /* 0x527320 */
static CollNode pool_8[48];                  /* 0x527020 */
static CollNode pool_10[24];                 /* 0x513710 */
static CollNode pool_expl[25];               /* 0x523e58 */
static CollNode pool_13[5];                  /* 0x5278a8 */
static CollNode pool_14[88];                 /* 0x541250 */
static CollNode pool_19[88];                 /* 0x51b8d8 */
static CollNode node_heli;                   /* 0x5378f8 */

static struct { CollNode *nodes; int n; } const pools[] = {
    { pool_ped, 620 }, { pool_obj, 3500 }, { pool_car, 400 }, { pool_7, 88 }, { pool_14, 88 }, { pool_19, 88 },
    { pool_8, 48 }, { pool_10, 24 }, { pool_expl, 25 }, { pool_13, 5 },
};

static void reset_node(CollNode *n)
{
    n->kind = 0xff;
    n->id = -1;
    n->used = 0;
    n->owner = NULL;
    n->next = NULL;
}

/* Coll_InitNodePools 0x434180 (the original resets the kind 7 pool twice) */
void coll_init_node_pools(void)
{
    for (size_t p = 0; p < sizeof pools / sizeof *pools; p++)
        for (int i = 0; i < pools[p].n; i++) reset_node(&pools[p].nodes[i]);
    reset_node(&node_heli);
}

/* Coll_Init 0x436ad0 */
void coll_init(void)
{
    memset(g_coll_count, 0, sizeof g_coll_count);
    memset(g_coll_obj_count, 0, sizeof g_coll_obj_count);
    memset(g_coll_ped_count, 0, sizeof g_coll_ped_count);
    memset(g_coll_grid, 0, sizeof g_coll_grid);
    coll_init_node_pools();
    g_coll_locked = false;
}

static CollNode *pool_node(int kind, int id, int n, CollNode *pool)
{
    if (id < 0 || id >= n) game_fatal(-0x7e, 0x5e, id);   /* (the original indexes past its pool) */
    (void)kind;
    return &pool[id];
}

static bool counted(int kind)
{
    return kind == COLL_PED || kind == COLL_OBJECT || kind == COLL_CAR || kind == COLL_KIND8 || kind == COLL_KIND10;
}

/* Coll_Insert 0x436c30. The node goes to the head of its cell. A node that is already the head is
   fatal (-0x7e), as is a cell outside the grid (-0x74); the original counts the entity before that
   test and writes negative cells unchecked (the port refuses those before touching anything). */
void coll_insert(int kind, int id, void *owner, int32_t grid_xy[2], int32_t x, int32_t y)
{
    int cx = (int16_t)(x >> 23), cy = (int16_t)(y >> 23);
    if (cx < 0 || cy < 0 || cx >= COLL_GRID || cy >= COLL_GRID) game_fatal(-0x74, 0x5e, 0x62);
    CollNode *head = g_coll_grid[cy][cx], *n;
    switch (kind) {
    case COLL_PED: n = pool_node(kind, id, 620, pool_ped); break;
    case COLL_OBJECT: n = pool_node(kind, id, 3500, pool_obj); break;
    case COLL_CAR: n = pool_node(kind, id, 400, pool_car); break;
    case COLL_KIND7: n = pool_node(kind, id, 88, pool_7); break;
    case COLL_KIND8: n = pool_node(kind, id, 48, pool_8); break;
    case COLL_KIND10: n = pool_node(kind, id, 24, pool_10); break;
    case COLL_EXPLOSION: n = pool_node(kind, id, 25, pool_expl); break;
    case COLL_KIND13: n = pool_node(kind, id, 5, pool_13); break;
    case COLL_KIND14: n = pool_node(kind, id, 88, pool_14); break;
    case COLL_KIND19: n = pool_node(kind, id, 88, pool_19); break;
    case COLL_HELI: n = &node_heli; break;
    default: game_fatal(-0x7e, 0x5e, 0);
    }
    if (counted(kind)) g_coll_count[cy][cx]++;
    n->kind = (uint8_t)kind;
    n->owner = owner;
    n->id = (int16_t)id;
    n->used = 1;
    grid_xy[0] = x;
    grid_xy[1] = y;
    if (n == head) game_fatal(-0x7e, 0x5e, 99);
    n->next = head;
    g_coll_grid[cy][cx] = n;
}

/* Coll_Remove 0x436e30: nothing happens unless the cell's head node is in use (a stale head hides the
   rest of the list). The cell recorded in the sprite is cleared to -1, -1. */
void coll_remove(void *owner, int32_t grid_xy[2])
{
    if (grid_xy[0] == -1 || grid_xy[1] == -1) return;
    int cx = (int16_t)(grid_xy[0] >> 23), cy = (int16_t)(grid_xy[1] >> 23);
    if (cx < 0 || cy < 0 || cx >= COLL_GRID || cy >= COLL_GRID) game_fatal(-0x74, 0x5e, 0x62);
    CollNode *n = g_coll_grid[cy][cx];
    if (!n || n->used != 1) return;
    if (n->owner != owner) {
        CollNode *prev;
        do {
            prev = n;
            n = n->next;
            if (!n) return;
        } while (n->owner != owner);
        prev->next = n->next;
    } else {
        g_coll_grid[cy][cx] = n->next;
    }
    n->used = 0;
    grid_xy[0] = grid_xy[1] = -1;
    if (counted(n->kind)) g_coll_count[cy][cx]--;
}

static int16_t *cell(int16_t (*a)[COLL_GRID], int32_t x, int32_t y)
{
    int cx = x >> 23, cy = y >> 23;
    if (cx < 0 || cy < 0 || cx >= COLL_GRID || cy >= COLL_GRID) game_fatal(-0x74, 0x5e, 0x62);
    return &a[cy][cx];
}
int coll_get_ped_count(int32_t x, int32_t y) { return *cell(g_coll_ped_count, x, y); }    /* 0x436b30 */
void coll_inc_ped_count(int32_t x, int32_t y) { ++*cell(g_coll_ped_count, x, y); }       /* 0x436b50 */
void coll_dec_ped_count(int32_t x, int32_t y) { --*cell(g_coll_ped_count, x, y); }       /* 0x436b80 */
int coll_get_obj_count(int32_t x, int32_t y) { return *cell(g_coll_obj_count, x, y); }    /* 0x436bb0 */
void coll_inc_obj_count(int32_t x, int32_t y) { ++*cell(g_coll_obj_count, x, y); }       /* 0x436bd0 */
void coll_dec_obj_count(int32_t x, int32_t y) { --*cell(g_coll_obj_count, x, y); }       /* 0x436c00 */

/* Coll_FindCarAt 0x436350 */
bool coll_find_car_at(int32_t x, int32_t y, int model)
{
    int cx = (int16_t)(x >> 23), cy = (int16_t)(y >> 23);
    if (cx < 0 || cy < 0 || cx >= COLL_GRID || cy >= COLL_GRID) return false;   /* (unchecked in the original) */
    for (CollNode *n = g_coll_grid[cy][cx]; n; n = n->next) {
        const Car *c = n->owner;
        if (n->kind == COLL_CAR && c->spr.x == x && c->spr.y == y && c->model == (int16_t)model) return true;
    }
    return false;
}

/* ---- box against box ---- */

float g_coll_contact[2];

/* the angle ranges at 0x4b0c32 / 0x4b0c34 / 0x4b0c3a / 0x4b0c3c (read from the exe) */
static int16_t box_range(uint32_t va)
{
    const uint8_t *p = exe_data(va, 2);
    if (!p) game_fatal(-2, 0, (int)va);
    return (int16_t)(p[0] | p[1] << 8);
}

/* one side of the test: a point of b inside a */
static int box_point_in(const CollBox *a, const CollBox *b)
{
    int16_t k0 = box_range(0x4b0c32), k1 = box_range(0x4b0c34), k2 = box_range(0x4b0c3a), k3 = box_range(0x4b0c3c);
    for (int i = 0; i < 5; i++) {   /* the four corners, then the centre (x[4] / y[4] are cx / cy) */
        int32_t bx = i < 4 ? b->x[i] : b->cx, by = i < 4 ? b->y[i] : b->cy;
        int16_t u1 = (int16_t)((math_atan2(by - a->y[1], bx - a->x[1]) - a->angle) & 0x3ff);
        int16_t u2 = (int16_t)((math_atan2(by - a->y[2], bx - a->x[2]) - a->angle) & 0x3ff);
        if (k0 <= u1 && u1 <= k2 && k1 <= u2 && u2 <= k3) {
            g_coll_contact[0] = (float)(int16_t)((uint32_t)bx >> 16);
            g_coll_contact[1] = (float)(int16_t)((uint32_t)by >> 16);
            return 1;
        }
    }
    return 0;
}

/* Coll_BoxVsBox 0x434690 */
int coll_box_vs_box(const CollBox *a, const CollBox *b)
{
    if (a->z2 < b->z2 + 0x200000 && b->z2 - 0x200000 < a->z2 && box_point_in(a, b)) return 1;
    if (b->z2 < a->z2 + 0x200000 && a->z2 - 0x200000 < b->z2 && box_point_in(b, a)) return 1;
    return -1;
}

/* ---- queries ---- */

CollHit g_coll_hits[COLL_HITS];
CollHit *g_coll_hit_head;
int g_coll_hit_count;
static CollBox query_box;                    /* 0x51b890 */

static bool in_world(int32_t x, int32_t y) { return x >= 0 && x < 0x40000000 && y >= 0 && y < 0x40000000; }

static void lock(void)
{
    if (g_coll_locked) game_fatal(-0xa8, 0x5e, 1);   /* collision list in use */
    g_coll_locked = true;
}

static void add_hit(int kind, int id, void *owner)
{
    CollHit *h = &g_coll_hits[g_coll_hit_count++];
    h->next = g_coll_hit_head;
    g_coll_hit_head = h;
    h->kind = (uint8_t)kind;
    h->id = (int16_t)id;
    h->owner = owner;
}

/* the per-cell entity count of a cell that may lie outside the grid (the original reads whatever is
   next to the array there) */
static int cell_count(int cx, int cy)
{
    return cx >= 0 && cy >= 0 && cx < COLL_GRID && cy < COLL_GRID ? g_coll_count[cy][cx] : 0;
}

/* Coll_BoxTouchesBlock 0x435e00 */
bool coll_box_touches_block(const CollBox *b, int bx, int by, int32_t z)
{
    if ((((uint32_t)b->gz[0] - 1u) ^ (uint32_t)z) & 0xffc00000u) return false;
    for (int i = 0; i < 4; i++)
        if (bx == b->x[i] >> 22 && by == b->y[i] >> 22) return true;
    return bx == (((b->x[3] - b->x[0]) >> 1) + b->x[0]) >> 22 && by == (((b->y[3] - b->y[0]) >> 1) + b->y[0]) >> 22;
}

/* Coll_QueryCellBlock 0x435e80. Boxes: peds 4 x 4 pixels (only within a block of the point), objects
   their object_info size (not status 1, 5, 7 or owned ones), cars their current box (not model 0x2f),
   kinds 10 and 8 fixed 23 x 66 and 23 x 23. */
void coll_query_cell_block(int32_t cx, int32_t cy, int32_t x, int32_t y, int32_t z, int kind, int mode, int exclude)
{
    int bx = x >> 22, by = y >> 22;
    if (!in_world(cx, cy)) return;
    for (CollNode *n = g_coll_grid[(int16_t)(cy >> 23)][(int16_t)(cx >> 23)]; n; n = n->next) {
        if (n->kind != kind && kind != 0) continue;
        const CollBox *b;
        int id;
        switch (n->kind) {
        case COLL_PED: {
            const Ped *p = n->owner;
            if (p->id == (int16_t)exclude || !(p->spr.x < x + 0x400000 && x - 0x400000 < p->spr.x) ||
                !(p->spr.y < y + 0x400000 && y - 0x400000 < p->spr.y))
                continue;
            b = coll_build_box(p->spr.x, p->spr.y, p->spr.z, 4, 4, p->spr.angle, 10, &query_box);
            id = p->id;
            break;
        }
        case COLL_OBJECT: {
            const Obj *o = n->owner;
            const ObjInfo *in = g_obj_infos[o->type];
            if (in->status == OBJ_STATUS_1 || in->status == OBJ_STATUS_ANIM || in->status == OBJ_STATUS_7 || o->owner != -1)
                continue;
            b = coll_build_box(o->spr.x, o->spr.y, o->spr.z, in->w >> 17, in->h >> 17, o->spr.angle, (int16_t)in->depth,
                               &query_box);
            id = o->id;
            break;
        }
        case COLL_CAR: {
            const Car *c = n->owner;
            if (c->model == 0x2f || c->id == (int16_t)exclude) continue;
            b = &c->box;
            id = c->id;
            break;
        }
        case COLL_KIND10: case COLL_KIND8: {   /* the owner is a sprite */
            const Sprite *sp = n->owner;
            b = n->kind == COLL_KIND10 ? coll_build_box(sp->x, sp->y, sp->z, 0x17, 0x42, sp->angle, 0x18, &query_box)
                                       : coll_build_box(sp->x, sp->y, sp->z, 0x17, 0x17, sp->angle, 0x14, &query_box);
            id = n->id;
            break;
        }
        default: continue;
        }
        if ((coll_box_touches_block(b, bx, by, z) || mode == 4) && g_coll_hit_count < COLL_HITS) add_hit(n->kind, id, n->owner);
    }
}

static CollHit *query_block(int32_t x, int32_t y, int32_t z, int kind, int mode, int exclude)
{
    lock();
    int cx = (int16_t)(x >> 23), cy = (int16_t)(y >> 23);
    g_coll_hit_head = NULL;
    g_coll_hit_count = 0;
    static const int8_t order[9][2] = { { 0, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 }, { 1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 } };
    for (int i = 0; i < 9; i++)
        if (cell_count(cx + order[i][0], cy + order[i][1]))
            coll_query_cell_block(x + order[i][0] * 0x800000, y + order[i][1] * 0x800000, x, y, z, kind, mode, exclude);
    return g_coll_hit_head;
}
CollHit *coll_query_block(int32_t x, int32_t y, int32_t z, int kind, int exclude) { return query_block(x, y, z, kind, 0, exclude); }
CollHit *coll_query_block_all(int32_t x, int32_t y, int32_t z, int kind) { return query_block(x, y, z, kind, 4, -1); }

/* Coll_QueryCellCars 0x436280 */
void coll_query_cell_cars(int32_t x, int32_t y, int exclude)
{
    if (!in_world(x, y)) return;
    for (CollNode *n = g_coll_grid[(int16_t)(y >> 23)][(int16_t)(x >> 23)]; n; n = n->next) {
        const Car *c = n->owner;
        if (n->kind == COLL_CAR && c->model != 0x2f && c->id != (int16_t)exclude && g_coll_hit_count < COLL_HITS)
            add_hit(n->kind, c->id, n->owner);
    }
}

/* Coll_QueryCars 0x4368d0 */
CollHit *coll_query_cars(int32_t x, int32_t y, int exclude)
{
    lock();
    int cx = (int16_t)(x >> 23), cy = (int16_t)(y >> 23);
    g_coll_hit_head = NULL;
    g_coll_hit_count = 0;
    static const int8_t order[9][2] = { { 0, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 }, { 1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 } };
    for (int i = 0; i < 9; i++)
        if (cell_count(cx + order[i][0], cy + order[i][1]))
            coll_query_cell_cars(x + order[i][0] * 0x800000, y + order[i][1] * 0x800000, exclude);
    return g_coll_hit_head;
}

/* Coll_GetFiresInBlock 0x435040: the entries are counted locally (the global count stays). */
CollHit *coll_get_fires_in_block(int32_t x, int32_t y)
{
    lock();
    int n = 0;
    CollHit *prev = NULL;
    g_coll_hit_head = NULL;
    if (x >> 23 < 0 || y >> 23 < 0) return NULL;
    for (CollNode *c = g_coll_grid[(int16_t)(y >> 23)][(int16_t)(x >> 23)]; c; c = c->next) {
        const Obj *o = c->owner;
        if (c->kind == COLL_OBJECT && o->type == 0x12 && x >> 22 == o->spr.x >> 22 && y >> 22 == o->spr.y >> 22 && n < COLL_HITS) {
            CollHit *h = &g_coll_hits[n++];
            h->next = prev;
            g_coll_hit_head = h;
            h->kind = c->kind;
            h->id = c->id;
            h->owner = c->owner;
            prev = h;
        }
    }
    return n > 0 ? &g_coll_hits[0] : NULL;
}

/* Coll_IsTrafficObjectNear 0x434580 (pixel distances: the high words of the differences) */
bool coll_is_traffic_object_near(int32_t x, int32_t y, int32_t z)
{
    int cx = (int16_t)(x >> 23), cy = (int16_t)(y >> 23);
    if (!cell_count(cx, cy) || !in_world(x, y)) return false;
    for (CollNode *n = g_coll_grid[cy][cx]; n; n = n->next) {
        if (n->kind != COLL_OBJECT) continue;
        const Obj *o = n->owner;
        if (o->type != 0xf && o->type != 0x3f && o->type != 0x40) continue;
        int16_t dx = (int16_t)((uint32_t)(o->spr.x - x) >> 16), dy = (int16_t)((uint32_t)(o->spr.y - y) >> 16);
        int32_t dz = o->spr.z - z;
        int16_t ax = (int16_t)(dx < 0 ? -dx : dx), ay = (int16_t)(dy < 0 ? -dy : dy);
        int16_t az = (int16_t)((dz >> 16 ^ dz >> 31) - (dz >> 31));
        if ((ax > ay ? ax : ay) < 8 && az < 8) return true;
    }
    return false;
}

int coll_count_nodes(int kind)
{
    int k = 0;
    for (int y = 0; y < COLL_GRID; y++)
        for (int x = 0; x < COLL_GRID; x++)
            for (CollNode *n = g_coll_grid[y][x]; n; n = n->next) k += n->kind == kind;
    return k;
}

/* Coll_BuildBox 0x434300. Corner order for heading a, with s = sin, c = cos, hh along the heading and
   hw across it: 0 = centre + hh(s, c) + hw(s', c'), 1 = + hh - hw, 2 = - hh + hw, 3 = - hh - hw, where
   ' is a + 0x100. Headings 0, 0x100, 0x200 and 0x300 are done exactly; other headings with a zero low
   byte (out of range) leave the corners as they were. */
CollBox *coll_build_box(int32_t x, int32_t y, int32_t z, int hw, int hh, int angle, int id, CollBox *b)
{
    int16_t a = (int16_t)angle;
    int32_t w = hw * 0x10000, h = hh * 0x10000;
    if ((a & 0xff) == 0) {
        if (a == 0x200) {
            b->x[0] = x - w, b->y[0] = y - h, b->x[1] = x + w, b->y[1] = y - h;
            b->x[2] = x - w, b->y[2] = y + h, b->x[3] = x + w, b->y[3] = y + h;
        } else if (a == 0) {
            b->x[0] = x + w, b->y[0] = y + h, b->x[1] = x - w, b->y[1] = y + h;
            b->x[2] = x + w, b->y[2] = y - h, b->x[3] = x - w, b->y[3] = y - h;
        } else if (a == 0x100) {
            b->x[0] = x + h, b->y[0] = y - w, b->x[1] = x + h, b->y[1] = y + w;
            b->x[2] = x - h, b->y[2] = y - w, b->x[3] = x - h, b->y[3] = y + w;
        } else if (a == 0x300) {
            b->x[0] = x - h, b->y[0] = y + w, b->x[1] = x - h, b->y[1] = y - w;
            b->x[2] = x + h, b->y[2] = y + w, b->x[3] = x + h, b->y[3] = y - w;
        }
    } else {
        int32_t s = math_sin(a) * hh, c = math_cos(a) * hh;
        int u = (a + 0x100) & 0x3ff;
        int32_t s2 = math_sin(u) * hw, c2 = math_cos(u) * hw;
        int32_t fx = x + s, fy = y + c, rx = x - s, ry = y - c;
        b->x[0] = fx + s2, b->y[0] = fy + c2;
        b->x[1] = fx - s2, b->y[1] = fy - c2;
        b->x[2] = rx + s2, b->y[2] = ry + c2;
        b->x[3] = rx - s2, b->y[3] = ry - c2;
    }
    const Map *m = g_game.map;
    for (int i = 0; i < 4; i++) b->gz[i] = map_get_ground_z(m, b->x[i], b->y[i], z - 0x200000) - 1;
    b->cx = x;
    b->id = (int16_t)id;
    b->z2 = z;
    b->z = z;
    b->angle = a;
    b->cy = y;
    return b;
}

/* Map_GetGroundZ 0x4544e0. From layer z >> 22 down to layer 5 the first layer whose cached type isn't
   air (bits 4-6) is the ground; layers past 4 count as 4 (so a start at layer 5 or below, or
   negative, looks at layer 4 directly). A flat ground block gives the bottom face of the layer
   (layer * 0x400000 + 0x3f0000); a slope (bit 7) gives the height of the slope type (type_map bits
   8-13) at the position inside the block: 1-8 are 2-block slopes, 9-0x28 8-block ones, 0x29-0x2c
   1-block ones, in steps of whole pixels. */
int32_t map_get_ground_z(const Map *m, int32_t x, int32_t y, int32_t z)
{
    int bx = x >> 22, by = y >> 22;
    uint8_t zb = (uint8_t)(z >> 22);
    int k = zb;
#define AIR(l) ((m->type_cache[l][by & 0xff][bx & 0xff] & 0x70) == 0)
    if (k <= 4) {
        while (k <= 4 && AIR(k)) k++, zb++;
    }
#undef AIR
    if (zb > 4) k = 4;
    uint8_t t = m->type_cache[k][by & 0xff][bx & 0xff];
    if (!(t & 0x80)) return k * 0x400000 + 0x3f0000;
    unsigned slope = map_get_type_at(m, x, y, k * 0x400000) >> 8 & 0x3f;
    int lx = (int16_t)((uint32_t)x >> 16) - bx * 0x40, ly = (int16_t)((uint32_t)y >> 16) - by * 0x40;
    if (slope > 0x2c) game_fatal(-0x4a, 0x54, (int)slope);
    int x2 = lx >> 1, y2 = ly >> 1, x8 = lx >> 3, y8 = ly >> 3, base = (k + 1) * 0x40, d;
    static const int8_t tops[8] = { 9, 0xf, 0x17, 0x1f, 0x27, 0x2f, 0x37, 0x3f };
    static const int8_t bots[8] = { 2, 8, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38 };
    switch (slope) {
    case 0: d = 0x3f; break;
    case 1: d = 0x1f - y2; break;
    case 2: d = 0x3f - y2; break;
    case 3: d = y2; break;
    case 4: d = y2 + 0x20; break;
    case 5: d = 0x1f - x2; break;
    case 6: d = 0x3f - x2; break;
    case 7: d = x2; break;
    case 8: d = x2 + 0x20; break;
    case 0x29: d = 0x3f - ly; break;
    case 0x2a: d = ly; break;
    case 0x2b: d = 0x3f - lx; break;
    case 0x2c: d = lx; break;
    default:
        if (slope <= 0x10) d = tops[slope - 9] - y8;
        else if (slope <= 0x18) d = y8 + bots[slope - 0x11];
        else if (slope <= 0x20) d = tops[slope - 0x19] - x8;
        else d = x8 + bots[slope - 0x21];
    }
    return (base - d) * 0x10000;
}
