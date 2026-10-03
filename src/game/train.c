/* Trains 0x46a9a0-0x46e81f and the train helpers of the car module (train.h, docs/trains.md). */
#include "train.h"
#include "../exe.h"
#include "../map.h"
#include "car.h"
#include "carcoll.h"
#include "carinfo.h"
#include "coll.h"
#include "game.h"
#include "gmath.h"
#include "lights.h"
#include "obj.h"
#include "ped.h"
#include "ped_internal.h"
#include "player.h"
#include "rail.h"
#include "stubs.h"
#include <stdlib.h>
#include <string.h>

Train g_trains[TRAIN_MAX];                  /* 0x7514a8 (bss: kept from the last level where not set) */
uint8_t g_train_count;                      /* 0x75377b */
static uint8_t trains_on;                   /* 0x75377a */
static uint8_t tables_built;                /* 0x75377f (once per run) */
/* (0x751458, set to 1 by Train_CreateFromMap, and 0x75377c-0x75377e, cleared by it, are never read:
   not kept) */

/* The curve table 0x4b22e8: 9 rows of 0x5a steps {u8 along, u8 across, s16 angle}. Row 0 is straight
   (along 0..63, across 31), 1 / 2 a corner left / right (0x31 steps), 3-6 the two ends of a curve
   (0x46 steps), 7 / 8 its middle (10 steps). The positions are initialised data; the angles and the
   mirrored rows are built by Train_BuildCurveTables. The port keeps a zero tail past row 8 (the
   original would read on into the next table). */
typedef struct { uint8_t a, b; int16_t angle; } CurveStep;
static CurveStep curve_tab[TRAIN_CURVES * TRAIN_CURVE_STEPS + 0x100];
#define CURVE(r, i) curve_tab[(r) * TRAIN_CURVE_STEPS + (i)]
/* 0x4b22e0: the headings of the four axes as the curve rows use them, {-y, +y, -x, +x} = 0x200,
   0x400, 0x300, 0x100 (initialised data); Train_Reverse swaps them for every train at once, and they
   are never reset. */
static int16_t axis_angle[4];

static CollBox hits_box;                    /* 0x751460 */
static CollBox proj_box;                    /* 0x4be128 */
static TrainNear near_res;                  /* 0x753758 */
static TrainBoardInfo board_info;           /* 0x75375c */
static TrainDoorOffsets door_ofs;           /* 0x753770 */

static inline uint8_t type_cache_at(int32_t x, int32_t y, int32_t z)
{
    int bx = x >> 22, by = y >> 22, bz = z >> 22;
    if (bx < 0 || bx > 255 || by < 0 || by > 255 || bz < 0 || bz > 5) return 0;   /* (port: outside the cache) */
    return g_game.map->type_cache[bz][by][bx];
}
static int ncars(const Train *t) { return t->kind == TRAIN_KIND_FOUR ? 4 : 1; }

uint8_t *train_get(int i) { return (uint8_t *)&g_trains[(int16_t)i]; }    /* Train_Get 0x46e7a0 */
int train_get_count(void) { return g_train_count; }                       /* Train_GetCount 0x46e7c0 */
const TrainBoardInfo *train_get_board_info(void) { return &board_info; }

int train_curve_entry(int curve, int step, uint8_t *a, uint8_t *b)
{
    const CurveStep *e = &CURVE(curve, step);
    if (a) *a = e->a;
    if (b) *b = e->b;
    return e->angle;
}

/* Train_BuildCurveTables 0x46bc30 (after loading the initialised data from the exe): row 1's angles
   are i * 256 / 48 (truncated: __ftol), row 2 mirrors row 1 (across 63 - b, angle negated); rows 3 / 4
   likewise with i * 256 / 150 over 70 steps; rows 5 / 6 run row 3 backwards (5 mirrored across);
   rows 7 / 8 continue rows 3 / 4 with i = 70..79. The constants are the exe's doubles 1/48 (0x4a8c50),
   1/150 (0x4a8c40) and 256 (0x4a8c48). */
static double exe_double(uint32_t va)
{
    const uint8_t *p = exe_data(va, 8);
    double d = 0;
    if (p) memcpy(&d, p, 8);
    return d;
}
static void train_build_curve_tables(void)
{
    const uint8_t *src = exe_data(0x4b22e8, TRAIN_CURVES * TRAIN_CURVE_STEPS * 4);
    for (int i = 0; src && i < TRAIN_CURVES * TRAIN_CURVE_STEPS; i++) {
        curve_tab[i].a = src[i * 4];
        curve_tab[i].b = src[i * 4 + 1];
        curve_tab[i].angle = (int16_t)(src[i * 4 + 2] | src[i * 4 + 3] << 8);
    }
    for (int i = 0; i < 4; i++) axis_angle[i] = (int16_t)exe_u16(0x4b22e0 + 2u * (unsigned)i);
    double k48 = exe_double(0x4a8c50), k150 = exe_double(0x4a8c40), k256 = exe_double(0x4a8c48);
    for (int i = 0; i < 0x31; i++) {
        int16_t v = (int16_t)(int32_t)((double)i * k48 * k256);
        CURVE(1, i).angle = v;
        CURVE(2, i).angle = (int16_t)-v;
        CURVE(2, i).b = (uint8_t)(0x3f - CURVE(1, i).b);
    }
    for (int i = 0; i < 0x46; i++) {
        int16_t v = (int16_t)(int32_t)((double)i * k150 * k256);
        CURVE(3, i).angle = v;
        CURVE(4, i).angle = (int16_t)-v;
        CURVE(4, i).b = (uint8_t)(0x3f - CURVE(3, i).b);
    }
    for (int i = 0; i < 0x46; i++) {
        uint8_t b5 = CURVE(5, i).b;
        CURVE(5, i).b = (uint8_t)(0x3f - b5);
        CURVE(6, i).b = b5;
        int16_t a = CURVE(3, 0x45 - i).angle;
        CURVE(5, i).angle = (int16_t)-a;
        CURVE(6, i).angle = a;
    }
    for (int i = 0x46; i < 0x50; i++) {
        int16_t v = (int16_t)(int32_t)((double)i * k150 * k256);
        CURVE(7, i - 0x46).angle = v;
        CURVE(8, i - 0x46).angle = (int16_t)-v;
    }
}

/* ---- level start ---- */

/* Train_CreateCarriage 0x46b510: carriage c of train t on the railway running in `dir` from the
   start block at pixels (px, py), layer bottom pz. The carriages of a four-carriage train lie
   0x7e pixels apart along the track, carriage c at (c - 2) * 0x7e from the start block's edge; the
   two bogies 0x14 pixels in from the carriage's ends, 0x20 across (the middle of the block), 4 pixels
   over the layer's bottom. The sub-step of each bogie is its position in the block in the direction
   of travel. The two end bogies of the train get the end frames (front: train sprite 4, rear: 3), the
   others frame 2; the body is frame 0 (1 for the front carriage), turned 0x200 for directions 6 / 9.
   A four-carriage train's carriages get two door objects (type 0xe, 0x11 pixels either side, state 8:
   shut). The case analysis follows the disassembly: the decompiler shows the switches on the
   direction with shifted labels. */
