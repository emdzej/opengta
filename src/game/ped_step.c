/* Ped stepping and steering (0x454c60-0x458c9f): Ped_ComputeStep, the next position of an AI ped
   from its state, objective and action, and the steering helpers it and Ped_Process use: steering to
   the walk target (Ped_SteerToTarget), the direction probes (Ped_IsDirClear / _Far, Ped_CanSidestep,
   Ped_CheckAhead), turning at obstacles, aiming at a target (Ped_AttackTarget), plus the slope height
   helper Map_SlopeDelta. See docs/peds.md. */
#include "ped_internal.h"
#include "trigger.h"
#include <math.h>
#include <stdlib.h>

static int bk(int32_t v) { return v >> 22; }

/* the grid node moves when the 2 x 2-block cell changes */
static void move_to(Ped *p, int32_t x, int32_t y)
{
    if (((p->spr.x ^ x) & 0xff800000) != 0 || ((p->spr.y ^ y) & 0xff800000) != 0) {
        coll_remove(p, p->spr.unk20);
        coll_insert(COLL_PED, p->id, p, p->spr.unk20, x, y);
    }
    p->spr.x = x;
    p->spr.y = y;
}

/* The pixel distance the original computes inline: |dx| and |dy| in whole pixels; if one is 0 the
   other, else (int)sqrt(dx * dx + dy * dy) (FILD / FSQRT / _ftol, truncating). For integer squares
   below 2^31 the x87 extended sqrt and a double sqrt truncate to the same integer. */
static int pixel_dist(int32_t dx, int32_t dy)
{
    int ax = abs(dx) >> 16, ay = abs(dy) >> 16;
    if (ax == 0) return ay;
    if (ay == 0) return ax;
    return (int)sqrt((double)(ay * ay + ax * ax));
}
/* the larger of |dx| and |dy| in pixels (the original's other distance) */
static int pixel_max(int32_t dx, int32_t dy)
{
    int ax = abs(dx) >> 16, ay = abs(dy) >> 16;
    return ay < ax ? ax : ay;
}

/* g_peds[id] for ids the original uses unchecked (a -1 target reads the record before the table):
   the port reads ped 0 for an id outside the table. */
static Ped *ped_at(int id)
{
    id = (int16_t)id;
    return &g_peds[id >= 0 && id < PED_MAX ? id : 0];
}
/* likewise for car ids (Ped_AttackTarget / Ped_ComputeStep pass ped ids or -1 as cars in places) */
static Car *car_at(int id)
{
    id = (int16_t)id;
    return car_get(id >= 0 && id < CAR_MAX ? id : 0);
}

/* the heading snapped to the nearest of the four axes (by eighths: 0, 7 -> 0, 1, 2 -> 0x100 ...) */
static int16_t snap_octant(int a)
{
    switch (a / 128) {
    case 1: case 2: return 0x100;
    case 3: case 4: return 0x200;
    case 5: case 6: return 0x300;
    default: return 0;
    }
}
/* the heading's quarter by sixteenths (0, 1, 14, 15 -> 0, 2..5 -> 0x100 ...) */
static int quarter(int a)
{
    switch ((int16_t)(a / 64)) {
    case 2: case 3: case 4: case 5: return 0x100;
    case 6: case 7: case 8: case 9: return 0x200;
    case 10: case 11: case 12: case 13: return 0x300;
    default: return 0;
    }
}

/* Map_SlopeDelta 0x454c60: the height change from (x1, y1) to (x2, y2) on a block of type map `type`
   (slope bits 8..13): two-block slopes 1..8 change by half the distance, eight-block slopes 9..0x28
   by an eighth, one-block slopes 0x29..0x2c by all of it, along y (types 1-4, 9-0x18, 0x29 / 0x2a)
   or x; downhill directions negate. Another slope type is fatal (-0x4a). */
int map_slope_delta(uint32_t type, int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    int s = (int)(type >> 8) & 0x3f;
    if ((type & 0x3f00) == 0) return 0;
    switch (s) {
    case 1: case 2: return (y2 - y1) >> 1;
    case 3: case 4: return -((y2 - y1) >> 1);
    case 5: case 6: return (x2 - x1) >> 1;
    case 7: case 8: return -((x2 - x1) >> 1);
    case 9: case 10: case 11: case 12: case 13: case 14: case 15: case 16: return (y2 - y1) >> 3;
    case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17: case 0x18: return -((y2 - y1) >> 3);
    case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f: case 0x20: return (x2 - x1) >> 3;
    case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27: case 0x28: return -((x2 - x1) >> 3);
    case 0x29: return y1 - y2;
    case 0x2a: return y2 - y1;
    case 0x2b: return x2 - x1;
    case 0x2c: return x1 - x2;
    default: game_fatal(-0x4a, 0x54, s);
    }
    return 0;
}

/* Ped_IsDirClear 0x455dc0 / Ped_IsDirClearFar 0x455fc0: can a ped at (x, y, z) walk toward `angle`?
   Not if a corner of a box ahead (8 x 8 at 16 pixels; far: 16 x 16 at 32) is over a building or air,
   a map wall is hit (6 x 6 at 6 then 4 x 4 at 4; far: 12 x 12 at 24 then 4 x 4), a slope edge is hit
   (the box of the wall test, or the first box again when the ground was fine) or the block 6 pixels
   ahead is railway. All boxes are built in the one shared box (0x728450). */
static bool dir_clear(int32_t x, int32_t y, int32_t z, int angle, bool far)
{
    int a = angle & 0x3ff;
    int r1 = far ? 0x20 : 0x10, h1 = far ? 0x10 : 8, r2 = far ? 0x18 : 6, h2 = far ? 0xc : 6;
    CollBox *b = coll_build_box(SIN(a) * r1 + x, COS(a) * r1 + y, z, h1, h1, 0, 10, &g_ped_box);
    bool ground = false, wall = false;
    for (int k = 0; k < 4; k++) {
        int t = ped_ground_type(bk(b->x[k]), bk(b->y[k]), bk(z));
        if (t == 5 || t == 0) {
            ground = true;
            break;
        }
    }
    b = coll_build_box(x + SIN(a) * r2, y + COS(a) * r2, z, h2, h2, 0, 10, &g_ped_box);
    coll_compute_bounds(b);
    if (coll_map_walls(b, 0) != -1) {
        wall = true;
    } else {
        b = coll_build_box(x + SIN(a) * 4, y + COS(a) * 4, z, 4, 4, 0, 10, &g_ped_box);
        coll_compute_bounds(b);
        if (coll_map_walls(b, 0) != -1) wall = true;
    }
    if (!ground) {
        if (far) b = coll_build_box(SIN(a) * 0x20 + x, COS(a) * 0x20 + y, z, 0x10, 0x10, 0, 10, &g_ped_box);
        else b = coll_build_box(x + SIN(a) * 8, y + COS(a) * 8, z, 8, 8, 0, 10, &g_ped_box);
        coll_compute_bounds(b);
    }
    int slope = coll_map_slopes(b, 1);
    bool rail = map_test_block_attr(1, bk(x + SIN(a) * 6), bk(y + COS(a) * 6), bk(z)) != 0;
    return !rail && !ground && !wall && slope == -1;
}
bool ped_is_dir_clear(int32_t x, int32_t y, int32_t z, int angle) { return dir_clear(x, y, z, angle, false); }
bool ped_is_dir_clear_far(int32_t x, int32_t y, int32_t z, int angle) { return dir_clear(x, y, z, angle, true); }

/* Ped_CanSidestep 0x455c50: stepping aside (dir 1: to heading - 0x100, else + 0x100) stays one to
   three blocks inside the map's 2..0xfc block margin (the original only tests the bounds there),
   and two short probes (6 pixels to the - 0x100 side, 4 pixels to the + 0x100 side) hit no map wall.
   A y below the margin ends the loop as a failure too. */
