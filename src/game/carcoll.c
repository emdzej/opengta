/* The collision module 0x40e350-0x412310 (shape against walls / buildings / slopes, the grid hit
   list, the car responses, Car_UpdateGround) and the box queries 0x4348c0-0x435a90 of the grid.
   See docs/cars.md. Float code follows the rules in carphys.h. */
#include "carcoll.h"
#include "../audio/audio.h"
#include "../exe.h"
#include "carinfo.h"
#include "gmath.h"
#include "obj.h"
#include "ped.h"
#include "player.h"
#include "stubs.h"
#include "trigger.h"
#include <string.h>

#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

CarCollState g_cc;

static int16_t px16(int32_t v) { return (int16_t)((uint32_t)v >> 16); }   /* the pixel (high) word */
static int32_t bx_of(const CollBox *b, int i) { return i < 4 ? b->x[i] : b->cx; }
static int32_t by_of(const CollBox *b, int i) { return i < 4 ? b->y[i] : b->cy; }
static int32_t bz_of(const CollBox *b, int i) { return i < 4 ? b->gz[i] : b->z; }

uint8_t car_cache_at(int idx)
{
    const Map *m = g_game.map;
    if (!m || idx < 0 || idx >= MAP_Z * MAP_H * MAP_W) return 0;
    return (&m->type_cache[0][0][0])[idx];
}

static const uint8_t *exe_table(uint32_t va, int n)
{
    const uint8_t *p = exe_data(va, (size_t)n);
    if (!p) game_fatal(-2, 0, (int)va);
    return p;
}
/* 0x4abe70: the corner that follows each corner around the box (4 bytes) */
static int next_corner(int i) { return exe_table(0x4abe70, 4)[i]; }
/* 0x4abe30: the nine cell steps of the 3 x 3 walks (s16, cumulative) */
static int16_t cell_step(int i) { const uint8_t *p = exe_table(0x4abe30, 18); return (int16_t)(p[2 * i] | p[2 * i + 1] << 8); }

/* ---- Coll_ComputeBounds 0x40e350 / Coll_ComputeMinZ 0x40e5a0 ---- */

static int32_t min4(int32_t a, int32_t b, int32_t c, int32_t d)
{
    int32_t m1 = a <= b ? a : b, m2 = c <= d ? c : d;
    return m2 < m1 ? m2 : m1;
}
static int32_t max4(int32_t a, int32_t b, int32_t c, int32_t d)
{
    int32_t m1 = a <= b ? b : a, m2 = c <= d ? d : c;
    return m2 < m1 ? m1 : m2;
}

void coll_compute_bounds(const CollBox *b)
{
    g_cc.min_x = min4(b->x[0], b->x[1], b->x[2], b->x[3]);
    g_cc.min_y = min4(b->y[0], b->y[1], b->y[2], b->y[3]);
    g_cc.max_x = max4(b->x[0], b->x[1], b->x[2], b->x[3]);
    g_cc.max_y = max4(b->y[0], b->y[1], b->y[2], b->y[3]);
    g_cc.min_gz = min4(b->gz[0], b->gz[1], b->gz[2], b->gz[3]);
    g_cc.max_gz = max4(b->gz[0], b->gz[1], b->gz[2], b->gz[3]);
    g_cc.layer_lo = (int16_t)((g_cc.min_gz - 1) >> 22);
    g_cc.layer_z = (int16_t)((b->z - 1) >> 22);
    g_cc.layer_hi = (int16_t)((g_cc.max_gz - 1) >> 22);
}

void coll_compute_min_z(const CollBox *b) { g_cc.min_gz = min4(b->gz[0], b->gz[1], b->gz[2], b->gz[3]); }

/* Coll_CircleVsShape 0x40e5f0: a corner or the centre within r pixels, or a corner or the centre of
   the circle's square strictly inside the bounds of the last Coll_ComputeBounds */
bool coll_circle_vs_shape(int32_t x, int32_t y, int r, const CollBox *b)
{
    for (int i = 0; i < 5; i++) {
        int dx = px16(bx_of(b, i)) - (x >> 16), dy = px16(by_of(b, i)) - (y >> 16);
        if (dy * dy + dx * dx < r * r) return true;
    }
    int32_t x0 = x - r * 0x10000, y0 = y - r * 0x10000, x1 = r * 0x10000 + x, y1 = r * 0x10000 + y;
#define IN(px, py) (g_cc.min_x < (px) && (px) < g_cc.max_x && g_cc.min_y < (py) && (py) < g_cc.max_y)
    return IN(x0, y0) || IN(x1, y0) || IN(x0, y1) || IN(x1, y1) || IN(x, y);
#undef IN
}

/* Coll_CheckSpecialTile 0x40e710: the city's one special face tile (0x4abe6c[style]; in style 2 also
   0xb4) is a solid 8 x 8 pixel post in the middle of the face's edge */
int coll_check_special_tile(int tile, int32_t x, int32_t y, const CollBox *b)
{
    int s = (int16_t)style_requested();
    int8_t t = (int8_t)tile;
    if ((int8_t)exe_table(0x4abe6c + (uint32_t)s, 1)[0] == t || (s == 2 && t == -0x4c)) {
        CollBox *tb = &g_cc.tile_box;
        tb->y[0] = y - 0x40000, tb->x[0] = x - 0x40000, tb->x[1] = x + 0x40000, tb->cx = x;
        tb->y[2] = y + 0x40000, tb->cy = y, tb->angle = 0, tb->z2 = b->z2, tb->id = b->id;
        tb->x[2] = tb->x[1], tb->x[3] = tb->x[0], tb->y[1] = tb->y[0], tb->y[3] = tb->y[2];
        if (coll_box_vs_box(b, tb) == 1) return 1;
    }
    return -1;
}

/* ---- the edge test shared by the map tests ----
   An edge is a block side: kind 1 the vertical line x = X from y = Y to Y + 64, kind 2 the horizontal
   line y = Y from x = X to X + 64 (pixels). A box side crosses it when its two corners lie on
   different sides of the line (bit 15 of the differences) and the crossing point, interpolated in
   16-bit integer maths, is within the edge. The crossing becomes the contact point. The original keeps
   up to 18 edges on its stack (more would overwrite it); the port keeps 64. */
enum { EDGES_MAX = 64 };
typedef struct { int16_t x, y, kind; } Edge;
typedef struct { Edge e[EDGES_MAX]; int n; } Edges;

static void add_edge(Edges *es, int x, int y, int kind)
{
    if (es->n < EDGES_MAX) es->e[es->n++] = (Edge){ (int16_t)x, (int16_t)y, (int16_t)kind };
}

static int16_t test_edges_k(const CollBox *b, const Edges *es)
{
    for (int k = 0; k < es->n; k++) {
        const Edge *e = &es->e[k];
        if (e->kind == 1) {
            int16_t X = e->x, Y = e->y;
            for (int i = 0; i < 4; i++) {
                int j = next_corner(i);
                uint16_t u = (uint16_t)(X - px16(b->x[i]));
                int16_t dx = px16(b->x[i] - b->x[j]), dy = px16(b->y[i] - b->y[j]);
                int16_t t = dx == 0 ? 0 : (int16_t)((dy * (int16_t)u) / dx);
                int yy = px16(b->y[i]) + t;
                if (Y <= yy && yy <= Y + 0x40 && (((X - px16(b->x[j])) ^ u) & 0x8000)) {
                    g_coll_contact[0] = (float)X;
                    g_coll_contact[1] = (float)(px16(b->y[i]) + t);
                    return (int16_t)k;
                }
            }
        } else if (e->kind == 2) {
            int16_t Y = e->y, X = e->x;
            for (int i = 0; i < 4; i++) {
                int j = next_corner(i);
                uint16_t u = (uint16_t)(Y - px16(b->y[i]));
                int16_t dy = px16(b->y[i] - b->y[j]), dx = px16(b->x[i] - b->x[j]);
                int16_t t = dy == 0 ? 0 : (int16_t)((dx * (int16_t)u) / dy);
                int xx = px16(b->x[i]) + t;
                if (X <= xx && xx <= X + 0x40 && (((Y - px16(b->y[j])) ^ u) & 0x8000)) {
                    g_coll_contact[0] = (float)(px16(b->x[i]) + t);
                    g_coll_contact[1] = (float)Y;
                    return (int16_t)k;
                }
            }
        }
    }
    return -1;
}
static int test_edges(const CollBox *b, const Edges *es) { return test_edges_k(b, es) >= 0 ? 10 : -1; }