static int train_create_carriage(int t, int c, int dir, int16_t px, int16_t py, int16_t pz)
{
    Train *T = &g_trains[t];
    TrainCar *k = &T->car[c];
    k->crashed = 0;
    uint32_t angle = 0;
    switch (dir) {
    case 3: angle = (uint16_t)axis_angle[3]; k->orient = 0, k->backwards = 1; break;
    case 6: angle = (uint16_t)axis_angle[2]; k->orient = 1, k->backwards = 2; break;   /* (the dword 0x4b22e4) */
    case 9: angle = (uint16_t)axis_angle[0]; k->orient = 1, k->backwards = 2; break;   /* (the dword 0x4b22e0) */
    case 12: angle = (uint16_t)axis_angle[1]; k->orient = 0, k->backwards = 1; break;
    default: break;
    }
    int n = 0, hd = 0, sp = 0;
    if (T->kind == TRAIN_KIND_FOUR) n = 4, hd = 0x14, sp = 0x7e;
    else if (T->kind == TRAIN_KIND_SINGLE) n = 1, hd = 0x1e, sp = 0x7e;
    else game_fatal(-0x11a, 0x1b8, T->kind);
    int base = sprite_group_base(SPRITE_GROUP_TRAIN);
    k->state = TRAIN_CAR_RUNNING;
    k->hits = 10;
    int f0 = base + 2, f1 = base + 2;
    if (T->kind == TRAIN_KIND_FOUR) {
        if (T->front_car == c) {
            if (T->front_bogie == 0) f0 = base + 4;
            else if (T->front_bogie == 1) f1 = base + 4;
        }
        if (T->rear_car == c) {
            if (T->rear_bogie == 0) f0 = base + 3;
            else if (T->rear_bogie == 1) f1 = base + 3;
        }
    }
    TrainBogie *b0 = &k->bogie[0], *b1 = &k->bogie[1];
    int id = (c + t * 4) * 2;
    int32_t z = (pz - 4) << 16;
    int a = (int)(angle & 0x3ff);
    if (dir == 9 || dir == 12 || dir == 3 || dir == 6) {
        bool along_y = dir == 9 || dir == 12;
        int16_t along = (int16_t)((along_y ? py : px) + ((c & 0xff) - (n >> 1)) * sp);
        int32_t across = ((along_y ? px : py) + 0x20) << 16;
        int32_t p0 = (along + hd) << 16, p1 = ((sp - hd) + along) << 16;
        if (along_y) sprite_init(&b0->spr, across, p0, z, a, f0);
        else sprite_init(&b0->spr, p0, across, z, a, f0);
        coll_insert(COLL_KIND8, (int16_t)id, &b0->spr, b0->spr.unk20, b0->spr.x, b0->spr.y);
        b0->in_grid = 1;
        if (along_y) sprite_init(&b1->spr, across, p1, z, a, f1);
        else sprite_init(&b1->spr, p1, across, z, a, f1);
        coll_insert(COLL_KIND8, (int16_t)(id + 1), &b1->spr, b1->spr.unk20, b1->spr.x, b1->spr.y);
        b1->in_grid = 1;
        int16_t c0 = (int16_t)((along_y ? b0->spr.y : b0->spr.x) >> 16) % 64;
        int16_t c1 = (int16_t)((along_y ? b1->spr.y : b1->spr.x) >> 16) % 64;
        if (dir == 3 || dir == 12) b0->sub = (uint8_t)c0, b1->sub = (uint8_t)c1;
        else b0->sub = (uint8_t)(0x3f - c0), b1->sub = (uint8_t)(0x3f - c1);
    }
    for (int i = 0; i < 2; i++) {
        TrainBogie *g = &k->bogie[i];
        g->len = 0x40;
        g->bx = (uint8_t)((uint16_t)(g->spr.x >> 16) >> 6);
        g->by = (uint8_t)((uint16_t)(g->spr.y >> 16) >> 6);
        g->bz = (uint8_t)((uint16_t)(g->spr.z >> 16) >> 6);
        g->piece = g->prev = (uint8_t)dir;
        g->slope_dir = 1;
        g->slope_h = g->on_slope = g->curve = g->in_bend = 0;
        g->id = (uint8_t)(id + i);
        g->blocks = 0;
        k->door[i].open = 0;
        k->door[i].u0d = 0;
        k->door[i].obj = -1;
    }
    int32_t cx = (b0->spr.x >> 16 << 16) + ((b1->spr.x - b0->spr.x) >> 1 & (int32_t)0xffff0000);
    int32_t cy = (b0->spr.y >> 16 << 16) + ((b1->spr.y - b0->spr.y) >> 1 & (int32_t)0xffff0000);
    int32_t cz = b0->spr.z - 0x10000;
    int h = math_atan2(b1->spr.y - b0->spr.y, b1->spr.x - b0->spr.x);
    /* (the original stores the low byte of each coordinate >> 6, not the block: 0 for whole pixels) */
    k->bx = (uint8_t)(cx >> 6), k->by = (uint8_t)(cy >> 6), k->bz = (uint8_t)(cz >> 6);
    k->id = (uint8_t)(t * 4 + c);
    int turn = k->backwards == 2 ? 0x200 : 0;
    if (T->kind == TRAIN_KIND_FOUR) {
        int frame = base + (T->front_car == c ? 1 : 0);
        sprite_init(&k->spr, cx, cy, cz, (turn + h) & 0x3ff, frame);
        coll_insert(COLL_KIND10, k->id, &k->spr, k->spr.unk20, cx, cy);
        for (int i = 0; i < 2; i++) {
            int o = (int16_t)obj_create_attached(k->id, 2, i == 0 ? 0x11 : -0x12, 0, 0xe);
            k->door[i].obj = (int16_t)o;
            if (o != -1) {
                k->door[i].o = obj_get(o);
                obj_set_state(o, 8);
            }
        }
    } else {
        sprite_init(&k->spr, cx, cy, cz, (turn + h) & 0x3ff, sprite_group_base(SPRITE_GROUP_TRAM));
        coll_insert(COLL_KIND10, k->id, &k->spr, k->spr.unk20, cx, cy);
    }
    return 0x14;
}

/* Train_CreateFromMap 0x46b240: one train per start the rail tracer found. The kind comes from the
   start block: road types (Map_TestBlockAttr 3) would make a single unit, anything else four
   carriages, but a single unit is left uncreated (the disassembly jumps over all of it, 0x46b2f7): its
   record only gets the kind, and Train_Update skips it. A train starts at its station (occupying it),
   in the station sequence at step 0 (it leaves at once), speed 0, direction 6, not ridden, doors shut
   (frame 8), top speed 0x3c. Directions 3 / 12 make carriage 3 the front (its bogie 1 leading), 6 / 9
   carriage 0 (bogie 0). */
int train_create_from_map(void)
{
    const RailInfo *in = rail_get_info();
    g_train_count = in->count;
    for (int t = 0; t < g_train_count && t < TRAIN_MAX; t++) {
        Train *T = &g_trains[t];
        int dir = in->dir[t];
        int16_t px = (int16_t)(in->x[t] << 6), py = (int16_t)(in->y[t] << 6);
        int z = in->z[t];
        T->kind = map_test_block_attr(3, in->x[t], in->y[t], z) ? TRAIN_KIND_SINGLE : TRAIN_KIND_FOUR;
        if (T->kind == TRAIN_KIND_SINGLE) continue;
        switch (dir) {
        case 3: case 12: T->front_car = 3, T->rear_car = 0, T->rear_bogie = 0, T->front_bogie = 1; break;
        case 6: case 9: T->front_car = 0, T->rear_car = 3, T->front_bogie = 0, T->rear_bogie = 1; break;
        default: break;
        }
        T->station_x = in->x[t], T->station_y = in->y[t];
        T->cross_x = T->cross_y = 0xff;
        T->switch_x = T->switch_y = 0xff;
        T->u1f = 0;
        T->station = in->station[t];
        T->u20 = 0;
        rail_toggle_station(T->station);
        T->state = TRAIN_ST_STATION;
        T->sub = 0;
        T->passengers = 0;
        T->u08 = 0;
        T->speed = 0;
        T->dir = 6;
        T->boarded = 1;
        T->door_frame = 8;
        T->doors_open = 0;
        T->timer = 0;
        T->ped_tick = 0;
        T->max_speed = 0x3c;
        for (int c = 0; c < 4; c++) train_create_carriage(t, c, dir, px, py, (int16_t)((z + 1) * 0x40));
    }
    return 0x14;
}

/* Train_InitAll 0x46a9a0: the curve tables once per run, then the trains; on (0x75377a) when that
   reports 0x14 (it always does) */
void train_init_all(void)
{
    trains_on = 0;
    if (!tables_built) {
        train_build_curve_tables();
        tables_built = 1;
    }
    if (train_create_from_map() == 0x14) trains_on = 1;
}

/* ---- following the railway ---- */