static bool ped_can_sidestep(const Ped *p, int dir)
{
    for (int k = 1;; k++) {
        int a = (dir == 1 ? (uint16_t)p->spr.angle - 0x100 : (uint16_t)p->spr.angle + 0x100) & 0x3ff;
        int bx = bk(SIN(a) * k * 0x40 + p->spr.x);
        if (bx < 2 || bx > 0xfc) return false;
        int by = bk(COS(a) * k * 0x40 + p->spr.y);
        if (by < 2 || by > 0xfc) return false;
        if (k + 1 > 3) {
            int l = ((uint16_t)p->spr.angle - 0x100) & 0x3ff, r = ((uint16_t)p->spr.angle + 0x100) & 0x3ff;
            CollBox *b = coll_build_box(p->spr.x + SIN(l) * 6, p->spr.y + COS(l) * 6, p->spr.z, 6, 6, 0, 10, &g_ped_box);
            coll_compute_bounds(b);
            if (coll_map_walls(b, 0) != -1) return false;
            b = coll_build_box(p->spr.x + SIN(r) * 4, p->spr.y + COS(r) * 4, p->spr.z, 4, 4, 0, 10, &g_ped_box);
            coll_compute_bounds(b);
            return coll_map_walls(b, 0) == -1;
        }
    }
}

/* Ped_TurnAtObstacle 0x4561c0 (Ped_Process passes its "blocked" flag; the original reads only the
   ped): the heading snaps to an axis; a player ped without a walk target stops there. If ahead is
   blocked the ped turns a quarter: to the side that is clear, and with both clear by its state:
   fleeing (state 1) away from the target point (or the target ped when none), going somewhere
   (state 4) toward it if it can step that way, else (0x72844c, never changed: the original
   increments 0x74f0fe instead) always the same side. A ped walking to a car door (mode 0xb) turns
   about instead. */
void ped_turn_at_obstacle(Ped *p, int blocked)
{
    (void)blocked;
    p->spr.angle = (int16_t)quarter(p->spr.angle);
    if (p->player_ctl == 1 && p->walk_x < 1) return;
    if (ped_is_dir_clear(p->spr.x, p->spr.y, p->spr.z, p->spr.angle)) return;
    bool left = ped_is_dir_clear(p->spr.x, p->spr.y, p->spr.z, p->spr.angle + 0x100);
    bool right = ped_is_dir_clear(p->spr.x, p->spr.y, p->spr.z, p->spr.angle - 0x100);
    int dir;   /* 1: turn -0x100, -1: turn +0x100 */
    if (!right) {
        dir = -1;
    } else if (!left) {
        dir = 1;
    } else if (p->state == 1) {
        int16_t a = p->spr.angle;
        const Ped *t = ped_at(p->target_ped);
        dir = 1;
        if (a == 0x200) {
            if (p->target_x == 0 ? p->spr.x <= t->spr.x : p->spr.x <= p->target_x) dir = -1;
        } else if (a == 0) {
            if (p->target_x == 0 ? t->spr.x < p->spr.x : p->target_x < p->spr.x) dir = -1;
        } else if (a == 0x100) {
            if (p->target_x == 0 ? p->spr.y <= t->spr.y : p->spr.y <= p->target_y) dir = -1;
        } else if (a == 0x300) {
            if (p->target_x == 0 ? t->spr.y < p->spr.y : p->target_y < p->spr.y) dir = -1;
        }
    } else if (p->state == 4) {
        int16_t a = p->spr.angle;
        const Ped *t = ped_at(p->target_ped);
        bool toward;   /* the original's "first" branch: try stepping to the + side */
        if (a == 0x200) toward = p->spr.x > (p->target_x == 0 ? t->spr.x : p->target_x);
        else if (a == 0) toward = p->spr.x <= (p->target_x == 0 ? t->spr.x : p->target_x);
        else if (a == 0x100) toward = p->spr.y > (p->target_x == 0 ? t->spr.y : p->target_y);
        else if (a == 0x300) toward = p->spr.y <= (p->target_x == 0 ? t->spr.y : p->target_y);
        else {
            dir = 1;
            goto turn;
        }
        if (toward) dir = ped_can_sidestep(p, -1) ? -1 : 1;
        else dir = ped_can_sidestep(p, 1) ? 1 : -1;
    } else {
        dir = 1;
        if (g_ped_72844c == 0) dir = -1;
        g_ped_74f0fe++;   /* (the original's slip: the counter it tests is never incremented) */
        if (g_ped_72844c > 1) g_ped_72844c = 0;
    }
turn:
    if (p->mode == 0xb && p->walk_x > 0) {
        p->spr.angle = (int16_t)(((uint16_t)p->spr.angle - 0x200) & 0x3ff);
        p->u12 = (int16_t)-p->u12;
        return;
    }
    p->u88 = 0;
    if (dir == -1) p->spr.angle = (int16_t)(((uint16_t)p->spr.angle + 0x100) & 0x3ff);
    else p->spr.angle = (int16_t)(((uint16_t)p->spr.angle - 0x100) & 0x3ff);
}

/* Ped_CheckAhead 0x456640: 48 pixels ahead of the ped (inside the map) a car other than `c` (a
   16 x 16 box at c's height), or 96 pixels ahead a building or air block at c's layer. The list is
   left locked when a car is found (the callers unlock). */
bool ped_check_ahead(const Car *c, const Ped *p)
{
    int a = (uint16_t)p->spr.angle & 0x3ff;
    int32_t x = SIN(a) * 0x30 + p->spr.x, y = COS(a) * 0x30 + p->spr.y;
    if (!(x > 0x100000 && y > 0x100000 && x < 0x3ff00000 && y < 0x3ff00000)) return false;
    CollBox *b = coll_build_box(x, y, c->spr.z, 0x10, 0x10, 0, 10, &g_ped_box);
    if (coll_query_box(b, COLL_CAR, COLL_CAR, c->id)) return true;
    coll_unlock();
    int t = ped_ground_type(bk(SIN(a) * 0x60 + p->spr.x), bk(COS(a) * 0x60 + p->spr.y), bk(c->spr.z));
    return t == 5 || t == 0;
}

/* Ped_ChooseTurn 0x456ec0: the axis to turn to from heading `angle` (by quarters) toward (tx, ty)
   from (x, y); heading north / south (0) also by the side of the last swerve (+0x12). */
static int ped_choose_turn(int32_t x, int32_t y, int32_t tx, int32_t ty, int angle, int16_t swerve)
{
    int q = quarter((int16_t)angle);
    if (q == 0x200) return tx <= x ? 0x300 : 0x100;
    if (q == 0) {
        if (tx <= x) return swerve < 0 ? 0x100 : 0x300;
        return swerve < 0 ? 0x300 : 0x100;
    }
    return y <= ty ? 0 : 0x200;
}

/* Ped_SteerToTarget 0x455040: a ped with a walk target (+0x38) heads for it.
   - Modes about cars (1, 3, 4, 5, 0xb, 0xe, 0x3d, 200) retarget to the car's door point (mode 0xe the
     far side) while the car stands and the point is near (within 0x50 blocks... 0x1400000).
   - Within 18 pixels (8 with 0x74f100 = 1) of the target, walking: the mode's arrival action: 0
     stands facing +0x44, 0xb (a player at a car door) gets in (Ped_TryEnterCar with {2, ped}, the car
     taken from its driver when slow), 0x37 / 0x38 get in as states 0xf / 0x10, 0x39 stops, 0x3c
     (a pavement detour) squares the heading, 100 waits (state 3).
   - Else the heading is the octant toward the target; when it changes, cars near (a 32 x 32 box),
     on the way and close (10 x 10, 20 x 20) decide between turning straight to the target, swerving
     around by quarters (+0xf4 / +0xf8 / +0xfa count the attempts) or turning by eighths.
   Finally a new heading straight at the target must be clear (Ped_IsDirClear), and a ped without a
   target ped also needs room to both sides ahead, else it keeps its old heading (and may start the
   0xd wait). A ped stopped while walking gets speed 1. */