/* Coll_MapWalls 0x40e7e0: the left (x side) and top (y side) faces of the flat blocks under the bounds
   (at the lowest ground layer) are walls when their tile is one of the level's fence tiles
   (Door_IsFaceSlotUsed); with `special`, the special tile on such a face is a post. */
int coll_map_walls(const CollBox *b, int special)
{
    Edges es = { .n = 0 };
    const Map *m = g_game.map;
    for (int16_t by = (int16_t)(g_cc.min_y >> 22); by <= g_cc.max_y >> 22; by++) {
        for (int16_t bx = (int16_t)(g_cc.min_x >> 22); bx <= g_cc.max_x >> 22; bx++) {
            const MapBlock *blk = map_get_block(m, bx, by, g_cc.layer_lo);
            if (!blk || !(blk->type_map & 0x80)) continue;
            if (blk->left != 0) {
                if (door_is_face_slot_used(blk->left)) add_edge(&es, bx << 6, by << 6, 1);
                else if (special == 1 && coll_check_special_tile(blk->left, bx << 22, by * 0x400000 + 0x200000, b) == 1)
                    return 10;
            }
            if (blk->top != 0) {
                if (door_is_face_slot_used(blk->top)) add_edge(&es, bx << 6, by << 6, 2);
                else if (special == 1 && coll_check_special_tile(blk->top, bx * 0x400000 + 0x200000, by << 22, b) == 1)
                    return 10;
            }
        }
    }
    return es.n > 0 ? test_edges(b, &es) : -1;
}

/* Coll_MapWallsEx 0x40ebe0 */
int coll_map_walls_ex(const CollBox *b, int special, int *obx, int *oby, int *obz)
{
    Edges es = { .n = 0 };
    const Map *m = g_game.map;
    for (int16_t by = (int16_t)(g_cc.min_y >> 22); by <= g_cc.max_y >> 22; by++) {
        for (int16_t bx = (int16_t)(g_cc.min_x >> 22); bx <= g_cc.max_x >> 22; bx++) {
            const MapBlock *blk = map_get_block(m, bx, by, g_cc.layer_lo);
            if (!blk || !(blk->type_map & 0x80)) continue;
            if (blk->left != 0) {
                if (mission_get_byte7711b0(blk->left) == 1) {
                    add_edge(&es, bx << 6, by << 6, 1);
                } else if (special == 1 && coll_check_special_tile(blk->left, bx << 22, by * 0x400000 + 0x200000, b) == 1) {
                    *obx = bx, *oby = by, *obz = g_cc.layer_lo;
                    return 10;
                }
            }
            if (blk->top != 0) {
                if (mission_get_byte7711b0(blk->top) == 1) {
                    add_edge(&es, bx << 6, by << 6, 2);
                } else if (special == 1 && coll_check_special_tile(blk->top, bx * 0x400000 + 0x200000, by << 22, b) == 1) {
                    *obx = bx, *oby = by, *obz = g_cc.layer_lo;
                    return 10;
                }
            }
        }
    }
    if (es.n == 0) return -1;
    int16_t k = test_edges_k(b, &es);
    if (k < 0) return -1;
    *obx = es.e[k].x >> 6, *oby = es.e[k].y >> 6, *obz = g_cc.layer_lo;
    return 10;
}

/* Coll_MapSolid 0x40f060: a corner inside a building block (type 5) at the lowest ground layer gives
   its index (4 for several); otherwise the outer sides of the building blocks under the bounds are
   edges */
int coll_map_solid(const CollBox *b)
{
    int r = -1;
    for (int i = 0; i < 4; i++)
        if ((car_cache_at(((g_cc.layer_lo * 0x100) + (b->y[i] >> 22)) * 0x100 + (b->x[i] >> 22)) & 0x70) == 0x50)
            r = r != -1 ? 4 : i;
    if (r != -1) return r;
    Edges es = { .n = 0 };
    for (int16_t by = (int16_t)(g_cc.min_y >> 22); by <= g_cc.max_y >> 22; by++) {
        int row = (by + g_cc.layer_lo * 0x100) * 0x100;
        for (int16_t bx = (int16_t)(g_cc.min_x >> 22); bx <= g_cc.max_x >> 22; bx++) {
            int idx = bx + row;
            if ((car_cache_at(idx) & 0x70) != 0x50) continue;
            int16_t right = (int16_t)((bx + 1) * 0x40);
            if (right > 0x40 && (car_cache_at(idx - 1) & 0x70) != 0x50) add_edge(&es, bx << 6, by << 6, 1);
            if (right < 0x4000 && (car_cache_at(idx + 1) & 0x70) != 0x50) add_edge(&es, right, by << 6, 1);
            if (by > 0 && (car_cache_at(idx - 0x100) & 0x70) != 0x50) add_edge(&es, bx << 6, by << 6, 2);
            if (by < 0xff && (car_cache_at(idx + 0x100) & 0x70) != 0x50) add_edge(&es, bx << 6, (by + 1) * 0x40, 2);
        }
    }
    return es.n > 0 ? test_edges(b, &es) : -1;
}

/* Coll_MapSlopes 0x40f4c0: around each slope block under the bounds (at its ground layer) the sides
   whose neighbour is no slope are edges, one pixel outside the block, except the sides a slope rises
   from (the low end and, when the box spans layers, the sides of the partial slopes); `bike` also
   lets the 2-block and 1-block slopes' sides be open */
int coll_map_slopes(const CollBox *b, int bike)
{
    Edges es = { .n = 0 };
    const Map *m = g_game.map;
    bool span = g_cc.layer_lo != g_cc.layer_hi;
    for (int16_t by = (int16_t)(g_cc.min_y >> 22); by <= g_cc.max_y >> 22; by++) {
        int16_t ey = (int16_t)(by * 0x40 + 0x41);
        for (int16_t bx = (int16_t)(g_cc.min_x >> 22); bx <= g_cc.max_x >> 22; bx++) {
            int16_t ex = (int16_t)(bx * 0x40 + 0x41);
            int16_t layer = (int16_t)(map_get_ground_z(m, bx * 0x400000 + 0x200000, by * 0x400000 + 0x200000, g_cc.min_gz) >> 22);
            int idx = (layer * 0x100 + by) * 0x100 + bx;
            uint8_t t = car_cache_at(idx);
            if (!(t & 0x80) || !(t & 0x70)) continue;
            bool N = false, S = false, E = false, W = false;
            int slope = (int)(map_get_type_map(m, bx, by, layer) >> 8 & 0x3f);
            if (slope == 9) S = true;
            else if (slope >= 10 && slope <= 0x10) { if (span) N = true; }
            else if (slope == 0x11) N = true;
            else if (slope >= 0x12 && slope <= 0x18) { if (span) S = true; }
            else if (slope == 0x19) E = true;
            else if (slope >= 0x1a && slope <= 0x20) { if (span) W = true; }
            else if (slope == 0x21) W = true;
            else if (slope >= 0x22 && slope <= 0x28) { if (span) E = true; }
            if (bike == 1) {
                switch (slope) {
                case 1: S = true; break;
                case 3: N = true; break;
                case 5: E = true; break;
                case 7: W = true; break;
                case 0x29: S = true; if (span) N = true; break;
                case 2: if (span) N = true; break;
                case 0x2a: N = true; if (span) S = true; break;
                case 4: if (span) S = true; break;
                case 0x2b: E = true; if (span) W = true; break;
                case 6: if (span) W = true; break;
                case 0x2c: W = true; if (span) E = true; break;
                case 8: if (span) E = true; break;
                default: break;
                }
            }
            if (ex > 0x41 && !(car_cache_at(idx - 1) & 0x80) && !W) add_edge(&es, ex - 0x42, by << 6, 1);
            if (ex < 0x4001 && !(car_cache_at(idx + 1) & 0x80) && !E) add_edge(&es, ex, by << 6, 1);
            if (ey > 0x41 && !(car_cache_at(idx - 0x100) & 0x80) && !N) add_edge(&es, bx << 6, ey - 0x42, 2);
            if (ey < 0x4001 && !(car_cache_at(idx + 0x100) & 0x80) && !S) add_edge(&es, bx << 6, ey, 2);
        }
    }
    return es.n > 0 ? test_edges(b, &es) : -1;
}