/* Train_BogieStep 0x46c460: into the next block by the piece (a step down sets the slope state 2) */
static void train_bogie_step(TrainBogie *g)
{
    switch (g->piece) {
    case 2: case 5: case 8: case 11: g->slope_dir = 2; break;
    default: break;
    }
    rail_step(g->piece, &g->bx, &g->by, &g->bz);
}

/* Train_BogieNextPiece 0x46c5d0: the piece the bogie came by becomes prev; the next one is probed
   from the new block like the tracer does (rail_probes), each hit also setting the curve (corner rows
   1 / 2 over 0x31 steps, straight row 0 over 0x40); a down piece (2, 5, 8, 0xb) restarts the block
   count at 9. 0x17 when no neighbour answers (the piece is then the reverse one, curve unchanged). */
static int train_bogie_next_piece(TrainBogie *g)
{
    int p = g->piece;
    if (p == 2 || p == 5 || p == 8 || p == 11) g->blocks = 9;
    g->prev = g->piece;
    if (p < 1 || p > 12) return 0;
    int grp = (p - 1) / 3;
    const RailProbe *pr = rail_probes[grp];
    g->piece = rail_reverse_dir[grp];
    for (int k = 0; k < 9; k++)
        if (map_test_block_attr(1, g->bx + pr[k].dx, g->by + pr[k].dy, g->bz + pr[k].dz)) {
            g->piece = pr[k].dir;
            g->curve = pr[k].curve;
            g->len = pr[k].curve ? 0x31 : 0x40;
        }
    return g->piece == rail_reverse_dir[grp] ? 0x17 : 0;
}

/* Train_BogieComputePos 0x46cdb0: on a slope the height follows the step (up: 8 pixels a block from
   60 over the layer's bottom; down from 53); then the position in the block from the curve row at
   the sub-step, turned to the direction it came in by, and the heading from that axis' angle minus
   the step's. */
static void train_bogie_compute_pos(TrainBogie *g)
{
    if (g->on_slope) {
        if (g->slope_dir == 1)
            g->spr.z = (int32_t)(((uint32_t)g->bz * 0x40 - (uint32_t)(g->sub >> 3) - g->slope_h + 0x3c) * 0x10000u);
        else if (g->slope_dir == 2)
            g->spr.z = (int32_t)((((uint32_t)g->sub & 0xfffffff8u) + 0x1a8 + ((uint32_t)g->bz * 0x40 - g->slope_h) * 8) * 0x2000u);
    }
    const CurveStep *e = &curve_tab[(g->sub + g->curve * TRAIN_CURVE_STEPS) % (int)(sizeof curve_tab / sizeof curve_tab[0])];
    uint32_t bx = (uint32_t)g->bx * 0x40, by = (uint32_t)g->by * 0x40;
    int16_t k;
    switch (g->prev) {
    case 1: case 2: case 3:
        g->spr.x = (int32_t)((bx + e->a) << 16), g->spr.y = (int32_t)((by + e->b) << 16), k = axis_angle[3];
        break;
    case 4: case 5: case 6:
        g->spr.x = (int32_t)((bx - e->a + 0x3f) << 16), g->spr.y = (int32_t)((by - e->b + 0x3f) << 16), k = axis_angle[2];
        break;
    case 7: case 8: case 9:
        g->spr.x = (int32_t)((bx + e->b) << 16), g->spr.y = (int32_t)((by - e->a + 0x3f) << 16), k = axis_angle[0];
        break;
    case 10: case 11: case 12:
        g->spr.x = (int32_t)((bx - e->b + 0x3f) << 16), g->spr.y = (int32_t)((by + e->a) << 16), k = axis_angle[1];
        break;
    default: return;
    }
    g->spr.angle = (int16_t)((k - e->angle) & 0x3ff);
}

/* Train_CarriageUpdateCentre 0x46d050: the body halfway between the bogies (whole pixels), a pixel
   over the lower of them when level (the depth key of the one with the smaller key), else halfway and
   the key of the higher one; heading along the bogies (+0x200 for a backwards body). The grid node
   moves when the centre's block differs from the one stored. */
static void train_carriage_update_centre(TrainCar *k)
{
    TrainBogie *b0 = &k->bogie[0], *b1 = &k->bogie[1];
    int32_t cx = (b0->spr.x >> 16 << 16) + ((b1->spr.x - b0->spr.x) >> 1 & (int32_t)0xffff0000);
    int32_t cy = (b0->spr.y >> 16 << 16) + ((b1->spr.y - b0->spr.y) >> 1 & (int32_t)0xffff0000);
    int32_t z0 = b0->spr.z, z1 = b1->spr.z;
    if (z0 == z1) {
        k->spr.z = z0 - 0x10000;
        k->spr.zkey = (b1->spr.zkey <= b0->spr.zkey ? b1->spr.zkey : b0->spr.zkey) - 0x10000;
    } else if (z0 < z1) {
        k->spr.z = z1 - ((z1 - z0) >> 1 & (int32_t)0xffff0000);
        k->spr.zkey = b0->spr.zkey - 0x10000;
    } else {
        k->spr.z = z0 - ((z0 - z1) >> 1 & (int32_t)0xffff0000);
        k->spr.zkey = b1->spr.zkey - 0x10000;
    }
    int a = math_atan2(b1->spr.y - b0->spr.y, b1->spr.x - b0->spr.x);
    if (k->backwards == 2) a -= 0x200;
    k->spr.angle = (int16_t)(a & 0x3ff);
    if (cx >> 22 == k->bx && cy >> 22 == k->by) {
        k->spr.y = cy;
        k->spr.x = cx;
        return;
    }
    coll_remove(&k->spr, k->spr.unk20);
    k->spr.x = cx;
    k->spr.y = cy;
    k->bx = (uint8_t)(cx >> 22);
    k->by = (uint8_t)(cy >> 22);
    coll_insert(COLL_KIND10, k->id, &k->spr, k->spr.unk20, cx, cy);
}

/* Train_BogieCheckCurve 0x46d210 (four-carriage trains): the curve marks (Map_TestBlockAttr 7, ext
   4 / 5) override the piece's curve. The first mark of a curve starts it (ext 4: row 4, ext 5: row 3,
   0x46 steps), the second ends it (ext 4: row 5, ext 5: row 6); unmarked blocks in between take 10
   steps of row 7 / 8 (from the corner rows 1 / 2 the probe chose); other unmarked blocks are straight
   (row 0, 0x40 steps: the probe's corners are only used inside a curve). */
static void train_bogie_check_curve(const Train *T, TrainBogie *g)
{
    if (T->kind != TRAIN_KIND_FOUR) return;
    int v = map_test_block_attr(7, g->bx, g->by, g->bz);
    if (v == 0) {
        if (g->in_bend == 0) {
            g->curve = 0;
            g->len = 0x40;
        } else {
            g->len = 10;
            if (g->curve == 1) g->curve = 7;
            else if (g->curve == 2) g->curve = 8;
        }
    } else {
        g->len = 0x46;
        if (g->in_bend == 0) {
            g->in_bend = 1;
            g->curve = v == 4 ? 4 : 3;
        } else {
            g->in_bend = 0;
            if (v == 4) g->curve = 5;
            else if (v == 5) g->curve = 6;
        }
    }
}

/* Train_BogieSlope 0x46d7a0: on an 8-block slope (types 9-0x28) the height offset of its part
   ((type - 9) % 8 * 8) and the depth key just over the layer; elsewhere level, and the depth key is
   the z unless the block ahead in the piece's direction is the foot of a slope rising that way (types
   0x21, 0x19, 9, 0x11 for +x, -x, -y, +y), which also restarts the block count at 1. */