void ped_steer_to_target(Ped *p)
{
    int32_t range = 0x120000, range0 = 0x120000;
    int16_t s1 = p->spr.angle;
    Car *c = NULL;
    bool close10 = false, close20 = false;
    int16_t l14 = 0;
    if (g_ped_74f100 == 1) range = range0 = 0x80000;
    int16_t m = p->mode;
    if (m == 1 || m == 3 || m == 0xb || m == 4 || m == 5 || m == 0xe || m == 0x3d || m == 200) {
        c = car_get(p->car);
        int r = c->door_dx, a = c->spr.angle & 0x3ff;
        int32_t px, py;
        if (p->mode == 0xe) {
            px = c->spr.x - SIN(a) * r;
            py = c->spr.y - COS(a) * r;
        } else {
            px = SIN(a) * r + c->spr.x;
            py = COS(a) * r + c->spr.y;
        }
        int b = (c->spr.angle + 0x100) & 0x3ff;
        px = SIN(b) * c->door_dy + px;
        py = COS(b) * c->door_dy + py;
        range = range0;
        if (c->speed == 0 && abs(p->walk_x - px) < 0x1400000 && abs(p->walk_y - py) < 0x1400000) {
            p->walk_x = px;
            p->walk_y = py;
        }
    }
    int32_t y0 = p->spr.y, x0 = p->spr.x, wx = p->walk_x, wy = p->walk_y;
    if (x0 - range < wx && wx < x0 + range && y0 - range < wy && wy < y0 + range && p->anim < 0x11) {
        switch (p->mode) {
        case 0:
            p->u48 = 1;
            p->spr.angle = p->u44;
            p->walk_x = 0;
            break;
        case 0xb:
            if (p->player_ctl == 1) {
                p->spr.angle = p->u44;
                p->speed = 0;
                p->walk_x = 0;
                p->u48 = 0;
                if (c->speed < 5) {
                    move_to(p, wx, wy);
                    p->u40 = c->driver;
                }
                if (p->u40 != -1) g_peds[p->u40].car = c->id;
                g_ped_enter_ref[1] = p->id;
                g_ped_enter_ref[0] = 2;
                p->anim = 1;
                p->u78 = 0xb;
                ped_try_enter_car(g_ped_enter_ref);
            }
            break;
        case 0x37:
            g_ped_enter_ref[1] = p->id;
            p->walk_x = 0;
            g_ped_enter_ref[0] = 2;
            p->state = 0xf;
            ped_try_enter_car(g_ped_enter_ref);
            break;
        case 0x38:
            p->state = 0x10;
            p->walk_x = 0;
            p->accel = 0;
            g_ped_enter_ref[1] = p->id;
            g_ped_enter_ref[0] = 2;
            ped_try_enter_car(g_ped_enter_ref);
            break;
        case 0x39:
            p->mode = -1;
            p->walk_x = 0;
            p->speed = 0;
            p->accel = 0;
            break;
        case 0x3c: {
            int16_t a = p->spr.angle;
            p->walk_x = 0;
            p->mode = -1;
            p->spr.angle = snap_octant(a);
            break;
        }
        case 100:
            p->walk_x = 0;
            p->speed = 0;
            p->spr.angle = p->u44;
            p->state = 3;
            p->u78 = 8;
            p->u7c = 8;
            break;
        }
    } else if (p->anim < 0x11) {
        int16_t s = p->spr.angle;
        int32_t xr = x0 + range, xl = x0 - range, yr = y0 + range, yl = y0 - range;
        if (xr < wx) s = 0x100;
        if (wx < xl) s = 0x300;
        if (yr < wy) s = 0;
        if (wy < yl) s = 0x200;
        if (xr < wx && yr < wy) s = 0x80;
        if (wx < xl && yr < wy) s = 0x380;
        if (xr < wx && wy < yl) s = 0x180;
        if (wx < xl && wy < yl) s = 0x280;
        int16_t s2 = p->spr.angle;
        if (s != s2) {
            if (p->u12 == 0 && p->u78 == 0xb) {
                if (p->target_ped < 0 || ped_at(p->target_ped)->car < 0) {
                    int i6 = s2, i7 = s;
                    int v;
                    if ((i6 + 0x80 < i7 && ((i7 - i6) & 0x3ff) < 0x200) || (i7 < i6 - 0x80 && 0x200 < ((i6 - i7) & 0x3ff)))
                        v = s2 + 0x80;
                    else if ((i7 <= i6 + 0x80 || ((i7 - i6) & 0x3ff) < 0x201) && (i6 - 0x80 <= i7 || 0x1ff < ((i6 - i7) & 0x3ff)))
                        goto probe;
                    else
                        v = s2 - 0x80;
                    p->spr.angle = (int16_t)(v & 0x3ff);
                    if (p->spr.angle == p->prev_angle) p->spr.angle = (int16_t)(((uint16_t)p->spr.angle - 400) & 0x3ff);
                } else {
                    coll_unlock();   /* (the original reads the target's car and unlocks here) */
                    if (p->speed == 0 && p->anim < 0x11) p->speed = 1;
                    if (p->car != -1 && p->control == 2) p->u48 = 0;
                }
            }
        probe:;
            CollBox *b = coll_build_box(p->spr.x, p->spr.y, p->spr.z, 0x20, 0x20, 0, 10, &g_ped_box);
            bool near = coll_query_box(b, COLL_CAR, COLL_PED, p->id) != NULL;
            coll_unlock();
            int a = math_atan2(p->walk_y - y0, p->walk_x - x0);
            int32_t x = p->spr.x, y = p->spr.y;
            l14 = (int16_t)a;
            uint16_t d1 = (uint16_t)((p->spr.angle - l14) & 0x3ff), d2 = (uint16_t)((l14 - p->spr.angle) & 0x3ff);
            int d = pixel_dist(p->walk_x - x, p->walk_y - y);
            CollHit *h;
            if (d < 0x41) {
                b = coll_build_box(SIN(a & 0x3ff) * (d / 2) + x, COS(a & 0x3ff) * (d / 2) + y, p->spr.z, 2, (d - 4) / 2, 0, 10,
                                   &g_ped_box);
                h = coll_query_box(b, COLL_CAR, COLL_PED, p->id);
                coll_unlock();
            } else {
                b = coll_build_box(SIN(a & 0x3ff) * 0x20 + x, COS(a & 0x3ff) * 0x20 + y, p->spr.z, 4, 0x1c, 0, 10, &g_ped_box);
                h = coll_query_box(b, COLL_CAR, COLL_PED, p->id);
                coll_unlock();
            }
            bool ahead = h != NULL;
            CollHit *last = NULL;
            if (near) {
                b = coll_build_box(p->spr.x, p->spr.y, p->spr.z, 10, 10, 0, 10, &g_ped_box);
                close10 = coll_query_box(b, COLL_CAR, COLL_PED, p->id) != NULL;
                coll_unlock();
                b = coll_build_box(p->spr.x, p->spr.y, p->spr.z, 0x14, 0x14, 0, 10, &g_ped_box);
                last = coll_query_box(b, COLL_CAR, COLL_PED, p->id);
                close20 = last != NULL;
                coll_unlock();
            }
            coll_unlock();
            if (last == NULL || (!close10 && near)) {
                int16_t sw;
                if (ahead && near == ahead && (sw = p->u12) != 0 && close20 == ahead) {
                    if (!close10 && (p->uf4 == 1 || p->uf8 > 10)) {
                        /* going round the car: a quarter turn to the swerve side, the other way
                           after four tries */
                        if (p->uf4 == 0) p->ufa++;
                        else p->ufa = 0;
                        p->uf4 = 0;
                        p->uf8 = 0;
                        if (sw < 1) {
                            if (p->ufa > 3) {
                                p->ufa = 0;
                                p->spr.angle = (int16_t)(((uint16_t)p->spr.angle - 0x100) & 0x3ff);
                            } else {
                                p->spr.angle = (int16_t)(((uint16_t)p->spr.angle + 0x100) & 0x3ff);
                            }
                        } else if (p->ufa < 4) {
                            p->spr.angle = (int16_t)(((uint16_t)p->spr.angle - 0x100) & 0x3ff);
                        } else {
                            p->ufa = 0;
                            p->spr.angle = (int16_t)(((uint16_t)p->spr.angle + 0x100) & 0x3ff);
                        }
                        coll_unlock();
                        int na = (uint16_t)p->spr.angle & 0x3ff;
                        b = coll_build_box(p->spr.x + SIN(na) * 0xc, p->spr.y + COS(na) * 0xc, p->spr.z, 4, 4, 0, 10, &g_ped_box);
                        h = coll_query_box(b, COLL_CAR, COLL_PED, p->id);
                        coll_unlock();
                        if (h) {
                            p->uf8 = 5;
                            if (p->u12 < 1) p->spr.angle = (int16_t)(((uint16_t)p->spr.angle - 0x100) & 0x3ff);
                            else p->spr.angle = (int16_t)(((uint16_t)p->spr.angle + 0x100) & 0x3ff);
                        }
                    }
                } else {
                    if (!close10 && !close20) {
                        if (p->player_ctl != 1 && (p->state == 1 || p->state == 4)) {
                            p->uf4 = 0;
                            p->u12 = 0;
                            p->uf8 = 0;
                            p->ufa = 0;
                        }
                        if (d1 < d2) {
                            if (d1 < 0x201) p->spr.angle = l14;
                            else p->spr.angle = (int16_t)(((uint16_t)p->spr.angle - 0x80) & 0x3ff);
                        } else {
                            if (d2 < 0x201) p->spr.angle = l14;
                            else p->spr.angle = (int16_t)(((uint16_t)p->spr.angle + 0x80) & 0x3ff);
                        }
                    }
                    if (near && !ahead) p->spr.angle = l14;
                }
            }
            coll_unlock();
        }
    }
    /* (l14 stays 0 when no new heading was computed: a ped heading 0 still gets the test) */
    if (p->spr.angle == l14 && p->state != 0x11 && p->state != 0xf && p->state != 0x10) {
        if (!ped_is_dir_clear(p->spr.x, p->spr.y, p->spr.z, p->spr.angle)) {
            p->spr.angle = s1;
        } else if (p->target_ped == -1 && p->mode != 0xb && p->u78 != 0xe && p->u78 != 0xf) {
            if (ped_is_dir_clear_far(p->spr.x, p->spr.y, p->spr.z, ((uint16_t)s1 - 0x80) & 0x3ff)) {
                if (ped_is_dir_clear_far(p->spr.x, p->spr.y, p->spr.z, ((uint16_t)s1 + 0x80) & 0x3ff) &&
                    ped_is_dir_clear_far(p->spr.x, p->spr.y, p->spr.z, p->spr.angle))
                    goto done;
                p->u78 = 0xd;
                p->u84 = 4;
            }
            p->spr.angle = s1;
        }
    }
done:
    if (p->speed == 0 && p->anim < 0x11) p->speed = 1;
    if (p->car != -1 && p->control == 2) p->u48 = 0;
}