/* Coll_MapSlopesEx 0x40fac0 */
int coll_map_slopes_ex(const CollBox *b)
{
    Edges es = { .n = 0 };
    const Map *m = g_game.map;
    bool one = g_cc.layer_lo == g_cc.layer_hi;
    for (int16_t by = (int16_t)(g_cc.min_y >> 22); by <= g_cc.max_y >> 22; by++) {
        int16_t ey = (int16_t)(by * 0x40 + 0x41);
        for (int16_t bx = (int16_t)(g_cc.min_x >> 22); bx <= g_cc.max_x >> 22; bx++) {
            int16_t ex = (int16_t)(bx * 0x40 + 0x41);
            int16_t layer = (int16_t)(map_get_ground_z(m, bx * 0x400000 + 0x200000, by * 0x400000 + 0x200000, g_cc.min_gz) >> 22);
            int idx = (layer * 0x100 + by) * 0x100 + bx;
            if (!(car_cache_at(idx) & 0x80)) continue;
            bool E = true, S = true, W = true, N = true;
            switch (map_get_type_map(m, bx, by, layer) >> 8 & 0x3f) {
            case 0x10: if (one) N = false; break;
            case 0x18: if (one) S = false; break;
            case 0x20: if (one) W = false; break;
            case 0x28: if (one) E = false; break;
            default: break;
            }
            if (ex > 0x41 && !(car_cache_at(idx - 1) & 0x80) && !W) add_edge(&es, ex - 0x42, by << 6, 1);
            if (ex < 0x4001 && !(car_cache_at(idx + 1) & 0x80) && !E) add_edge(&es, ex, by << 6, 1);
            if (ey > 0x41 && !(car_cache_at(idx - 0x100) & 0x80) && !N) add_edge(&es, bx << 6, ey - 0x42, 2);
            if (ey < 0x4001 && !(car_cache_at(idx + 0x100) & 0x80) && !S) add_edge(&es, bx << 6, ey, 2);
        }
    }
    return es.n > 0 ? test_edges(b, &es) : -1;
}

/* Coll_MapAll 0x40ffe0 */
int coll_map_all(const CollBox *b, int bike)
{
    g_cc.kind = -1;
    int8_t r = -1, k = (int8_t)coll_map_solid(b);
    if (k != -1) g_cc.kind = 2, r = k;
    k = (int8_t)coll_map_walls(b, 1);
    if (k != -1) {
        bool solid = g_cc.kind == 2;
        g_cc.kind = 4, r = k;
        if (solid) g_cc.kind = 9;
    }
    k = (int8_t)coll_map_slopes(b, bike);
    if (k == -1) return r;
    if (g_cc.kind == 2) g_cc.kind = 10;
    else if (g_cc.kind == 4) g_cc.kind = 0xb;
    else if (g_cc.kind == 9) g_cc.kind = 0xc;
    else g_cc.kind = 3;
    return k;
}

/* ---- the grid ---- */

static CollNode *grid_cell(int idx)
{
    return idx >= 0 && idx < COLL_GRID * COLL_GRID ? (&g_coll_grid[0][0])[idx] : NULL;
}
static int grid_count(int idx)
{
    return idx >= 0 && idx < COLL_GRID * COLL_GRID ? (&g_coll_count[0][0])[idx] : 0;
}

/* the object types a car bumps into (Coll_ShouldCollide; the second list also has type 0) */
static bool solid_obj_type(int t, bool zero_ok)
{
    switch (t) {
    case 0: return zero_ok;
    case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 0x18: case 0x1a: case 0x1b:
    case 0x1c: case 0x28: case 0x44: case 0x45: case 0x4e: case 0x4f: case 0x50: case 0x51: case 0x53:
    case 0x54: case 0x57: case 0x59: case 0x5f: case 0x60: case 0x61: case 0x62: case 99: case 100: case 0x65:
        return true;
    default: return false;
    }
}

/* Coll_ShouldCollide 0x4100b0 (other peds never: the ped code handles them) */
bool coll_should_collide(int kind, const void *self, int other_kind, const void *other)
{
    switch (kind) {
    case 1: case 6: case 10: break;
    case 3: if (!solid_obj_type(((const Obj *)self)->type, false)) return false; break;
    default: return false;
    }
    switch (other_kind) {
    case 3: if (!solid_obj_type(((const Obj *)other)->type, true)) return false; break;
    case 6: case 10: break;
    default: return false;
    }
    if (kind == 6 && other_kind == 6) {
        const Car *a = self, *b = other;
        return (a->model != 0x2f && b->model != 0x2f) || a->model == b->model;
    }
    return true;
}

static CollBox entity_box;                  /* 0x51b890 (coll.c's query box) */

/* Coll_EntityVsBox 0x4348c0: the entity's box (peds 2 x 2, objects their object_info size or 6 x 6,
   cars their pending box, kinds 8 / 10 fixed) against b, after a z window and a circle prefilter
   around the entity's position of twice its half length */
int coll_entity_vs_box(int kind, void *owner, const CollBox *b)
{
    int32_t x = 0, y = 0, z = 0;
    int hw = 0, hh = 0, angle = 0, id = 0;
    const CollBox *eb = NULL;
    switch (kind) {
    case 1: {
        const Ped *p = owner;
        hw = 2, hh = 2, x = p->spr.x, y = p->spr.y, z = p->spr.z, id = 0x10;
        break;
    }
    case 3: {
        const Obj *o = owner;
        const ObjInfo *in = g_obj_infos[o->type];
        z = o->spr.z, x = o->spr.x, y = o->spr.y;
        if (in->status == 0 || in->status == 3 || in->status == 9) hw = in->w >> 17, hh = in->h >> 17;
        else hw = 6, hh = 6;
        angle = o->spr.angle;
        id = (int16_t)((uint32_t)in->depth >> 16);
        break;
    }
    case 6: {
        Car *c = owner;
        x = c->next_x, y = c->next_y, z = c->next_z;
        hw = (uint16_t)c->half_w, hh = (uint16_t)c->half_l, angle = c->next_heading;
        eb = &c->box_saved;
        id = 0x18;
        break;
    }
    case 8: case 10: {
        const Sprite *sp = owner;
        if (kind == 8) hw = 0x17, hh = 0x17;
        else hw = 0x12, hh = 0x3b;
        x = sp->x, y = sp->y, z = sp->z, angle = sp->angle, id = 0x18;
        break;
    }
    default:
        break;   /* (the original tests a box at 0, 0 then) */
    }
    int dz = (z >> 16) - px16(b->z);
    if ((dz < 0 ? -dz : dz) < 0x40 && coll_circle_vs_shape(x, y, hh * 2, b)) {
        if (!eb || eb->x[0] == 0) eb = coll_build_box(x, y, z, hw, hh, angle, id, &entity_box);
        return coll_box_vs_box(b, eb);
    }
    return -1;
}

static void hit_append(const CollNode *n)
{
    CollHit *h = &g_coll_hits[g_cc.nhits];
    h->next = g_coll_hit_head;
    g_coll_hit_head = h;
    h->kind = n->kind;
    h->id = n->id;
    h->owner = n->owner;
}

/* Coll_GatherHits 0x410280: every entity of the 3 x 3 cells around the box's centre that the filter
   lets through and whose box overlaps, into the hit list (by index; the head isn't reset), and the
   kind of what was hit into g_cc.kind */