static void train_bogie_slope(TrainBogie *g)
{
    int s = map_test_block_attr(5, g->bx, g->by, g->bz) & 0xff;
    g->on_slope = 1;
    if (s >= 9 && s <= 0x28) {
        g->slope_h = (uint8_t)((s - 9) % 8 * 8);
        g->spr.zkey = (int32_t)((uint32_t)g->bz * 0x400000u - 1);
        return;
    }
    g->on_slope = 0;
    g->slope_dir = 1;
    int ahead = -1, want = 0;
    switch (g->piece) {
    case 1: case 2: case 3: ahead = map_test_block_attr(5, g->bx + 1, g->by, g->bz), want = 0x21; break;
    case 4: case 5: case 6: ahead = map_test_block_attr(5, g->bx - 1, g->by, g->bz), want = 0x19; break;
    case 7: case 8: case 9: ahead = map_test_block_attr(5, g->bx, g->by - 1, g->bz), want = 9; break;
    case 10: case 11: case 12: ahead = map_test_block_attr(5, g->bx, g->by + 1, g->bz), want = 0x11; break;
    default: break;
    }
    if (ahead >= 0 && (uint8_t)ahead == want) g->blocks = 1;
    if (g->blocks != 1) g->spr.zkey = g->spr.z;
    else g->spr.zkey = (int32_t)((uint32_t)g->bz * 0x400000u - 1);
}

/* Train_CheckSwitch 0x46da30 (named Train_CheckStation before): a switch cell the train hasn't just
   passed; an entry cell (even index) of a held switch makes it wait for it (1), any other cell flips
   the switch (taking or releasing it). */
static int train_check_switch(Train *T, uint8_t x, uint8_t y)
{
    if (x == T->switch_x && y == T->switch_y) return 0;
    int i = rail_find_switch(x, y);
    if (i != 0xff) {
        T->switch_x = x, T->switch_y = y;
        if (rail_is_switch_set(i) && (i & 1) == 0) {
            T->wait_switch = (uint8_t)i;
            return 1;
        }
        rail_toggle_switch(i);
    }
    return 0;
}

/* ---- the helpers in the car module ---- */

/* Car_OnHit 0x407f00 (only Coll_ProjectileHit calls it): a crash sound by the car's speed (6 under 7,
   7 under 0x11, else 8), a bike falls (driver ejected, frame 7, damage 0x5a), an impulse away from
   (x, y) for the next frame, 20 damage (not for mission-locked cars), and a player car's engine
   weakens with it. */
static void car_on_hit(int32_t x, int32_t y, Car *c)
{
    int s = c->speed < 0x11 ? (c->speed < 7 ? 6 : 7) : 8;
    Snd_PlayAt(c->spr.x, c->spr.y, c->spr.z, s);
    if (c->vtype == 3 && c->status != 7) {
        ped_eject_driver(c);
        c->status = 7;
        sprite_set_frame(&c->spr, c->base_frame + 7);
        c->turret = 0;
        if (c->owner_status != 99) {
            c->damage = 0x5a;
            if (c->control == 1) c->thrust = carinfo_float(c->info, 0x80) * 0.35f;
        }
    }
    c->impulse_px = (float)(int16_t)(c->spr.x >> 16);
    c->impulse_py = (float)(int16_t)(c->spr.y >> 16);
    c->impulse_x = (float)((c->spr.x - x) >> 17);
    c->impulse_y = (float)((c->spr.y - y) >> 17);
    c->physics = 1;
    c->impulse_state = 2;
    if (c->damage < 100 && c->owner_status != 99) {
        c->damage = (int16_t)(c->damage + 0x14);
        if (c->damage > 100) c->damage = 100;
        if (c->control == 1 && c->damage > 0x19) c->thrust = (float)(0x7d - c->damage) * 0.01f * carinfo_float(c->info, 0x80);
    }
}

/* Coll_ProjectileHit 0x408090 (only Train_BogieCollide calls it): while the bogie's train moves, a
   0x17-pixel box (id 0x18) at the bogie hits, first found: another bogie (7, its id in the high
   half), a car (its vtype; hit as above when the speed is over 0), an object (6; kicked unless in
   state 6 when the speed is over 1), a ped (5; +0x5a cleared). 0xffff if nothing. */
static uint32_t coll_projectile_hit(int32_t x, int32_t y, int32_t z, int spd, int angle, int id)
{
    int16_t kind = -1;
    uint32_t r = 0xffff;
    if (train_command(5, (int16_t)id / 8) == 0) return r;
    CollBox *b = coll_build_box(x, y, z, 0x17, 0x17, angle, 0x18, &proj_box);
    CollHit *h = coll_query_box_first(b, COLL_KIND8, COLL_KIND8, id);
    if (h) {
        kind = 7;
        r = (uint32_t)(uint16_t)h->id << 16 | 7;
    }
    coll_unlock();
    if (kind != -1) return r;
    h = coll_query_box_first(b, COLL_CAR, COLL_KIND8, id);
    if (h) {
        Car *c = h->owner;
        coll_unlock();
        if ((int16_t)spd > 0) car_on_hit(x, y, c);
        kind = c->vtype;
        r = (r & 0xffff0000u) | (uint16_t)kind;
    }
    coll_unlock();
    if (kind != -1) return r;
    h = coll_query_box_first(b, COLL_OBJECT, COLL_KIND8, id);
    if (h) {
        Obj *o = h->owner;
        coll_unlock();
        if (o->state != 6 && (int16_t)spd > 1) obj_kick(b->x[0], b->y[0], o->id, spd + 4, angle);
        kind = 6;
        r = (r & 0xffff0000u) | 6;
    }
    coll_unlock();
    if (kind != -1) return r;
    h = coll_query_box_first(b, COLL_PED, COLL_KIND8, id);
    if (h) {
        Ped *p = h->owner;
        coll_unlock();
        p->u5a = -1;
        r = (r & 0xffff0000u) | 5;
    }
    coll_unlock();
    return r;
}

/* the list Train_Crash hands Train_SpawnCarriages (on its stack) */
typedef struct TrainWreck {
    int32_t x, y, z;
    int16_t angle, speed, kind;
    struct TrainWreck *next;
} TrainWreck;

/* Train_SpawnCarriages 0x407c30: each record becomes a car of model 0xb (kind 1; 0xc kind 2) in the
   first free slot (scanned only when cars exist), moving at the record's speed, heading its angle,
   damage 100 unless mission-locked (a player car's engine at a quarter), with crash sound 10. */
static void train_spawn_carriages(const TrainWreck *w)
{
    for (; w; w = w->next) {
        if (w->kind != 1 && w->kind != 2) continue;
        int i = 0;
        if (g_cars_count != 0)
            while (g_cars[(int16_t)i].status != -1 && (int16_t)++i < CAR_MAX) {}
        i = (int16_t)i;
        if (i >= CAR_MAX) continue;   /* (port: the original would write past the table) */
        car_init(w->x, w->y, w->z, w->angle, w->kind == 1 ? 0xb : 0xc, i);
        if (i == g_cars_count) g_cars_count++;
        Car *c = &g_cars[i];
        c->speed = w->speed;
        if (c->owner_status != 99) {
            c->damage = 100;
            if (c->control == 1) c->thrust = carinfo_float(c->info, 0x80) * 0.25f;
        }
        c->front_heading = (int16_t)(c->spr.angle & 0x3ff);
        Snd_PlayAt(w->x, w->y, w->z, 10);
    }
    coll_unlock();
}

/* ---- crashes ---- */

/* Train_Crash 0x46d650: carriage c is wrecked (once): out of the grid with its bogies, door objects
   deleted, the train brakes into the ridden state (0xb) and counts as not ridden, a rider dies
   (Player_TrainCrashKick), every other carriage of the train crashes too, and the carriage turns into
   a wreck car where it was. */
void train_crash(int t, int c)
{
    Train *T = &g_trains[(uint8_t)t];
    TrainCar *k = &T->car[(uint8_t)c & 3];
    if (k->crashed) return;
    k->state = TRAIN_CAR_WRECKED;
    k->crashed = 1;
    T->boarded = 1;
    if (T->kind == TRAIN_KIND_FOUR)
        for (int i = 0; i < 2; i++)
            if (k->door[i].obj != -1) obj_delete(k->door[i].obj);   /* (port: the original deletes -1 too) */
    coll_remove(&k->spr, k->spr.unk20);
    TrainWreck w = { k->spr.x, k->spr.y, k->spr.z, k->spr.angle, (int16_t)(T->speed / 10), T->kind, NULL };
    for (int i = 0; i < 2; i++) coll_remove(&k->bogie[i].spr, k->bogie[i].spr.unk20);
    T->state = TRAIN_ST_BRAKE;
    T->next_state = TRAIN_ST_RIDDEN;
    player_train_crash_kick((uint8_t)t);
    for (int u = 0; u < 4; u++)
        if (u != ((uint8_t)c & 3)) train_crash(t, u);
    train_spawn_carriages(&w);
}