/* the point to aim at around a target 32..64 pixels away (half the side offset) or further (all
   of it), to the side `a2` of the line of sight */
static void aim_offset(int d, int a2, int off, int32_t *tx, int32_t *ty)
{
    if (d < 0x41) {
        if (d > 0x20) {
            *tx = SIN(a2 & 0x3ff) * (int)((unsigned)off / 2) + *tx;
            *ty = COS(a2 & 0x3ff) * (int)((unsigned)off / 2) + *ty;
        }
    } else {
        *tx = SIN(a2 & 0x3ff) * off + *tx;
        *ty = COS(a2 & 0x3ff) * off + *ty;
    }
}

/* the machine gun's pause after a burst: 0x14 frames after 4 bursts (+0x47), else none */
static uint8_t mg_pause(const Ped *p) { return (int8_t)p->u47 < 4 ? 0 : 0x14; }

/* Ped_AttackTarget 0x456770: an armed AI ped faces its target ped (or the target's car, or for
   objective 0x30 the car a player drives, or for 0x39 a car by id) and fires in bursts: +0x68 counts
   down the frames between bursts, action 0x10 is firing (until +0x64 runs out). Some objectives aim
   beside the target (an offset of 16 / 8 pixels, alternating sides through 0x7284c8) on all but every
   10th / 5th aim (0x7284d8 counts). Waiting (state 3) it only aims; going to it (state 4) it also
   stops firing when a car is in the line of fire. Off screen it stops (except objectives 0x30 /
   0x39). Without a target it picks the first ped within 128 pixels (the result list stays locked:
   ComputeStep never calls it without one). */