int coll_gather_hits(const CollBox *b, int kind, const void *self)
{
    int16_t added = 0, last = -1;
    int16_t idx = (int16_t)((b->cy >> 23) * 0x80 + (b->cx >> 23));
    g_cc.nhits = 0;
    for (int s = 0; s < 9; s++) {
        idx = (int16_t)(idx + cell_step(s));
        if (idx <= 0 || idx >= 0x4000) continue;
        g_cc.u501d12 = 0;
        for (CollNode *n = grid_cell(idx); n; n = n->next) {
            if (n->owner == self) continue;
            if (!coll_should_collide(kind, self, n->kind, n->owner)) continue;
            last = (int16_t)coll_entity_vs_box(n->kind, n->owner, b);
            added = g_cc.nhits;
            if (last != 1 || g_cc.nhits >= COLL_HITS) continue;
            hit_append(n);
            switch (n->kind) {
            case 1: if (g_cc.kind == -1) g_cc.kind = 5; break;
            case 3:
                if (((const Obj *)n->owner)->weight == 3) g_cc.kind = 8;
                else if (g_cc.kind == -1) g_cc.kind = 7;
                break;
            case 6: case 10: g_cc.kind = 6; break;
            default: g_cc.kind = n->kind; break;
            }
            added = ++g_cc.nhits;
        }
    }
    return added == 0 ? last : 1;
}

/* Coll_FirstHit 0x410470: the first car, kind 10 or heavy object (weight 3) overlapping */
int coll_first_hit(const CollBox *b, int kind, const void *self)
{
    int16_t r = -1;
    int16_t idx = (int16_t)((b->cy >> 23) * 0x80 + (b->cx >> 23));
    for (int s = 0; s < 9; s++) {
        idx = (int16_t)(idx + cell_step(s));
        if (idx <= 0 || idx >= 0x4000) continue;
        for (CollNode *n = grid_cell(idx); n; ) {
            if (n->owner != self && coll_should_collide(kind, self, n->kind, n->owner)) {
                if ((n->kind == 3 && ((const Obj *)n->owner)->weight == 3) || n->kind == 6 || n->kind == 10)
                    r = (int16_t)coll_entity_vs_box(n->kind, n->owner, b);
            }
            n = n->next;
            if (!n) break;
            if (r != -1) return r;
        }
        if (r != -1) return r;
    }
    return r;
}

/* Coll_AnyObjectAt 0x411040 */
int coll_any_object_at(int32_t x, int32_t y, const CollBox *b)
{
    int16_t r = -1;
    int16_t idx = (int16_t)((int16_t)(y >> 23) * 0x80 + (int16_t)(x >> 23));
    for (int s = 0; s < 9; s++) {
        idx = (int16_t)(idx + cell_step(s));
        if (idx <= 0 || idx >= 0x4000) continue;
        g_cc.u501d12 = 0;
        for (CollNode *n = grid_cell(idx); n; ) {
            switch (n->kind) {
            case 1: case 3: case 6: case 8: case 10: r = (int16_t)coll_entity_vs_box(n->kind, n->owner, b); break;
            default: break;
            }
            n = n->next;
            if (!n || r != -1) break;
        }
        if (r == 1) return 1;
    }
    return r;
}

/* ---- car responses ---- */

/* Coll_CarCarImpulse 0x410560: car b (hit by a) is put back on its current pose and gets an impulse
   from a's motion at the contact point (the point found by the box test, else the midpoint of a's
   next and b's current position); b becomes a physics body */
void coll_car_car_impulse(Car *a, Car *b)
{
    a->hit_car = b->id;
    b->next_y = b->spr.y;
    b->next_z = b->spr.z;
    b->next_x = b->spr.x;
    b->body.cx = (float)px16(b->spr.x);
    b->next_heading = b->spr.angle;
    b->body.fx = 0;
    b->body.cy = (float)px16(b->spr.y);
    b->body.fy = 0;
    b->body.torque = 0;
    phys_sync_from_centre(&b->body);
    b->body.angle = car_heading_to_angle(b->next_heading);
    int bpx = px16(b->spr.x), bpy = px16(b->spr.y);
    float cx, cy;
    if (g_coll_contact[0] == 0.0f) {
        cx = (float)((double)(px16(a->next_x) + bpx) * 0.5f);
        cy = (float)((double)(px16(a->next_y) + bpy) * 0.5f);
    } else {
        cx = (float)(((double)bpx + g_coll_contact[0]) * 0.5f);
        cy = (float)(((double)bpy + g_coll_contact[1]) * 0.5f);
    }
    float m = a->body.mass;
    if (!(m <= 20.0f)) m = 20.0f;
    int dx = px16(a->next_x) - px16(a->spr.x) * 2 + bpx;
    int dy = px16(a->next_y) - px16(a->spr.y) * 2 + bpy;
    float dyf = (float)dy;
    float fx = (float)(((double)dx * m + a->thrust_in) * 0.0238095242530107498f);
    float fy = (float)(((double)dyf * m + a->thrust_in) * 0.0238095242530107498f);
    if (b->owner_status == 99) return;
    carphys_set_impact(a, b, cx, cy, fx, fy);
    if (b->physics) return;
    b->thrust_in = 0;
    b->physics = 1;
    if (b->body.x == 0.0f) {
        b->body.x = (float)px16(b->spr.x);
        b->body.y = (float)px16(b->spr.y);
        b->body.angle = car_heading_to_angle(b->spr.angle);
    }
}

static void play_at(const Car *c, int sample) { Snd_PlayAt(c->spr.x, c->spr.y, c->spr.z, sample); }

/* kick the object along the car's heading at its speed (physics cars: the last distance) */
static void kick(const Car *c, const Obj *o)
{
    obj_kick(0, 0, o->id, c->physics == 1 ? c->speed2 : c->speed, c->next_heading);
}

/* the player whose controlled car is c (Player_GetControlledKind / Id) */
static int player_in_car(const Car *c)
{
    for (int n = player_first(); n > -1; n = player_next(n))
        if (player_get_controlled_kind(n) == 0 && player_get_controlled_id(n) == c->id) return n;
    return -1;
}

/* Coll_ProcessHits 0x4107d0: what a car does to what it touched */
void coll_process_hits(int kind, Car *c)
{
    g_cc.u501d12 = 0;
    for (int16_t i = 0; i < g_cc.nhits; i++) {
        if (kind != 6 && kind != 10) continue;
        CollHit *h = &g_coll_hits[i];
        if (h->kind == 3) {
            Obj *o = h->owner;
            if (o->weight == 3) {
                /* heavy: bounce when moving toward it */
                if (kind != 6 || c->obj_hit != 0) continue;
                int ax = (o->spr.x - c->spr.x) >> 16, ay = (o->spr.y - c->spr.y) >> 16;
                int bx = (o->spr.x - c->next_x) >> 16, by = (o->spr.y - c->next_y) >> 16;
                ax = ax < 0 ? -ax : ax, ay = ay < 0 ? -ay : ay, bx = bx < 0 ? -bx : bx, by = by < 0 ? -by : by;
                if (by * by + bx * bx < ay * ay + ax * ax) {
                    phys_bounce_xy(&c->body);
                    if ((c->speed < 0 ? -c->speed : c->speed) > 1) play_at(c, 9);
                }
                continue;
            }
            switch (o->type) {
            case 0: case 1: case 4: case 5: case 0x45:
                if (kind == 6 && c->speed > 1) {
                    kick(c, o);
                    play_at(c, 0xc);
                }
                break;
            case 2: case 3:
                if (kind != 6) break;
                if (o->speed == 0 && c->speed > 0) {
                    play_at(c, o->type == 2 ? 6 : 7);
                    if (o->type == 3) play_at(c, 0xd);
                }
                if (c->speed > 0) kick(c, o);
                break;
            case 6: case 7: case 8: case 0x18: case 0x34:
                if (kind != 6) break;
                if (c->model != 0x25) {
                    c->body.vx = (float)((double)c->body.vx * 0.9090909090909091);
                    c->body.vy = (float)((double)c->body.vy * 0.9090909090909091);
                }
                if (o->speed == 0 && c->speed > 0) play_at(c, 0xb);
                if (c->speed > 0) kick(c, o);
                break;
            case 0x4e: case 0x4f: case 0x50: case 0x51: case 0x53: case 0x5f: case 0x60: case 0x61: case 0x62:
            case 99: case 100: case 0x65:
                if (kind == 6) {
                    int n = player_in_car(c);
                    if (n > -1) powerup_collect(n, o->spr.x, o->spr.y, 0);
                }
                break;
            case 0x54:
                if (kind == 6 && player_in_car(c) > -1) {
                    powerup_reveal(o->spr.x, o->spr.y);
                    play_at(c, 0xb);
                }
                break;
            case 0x59:
                if (kind == 6) play_at(c, 0xd);
                obj_create(o->spr.x, o->spr.y, o->spr.z, 0x5d, o->spr.angle);
                obj_remove_moving(o->id);
                break;
            default:   /* (0x29 too) */
                if (kind == 6 && c->speed > 0) kick(c, o);
                break;
            }
        } else if (h->kind == 6) {
            if (kind == 6) {
                car_collide_car(h->owner, c);
                coll_car_car_impulse(c, h->owner);
            }
        } else if (h->kind == 10 && kind == 6) {
            phys_bounce_xy(&c->body);
        }
    }
}