/* Train_FindDoorNear 0x46ab00: whether (x, y) lies within r + 0x28 pixels (in x and in y) of a
   bogie, the midpoint or a quarter point of carriage `car` (train car >> 2, carriage car & 3; a
   single unit takes the whole byte as the train), in the same layer as its first bogie: then its
   heading and speed / 10, else NULL. */
const TrainNear *train_find_door_near(int32_t x, int32_t y, int32_t z, int r, int car)
{
    int tr = (uint8_t)car >> 2, cc = (uint8_t)car & 3;
    const TrainCar *k = &g_trains[tr % TRAIN_MAX].car[cc];
    int16_t ax = (int16_t)(k->bogie[0].spr.x >> 16), bx = (int16_t)(k->bogie[1].spr.x >> 16);
    int16_t ay = (int16_t)(k->bogie[0].spr.y >> 16), by = (int16_t)(k->bogie[1].spr.y >> 16);
    int16_t mx = ax < bx ? (int16_t)(((bx - ax) >> 1) + ax) : (int16_t)(((ax - bx) >> 1) + bx);
    int16_t my = ay < by ? (int16_t)(((by - ay) >> 1) + ay) : (int16_t)(((ay - by) >> 1) + by);
    if (g_trains[tr % TRAIN_MAX].kind == TRAIN_KIND_SINGLE) cc = 0, tr = (uint8_t)car;
    int px = (int16_t)(x >> 16), py = (int16_t)(y >> 16);
    int lim = (int16_t)(r + 0x28);
#define NEAR(qx, qy) (abs(px - (qx)) < lim && abs(py - (qy)) < lim)
    bool hit = NEAR(ax, ay) || NEAR(bx, by) || NEAR(mx, my);
    if (!hit) {
        int16_t qy = ay < my ? (int16_t)(((my - ay) >> 1) + ay) : (int16_t)(((ay - my) >> 1) + my);
        int16_t qx = ax < mx ? (int16_t)(((mx - ax) >> 1) + ax) : (int16_t)(((ax - mx) >> 1) + mx);
        hit = NEAR(qx, qy);
        if (!hit) {
            int16_t sy = by < my ? (int16_t)(((my - by) >> 1) + by) : (int16_t)(((by - my) >> 1) + my);
            int16_t sx = bx < mx ? (int16_t)(((mx - bx) >> 1) + bx) : (int16_t)(((bx - mx) >> 1) + mx);
            hit = NEAR(sx, sy);
        }
    }
#undef NEAR
    if (!hit) return NULL;
    const Train *T = &g_trains[tr % TRAIN_MAX];
    const TrainCar *kk = &T->car[cc];
    if (((uint32_t)kk->bogie[0].spr.z ^ (uint32_t)z) & 0xffc00000u) return NULL;
    near_res.angle = kk->spr.angle;
    near_res.speed = (int16_t)(T->speed / 10);
    return &near_res;
}

/* Train_CheckCarriageHits 0x46d4f0: each bogie of a running carriage, as a 0x17 x 0x3f box (id 0x20)
   on its heading, against the bodies of other trains' carriages (kind 10): when the hit carriage has
   another index than this one and the bogie is near it (Train_FindDoorNear, 0x10), both crash. */
static void train_check_carriage_hits(int t, int n)
{
    Train *T = &g_trains[t];
    for (int c = 0; c < n; c++) {
        TrainCar *k = &T->car[c];
        if (k->state != TRAIN_CAR_RUNNING) continue;
        for (int i = 0; i < 2; i++) {
            TrainBogie *g = &k->bogie[i];
            CollBox *b = coll_build_box(g->spr.x, g->spr.y, g->spr.z, 0x17, 0x3f, g->spr.angle, 0x20, &hits_box);
            for (CollHit *h = coll_query_box(b, COLL_KIND10, COLL_KIND8, g->id); h; h = h->next) {
                int hid = h->id;
                if (hid / 4 == t) continue;
                if (hid % 4 != c && train_find_door_near(g->spr.x, g->spr.y, g->spr.z, 0x10, (uint8_t)hid)) {
                    train_crash(t, c);
                    train_crash(hid / 4, hid % 4);
                }
            }
            coll_unlock();
        }
    }
}

/* Train_BogieCollide 0x46d320: what the bogie ran into. A car of vtype 1, 2, 8 or 9 costs the carriage
   a hit (after 10 more it crashes: 1); a car of vtype 4 halves the train's speed; another train's
   bogie crashes both carriages (its own train's only with +0x1f set, which nothing sets). */
static int train_bogie_collide(int t, int c, int bi)
{
    Train *T = &g_trains[t];
    if (T->state == 0xd) return 0;
    TrainCar *k = &T->car[c];
    TrainBogie *g = &k->bogie[bi];
    uint32_t r = coll_projectile_hit(g->spr.x, g->spr.y, g->spr.z, T->speed / 10, k->spr.angle, g->id);
    int16_t kind = (int16_t)r, hid = (int16_t)(r >> 16);
    switch (kind) {
    case 1: case 2: case 8: case 9:
        if (k->hits-- == 0) {
            train_crash(t, c);
            return 1;
        }
        return 0;
    case 4:
        T->speed >>= 1;
        return 0;
    case 7: {
        int t2 = hid / 8;
        if (t2 == t || T->u1f) {
            if (T->u20) return 0;
            if (!T->u1f) return 0;
        }
        if (hid % 2 == 0) {
            train_crash(t, c);
            train_crash(t2, hid % 8 / 2);
        } else if (hid % 2 == 1) {
            train_crash(t2, hid % 8 / 2);
            train_crash(t, c);
        }
        return 1;
    }
    default: return 0;
    }
}

/* ---- doors and passengers ---- */

/* Train_FindPlatformDoors 0x46e1e0: at a station each running carriage on a straight (both bogies on
   the same piece) opens the sides that have pavement (Map_TestBlockAttr 9) a block out */
static void train_find_platform_doors(Train *T)
{
    for (int c = 0; c < ncars(T); c++) {
        TrainCar *k = &T->car[c];
        k->door[0].open = 0;
        k->door[1].open = 0;
        if (k->bogie[0].piece != k->bogie[1].piece || k->state != TRAIN_CAR_RUNNING) continue;
        int a = (k->spr.angle + 0x100) & 0x3ff;
        if (map_test_block_attr(9, (SIN(a) * 0x40 + k->spr.x) >> 22, (COS(a) * 0x40 + k->spr.y) >> 22, (int16_t)(k->spr.z >> 16) >> 6))
            k->door[0].open = 1;
        a = (k->spr.angle - 0x100) & 0x3ff;
        if (map_test_block_attr(9, (SIN(a) * 0x40 + k->spr.x) >> 22, (COS(a) * 0x40 + k->spr.y) >> 22, (int16_t)(k->spr.z >> 16) >> 6))
            k->door[1].open = 1;
    }
}

/* Train_UnloadPassengers 0x46e300: while passengers are left, every 5th call per carriage (then a
   random 0..2 restart) and 5 times in 6, one steps off a random open side, 0x14 pixels out (0x1b once
   a carriage heading along x was met: the distance stays changed for the later carriages) */