static void ped_attack_target(Ped *p)
{
    int8_t every = 0, off = 0;
    switch (p->objective) {
    case 0x18: case 0x1c: case 0x29: case 0x2b: case 0x37:
        every = 10, off = 0x10;
        break;
    case 0x1a: case 0x1d: case 0x2c: case 0x2d:
        every = 5, off = 8;
        break;
    case 0x1b: case 0x1e: case 0x1f: case 0x20: case 0x2e: case 0x2f: case 0x33:
        every = 0;
        g_ped_7284d8 = 1;
        break;
    }
    if (p->target_ped == -1) {
        CollBox *b = coll_build_box(p->spr.x, p->spr.y, p->spr.z, 0x80, 0x80, 0, 10, &g_ped_box);
        CollHit *h = coll_query_box(b, COLL_PED, COLL_PED, p->id);
        if (!h) return;
        p->state = 4;
        p->target_ped = ((const Ped *)h->owner)->id;
        return;
    }
    if (p->state == 3) {
        p->speed = 0;
        p->u12 = 0;
        if (!ped_is_on_screen(p) && p->objective != 0x30) {
            p->firing = 0;
            p->u7c = 8;
            p->u68 = 0;
        } else {
            Ped *t = ped_at(p->target_ped);
            const Car *aim = NULL;
            bool use_car = false;
            if (t->state == 7 && p->objective != 0x30) {
                use_car = true;
                if (t->car > -1) aim = car_at(t->car);
                /* (no car: the original reads a null car record; the port keeps the heading) */
            } else if (p->objective == 0x30) {
                use_car = true;
                int cid = p->target_ped;   /* (the ped id is used as a car id) */
                if (t->player_ctl == 1 && mission_has_player_slot(player_find_by_ped(t->id)))
                    cid = player_get_controlled_id(player_find_by_ped(p->target_ped));
                aim = car_at(cid);
            }
            if (use_car) {
                if (aim) p->spr.angle = (int16_t)math_atan2(aim->spr.y - p->spr.y, aim->spr.x - p->spr.x);
            } else if (every < g_ped_7284d8) {
                g_ped_7284d8 = 0;
                p->spr.angle = (int16_t)math_atan2(t->spr.y - p->spr.y, t->spr.x - p->spr.x);
            } else {
                int a = (uint16_t)math_atan2(t->spr.y - p->spr.y, t->spr.x - p->spr.x);
                p->spr.angle = (int16_t)a;
                int32_t tx = t->spr.x, ty = t->spr.y;
                int d = pixel_dist(tx - p->spr.x, ty - p->spr.y);
                int a2 = g_ped_7284c8 == 0 ? a + 0x100 : a - 0x100;
                aim_offset(d, a2, off, &tx, &ty);
                p->spr.angle = (int16_t)math_atan2(ty - p->spr.y, tx - p->spr.x);
                g_ped_7284d8++;
            }
            if (p->u7c == 0x10) {
                if (p->u64 == 0) {
                    p->u7c = 8;
                    if (p->objective == 0x33) {
                        p->u68 = 0x14;
                    } else {
                        p->u68 = p->state == 3 ? 0x32 : 100;
                        if (p->weapon == 2) p->u68 = mg_pause(p);
                    }
                }
            } else if (p->u68 == 0) {
                g_ped_7284d8++;
                p->firing = 1;
                if ((int8_t)p->u47 > 5) p->u47 = 0;
                p->u7c = 0x10;
                return;
            } else {
                p->u68--;
                p->firing = 0;
                p->u7c = 8;
            }
        }
    }
    if (p->state != 4 || p->u78 == 0xd) goto tail;
    {
        int16_t saved = p->spr.angle;
        if (!ped_is_on_screen(p) && p->objective != 0x39) {
            p->firing = 0;
            p->u7c = 8;
            p->u68 = 0x32;
            goto tail;
        }
        int tp = p->target_ped;
        Ped *t = ped_at(tp);
        if (t->state == 7 && p->objective != 0x39) {
            if (t->car > -1) {
                const Car *cc = car_at(t->car);
                p->spr.angle = (int16_t)math_atan2(cc->spr.y - p->spr.y, cc->spr.x - p->spr.x);
            }
        } else if (p->objective == 0x39) {   /* the target is a car id */
            if (tp > -1) {
                const Car *cc = car_at(tp);
                p->spr.angle = (int16_t)math_atan2(cc->spr.y - p->spr.y, cc->spr.x - p->spr.x);
            }
        } else if (every < g_ped_7284d8) {
            g_ped_7284d8 = 0;
            p->spr.angle = (int16_t)math_atan2(t->spr.y - p->spr.y, t->spr.x - p->spr.x);
        } else {
            int a = (uint16_t)math_atan2(t->spr.y - p->spr.y, t->spr.x - p->spr.x);
            p->spr.angle = (int16_t)a;
            int32_t tx = t->spr.x, ty = t->spr.y;
            int d = pixel_dist(tx - p->spr.x, ty - p->spr.y);
            aim_offset(d, a + 0x100, off, &tx, &ty);   /* (always the + side here) */
            g_ped_7284d8++;
            p->spr.angle = (int16_t)math_atan2(ty - p->spr.y, tx - p->spr.x);
        }
        if (p->u7c == 0x10) {
            if (p->u64 != 0) return;   /* (without the 0x7284c8 toggle) */
            p->u7c = 8;
            p->firing = 0;
            p->u68 = 200;
            if (p->weapon == 2) p->u68 = mg_pause(p);
        } else if (p->u68 == 0) {
            p->firing = 1;
            if ((int8_t)p->u47 > 7) p->u47 = 0;
            p->u7c = 0x10;
            CollBox *b = coll_build_box(p->spr.x + SIN(p->spr.angle) * 8, p->spr.y + COS(p->spr.angle) * 8, p->spr.z,
                                        4, 4, 0, 10, &g_ped_box);
            CollHit *h = coll_query_box(b, COLL_CAR, COLL_PED, p->id);
            coll_unlock();
            if (!h) return;
            p->u68 = 0x14;
            p->spr.angle = saved;
            p->u7c = 8;
            return;
        } else {
            p->spr.angle = saved;
            p->u68--;
            p->firing = 0;
            p->u7c = 8;
        }
    }
tail:
    if (++g_ped_7284c8 > 1) g_ped_7284c8 = 0;
}

/* a random swerve of the heading by -0x20..0x1f, keeping the sign of the last swerve (+0x12) */
static void wobble(Ped *p)
{
    int16_t old = p->u12;
    int16_t v = (int16_t)((int16_t)math_random() % 64 - 0x20);
    p->u12 = v;
    if (old < 0 && v < 0) p->u12 = (int16_t)abs(v);
    if (old > 0 && p->u12 > 0) p->u12 = (int16_t)-p->u12;
}
static void wobble_turn(Ped *p)
{
    wobble(p);
    p->spr.angle = (int16_t)((p->u12 + p->spr.angle) & 0x3ff);
}

/* the group member a follower (objective 0x16) keeps up with: the original reads the short at
   0x72849a + slot * 2 of its group, i.e. the member before it (slot 0 reads the count byte and the
   padding as a ped id) */
static Ped *group_ahead(const Ped *p)
{
    int g = (int8_t)p->group, s = (int8_t)p->group_slot;
    long off = 2 + (long)s * 2;
    if (g < 0 || g >= PED_GROUPS || off < 0 || off + 2 > (long)sizeof(PedGroup)) return ped_at(0);
    const uint8_t *b = (const uint8_t *)&g_ped_groups[g] + off;
    return ped_at((int16_t)(b[0] | b[1] << 8));
}

/* the getting-in sequence of objectives 0x28 / 0x35 at the car (the original's 0x458465 path) */
static void enter_now(Ped *p)
{
    int16_t id = p->car;
    p->speed = 0;
    p->u48 = 0;
    Car *c = car_get(id);
    p->car = id;
    p->spr.angle = c->spr.angle;
    CAR_I16(c, 0xdc) = 0;
    CAR_I16(c, 0xb6) = 0;
    p->u40 = c->driver;
    ped_finish_enter_car(p, c->id);   /* (Ped_FinishEnterCar takes the car record and the ped id) */
    p->u7c = 0x11;
    p->u78 = 8;
    p->state = 7;
}

/* Ped_ComputeStep 0x456fb0: the next position (*x, *y) of an AI ped: speed (+6) along the heading,
   both chosen here by state:
   - state 1 (fleeing from the target point / ped; given up after 500 frames unless a mission ped):
     every 5th call it picks the way away, probing for buildings, water, rails, walls and slopes
     (wait 0xd), at its move speed;
   - state 2 (wandering): a new mood (+0x7c) every so often from the cycle 0x74f0fb: walk (2), wait
     (4, 5), slow (7), stand (8), with a duration (+0x84) from Math_Random;
   - state 3 (waiting): stands in action 8;
   - state 4 (going to a target point, ped or car): steering every 5th call, then by objective:
     following the group (0x16), going to a point (default), chasing a ped (0x1c..), mugging (0x1f,
     0x21: pulls the victim down), getting a car (0x27 hijack / 0x28 enter, 0x34 / 0x35 door sides),
     0x39 near a car;
   - state 10 (falling): speed ±2, health - 2.
   Then (outside states 1, 4, 5, 10, 0xb, 0xe, the 0xd wait, firing, anims past 0xae, objective
   0x23) the action (+0x7c) swerves or turns it: random swerves while walking (2), slow (4), wait (5),
   7; standing (8) looks about; 9 / 10 / 0xc / 0xd turn and square the heading after +0x84 frames.
   Attacking objectives aim first (Ped_AttackTarget); objective 0x15 looks for the player within 32
   pixels. */