/* Car_CollideObjects 0x410dc0: the pending box against the grid; on a hit the car goes back to the
   last free pose (cars, heavy objects) and becomes a physics body. The state +0x149 alternates the
   axis-only retries 1 -> 2 -> 3 -> 1 while it keeps hitting. */
void car_collide_objects(int kind, Car *c)
{
    g_coll_contact[0] = 0;
    g_coll_contact[1] = 0;
    if (kind != 6) return;
    if (c->control == CAR_CTL_PHYSICS) {
        if (c->obj_hit == 2) car_box_move_x(c);
        else if (c->obj_hit == 3) car_box_move_y(c);
    }
    if (coll_gather_hits(&c->box_saved, 6, c) == -1) {
        c->obj_hit = 0;
        c->hit_car = -1;
        return;
    }
    if (c->obj_hit == 0) {
        if ((c->speed < 0 ? -c->speed : c->speed) > 10 && g_cc.kind == 6 && (int16_t)math_random() % 100 > 0x32)
            Snd_PlayScream(c->spr.x, c->spr.y, c->spr.z);
        coll_process_hits(6, c);
        if (g_cc.kind != 8 && g_cc.kind != 6) return;
        if (c->model == 0x25 && g_cc.kind == 6) {
            /* a tank only stops for another tank */
            for (int16_t i = 0; i < g_cc.nhits; i++)
                if (g_coll_hits[i].kind == 6 && ((const Car *)g_coll_hits[i].owner)->model == 0x25) {
                    car_bisect_move(c);
                    c->physics = 1;
                    c->thrust_in = 0;
                    c->obj_hit = 1;
                }
            return;
        }
        car_bisect_move(c);
        if (c->control != CAR_CTL_AI3) {
            if (c->body.mass < 15.0f) {
                c->body.vx = (float)((double)c->body.vx * 0.5f);
                c->body.vy = (float)((double)c->body.vy * 0.5f);
            }
            c->physics = 1;
            c->thrust_in = 0;
            c->obj_hit = 1;
            return;
        }
        if (c->speed < 1) return;
        int16_t hl = c->half_l, s = (int16_t)(c->speed - 1);
        c->speed = s;
        if (hl < s) c->speed = hl;
        if ((float)hl < c->body.vx) c->body.vx = (float)((double)c->body.vx * 0.5f);
        if (!(c->body.vy <= (float)hl)) c->body.vy = (float)((double)c->body.vy * 0.5f);
        return;
    }
    if (g_cc.kind != 8) {
        if (g_cc.kind != 6) {
            c->obj_hit = 0;
            c->hit_car = -1;
            return;
        }
        if (c->model == 0x25) return;
    }
    car_sync_physics(c);
    if (c->obj_hit == 1) c->obj_hit = 2;
    else if (c->obj_hit == 2) c->obj_hit = 3;
    else if (c->obj_hit == 3) c->obj_hit = 1;
}

/* Car_AddCrashDeltas 0x411110: the damage delta of the corner (pair) that hit */
void car_add_crash_deltas(Car *c, int result)
{
    if (c->speed <= 1) return;
    Sprite *sp = &c->spr;
    switch ((int16_t)result) {
    case 0: sprite_add_delta(sp, 3); break;
    case 1: sprite_add_delta(sp, 0); break;
    case 2: sprite_add_delta(sp, 2); break;
    case 3: sprite_add_delta(sp, 4); break;
    case 4: case 10:
        switch (g_cc.corner_pair) {
        case 1: case 0x10: sprite_add_delta(sp, 3); sprite_add_delta(sp, 0); break;
        case 2: case 0x20: sprite_add_delta(sp, 4); sprite_add_delta(sp, 3); break;
        case 0x13: case 0x31: sprite_add_delta(sp, 2); break;
        case 0x23: case 0x32: sprite_add_delta(sp, 5); break;
        default: break;
        }
        break;
    default: break;
    }
}

static int iabs(int v) { return v < 0 ? -v : v; }

/* the impulse divisor of a wall hit: 8 slow, 4 medium, 2 fast (|thrust| + |speed|) */
static void wall_impulse(Car *c)
{
    int s = iabs(x87_ftol(c->thrust_in));
    s += iabs(c->speed);
    int d = s < 3 ? 8 : s < 10 ? 4 : 2;
    double fd = (double)d;
    g_cc.bounce_x = (float)(((double)c->body.x - c->impulse_px) / fd);
    g_cc.bounce_y = (float)(((double)c->body.y - c->impulse_py) / fd);
    c->impulse_x = g_cc.bounce_x;
    c->impulse_y = g_cc.bounce_y;
    if (c->impulse_state == 0) c->impulse_state = 2;
}

/* Car_CollideMap 0x411280 */
void car_collide_map(int kind, Car *c)
{
    g_coll_contact[0] = 0;
    g_coll_contact[1] = 0;
    if (kind != 6) return;
    if (c->control == CAR_CTL_PHYSICS) {
        if (c->map_hit == 2) car_box_move_x(c);
        else if (c->map_hit == 3) car_box_move_y(c);
    }
    int r = (int16_t)coll_map_all(&c->box_saved, c->vtype == 3);
    if (r == -1) goto clear;
    if (c->map_hit != 0) {
        /* still in the wall: retry with only one axis of the move */
        car_sync_physics(c);
        if (c->map_hit == 1) c->map_hit = 2;
        else if (c->map_hit == 2) c->map_hit = 3;
        else if (c->map_hit == 3) c->map_hit = 1;
        goto clear;
    }
    c->physics = 1;
    c->speed = 0;
    c->thrust_in = 0;
    if (g_coll_contact[0] == 0.0f || g_coll_contact[1] == 0.0f) {
        c->impulse_px = (float)px16(c->spr.x);
        c->impulse_py = (float)px16(c->spr.y);
    } else {
        c->impulse_px = g_coll_contact[0];
        c->impulse_py = g_coll_contact[1];
    }
    car_bisect_move(c);
    {
        int h = c->next_heading;
        int lo = (h + 8) & 0xff;   /* (h + 8) mod 256 */
        if (lo < 0x10 || r == 4) {
            /* near an axis, or several corners in a building */
            if (r == 10) { wall_impulse(c); goto hit; }
            int first = -1, second = -1;
            if (g_cc.kind == 2 || g_cc.kind == 3) {
                for (int i = 0; i < 4; i++) {
                    uint8_t t = car_type_cache(c->box_saved.x[i], c->box_saved.y[i], c->box_saved.gz[i] - 1);
                    bool in = g_cc.kind == 2 ? (t & 0x70) == 0x50 : (t & 0x80) != 0;
                    if (!in) continue;
                    if (first == -1) first = i;
                    else second = i;
                }
                if (first != -1 && second != -1) {
                    g_coll_contact[0] = (float)((px16(c->box_saved.x[second]) + px16(c->box_saved.x[first])) >> 1);
                    g_cc.corner_pair = (int16_t)(first * 0x10 + second);
                    g_coll_contact[1] = (float)((px16(c->box_saved.y[first]) + px16(c->box_saved.y[second])) >> 1);
                    wall_impulse(c);
                    goto hit;
                }
            }
            phys_bounce_xy(&c->body);
            goto hit;
        }
        if (r == 10) { wall_impulse(c); goto hit; }
        /* one corner in: bounce off the side that corner faces, push back by |thrust| + |v| */
        int corner = r;
        float ix = 0, iy = 0;
        float vmax = c->body.vx <= c->body.vy ? c->body.vy : c->body.vx;
        int sum = iabs(x87_ftol(c->thrust_in)) + iabs(x87_ftol(vmax));
        switch (((h >> 4) & 0x30) + corner) {
        case 0: case 0x11: case 0x23: case 0x32: ix = (float)-sum; phys_bounce_x(&c->body); break;
        case 1: case 0x13: case 0x22: case 0x30: iy = (float)-sum; phys_bounce_y(&c->body); break;
        case 2: case 0x10: case 0x21: case 0x33: iy = (float)sum; phys_bounce_y(&c->body); break;
        case 3: case 0x12: case 0x20: case 0x31: ix = (float)sum; phys_bounce_x(&c->body); break;
        default: break;
        }
        c->impulse_px = (float)px16(c->box_saved.x[corner]);
        c->impulse_py = (float)px16(c->box_saved.y[corner]);
        c->impulse_x = ix;
        c->impulse_y = iy;
        if (c->impulse_state == 0) c->impulse_state = 2;
    }
hit:
    c->map_hit = 1;
    car_add_crash_deltas(c, r);
    if (iabs(c->speed2) > 1) play_at(c, 9);
    if (c->vtype == 3 && c->status != 7 && c->speed > 0xc) {
        ped_eject_driver(c);
        c->status = 7;
        sprite_set_frame(&c->spr, c->base_frame + 7);
        c->turret = 0;
    }
    return;
clear:
    if (r == 10) {
        wall_impulse(c);
        return;
    }
    if (r != -1) return;
    if (c->map_hit != 0) car_add_damage(c, c->speed >> 2);
    c->map_hit = 0;
}