static void train_unload_passengers(Train *T)
{
    int dist = T->kind == TRAIN_KIND_FOUR ? 0x14 : 0x1b;
    for (int c = 0; c < ncars(T); c++) {
        TrainCar *k = &T->car[c];
        if (T->passengers == 0) continue;
        uint8_t tick = T->ped_tick++;
        if (tick <= 4 || k->state != TRAIN_CAR_RUNNING) continue;
        T->ped_tick = (uint8_t)(math_random() % 3);
        if (math_random() % 6 == 0) continue;
        int side = math_random() % 2 == 0 ? 1 : 0;
        if (!k->door[side].open) continue;
        int16_t h = k->spr.angle;
        int a = (h + (side ? -0x100 : 0x100)) & 0x3ff;
        if (h == 0x300 || h == 0x100) dist = 0x1b;
        if (ped_create_anim41(SIN(a) * dist + k->spr.x, COS(a) * dist + k->spr.y, k->spr.z, a)) T->passengers--;
    }
}

/* Train_LoadPassengers 0x46e450: the ped (the player getting off, Ped_PlayerExitCar) is put at the
   open side of carriage 1 (a single unit: carriage 0, from its first bogie) 0x14 pixels out and sent
   three times as far; a random 0..2 restarts the passenger tick. */
void train_load_passengers(int train, int ped)
{
    Train *T = &g_trains[(uint8_t)train];
    int dist = T->kind == TRAIN_KIND_FOUR ? 0x14 : 0x1b;
    int ci = T->kind == TRAIN_KIND_FOUR ? 1 : 0;
    TrainCar *k = &T->car[ci];
    if (k->state != TRAIN_CAR_RUNNING && T->kind == TRAIN_KIND_FOUR) return;
    T->ped_tick = (uint8_t)(math_random() % 3);
    int a;
    if (k->door[0].open) a = T->kind == TRAIN_KIND_FOUR ? k->spr.angle + 0x100 : T->car[0].bogie[0].spr.angle + 0x100;
    else if (k->door[1].open) a = T->kind == TRAIN_KIND_FOUR ? k->spr.angle - 0x100 : T->car[0].bogie[0].spr.angle - 0x100;
    else return;
    a &= 0x3ff;
    int32_t px, py, pz, dx, dy;
    if (T->kind == TRAIN_KIND_FOUR) {
        dx = k->spr.x + SIN(a) * dist * 3;
        dy = k->spr.y + COS(a) * dist * 3;
        if (k->spr.angle == 0x300 || k->spr.angle == 0x100) dist = 0x1b;
        pz = k->spr.z;
        py = COS(a) * dist + k->spr.y;
        px = SIN(a) * dist + k->spr.x;
    } else {
        const TrainCar *k0 = &T->car[0];
        dx = k0->spr.x + SIN(a) * dist * 3;
        dy = k0->spr.y + COS(a) * dist * 3;
        pz = k0->spr.z;
        py = k0->spr.y + COS(a) * dist;
        px = k0->spr.x + SIN(a) * dist;
    }
    if ((int16_t)ped_place_and_send(ped, px, py, pz, a, dx, dy) != 0) T->passengers--;
}

/* Train_ClearDoorAreas 0x46e640: ped walk targets at the open doors are dropped (the door positions
   are never set: 0, 0) */
static void train_clear_door_areas(Train *T)
{
    for (int c = 0; c < ncars(T); c++)
        for (int i = 0; i < 2; i++)
            if (T->car[c].door[i].open) ped_clear_targets_in_block(T->car[c].door[i].x, T->car[c].door[i].y);
}

/* Train_UpdateDoorSprites 0x46dcc0: four-carriage trains show the door frame on the open sides'
   door objects of running carriages (all, with +0x20) */
void train_update_door_sprites(int train)
{
    Train *T = &g_trains[(uint8_t)train];
    for (int c = 0; c < ncars(T); c++) {
        TrainCar *k = &T->car[c];
        if ((k->state != TRAIN_CAR_RUNNING && !T->u20) || T->kind != TRAIN_KIND_FOUR) continue;
        for (int i = 0; i < 2; i++)
            if (k->door[i].obj != -1 && k->door[i].open) obj_set_state(k->door[i].obj, T->door_frame);
    }
}

/* Train_CheckPlatformSides 0x46ae40: may the rider step off? Carriage 1 (a single unit: 0) marks both
   sides open, then (when running, or always for a single unit) closes each side whose block 3 x 0x14
   pixels out isn't pavement (type 3; a single unit measures from its first bogie). -1 when both are
   closed. */
int train_check_platform_sides(int train)
{
    Train *T = &g_trains[(uint8_t)train];
    int dist = T->kind == TRAIN_KIND_FOUR ? 0x14 : 0x1b;
    TrainCar *k = &T->car[T->kind == TRAIN_KIND_FOUR ? 1 : 0];
    k->door[0].open = 1;
    k->door[1].open = 1;
    if (k->state == TRAIN_CAR_RUNNING || T->kind != TRAIN_KIND_FOUR) {
        if (T->kind == TRAIN_KIND_FOUR) {
            int a = (k->spr.angle + 0x100) & 0x3ff;
            if ((type_cache_at(k->spr.x + SIN(a) * dist * 3, k->spr.y + COS(a) * dist * 3, k->spr.z) & 0x70) != 0x30)
                k->door[0].open = 0;
            a = (k->spr.angle - 0x100) & 0x3ff;
            if ((type_cache_at(SIN(a) * dist + (k->spr.x + SIN(a) * dist * 2), k->spr.y + COS(a) * dist * 3, k->spr.z) & 0x70) != 0x30)
                k->door[1].open = 0;
        } else {
            const TrainBogie *g = &T->car[0].bogie[0];
            int a = (g->spr.angle + 0x100) & 0x3ff;
            if ((type_cache_at(g->spr.x + SIN(a) * dist * 3, g->spr.y + COS(a) * dist * 3, k->spr.z) & 0x70) != 0x30)
                k->door[0].open = 0;
            a = (g->spr.angle - 0x100) & 0x3ff;
            if ((type_cache_at(SIN(a) * dist + (g->spr.x + SIN(a) * dist * 2), g->spr.y + COS(a) * dist * 3, g->spr.z) & 0x70) != 0x30)
                k->door[1].open = 0;
        }
    }
    return !k->door[0].open && !k->door[1].open ? -1 : 1;
}

/* Train_GetDoorOffsets 0x46b090 (id: train in bits 2-7, carriage the whole low byte): when a
   four-carriage train has its doors fully open (frame 7), or a single unit stands at a station, the
   step-off offsets of carriage id's open sides: left (0, 0x14), right (0, -0x14) (0x1b for a single
   unit). NULL otherwise. The one caller passes 0. */
const TrainDoorOffsets *train_get_door_offsets(int id)
{
    door_ofs.left = door_ofs.right = 0;
    int t = (id >> 2) & 0x3f, c = id & 0xff;
    if (t >= TRAIN_MAX || c >= TRAIN_CARS) return NULL;   /* (port: the original indexes on) */
    const Train *T = &g_trains[t];
    const TrainCar *k = &T->car[c];
    int d;
    if (T->kind == TRAIN_KIND_FOUR) {
        if (T->door_frame != 7 || k->state != TRAIN_CAR_RUNNING) return NULL;
        d = 0x14;
    } else if (T->kind == TRAIN_KIND_SINGLE) {
        if (k->state != TRAIN_CAR_RUNNING || T->state != TRAIN_ST_STATION) return NULL;
        d = 0x1b;
    } else {
        game_fatal(-0x11a, 0x1b3, T->kind);
    }
    door_ofs.left = k->door[0].open != 0;
    if (door_ofs.left) door_ofs.left_dx = 0, door_ofs.left_dy = (int16_t)d;
    if (k->door[1].open) {
        door_ofs.right_dx = 0;
        door_ofs.right = 1;
        door_ofs.right_dy = (int16_t)-d;
        return &door_ofs;
    }
    if (!door_ofs.left) {
        door_ofs.right = 0;
        return NULL;
    }
    return &door_ofs;
}

/* Train_GetCarriage 0x46b200: the body sprite of carriage id (train id >> 2, carriage id & 3), as the
   original's dwords (x, y, z, zkey, ..., angle in the low half of [6]) */
const int32_t *train_get_carriage(int id)
{
    return &g_trains[((uint8_t)id >> 2) % TRAIN_MAX].car[id & 3].spr.x;
}