void ped_compute_step(int32_t *x, int32_t *y, Ped *p)
{
    uint8_t gtype = 0;   /* local_6 */
    int16_t ped_s5 = 0;  /* the ped whose speed some objectives copy (the original's leftover sVar5) */
    if (++g_ped_74f0fb > 0x14) g_ped_74f0fb = 1;
    switch (p->objective) {
    case 0x15: {
        CollBox *b = coll_build_box(p->spr.x, p->spr.y, p->spr.z, 0x20, 0x20, 0, 10, &g_ped_box);
        CollHit *h = coll_query_box(b, COLL_PED, COLL_PED, p->id);
        if (h && ((const Ped *)h->owner)->id == (int16_t)g_ped_player_ped) {
            p->state = 4;
            p->target_ped = (int16_t)g_ped_player_ped;
        }
        coll_unlock();
        break;
    }
    case 0x18: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f: case 0x20: case 0x29: case 0x2b:
    case 0x2c: case 0x2d: case 0x2e: case 0x2f: case 0x30: case 0x33: case 0x37: case 0x39:
        if (p->u7c != 0x12 && p->target_ped != -1 && p->state != 1) ped_attack_target(p);
        break;
    }
    switch (p->state) {
    case 1:
        if (p->u0e >= 0x1f5 && p->u8b == 0) {
            p->u78 = 8;
            p->state = 2;
            p->u7c = 2;
            p->u8b = 0;
            break;
        }
        if (p->u78 == 0xd) {
            if ((int8_t)p->u88 > 4 && p->u7c != 0xf) {
                int a;
                if (p->target_ped == -1) {
                    if (p->target_x < 1 || p->target_y < 1) p->target_x = p->walk_x, p->target_y = p->walk_y;
                    a = math_atan2(p->spr.y - p->target_y, p->spr.x - p->target_x);
                } else {
                    Ped *t = ped_at(p->target_ped);
                    if (t->state != 7 && t->state != 6) {
                        a = math_atan2(p->spr.y - t->spr.y, p->spr.x - t->spr.x);
                    } else if (t->car < 0) {
                        p->target_x = 0;
                        p->state = 2;
                        p->u7c = 2;
                        p->target_y = 0;
                        p->walk_x = p->walk_y = 0;
                        p->target_ped = -1;
                        *x = SIN(p->spr.angle) * p->speed + p->spr.x;
                        *y = COS(p->spr.angle) * p->speed + p->spr.y;
                        return;
                    } else {
                        const Car *c = car_at(t->car);   /* (the car seen from the target, not from the ped) */
                        a = math_atan2(c->spr.y - ped_at(p->target_ped)->spr.y, c->spr.x - ped_at(p->target_ped)->spr.x);
                    }
                }
                int aa = a & 0x3ff, bz = bk(p->spr.z);
                bool bad = false;
                int t = ped_ground_type(bk(p->spr.x + SIN(aa) * 0x14), bk(p->spr.y + COS(aa) * 0x14), bz);
                if (t == 5 || t == 0 || t == 1) bad = true;
                int32_t px = p->spr.x + SIN(aa) * 6, py = p->spr.y + COS(aa) * 6;
                t = ped_ground_type(bk(px), bk(py), bz);
                if (t == 5 || t == 0 || t == 1) bad = true;
                if (map_test_block_attr(1, bk(px), bk(py), bz)) bad = true;
                CollBox *b = coll_build_box(px, py, p->spr.z, 6, 6, 0, 10, &g_ped_box);
                coll_compute_bounds(b);
                int w = coll_map_walls(b, 0);
                if (!bad) {
                    b = coll_build_box(px, py, p->spr.z, 8, 8, 0, 10, &g_ped_box);
                    coll_compute_bounds(b);
                }
                int s = coll_map_slopes(b, 1);
                if (!bad && w == -1 && s == -1) p->u84 = 0;
                p->u88 = 0;
            }
            p->u88++;
            if (p->u84 < 1) {
                p->speed = (int8_t)p->move_speed;
                p->u7c = 2;
                p->u78 = 8;
                p->u84 = 0;
            }
        } else {
            if (p->u88 == 4) {
                int a;
                if (p->target_ped == -1) {
                    if (p->target_x < 1 || p->target_y < 1) p->target_x = p->walk_x, p->target_y = p->walk_y;
                    a = math_atan2(p->spr.y - p->target_y, p->spr.x - p->target_x);
                } else {
                    Ped *t = ped_at(p->target_ped);
                    if (t->state != 7 || t->u7c == 0x13) {
                        a = math_atan2(p->spr.y - t->spr.y, p->spr.x - t->spr.x);
                    } else if (t->car < 0) {
                        p->target_ped = -1;
                        p->state = 2;
                        p->u7c = 2;
                        p->target_x = p->target_y = 0;
                        p->walk_x = p->walk_y = 0;
                        goto tail;
                    } else {
                        const Car *c = car_at(t->car);
                        a = math_atan2(p->spr.y - c->spr.y, p->spr.x - c->spr.x);
                    }
                }
                p->walk_x = SIN(a & 0x3ff) * 0x3f + p->spr.x;
                p->u88 = 0;
                p->walk_y = COS(a & 0x3ff) * 0x3f + p->spr.y;
                ped_steer_to_target(p);
            } else {
                p->walk_x = SIN((uint16_t)p->spr.angle & 0x3ff) * 0x3f + p->spr.x;
                p->walk_y = COS((uint16_t)p->spr.angle & 0x3ff) * 0x3f + p->spr.y;
            }
            if ((int8_t)++p->u88 > 4) p->u88 = 0;
            p->firing = 0;
            p->speed = (int8_t)p->move_speed;
        }
        break;
    case 2: {
        int lid = map_get_lid_below(p->spr.x, p->spr.y, p->spr.z);
        if ((int8_t)lid == '>' || p->objective == 0x15) g_ped_74f0fb = 8;
        if (p->u84 == 0) {
            if (p->u78 == 0xc) p->u78 = 8;
            if (p->u78 == 0x10) p->u78 = 8;
            if (p->objective != 0x24 && p->objective != 0x23) {
                p->u78 = 8;
                int mod = 0;
                switch (g_ped_74f0fb) {
                case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8:
                walk:
                    p->u7c = 2;
                    mod = 400;
                    break;
                case 9: case 10: case 11: case 12: case 13: case 14: p->u7c = 4, mod = 200; break;
                case 15: p->u7c = 5, mod = 200; break;
                case 16: case 17: case 18: case 19:
                    if (p->u7c == 7) goto walk;
                    p->u7c = 7;
                    mod = 64;
                    break;
                case 20: p->u7c = 8, mod = 200; break;
                }
                if (mod) p->u84 = (int16_t)((int16_t)math_random() % mod);
            }
        }
        if (--p->u84 < 0) p->u84 = 0;
        break;
    }
    case 3:
        if (p->u7c == 8) p->speed = 0;
        break;
    case 4:
        if (p->u78 == 0xd) {
            p->u7c = 2;
            p->firing = 0;
        }
        if (p->u7c == 0x10) {
            *x = p->spr.x;
            *y = p->spr.y;
            p->speed = 0;
            return;
        }
        if (p->u7c == 0xf && p->u84 == 0) p->u7c = 2;
        if (p->u78 == 0xd) {
            /* waiting at an obstacle: every 5th call turn to the side of the target if clear */
            if (p->u84 < 5) p->u84--;
            if ((int8_t)p->u88 > 4 && p->u7c != 0x12) {
                int tp = p->target_ped, obj = p->objective;
                int32_t x0 = p->spr.x, y0 = p->spr.y;
                int d;
                int16_t s = p->spr.angle;
                int32_t cx, cy;
                if (tp == -1 && obj != 0x39) {
                    d = ped_choose_turn(x0, y0, p->target_x, p->target_y, p->spr.angle, p->u12);
                } else if (obj == 0x39) {   /* (a -1 target reads car 0 here: car_at) */
                    const Car *c = car_at(tp);
                    p->target_x = p->walk_x = cx = c->spr.x;
                    p->target_y = p->walk_y = cy = c->spr.y;
                    if (s == 0x200 || s == 0) d = cx <= x0 ? 0x300 : 0x100;
                    else if (s == 0x100 || s == 0x300) d = y0 <= cy ? 0 : 0x200;
                    else d = 0x200;
                } else {
                    Ped *t = ped_at(tp);
                    if ((t->state == 7 || t->state == 6) && p->target_x == 0) {
                        const Car *c = car_at(t->car);
                        cx = c->spr.x, cy = c->spr.y;
                    } else if (p->target_x == 0) {
                        cx = t->spr.x, cy = t->spr.y;
                    } else {
                        cx = p->target_x, cy = p->target_y;
                    }
                    if (s == 0x200 || s == 0) d = cx <= x0 ? 0x300 : 0x100;
                    else if (s == 0x100 || s == 0x300) d = y0 <= cy ? 0 : 0x200;
                    else d = 0x200;
                }
                if (ped_is_dir_clear(p->spr.x, p->spr.y, p->spr.z, d)) {
                    p->spr.angle = (int16_t)d;
                    p->u84 = 2;
                }
                p->u88 = 0;
            }
            p->u88++;
            if (p->u84 < 1 && p->u7c != 0x12) {
                p->u7c = 2;
                p->u78 = 8;
                p->u84 = 0;
            }
            break;
        }
        if (p->u88 == 4) {
            int tp = p->target_ped;
            int32_t tx, ty;
            if (tp == -1 && p->objective != 0x39) {
                tx = p->target_x, ty = p->target_y;
            } else if (p->objective == 0x39) {
                const Car *c = car_at(tp);
                tx = c->spr.x, ty = c->spr.y;
                p->target_x = p->walk_x = tx;
                p->target_y = p->walk_y = ty;
            } else {
                Ped *t = ped_at(tp);
                ped_s5 = (int16_t)tp;
                if ((t->state == 7 || t->state == 6) && p->target_x == 0) {
                    const Car *c = car_at(t->car);
                    tx = c->spr.x, ty = c->spr.y;
                } else if (p->target_x != 0) {
                    tx = p->target_x, ty = p->target_y;
                } else {
                    tx = t->spr.x, ty = t->spr.y;
                }
            }
            p->walk_x = tx;
            p->walk_y = ty;
            ped_steer_to_target(p);
            p->u88 = 0;
        } else if (p->walk_x != 0) {
            ped_steer_to_target(p);
        }
        if ((int8_t)++p->u88 > 4) p->u88 = 0;
        if (p->mode == 0x3e && (gtype = (uint8_t)ped_ground_type(bk(p->spr.x), bk(p->spr.y), bk(p->spr.z))) == 2 &&
            p->u78 == 0x11)
            p->u78 = 0x12;
        switch (p->objective) {
        case 0x16: {   /* following the group: keep up with the member ahead */
            const Ped *g = group_ahead(p);
            int d = pixel_dist(g->spr.x - p->spr.x, g->spr.y - p->spr.y);
            if (d < 0xc) {
                p->speed = 0;
                p->anim = 0x88;
            } else if (d < 0x21) {
                p->speed = ped_at(p->target_ped)->speed;
                if (p->speed < 1) p->speed = 1;
            } else {
                p->speed = (int8_t)p->move_speed;
            }
            break;
        }
        default: {   /* to the target point (or the target ped) */
            int32_t tx = p->target_x, ty;
            if (tx == 0) {
                const Ped *t = ped_at(p->target_ped);
                ty = t->spr.y, tx = t->spr.x;
            } else {
                ty = p->target_y;
            }
            int d = pixel_max(tx - p->spr.x, ty - p->spr.y);
            if (d < 8) {
                p->speed = 0;
                p->anim = 0x88;
                if (p->target_x != 0) {
                    if (p->u78 == 0x12 && gtype == 3) {   /* across the road: done crossing */
                        p->target_x = p->target_y = -1;
                        p->walk_x = p->walk_y = -1;
                        p->u78 = 0x10;
                        p->u84 = 100;
                        p->state = 2;
                        p->u7c = 2;
                        p->mode = -1;
                    } else {   /* arrived: wait there facing +0x44 */
                        p->target_x = 0;
                        p->target_y = 0;
                        p->spr.angle = p->u44;
                        p->walk_x = 0;
                        p->speed = 0;
                        p->state = 3;
                        p->u78 = 8;
                        p->u7c = 8;
                    }
                }
            } else if (d < 0xc) {
                p->speed = 2;
            } else if (d < 0x19) {
                if (p->target_x != 0) {
                    p->speed = (int8_t)p->move_speed;
                } else {
                    int16_t s = ped_at(p->target_ped)->speed;
                    p->speed = s;
                    if ((int8_t)p->move_speed <= s) p->speed = (int8_t)p->move_speed;
                    if (p->speed < 1) p->speed = 2;
                }
            } else {
                p->speed = (int8_t)p->move_speed;
            }
            break;
        }
        case 0x1c: case 0x1d: case 0x1e: case 0x2b: case 0x2d: case 0x2f: {   /* chasing a ped */
            const Ped *t = ped_at(p->target_ped);
            int32_t tx, ty;
            if (t->state == 7 || t->state == 6) {
                const Car *c = car_at(t->car);
                tx = c->spr.x, ty = c->spr.y;
            } else {
                tx = t->spr.x, ty = t->spr.y;
            }
            int d = pixel_max(tx - p->spr.x, ty - p->spr.y);
            if (d > 0x1f) {
                if (t->state != 7 && t->state != 6) {
                    int a = p->anim;
                    if (d < 0x41) {
                        if (a != 0x62 && a != 99) {
                            p->speed = ped_at(ped_s5)->speed;   /* (ped 0 unless set above) */
                            if (p->speed == 4) p->speed = 3;
                            if (p->speed < 1) p->speed = 2;
                            break;
                        }
                    } else if (a != 0x62 && a != 99) {
                        p->speed = (int8_t)p->move_speed;
                        break;
                    }
                    p->speed = 0;
                    break;
                }
                if (d > 0x5f) {
                    p->speed = (int8_t)p->move_speed;
                    break;
                }
            }
            p->speed = 0;
            if (p->weapon != 2) p->u68 = 0;
            break;
        }
        case 0x1f: case 0x21: {   /* mugging: run up, then knock the victim down */
            p->mode = -1;
            Ped *t = ped_at(p->target_ped);
            int32_t tx, ty;
            if (t->state == 7) {
                const Car *c = car_at(t->car);
                tx = c->spr.x, ty = c->spr.y;
            } else {
                tx = t->spr.x, ty = t->spr.y;
            }
            int d = pixel_max(tx - p->spr.x, ty - p->spr.y);
            if (d < 0x14) {
                int a = t->anim;
                if ((a < 0x11 || a == 0x88 || a == 0xa9 || a == 0x62 || a == 99) && t->state != 7 &&
                    abs(p->spr.z - t->spr.z) < 500000 && t->state != 0x12) {
                    p->idle_count = 0;
                    t->speed = 0;
                    p->anim = 0x68;
                    t->anim = 0x2b;
                    t->firing = 0;
                    p->walk_x = 0;
                    p->spr.angle = (int16_t)math_atan2(t->spr.y - p->spr.y, t->spr.x - p->spr.x);
                    t->spr.zkey--;
                    t->state = 9;
                    Snd_PlayPedVoice(p->spr.x, p->spr.y, p->spr.z, 0x15, p->id);
                    t->walk_x = -1;
                    p->state = 3;
                    p->u7c = 8;
                    p->objective = 0x21;
                    p->u68 = 200;
                    p->firing = 0;
                }
            }
            if (d < 0x20) {
                p->u68 = 100;   /* (overrides the 200 just set) */
                p->speed = 3;
            } else if (d < 0xc9) {
                p->speed = d < 0x40 ? 3 : 4;
            } else {
                p->speed = 5;
            }
            break;
        }
        case 0x27: case 0x28: {   /* to a car's door: hijack it (0x27) or get in (0x28) */
            Car *c = car_get(p->car);
            if (c->vtype == 3) {
                p->target_x = p->walk_x = c->spr.x;
                p->target_y = p->walk_y = c->spr.y;
            } else {
                ped_walk_to_car_side_b(p);
            }
            int d = pixel_max(p->target_x - p->spr.x, p->target_y - p->spr.y);
            int busy = CAR_I32(c, 0x244);
            if (d < 0x11 && busy != 1 && abs(p->spr.z - c->spr.z) < 500000) {
                int tp = p->target_ped;
                if (tp == -1 || ped_at(tp)->state != 9) {
                    if (p->objective == 0x27) {
                        if (busy == 0 && !mission_has_player_slot(player_find_by_ped(tp))) {
                            p->state = 0xb;
                            p->walk_x = 0;
                            p->objective = 0x26;
                            p->mode = -1;
                            p->u78 = 8;
                            p->u7c = 2;
                            int16_t drv = c->driver;
                            CAR_I32(c, 0x244) = 1;
                            if (c->vtype == 3) {   /* a bike: knock the rider off */
                                c->speed = 0;
                                CAR_I16(c, 0xb6) = 0;
                                c->unkc0 = 1;
                                if (drv > -1) g_peds[drv].ufc = 1;
                                p->speed = 0;
                                p->ufc = 1;
                                player_exit_car(c->driver, c->id);
                                Snd_PlayPedVoice(p->spr.x, p->spr.y, p->spr.z, 0x15, p->id);
                                g_peds[c->driver].anim = 0x39;
                                p->walk_x = 0;
                                p->mode = 0;
                                p->speed = 0;
                            } else {   /* a car: open the door and pull the driver out (anim 0x1a) */
                                if (drv > -1) g_peds[drv].ufc = 1;
                                p->ufc = 1;
                                p->anim = 0x1a;
                                p->u40 = c->driver;
                                p->walk_x = 0;
                                p->mode = 0;
                                p->speed = 0;
                                int a = c->spr.angle & 0x3ff, b = (c->spr.angle + 0x100) & 0x3ff;
                                int32_t nx = SIN(a) * c->door_dx + SIN(b) * (c->door_dy - 4) + c->spr.x;
                                int32_t ny = COS(a) * c->door_dx + COS(b) * (c->door_dy - 4) + c->spr.y;
                                p->spr.angle = c->spr.angle;
                                move_to(p, nx, ny);
                            }
                        }
                    } else if (busy == 0) {
                        enter_now(p);
                    }
                } else {
                    p->anim = 0x88;
                }
            } else {
                p->speed = 4;
                p->u40 = -1;
            }
            break;
        }
        case 0x32:
            break;
        case 0x34: case 0x35: {   /* to the driver's (0x34) or passenger's side */
            if (p->objective == 0x34) ped_walk_to_car_side_a(p);
            else ped_walk_to_car_side_b(p);
            Car *c = car_get(p->car);
            int d = pixel_max(p->target_x - p->spr.x, p->target_y - p->spr.y);
            if (d > 0x10 || p->u7c == 0x12) {
                p->speed = 4;
                p->u40 = -1;
                break;
            }
            if (p->objective == 0x34) {
                if (CAR_I32(c, 0x244) == 0) {
                    p->speed = 0;
                    p->u48 = 0;
                    p->u40 = -3;
                    p->anim = 1;
                    ped_sit_in_car(p);
                }
            } else if (CAR_I32(c, 0x244) == 0) {
                enter_now(p);
            }
            break;
        }
        case 0x39: {   /* near the car (target_ped holds a car id) */
            const Car *c = car_at(p->target_ped);
            int d = pixel_max(c->spr.x - p->spr.x, c->spr.y - p->spr.y);
            if (d > 0x2f) {
                if (p->anim == 0x62 || p->anim == 99) p->speed = 0;
                else p->speed = (int8_t)p->move_speed;
                break;
            }
            p->speed = 0;
            p->u68 = 0;
            break;
        }
        }
        break;
    case 10:
        p->speed = (int16_t)((p->speed > 0) * 4 - 2);
        p->health = (int8_t)(p->health - 2);
        break;
    }

    {
        int st = p->state;
        if (st == 0xe || st == 5 || st == 1 || p->u78 == 0xd || p->u7c == 0x10 || st == 4 || st == 10 || st == 0xb ||
            p->anim > 0xae || p->objective == 0x23)
            goto tail;
        int obj = p->objective;
        switch (p->u7c) {
        case 2:
            if (obj == 0x24) {
                p->u7c = 5;
            } else if ((int16_t)math_random() > 20000 && p->u78 != 0xc &&
                       (int8_t)map_get_lid_below(p->spr.x, p->spr.y, p->spr.z) != '>') {
                wobble_turn(p);
            }
            break;
        case 4:
            if (p->u84 != 0) {
                p->speed = 2;
                if ((int16_t)math_random() > 20000) wobble_turn(p);
                break;
            }
            p->speed = 1;
            p->u7c = 2;
            break;
        case 5:
            if (p->u84 != 0 || obj == 0x24) {
                p->speed = 3;
                if ((int16_t)math_random() > 20000 && p->objective != 0x24) wobble_turn(p);
                break;
            }
            p->speed = 1;
            p->u7c = 2;
            break;
        case 7:
            wobble(p);
            p->speed = 1;
            p->u84 = 0;
            p->spr.angle = (int16_t)((p->u12 + p->spr.angle) & 0x3ff);
            break;
        case 8:
            switch (obj) {
            case 0x18: case 0x1a: case 0x1b: case 0x20: case 0x29: case 0x2c: case 0x2e: case 0x33:
                break;
            default:
                if (p->u8b == 1) break;
                if (p->u84 == 0 && st != 3) {
                    p->u7c = 2;
                    p->speed = 1;
                    p->anim = 1;
                    p->u48 = 0;
                    p->turn = 0;
                } else {   /* standing: look about now and then */
                    p->speed = 0;
                    p->anim = 0x88;
                    if (g_ped_74f0fa == 0) {
                        if ((int16_t)math_random() < 10000) {
                            p->spr.angle = (int16_t)((p->spr.angle - 0x20) & 0x3ff);
                            g_ped_74f0fa = 1;
                        } else if ((int16_t)math_random() > 20000) {
                            p->spr.angle = (int16_t)((p->spr.angle + 0x20) & 0x3ff);
                            g_ped_74f0fa = 1;
                        }
                    } else if ((int16_t)math_random() < 16000) {
                        g_ped_74f0fa = 0;
                    }
                }
            }
            break;
        case 9: case 10:   /* the end of a side step: turn back an eighth (9 right, 10 left), square up */
            if (p->u84 != 0) break;
            p->u84 = 100;
            p->spr.angle = snap_octant((p->spr.angle + (p->u7c == 9 ? 0x80 : -0x80)) & 0x3ff);
            p->u7c = 2;
            if (obj == 0x24) p->u7c = 5;
            break;
        case 0xc:
            if (p->u84 != 0) break;
            p->u84 = 100;
            p->spr.angle = snap_octant((p->spr.angle - 0x80) & 0x3ff);
            break;
        case 0xd:
            if (p->u84 != 0) break;
            p->u84 = 100;
            p->u7c = 2;
            /* (the original jumps into case 0xc's snapping and leaves the switch there, so the
               speed = 0 of 0x15..0x18 below is never reached from here) */
            p->spr.angle = snap_octant(p->spr.angle);
            break;
        case 0x15: case 0x16: case 0x17: case 0x18:
            p->speed = 0;
            break;
        }
    }
tail:
    *x = SIN(p->spr.angle) * p->speed + p->spr.x;
    *y = COS(p->spr.angle) * p->speed + p->spr.y;
}