/* Car_ImpactSpeed 0x411950: the momentum difference along x and y (mass * speed component, integer
   speed components), truncated, summed in 16 bits, / 4 */
double car_impact_speed(const Car *a, const Car *b)
{
    int ay = (math_cos(a->spr.angle) * a->speed) >> 16, by = (math_cos(b->spr.angle) * b->speed) >> 16;
    int v1 = iabs(x87_ftol((double)ay * a->body.mass - (double)by * b->body.mass));
    int ax = (math_sin(a->spr.angle) * a->speed) >> 16, bx = (math_sin(b->spr.angle) * b->speed) >> 16;
    int v2 = iabs(x87_ftol((double)ax * a->body.mass - (double)bx * b->body.mass));
    return (double)(int16_t)((int16_t)(v1 + v2) >> 2);
}

/* the damage delta of a hit from direction d (relative to the heading) */
static int hit_delta(int d)
{
    if (d > 0x380) return 0;
    if (d > 0x280) return 5;
    if (d > 0x200) return 4;
    if (d > 0x180) return 1;
    if (d > 0x80) return 2;
    return 3;
}

/* bikes fall at speed */
static void bike_hit(Car *c, int s)
{
    if (c->vtype != 3 || s <= 6 || c->status == 7) return;
    if (s > 0xc) {
        ped_eject_driver(c);
        c->status = 7;
        sprite_set_frame(&c->spr, c->base_frame + 7);
        c->turret = 0;
        c->input = (int16_t)-carinfo_s16(car_info_of_model(c->model), 0x10);
    }
    car_add_damage(c, 1);
}

/* the damage of an impact for car c from the other car o: impact / (2 * mass), at least 10 from a
   tank, 1 if not positive */
static void impact_damage(Car *c, const Car *o, double s)
{
    if (c->model == 0x25) return;
    double t = s / ((double)c->body.mass + c->body.mass);
    if (o->model == 0x25) { if (t <= 10.0) t = 10.0; }
    else if (t <= 0.0) t = 1.0;
    car_add_damage(c, (int16_t)x87_ftol(t));
    if (c->damage == 100) c->player = (int16_t)player_find_by_ped(o->driver);
}


/* Car_CollideCar 0x411a20: `other` was hit by `mover` (both may swap roles: "m" is normally the mover,
   the other car when it is the faster one or a bus front against a non-front) */
void car_collide_car(Car *other, Car *mover)
{
    if (mover->falling != 0 || other->falling != 0) {
        car_set_damage(mover, 100);
        car_set_damage(other, 100);
        return;
    }
    car_play_crash_sound(mover->speed < other->speed ? other : mover);
    int sp = mover->speed;
    int16_t s = (int16_t)iabs(sp), half = (int16_t)(s >> 1);
    int32_t ox = mover->spr.x - math_sin(mover->spr.angle) * sp, oy = mover->spr.y - math_cos(mover->spr.angle) * sp;
    int16_t d1 = (int16_t)math_atan2(other->spr.y - oy, other->spr.x - ox);
    int16_t d2 = (int16_t)math_atan2(oy - other->spr.y, ox - other->spr.x);
    mover->hit_dir = (int16_t)((d1 - mover->spr.angle) & 0x3ff);
    other->hit_dir = (int16_t)((d2 - other->spr.angle) & 0x3ff);
    Car *m = mover, *o = other;
    if ((other->speed != 0 && iabs(mover->speed) - mover->length < iabs(other->speed) - other->length &&
         other->vtype != 3 && mover->vtype != 1) ||
        (other->vtype == 1 && mover->vtype != 1)) {
        m = other;
        o = mover;
    }
    bike_hit(m, s);
    bike_hit(o, s);
    car_reset_siren99(o);
    if (m->vtype == 3 && s < 7) m->speed = 0;
    if (o->vtype == 3 && s < 7) o->speed = 0;
    m->hit_car = o->id;
    int16_t v = (int16_t)iabs(m->ube);
    if (v == 0) v = (int16_t)iabs(m->speed);
    o->ube = v;
    if (o->control == CAR_CTL_PHYSICS || m->control == CAR_CTL_PHYSICS) {
        double imp = car_impact_speed(m, o);
        if (imp >= 1.0) {
            impact_damage(m, o, imp);
            impact_damage(o, m, imp);
        }
    }
    if (m->control == 0) m->brake = 1;
    if (o->control == 0) o->brake = 1;
    if (o->script_line > 0) car_trig_check_enter(o->id, m->id);
    else if (m->script_line > 0) car_trig_check_enter(m->id, o->id);
    if (m->control == CAR_CTL_PHYSICS) police_report_crime(0, m->id, 2, o->spr.x, o->spr.y, o->spr.z);
    if (o->control == CAR_CTL_PHYSICS) police_report_crime(0, o->id, 2, m->spr.x, m->spr.y, m->spr.z);
    if (m->damage < 100) {
        if (half > 6 && m->model != 0x2f) sprite_add_delta(&m->spr, hit_delta(m->hit_dir));
        if (m->control != CAR_CTL_PHYSICS || m->speed < 0 || m->speed > 0xc) m->speed >>= 1;
    }
    /* the other car: the original returns early (without the speed kick below) for some directions */
    int d = o->hit_dir;
    bool skip = o->damage > 99 || half < 7;
    if (d <= 0x80 || (d > 0x180 && d <= 0x280)) {
        if (skip) return;
        if (o->model != 0x2f) sprite_add_delta(&o->spr, hit_delta(d));
    } else if (!skip && o->model != 0x2f) {
        sprite_add_delta(&o->spr, hit_delta(d));
    }
    if (o->speed > 0) return;
    o->speed = 4;
}

/* Car_PushFromWalls 0x4120a0: at the lowest corners standing in a non-empty block, a small force
   toward an empty neighbour that lies toward the car's position (2 when standing, 1 moving) */