/* Train_CheckRoadAhead 0x46dd70 (named Train_SpawnExitPed before; single units only: never runs):
   back to running; a car or ped half a block ahead of the leading bogie (Coll_QueryBlock kind 6)
   stops it (state 4, then 0xc), as does a junction block whose light isn't green, every other one. */
static void train_check_road_ahead(Train *T)
{
    T->state = TRAIN_ST_RUN;
    const TrainBogie *g = &T->car[0].bogie[T->front_bogie & 1];
    int32_t x = g->spr.x, y = g->spr.y;
    int light = 0;
    bool known = true;
    switch (g->piece) {
    case 1: case 2: case 3: x += 0x800000; break;
    case 4: case 5: case 6: x -= 0x800000; break;
    case 7: case 8: case 9: y -= 0x800000; break;
    case 10: case 11: case 12: y += 0x800000; break;
    default: known = false; break;
    }
    if (known)
        for (CollHit *h = coll_query_block(x, y, g->spr.z, COLL_CAR, -1); h; h = h->next)
            if (h->kind == 1 || h->kind == 6) {
                T->state = TRAIN_ST_BRAKE;
                T->next_state = TRAIN_ST_LEFT;
                h->next = NULL;   /* (the original clears the link: the walk ends) */
            }
    coll_unlock();
    if (known) light = (int8_t)lights_query(0x34, x >> 22, y >> 22);
    if (map_test_block_attr(2, g->spr.x >> 22, g->spr.y >> 22, g->spr.z >> 22)) {
        if ((light == 0 || light == 1) && T->cross_count == 0) {
            T->state = TRAIN_ST_BRAKE;
            T->next_state = TRAIN_ST_LEFT;
        }
        if (++T->cross_count == 2) T->cross_count = 0;
    }
}

/* Train_Reverse 0x46df90: direction 6 <-> 7, the axis angles swapped for all trains at once; every
   bogie of the train's carriages turns round: sub-step len - sub,
   slope state 0 <-> 1, the reverse piece, the next piece probed again (keeping the curve and the
   length), the curve mirrored (1 <-> 2, 3 <-> 6, 4 <-> 5, 7 <-> 8) and a curve end's bend flag
   flipped. */
static void train_reverse(Train *T)
{
    static const uint8_t rev_piece[13] = { 0, 5, 4, 6, 2, 1, 3, 11, 10, 12, 8, 7, 9 };
    static const uint8_t rev_curve[9] = { 0, 2, 1, 6, 5, 4, 3, 8, 7 };
    if (T->dir == 6) {
        T->dir = 7;
        axis_angle[0] = 0x400, axis_angle[1] = 0x200, axis_angle[2] = 0x100, axis_angle[3] = 0x300;
    } else if (T->dir == 7) {
        T->dir = 6;
        axis_angle[0] = 0x200, axis_angle[1] = 0x400, axis_angle[2] = 0x300, axis_angle[3] = 0x100;
    }
    for (int c = 0; c < ncars(T); c++)
        for (int i = 0; i < 2; i++) {
            TrainBogie *g = &T->car[c].bogie[i];
            uint8_t len = g->len;
            g->sub = (uint8_t)(len - g->sub);
            if (g->slope_dir == 0) g->slope_dir = 1;
            else if (g->slope_dir == 1) g->slope_dir = 0;
            if (g->piece >= 1 && g->piece <= 12) g->piece = rev_piece[g->piece];
            uint8_t curve = g->curve;
            train_bogie_next_piece(g);
            g->curve = curve;
            g->len = len;
            if (g->curve < 9) g->curve = rev_curve[g->curve];
            if (g->curve > 2 && g->curve < 7) g->in_bend = g->in_bend == 0 ? 1 : g->in_bend == 1 ? 0 : g->in_bend;
        }
}

/* Train_DoorLeft 0x46dae0 / Train_DoorRight 0x46db60: the ridden train's door keys (the front carriage
   running). Left: direction 6 goes (state 3); direction 7 brakes into the ridden state, or from it
   turns round and goes. Right is the mirror image. */
static void train_door_left(Train *T)
{
    if (T->boarded != 2 || T->car[T->front_car & 3].state != TRAIN_CAR_RUNNING) return;
    if (T->dir != 6) {
        if (T->dir != 7) return;
        if (T->state != TRAIN_ST_RIDDEN) {
            T->state = TRAIN_ST_BRAKE;
            T->next_state = TRAIN_ST_RIDDEN;
            return;
        }
        train_reverse(T);
    }
    T->state = TRAIN_ST_RUN;
}
static void train_door_right(Train *T)
{
    if (T->boarded != 2 || T->car[T->front_car & 3].state != TRAIN_CAR_RUNNING) return;
    if (T->dir == 6) {
        if (T->state == TRAIN_ST_RIDDEN) {
            train_reverse(T);
            T->state = TRAIN_ST_RUN;
            return;
        }
        T->state = TRAIN_ST_BRAKE;
        T->next_state = TRAIN_ST_RIDDEN;
    } else if (T->dir == 7) {
        T->state = TRAIN_ST_RUN;
    }
}

/* Train_ToggleBoarded 0x46dbe0: boarding stops the train in the ridden state (at once at a station,
   else after braking) with top speed 0x50; leaving restarts the station sequence (state 0xc), top
   speed back to 0x3c (single unit 0x32 / 0x1e) */
static void train_toggle_boarded(Train *T)
{
    if (T->boarded == 1) {
        T->boarded = 2;
        if (T->state == TRAIN_ST_STATION) T->state = TRAIN_ST_RIDDEN;
        else T->next_state = TRAIN_ST_RIDDEN, T->state = TRAIN_ST_BRAKE;
        if (T->kind == TRAIN_KIND_FOUR) T->max_speed = 0x50;
        else if (T->kind == TRAIN_KIND_SINGLE) T->max_speed = 0x32;
    } else if (T->boarded == 2) {
        T->boarded = 1;
        if (T->state == TRAIN_ST_RIDDEN) T->state = TRAIN_ST_LEFT;
        else T->state = TRAIN_ST_BRAKE, T->next_state = TRAIN_ST_LEFT;
        if (T->kind == TRAIN_KIND_FOUR) T->max_speed = 0x3c;
        else if (T->kind == TRAIN_KIND_SINGLE) T->max_speed = 0x1e;
    }
}

/* Train_IsBoarded 0x46dca0 */
bool train_is_boarded(int train) { return g_trains[(uint8_t)train].boarded == 2; }

/* Train_GetBoardInfo 0x46e6c0: the front carriage (a single unit: carriage 0) for the camera */
static void train_get_board_info_fill(const Train *T)
{
    board_info.h = 0x7e;
    board_info.w = 0x30;
    const TrainCar *k = T->kind == TRAIN_KIND_FOUR ? &T->car[T->front_car & 3] : &T->car[0];
    board_info.x = k->spr.x;
    board_info.y = k->spr.y;
    board_info.z = k->spr.z;
    board_info.angle = k->spr.angle;
    board_info.speed = T->speed;
}

/* Train_Command 0x46a9f0 */
int train_command(int cmd, int train)
{
    Train *T = &g_trains[(uint8_t)train % TRAIN_MAX];
    switch ((uint8_t)cmd) {
    case 1: case 2: train_toggle_boarded(T); return 0x14;
    case 3: train_door_left(T); return 0x14;
    case 4: train_door_right(T); return 0x14;
    case 5: return T->speed;
    case 7: train_get_board_info_fill(T); return 0x14;
    case 8: return (uint8_t)T->rider;
    case 9: T->rider = g_ped_74f0f8; return 0x14;   /* Ped_unk_0045fbe0: the ped that just boarded */
    default: game_fatal(-0x119, 0x1b2, (uint8_t)cmd);
    }
}

/* ---- the frame ---- */