void car_push_from_walls(Car *c)
{
    const CollBox *b = &c->box_saved;
    int32_t lo = min4(b->gz[0], b->gz[1], b->gz[2], b->gz[3]);
    int16_t f = (int16_t)(2 - (c->speed != 0));
    for (int i = 0; i < 4; i++) {
        if (b->gz[i] != lo) continue;
        int16_t by = (int16_t)(b->y[i] >> 22), bx = (int16_t)(b->x[i] >> 22);
        int idx = ((int16_t)((lo - 1) >> 22) * 0x100 + by) * 0x100 + bx;
        if (car_cache_at(idx) == 0) continue;
        float px = (float)px16(b->x[i]), py = (float)px16(b->y[i]);
        if (bx >= 1 && car_cache_at(idx - 1) == 0 && bx > c->next_x >> 22)
            phys_add_force_at_point(&c->body, 0, px, py, (float)-f, 0);
        else if (bx < 0xff && car_cache_at(idx + 1) == 0 && bx < c->next_x >> 22)
            phys_add_force_at_point(&c->body, 0, px, py, (float)f, 0);
        if (by >= 1 && car_cache_at(idx - 0x100) == 0 && by > c->next_y >> 22)
            phys_add_force_at_point(&c->body, 0, px, py, 0, (float)-f);
        else if (by <= 0xfe && car_cache_at(idx + 0x100) == 0 && c->next_y >> 22 > by)
            phys_add_force_at_point(&c->body, 0, px, py, 0, (float)f);
    }
}

/* Car_UpdateGround 0x412310: the ground under the next position; falling (+0x110 counts the frames,
   4 pixels a frame once fast falls are done, 8 damage per frame on flat ground), landing (damage
   doubled on flat ground, deltas, debris objects, sound), water (a splash and sinking), and the draw
   depth key from the lowest corner */
void car_update_ground(Car *c)
{
    const Map *m = g_game.map;
    CollBox *b = &c->box_saved;
    int32_t gz = map_get_ground_z(m, c->next_x, c->next_y, c->next_z - 0x200000);
    c->next_z = gz;
    int slope = -1;
    for (int i = 0; i < 4; i++)
        if (car_type_cache(b->x[i], b->y[i], b->gz[i] - 1) & 0x80) { slope = i; break; }
    if (c->falling == 0 && c->spr.z + 0x200000 < gz) {
        c->falling = 1;
        if (slope != -1) c->spr.z -= 0x100000;
        c->next_z = c->spr.z;
    } else if (c->falling < 1) {
        if ((car_type_cache(c->next_x, c->next_y, gz - 1) & 0x70) == 0x10 && c->sinking == 0 && c->vtype != 0xd) {
            c->sinking = 1;
            Snd_PlayAt(c->next_x, c->next_y, gz, 0x12);
        }
    } else {
        c->falling++;
        int32_t z = c->spr.z;
        if (z < gz) c->next_z = z;
        else if (gz < z - 0x400000 && c->map_hit == 0) {
            phys_bounce_xy(&c->body);
            car_sync_physics(c);
            c->map_hit = 1;
        }
        int32_t z2 = c->next_z;
        if (z2 < g_cc.min_gz - 0x100000) {
            int16_t u = 8;
            if (c->falling < c->speed >> 2) u = slope != -1 ? -4 : 0;
            c->next_z = u * 0x10000 + z2;
            if (slope == -1 && u > 0) car_add_damage(c, u);
            if (c->damage > 0x31) car_set_damage(c, 0x32);
            if (c->next_z > 0x1400000) c->next_z = 0x1400000;
        } else if (iabs((gz - z2) >> 16) < 0x11) {
            int32_t y = c->next_y;
            if ((car_type_cache(c->next_x, y, z2 - 1) & 0x70) == 0x10 && c->sinking == 0 && c->vtype != 0xd) {
                c->sinking = 1;
                Snd_PlayAt(c->next_x, y, z2, 0x12);
                c->falling = 0;
            } else {
                if (slope == -1) car_add_damage(c, c->damage);
                if (c->damage != 100 && c->damage > 0x32)
                    for (int k = 0; k <= 5; k++) sprite_add_delta(&c->spr, k);
                for (int i = 0; i < 4; i++) {
                    int a = (int16_t)math_random() & 0x3ff;
                    obj_create(b->x[i], b->y[i], c->spr.zkey + 1, 0xd, a);
                }
                Snd_PlayAt(c->next_x, c->next_y, c->next_z, 0xe);
                c->falling = 0;
            }
        } else {
            if (g_cc.min_gz < z2 - 0x100000 && c->map_hit == 0) {
                if (c->physics == 0) {
                    c->physics = 1;
                    c->body.vx = (float)((double)(float)px16(c->next_x) - (float)px16(c->spr.x));
                    c->body.vy = (float)((double)(float)px16(c->next_y) - (float)px16(c->spr.y));
                }
                car_sync_physics(c);
                phys_bounce_xy(&c->body);
                c->map_hit = 1;
                c->speed = 0;
            }
            if (iabs(c->speed) < 4 || c->physics == 0) {
                c->physics = 1;
                car_push_from_walls(c);
            }
        }
    }
    g_cc.min_gz = min4(b->gz[0], b->gz[1], b->gz[2], b->gz[3]);
    int32_t lo = g_cc.min_gz;
    c->spr.zkey = lo;
    int k = b->gz[0] == lo ? 0 : b->gz[1] == lo ? 1 : b->gz[2] == lo ? 2 : (b->gz[3] != lo) + 3;
    if ((int8_t)car_type_cache(bx_of(b, k), by_of(b, k), bz_of(b, k) - 1) < 0) {
        uint32_t u7 = (uint32_t)(lo - 0x400000);
        c->spr.zkey = (int32_t)u7;
        uint32_t u6 = u7 - (uint32_t)c->z_offset;
        if (((u6 ^ u7) & 0xffc00000u) == 0) c->spr.zkey = (int32_t)u6;
    } else {
        c->spr.zkey = lo - c->z_offset;
    }
    if (c->spr.zkey < 0) c->spr.zkey = 1;
}

/* ---- the box queries (0x434ac0-0x435a90) ---- */

static int16_t last_cx = -1, last_cy = -1;   /* 0x524950 / 0x524952 */

static void query_lock(void)
{
    if (g_coll_locked) game_fatal(-0xa8, 0x5e, 1);
    g_coll_locked = true;
}
static void query_add(const CollNode *n)
{
    CollHit *h = &g_coll_hits[g_coll_hit_count++];
    h->next = g_coll_hit_head;
    g_coll_hit_head = h;
    h->kind = n->kind;
    h->id = n->id;
    h->owner = n->owner;
}
static bool in_world(int32_t x, int32_t y) { return x >= 0 && x < 0x40000000 && y >= 0 && y < 0x40000000; }
static bool kind_ok(int nk, int kind) { return nk == kind || kind == 0 || kind == 0x11 || kind == 0x12; }
/* objects the box queries see: not status 1 / 5, not owned; not the caller unless kind 0x12 and type 0xe */
static bool obj_ok(const Obj *o, int kind, int self_kind, int self_id)
{
    uint8_t st = g_obj_infos[o->type]->status;
    return st != 1 && st != 5 && o->owner == -1 &&
           (self_kind != 3 || (int16_t)self_id != o->id || (kind == 0x12 && o->type == 0xe));
}
/* a car hit records the direction of the hit (from the query box's first corner) */
static void car_hit_dir(Car *c, int32_t x, int32_t y)
{
    c->hit_dir = (int16_t)((math_atan2(y - c->spr.y, x - c->spr.x) - c->spr.angle) & 0x3ff);
}

/* Coll_QueryCellBox 0x434ac0 (skips the cell of the previous call) */
static void query_cell_box(int32_t x, int32_t y, const CollBox *b, int kind, int self_kind, int self_id)
{
    if ((last_cx == x >> 23 && last_cy == y >> 23) || !in_world(x, y)) return;
    last_cy = (int16_t)(y >> 23), last_cx = (int16_t)(x >> 23);
    for (CollNode *n = g_coll_grid[last_cy][last_cx]; n; n = n->next) {
        if (!kind_ok(n->kind, kind)) continue;
        if (n->kind == 1 && kind != 0x12) {
            if ((self_kind != 1 || (int16_t)self_id != ((const Ped *)n->owner)->id) && kind != 0x11 &&
                coll_entity_vs_box(1, n->owner, b) != -1 && g_coll_hit_count < COLL_HITS)
                query_add(n);
        } else if (n->kind == 3) {
            if (obj_ok(n->owner, kind, self_kind, self_id) && coll_entity_vs_box(3, n->owner, b) != -1 &&
                g_coll_hit_count < COLL_HITS)
                query_add(n);
        } else if (n->kind == 6 && kind != 0x12) {
            Car *c = n->owner;
            bool driven = false;
            if (c->control == 2) {
                const Ped *p = ped_get((int16_t)self_id);
                driven = p->car == c->id && p->control == 2;
            }
            if (!driven && (self_kind != 6 || (int16_t)self_id != c->id) && c->model != 0x2f &&
                coll_entity_vs_box(6, c, b) != -1 && g_coll_hit_count < COLL_HITS) {
                query_add(n);
                car_hit_dir(c, b->x[0], b->y[0]);
            }
        } else if (n->kind == 10 && kind != 0) {
            if ((self_kind != 10 || (int16_t)self_id != n->id) && coll_entity_vs_box(10, n->owner, b) != -1 &&
                g_coll_hit_count < COLL_HITS)
                query_add(n);
        } else if (n->kind == 8 && kind != 0x12) {
            if ((self_kind != 8 || (int16_t)self_id != n->id) && coll_entity_vs_box(8, n->owner, b) != -1 &&
                g_coll_hit_count < COLL_HITS)
                query_add(n);
        }
    }
}

/* Coll_QueryBox 0x434e80: the nine points 100 pixels apart around the midpoint of corners 0 and 3 */
CollHit *coll_query_box(const CollBox *b, int kind, int self_kind, int self_id)
{
    last_cx = last_cy = -1;
    int32_t cx = ((b->x[0] - b->x[3]) >> 1) + b->x[3], cy = ((b->y[0] - b->y[3]) >> 1) + b->y[3];
    query_lock();
    g_coll_hit_head = NULL;
    g_coll_hit_count = 0;
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++) query_cell_box(cx + i * 0x640000, cy + j * 0x640000, b, kind, self_kind, self_id);
    return g_coll_hit_head;
}

/* Coll_QueryCellCarBox 0x435170: against the car's pending box (no previous-cell skip: a cell can be
   walked more than once and its entities listed again) */
static void query_cell_car_box(int32_t x, int32_t y, Car *self, int kind, int exclude)
{
    if (!in_world(x, y)) return;
    last_cy = (int16_t)(y >> 23), last_cx = (int16_t)(x >> 23);
    const CollBox *b = &self->box_saved;
    for (CollNode *n = g_coll_grid[last_cy][last_cx]; n; n = n->next) {
        if (!kind_ok(n->kind, kind)) continue;
        if (n->kind == 1 && kind != 0x12) {
            if (kind != 0x11 && coll_entity_vs_box(1, n->owner, b) != -1 && g_coll_hit_count < COLL_HITS) query_add(n);
        } else if (n->kind == 3) {
            const Obj *o = n->owner;
            uint8_t st = g_obj_infos[o->type]->status;
            if (st != 1 && st != 5 && o->owner == -1 && ((int16_t)exclude != o->id || (kind == 0x12 && o->type == 0xe)) &&
                coll_entity_vs_box(3, n->owner, b) != -1 && g_coll_hit_count < COLL_HITS)
                query_add(n);
        } else if (n->kind == 6 && kind != 0x12) {
            Car *c = n->owner;
            if ((int16_t)exclude != c->id && c->model != 0x2f && coll_entity_vs_box(6, c, b) != -1 &&
                g_coll_hit_count < COLL_HITS) {
                query_add(n);
                car_hit_dir(c, self->spr.x, self->spr.y);
            }
        } else if (n->kind == 10 && kind != 0) {
            if (coll_entity_vs_box(10, n->owner, b) != -1 && g_coll_hit_count < COLL_HITS) query_add(n);
        } else if (n->kind == 8 && kind != 0x12) {
            if (coll_entity_vs_box(8, n->owner, b) != -1 && g_coll_hit_count < COLL_HITS) query_add(n);
        }
    }
}

/* Coll_QueryCarBox 0x4354b0: the cell counts tested are the 3 x 3 neighbours of (x, y)'s cell, the
   points walked 100 pixels apart (which can be the same cell) */
CollHit *coll_query_car_box(Car *c, int32_t x, int32_t y, int kind, int exclude)
{
    g_coll_hit_head = NULL;
    query_lock();
    int idx = (y >> 23) * 0x80 + (x >> 23);
    g_coll_hit_count = 0;
    last_cx = last_cy = -1;
    static const int steps[9] = { -0x81, -0x80, -0x7f, -1, 0, 1, 0x7f, 0x80, 0x81 };
    for (int k = 0; k < 9; k++)
        if (grid_count(idx + steps[k]))
            query_cell_car_box(x + (k % 3 - 1) * 0x640000, y + (k / 3 - 1) * 0x640000, c, kind, exclude);
    return g_coll_hit_head;
}

/* Coll_QueryCellBoxFirst 0x4356a0: stops at the first entity (peds only within 8 + extent pixels of
   (px, py) first) */
static void query_cell_box_first(int32_t x, int32_t y, const CollBox *b, int kind, int self_kind, int self_id,
                                 int extent, int32_t px, int32_t py)
{
    if ((last_cx == x >> 23 && last_cy == y >> 23) || !in_world(x, y)) return;
    last_cy = (int16_t)(y >> 23), last_cx = (int16_t)(x >> 23);
    for (CollNode *n = g_coll_grid[last_cy][last_cx]; n; n = n->next) {
        if (!kind_ok(n->kind, kind)) continue;
        if (n->kind == 1 && kind != 0x12) {
            const Ped *p = n->owner;
            int r = ((int16_t)extent + 8) * 0x10000;
            if ((self_kind != 1 || (int16_t)self_id != p->id) && kind != 0x11 && px < p->spr.x + r &&
                p->spr.x - r < px && py < p->spr.y + r && p->spr.y - r < py && coll_entity_vs_box(1, n->owner, b) != -1 &&
                g_coll_hit_count < COLL_HITS) {
                query_add(n);
                return;
            }
        } else if (n->kind == 3) {
            if (obj_ok(n->owner, kind, self_kind, self_id) && coll_entity_vs_box(3, n->owner, b) != -1 &&
                g_coll_hit_count < COLL_HITS) {
                query_add(n);
                return;
            }
        } else if (n->kind == 6 && kind != 0x12) {
            Car *c = n->owner;
            if ((self_kind != 6 || (int16_t)self_id != c->id) && (c->model != 0x2f || self_kind == 3) &&
                coll_entity_vs_box(6, c, b) != -1 && g_coll_hit_count < COLL_HITS) {
                query_add(n);
                car_hit_dir(c, b->x[0], b->y[0]);
                return;
            }
        } else if (n->kind == 10 && kind != 0) {
            if ((self_kind != 10 || (int16_t)self_id != n->id) && coll_entity_vs_box(10, n->owner, b) != -1 &&
                g_coll_hit_count < COLL_HITS) {
                query_add(n);
                return;
            }
        } else if (n->kind == 8 && kind != 0x12) {
            if ((self_kind != 8 || (int16_t)self_id != n->id) && coll_entity_vs_box(8, n->owner, b) != -1 &&
                g_coll_hit_count < COLL_HITS) {
                query_add(n);
                return;
            }
        }
    }
}

/* Coll_QueryBoxFirst 0x435a90: the centre cell first, then the eight around it, until one hit */
CollHit *coll_query_box_first(const CollBox *b, int kind, int self_kind, int self_id)
{
    last_cx = last_cy = -1;
    int32_t cx = ((b->x[0] - b->x[3]) >> 1) + b->x[3], cy = ((b->y[0] - b->y[3]) >> 1) + b->y[3];
    int d = cx - b->x[0];
    int extent = ((d >> 16) ^ (d >> 31)) - (d >> 31);
    query_lock();
    g_coll_hit_head = NULL;
    g_coll_hit_count = 0;
    int16_t gy = (int16_t)(cy >> 23), gx = (int16_t)(cx >> 23);
    static const int8_t order[9][2] = { { 0, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 }, { 1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 } };
    for (int k = 0; k < 9; k++) {
        int idx = (int16_t)(gy + order[k][1]) * 0x80 + (int16_t)(gx + order[k][0]);
        if (grid_count(idx) == 0) continue;
        query_cell_box_first(cx + order[k][0] * 0x640000, cy + order[k][1] * 0x640000, b, kind, self_kind, self_id,
                             extent, cx, cy);
        if (g_coll_hit_head) break;
    }
    return g_coll_hit_head;
}