/* Train_Update 0x46bd60. Per train (not single units; not in state 0xd, which nothing sets):
   carriage collisions; doors close a frame step outside stations; the state machine:
   - 2, the station sequence by +0x1d: 0 leave (next station, state 10); 5 find the platform sides,
     doors from frame 1; 1 open them a frame step at a time (to 7, with the door sound 0x29 every
     frame), then 3: on alternate calls pick 0..15 passengers (and a 100-frame limit) or let them off
     until done, then 2: a 100-frame countdown (peds may board) after which the door areas are
     cleared, 4: the doors close (sound), back to 0;
   - 3 accelerate by 1 to the top speed; 4 brake by 1, then the state in +9; 9 wait for the switch;
     10 wait for the next station to be free, then take it and run; 0xc after the rider left: turn round
     if reversed, then the station sequence from 5.
   Then each running carriage's bogies: out of the grid; past the block's steps, into the next block
   (piece, curve marks, slope), where the leading bogie of an unridden train toggles level crossings,
   brakes for a new station (then the station sequence) and for held switches; the position along the
   curve; back into the grid; speed / 10 steps on; collisions. Then the body between the bogies. The
   door objects follow the door frame. */
void train_update(void)
{
    uint8_t n = 0;
    for (int t = 0; t < g_train_count && t < TRAIN_MAX; t++) {
        Train *T = &g_trains[t];
        if (T->kind == TRAIN_KIND_SINGLE) continue;
        if (T->state != 0xd) {
            if (T->kind == TRAIN_KIND_FOUR) n = 4;
            else if (T->kind == TRAIN_KIND_SINGLE) n = 1;
            train_check_carriage_hits(t, n);
            if (T->state != TRAIN_ST_STATION && T->kind == TRAIN_KIND_FOUR && T->door_frame < 8)
                T->door_frame = T->door_frame < 2 ? 8 : (uint8_t)(T->door_frame - 1);
            switch (T->state) {
            case TRAIN_ST_STATION:
                switch (T->sub) {
                case 0:
                    T->state = TRAIN_ST_LEAVE;
                    T->next_station = (uint8_t)rail_next_station(T->station);
                    T->sub = 5;
                    break;
                case 1:
                    if (T->kind == TRAIN_KIND_FOUR && T->door_frame <= 6) {
                        T->door_frame++;
                    } else if (T->kind == TRAIN_KIND_FOUR || T->kind == TRAIN_KIND_SINGLE) {
                        if (T->kind == TRAIN_KIND_FOUR) T->doors_open = 1;
                        T->sub = 3;
                        T->toggle = 1;
                        T->ped_tick = 5;
                    }
                    Snd_PlayAt(T->car[0].spr.x, T->car[0].spr.y, 0, 0x29);
                    break;
                case 2:
                    if (T->toggle == 1) {
                        T->toggle = 0;
                        T->passengers = 100;
                    } else if (T->passengers == 0) {
                        T->sub = 4;
                        train_clear_door_areas(T);
                        T->doors_open = 0;
                    } else {
                        T->passengers--;
                    }
                    break;
                case 3:
                    if (T->toggle == 0) {
                        if (T->passengers == 0 || T->timer-- == 0) {
                            T->toggle = 1;
                            T->sub = 2;
                        } else {
                            train_unload_passengers(T);
                        }
                    } else {
                        T->toggle = 0;
                        T->passengers = (int16_t)(math_random() % (T->kind == TRAIN_KIND_FOUR ? 16 : 8));
                        T->timer = 100;
                    }
                    break;
                case 4:
                    if (T->kind == TRAIN_KIND_FOUR && T->door_frame >= 2) {
                        T->door_frame--;
                    } else if (T->kind == TRAIN_KIND_FOUR || T->kind == TRAIN_KIND_SINGLE) {
                        if (T->kind == TRAIN_KIND_FOUR) T->door_frame = 8;
                        T->sub = 0;
                    }
                    Snd_PlayAt(T->car[0].spr.x, T->car[0].spr.y, 0, 0x29);
                    break;
                case 5:
                    train_find_platform_doors(T);
                    T->sub = 1;
                    T->door_frame = 1;
                    break;
                }
                break;
            case TRAIN_ST_RUN: {
                if (T->kind == TRAIN_KIND_SINGLE && T->boarded == 1) train_check_road_ahead(T);
                uint8_t s = (uint8_t)(T->speed + 1);
                T->speed = s;
                if (T->max_speed < s) T->speed = T->max_speed;
                break;
            }
            case TRAIN_ST_BRAKE:
                if (T->speed == 0) {
                    T->u08 = 0;
                    T->state = T->next_state;
                    T->u20 = 0;
                } else {
                    T->speed--;
                }
                break;
            case TRAIN_ST_SWITCH:
                if (!rail_is_switch_set(T->wait_switch)) {
                    rail_toggle_switch(T->wait_switch);
                    T->state = TRAIN_ST_RUN;
                }
                break;
            case TRAIN_ST_LEAVE:
                if (rail_get_station_flag(T->next_station) == 0) {
                    rail_toggle_station(T->station);
                    rail_toggle_station(T->next_station);
                    T->station = T->next_station;
                    T->state = TRAIN_ST_RUN;
                }
                break;
            case TRAIN_ST_LEFT:
                if (T->dir == 7) train_reverse(T);
                T->state = TRAIN_ST_STATION;
                T->sub = 5;
                break;
            default: break;
            }
            for (int c = 0; c < n; c++) {
                TrainCar *k = &T->car[c];
                if (k->state != TRAIN_CAR_RUNNING) continue;
                for (int bi = 0; bi < 2; bi++) {
                    if (k->state != TRAIN_CAR_RUNNING) continue;
                    TrainBogie *g = &k->bogie[bi];
                    coll_remove(&g->spr, g->spr.unk20);
                    if (g->len <= g->sub) {
                        g->sub = (uint8_t)(g->sub - g->len);
                        if (g->blocks) g->blocks--;
                        train_bogie_step(g);
                        train_bogie_next_piece(g);
                        if (T->kind != TRAIN_KIND_SINGLE) train_bogie_check_curve(T, g);
                        if (T->boarded == 1 && c == T->front_car && bi == T->front_bogie) {
                            uint8_t x = g->bx, y = g->by;
                            if ((x == T->cross_x && y == T->cross_y) || rail_toggle_crossing(x, y) == 0x14)
                                T->cross_x = x, T->cross_y = y;
                            if ((x != T->station_x || y != T->station_y) && rail_find_station(x, y)) {
                                T->station_x = x, T->station_y = y;
                                T->next_state = TRAIN_ST_STATION;
                                T->state = TRAIN_ST_BRAKE;
                            }
                            if (train_check_switch(T, g->bx, g->by)) {
                                T->next_state = TRAIN_ST_SWITCH;
                                T->state = TRAIN_ST_BRAKE;
                            }
                        }
                        g->spr.x = g->bx << 22;
                        g->spr.y = g->by << 22;
                        g->spr.z = g->bz * 0x400000 + 0x3c0000;
                        train_bogie_slope(g);
                    }
                    train_bogie_compute_pos(g);
                    coll_insert(COLL_KIND8, (int16_t)(bi + (c + t * 4) * 2), &g->spr, g->spr.unk20, g->spr.x, g->spr.y);
                    g->sub = (uint8_t)(g->sub + T->speed / 10);
                    train_bogie_collide(t, c, bi);
                }
                if (k->state == TRAIN_CAR_RUNNING) train_carriage_update_centre(k);
            }
        }
        train_update_door_sprites(t);
    }
}

/* Train_UpdateAll 0x46a9d0 */
int train_update_all(void)
{
    if (!trains_on) return 0x15;
    train_update();
    return 0x14;
}

/* Train_AnyWrecked 0x46e7d0: a train in state 0xd (never) or a wrecked carriage */
bool train_any_wrecked(void)
{
    for (int t = 0; t < g_train_count && t < TRAIN_MAX; t++) {
        if (g_trains[t].state == 0xd) return true;
        for (int c = 0; c < 4; c++)
            if (g_trains[t].car[c].state == TRAIN_CAR_WRECKED) return true;
    }
    return false;
}

void train_fill_snd(int i, SndTrain *out)
{
    const Train *T = &g_trains[i];
    out->speed = T->speed;
    out->pan_x = T->car[0].spr.x;
    out->x = T->car[3].spr.x, out->y = T->car[3].spr.y, out->z = T->car[3].spr.z;
}
