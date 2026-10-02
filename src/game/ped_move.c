/* Ped movement (0x458ca0-0x45cd4f): Ped_Process, the per-frame step of a ped on foot (steering,
   the next position, walls, slopes, water, falling, contacts with peds, cars and objects), and the
   collision responses it uses; Ped_UpdateRiding for peds attached to something. See docs/peds.md. */
#include "ped_internal.h"
#include <stdlib.h>

/* the position change of a ped: the grid node moves when the 2 x 2-block cell changes */
static void move_to(Ped *p, int32_t x, int32_t y)
{
    if (((p->spr.x ^ x) & 0xff800000) != 0 || ((p->spr.y ^ y) & 0xff800000) != 0) {
        coll_remove(p, p->spr.unk20);
        coll_insert(COLL_PED, p->id, p, p->spr.unk20, x, y);
    }
    p->spr.x = x;
    p->spr.y = y;
}

static int bk(int32_t v) { return v >> 22; }

/* the side step distance of Ped_AvoidPed / Ped_AvoidObject by the other's speed (+0x84) */
static void avoid_set_u84(Ped *q, bool left)
{
    switch (q->speed) {
    case 0: q->u84 = 0; break;
    case 1: q->u84 = left ? 0x10 : 0x18; break;
    case 2: q->u84 = 0xc; break;
    case 3: q->u84 = 8; break;
    case 4: q->u84 = 4; break;
    }
}

/* Ped_AvoidPed 0x458ca0: p walks into q. Slow (speed < 2): an ambient q that walks freely turns
   aside (action 10 left / 9 right by the octant of p seen from q). Faster: p stops and q is pushed 4
   pixels along an axis picked from p's quadrant and q's place in its block (away from the block's
   edges), q facing that way. */
static void ped_avoid_ped(Ped *p, Ped *q)
{
    if (p->speed < 2) {
        if (q->player_ctl != 1 && q->u78 == 8 && q->walk_x == 0 && q->u7c != 9 && q->u7c != 10) {
            int a = math_atan2(p->spr.y - q->spr.y, p->spr.x - q->spr.x);
            switch (a >> 7 & 7) {
            case 1: case 3: case 5: case 7:
                q->u7c = 10;
                q->spr.angle = (int16_t)((q->spr.angle + 0x80) & 0x3ff);
                avoid_set_u84(q, true);
                break;
            default:
                q->u7c = 9;
                q->spr.angle = (int16_t)((q->spr.angle - 0x80) & 0x3ff);
                avoid_set_u84(q, false);
                break;
            }
        }
        return;
    }
    p->speed = 0;
    int quad;
    switch (p->spr.angle / 64) {
    case 2: case 3: case 4: case 5: quad = 0x100; break;
    case 6: case 7: case 8: case 9: quad = 0x200; break;
    case 10: case 11: case 12: case 13: quad = 0x300; break;
    default: quad = 0; break;
    }
    coll_remove(q, q->spr.unk20);
    int fx = (q->spr.x >> 16) & 0x3f, fy = (q->spr.y >> 16) & 0x3f;
    int16_t s = 0;
    if (quad == 0x200) {
        if (q->spr.x < p->spr.x) s = fx > 7 ? 0x280 : 0x180;
        else s = fx < 0x3b ? 0x180 : 0x280;
    } else if (quad == 0) {
        if (q->spr.x < p->spr.x) s = fx > 7 ? 0x380 : 0x80;
        else s = fx < 0x35 ? 0x80 : 0x380;
    } else if (quad == 0x100) {
        if (q->spr.y < p->spr.y) s = fy > 7 ? 0x180 : 0x80;
        else s = fy < 0x35 ? 0x80 : 0x180;
    } else if (quad == 0x300) {
        if (q->spr.y < p->spr.y) s = fy > 7 ? 0x280 : 0x380;
        else s = fy < 0x35 ? 0x28a : 0x380;   /* (0x28a: the original's constant) */
    }
    if (fx < 8) s = 0x100;
    if (fx > 0x34) s = 0x300;
    if (fy < 8) s = 0;
    if (fy > 0x34) s = 0x200;
    int32_t nx = q->spr.x + SIN(s) * 4, ny = q->spr.y + COS(s) * 4;
    p->speed = 1;
    coll_insert(COLL_PED, q->id, q, q->spr.unk20, nx, ny);
    q->spr.x = nx;
    q->spr.y = ny;
    q->spr.angle = s;
}

/* Ped_CanMoveForward 0x459070: 4 pixels ahead (behind when backing) not in a building nor against
   a map wall. */
static bool ped_can_move_forward(Ped *p)
{
    int d = p->speed < 0 ? -1 : 1;
    CollBox *b = coll_build_box(p->spr.x + SIN(p->spr.angle) * d * 4, p->spr.y + COS(p->spr.angle) * d * 4,
                                p->spr.z, 4, 4, 0, 10, &g_ped_box);
    coll_compute_bounds(b);
    for (int k = 0; k < 3; k++)
        if (ped_ground_type(bk(b->x[k]), bk(b->y[k]), bk(p->spr.z)) == 5) return false;
    return coll_map_walls(b, 0) == -1;
}

/* Ped_AvoidObject 0x459130: types 6, 7, 0x18 stop a player and turn others a quarter (snapped to an
   axis); power-up objects are collected by a player; other objects turn a freely walking ambient
   ped aside (as Ped_AvoidPed), unless that way is blocked. Type 0x28 does nothing. */
static void ped_avoid_object(Ped *p, Obj *o)
{
    switch (o->type) {
    case 6: case 7: case 0x18: {
        if (p->player_ctl == 1) {
            p->speed = 0;
            return;
        }
        int a = (p->spr.angle - 0x100) & 0x3ff;
        p->spr.angle = (int16_t)a;
        switch (a >> 7) {
        case 0: case 7: p->spr.angle = 0; break;
        case 1: case 2: p->spr.angle = 0x100; break;
        case 3: case 4: p->spr.angle = 0x200; break;
        case 5: case 6: p->spr.angle = 0x300; break;
        }
        return;
    }
    case 0x28:
        return;
    case 0x4e: case 0x4f: case 0x50: case 0x51: case 0x53: case 0x5f: case 0x60: case 0x61: case 0x62:
    case 99: case 100: case 0x65: {
        int n = player_find_by_ped(p->id);
        if (n != -1) powerup_collect(n, o->spr.x, o->spr.y, 2);
        return;
    }
    default:
        if (p->player_ctl != 1 && p->u78 == 8 && p->walk_x == 0 && p->u7c != 9 && p->u7c != 10) {
            int16_t saved = p->spr.angle;
            int a = math_atan2(o->spr.y - p->spr.y, o->spr.x - p->spr.x);
            switch ((uint16_t)(a - o->spr.angle) >> 7 & 7) {
            case 1: case 3: case 5: case 7:
                p->u7c = 10;
                p->spr.angle = (int16_t)((p->spr.angle + 0x80) & 0x3ff);
                if (!ped_can_move_forward(p)) {
                    p->spr.angle = saved;
                    return;
                }
                avoid_set_u84(p, true);
                return;
            default:
                p->u7c = 9;
                p->spr.angle = (int16_t)((p->spr.angle - 0x80) & 0x3ff);
                if (!ped_can_move_forward(p)) {
                    p->spr.angle = saved;
                    return;
                }
                avoid_set_u84(p, false);
                return;
            }
        }
        return;
    }
}

/* Map_FindWalkableZ 0x459430: looking up to two layers down (z grows downward) from z for a
   non-air block; a building there is not walkable; air or a flat block makes it look up to two
   layers up instead, where neither a building nor air may be. The layer is clamped to 0..5 first
   (as block z + 1, so a z past the bottom reads layer 4.97). */
bool map_find_walkable_z(int32_t x, int32_t y, int32_t z)
{
    int bx = bk(x), by = bk(y);
    int32_t z1 = z;
    int n = 0;
    do {
        n++;
        if (ped_ground_type(bx, by, bk(z1)) != 0) break;
        z1 += 0x400000;
        if (n > 1) break;
    } while (z1 < 0x1400000);
    int16_t l = (int16_t)(bk(z1) + 1);
    if (l >= 6) z1 = 0x13e0000;
    else if (l < 1) z1 = 0x3e0000;
    int t = ped_ground_type(bx, by, bk(z1));
    uint32_t tm = map_get_type_at(g_game.map, x, y, z1);
    if (t == 5) return false;
    if (t == 0 || ((tm >> 8) & 0x3f) == 0) {
        n = 0;
        do {
            n++;
            if (ped_ground_type(bx, by, bk(z)) != 0) break;
            z -= 0x400000;
            if (n > 1) break;
        } while (z > 0x400000);
        l = (int16_t)(bk(z) + 1);
        if (l >= 6) z = 0x13e0000;
        else if (l < 1) z = 0x3e0000;
        t = ped_ground_type(bx, by, bk(z));
        if (t == 5 || t == 0) return false;
    }
    return true;
}

/* Ped_SlopeAdjust 0x4595b0: on a slope an ambient ped walks along it: the slope's direction (from
   its type: 1 / 3 north-south, 2 / 4 east-west) snaps the heading to that axis. */
static void ped_slope_adjust(int slope, Ped *p)
{
    int dir = 0;
    switch (slope) {
    case 0: return;
    case 1: case 2: case 9: case 10: case 11: case 12: case 13: case 14: case 15: case 16: case 0x29: dir = 1; break;
    case 3: case 4: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17: case 0x18:
    case 0x2a: dir = 3; break;
    case 5: case 6: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f: case 0x20:
    case 0x2b: dir = 4; break;
    case 7: case 8: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27: case 0x28:
    case 0x2c: dir = 2; break;
    default: game_fatal(-0x4a, 0x54, slope);
    }
    int o = p->spr.angle / 128;
    if (dir == 1 || dir == 3) {
        if (o <= 2 || o == 7) p->spr.angle = 0;
        else if (o <= 6) p->spr.angle = 0x200;
    } else if (dir == 2 || dir == 4) {
        if (o <= 2 || o == 7) p->spr.angle = 0x100;
        else if (o <= 6) p->spr.angle = 0x300;
    }
}

/* Ped_CheckWallHit 0x459770: a building under one of the box's first three corners stops the ped
   (true); else the ground at the new position (x, y): air is a drop that turns a non-player (or a
   player walking to a car door, mode 0xb; not with objective 0x23) back unless there is walkable
   ground near (then it counts as pavement, 3). *ground gets the type seen last. hit_type is unused
   (the original passes it). */
bool ped_check_wall_hit(const CollBox *b, Ped *p, int *hit_type, int *ground, int32_t x, int32_t y)
{
    (void)hit_type;
    for (int k = 0; k < 3; k++) {
        int t = ped_ground_type(bk(b->x[k]), bk(b->y[k]), bk(p->spr.z));
        *ground = t;
        if (t == 5) return true;
    }
    int t = ped_ground_type(bk(x), bk(y), bk(p->spr.z));
    *ground = t;
    if (t == 0 && (p->player_ctl != 1 || p->mode == 0xb) && p->objective != 0x23) {
        if (!map_find_walkable_z(x, y, p->spr.z)) {
            p->spr.angle = (int16_t)((p->spr.angle - 0x200) & 0x3ff);
            p->u12 = (int16_t)-p->u12;
            return true;
        }
        *ground = 3;
    }
    return false;
}

/* Ped_AvoidCar 0x459860: the ped walks around a car in its way: d is the car's heading relative to
   the direction to its door (0..0x3ff). Ahead / behind (outside 0x60..0x1a0 and 0x260..0x3a0) it
   turns square to the car, else along it; +0x12 keeps the side chosen (0x80 / -0x80) and +0xf4 = 1
   marks that it is going round. A ped walking to a target (states 1, 4) takes the side closer to it. */
static void ped_avoid_car(Ped *p, const Car *cc, int16_t d)
{
    const Car *c = car_get(cc->id);
    uint16_t a;
    int16_t s;
    if ((d < 0x60 || d > 0x1a0) && (d > 0x3a0 || d < 0x260)) {
        s = p->u12;
        if (s == 0) {
            p->u12 = (d < 0x1a1 || d > 0x25f) ? 0x80 : -0x80;
            a = (uint16_t)((c->spr.angle + 0x100) & 0x3ff);
            if (p->state != 4 && p->state != 1) {
                p->spr.angle = (int16_t)a;
                p->uf4 = 1;
                return;
            }
            goto toward_target;
        }
        if (d < 0x1a1 || d > 599) {
            if (s > 0) {
                p->uf4 = 1;
                p->spr.angle = (int16_t)((c->spr.angle + 0x100) & 0x3ff);
                return;
            }
        } else if (s < 1) {
            p->uf4 = 1;
            p->spr.angle = (int16_t)((c->spr.angle + 0x100) & 0x3ff);
            return;
        }
        a = (uint16_t)(c->spr.angle - 0x100);
    } else {
        s = p->u12;
        if (s == 0) {
            if ((d < 0x100 && d > 0x5f) || (d < 0x300 && d > 0x25f)) a = (uint16_t)c->spr.angle;
            else a = (uint16_t)(c->spr.angle - 0x200);
            a &= 0x3ff;
            p->u12 = (d < 0x101 || d > 0x2ff) ? 0x80 : -0x80;
            if (p->state == 4 || p->state == 1) goto toward_target;
            p->uf4 = 1;
            return;   /* (the heading is not set on this path) */
        }
        if (p->uf4 == 0) {
            if (d > 0x5f && d < 0x1a1) {
                p->uf4 = 1;
                p->spr.angle = s < 0 ? c->spr.angle : (int16_t)((c->spr.angle - 0x200) & 0x3ff);
                return;
            }
            if (s < 0) {
                p->uf4 = 1;
                p->spr.angle = (int16_t)(c->spr.angle & 0x3ff);
                return;
            }
        } else {
            if (d > 0x5f && d < 0x1a1) {
                p->uf4 = 1;
                p->spr.angle = s < 1 ? (int16_t)((c->spr.angle - 0x200) & 0x3ff) : (int16_t)(c->spr.angle & 0x3ff);
                return;
            }
            if (s < 1) {
                p->uf4 = 1;
                p->spr.angle = (int16_t)(c->spr.angle & 0x3ff);
                return;
            }
        }
        a = (uint16_t)(c->spr.angle - 0x200);
    }
    p->spr.angle = (int16_t)(a & 0x3ff);
    p->uf4 = 1;
    return;
toward_target: {
        int16_t t = (int16_t)math_atan2(p->walk_y - p->spr.y, p->walk_x - p->spr.x);
        if (t - (int16_t)a > 0x100 || t - (int16_t)a < -0x100) {
            a = (uint16_t)((a - 0x200) & 0x3ff);
            p->u12 = (int16_t)-p->u12;
        }
        p->spr.angle = (int16_t)a;
        p->uf4 = 1;
    }
}

/* Ped_StepToPavement 0x459b60: a ped on the road jumps 16 pixels onto the pavement next to it,
   trying west (0x200 heading... the sin / cos table entries 0x200 / 0x300), north, east, south in
   that order; it faces that way (the heading is left at the last tried when none is pavement). */
static void step_out(Ped *p, int want, bool equal)
{
    static const int16_t order[4][3] = {   /* heading, sin index, cos index */
        { 0x200, 0x200, 0x300 }, { 0x100, 0x100, 0x200 }, { 0, 0, 0x100 }, { 0x300, 0x300, 0x400 },
    };
    int32_t x = p->spr.x, y = p->spr.y;
    int bz = bk(p->spr.z);
    for (int i = 0; i < 4; i++) {
        p->spr.angle = order[i][0];
        int32_t sx = g_sin[order[i][1]], sy = g_sin[order[i][2]];
        int t = ped_ground_type(bk(sx * 0x10 + x), bk(sy * 0x10 + y), bz);
        if ((t == want) == equal) {
            move_to(p, x + sx, y + sy);
            return;
        }
    }
}
static void ped_step_to_pavement(Ped *p) { step_out(p, 3, true); }
/* Ped_StepOutOfBuilding 0x459d10: the same out of a building: the first direction that isn't one */
static void ped_step_out_of_building(Ped *p) { step_out(p, 5, false); }

/* Ped_WalkTowardPavement 0x459ec0: from the crossing actions 0x19..0x1c, the walk target is the
   centre of the first pavement block in that direction (one block steps; mode 0x3e). */
static void ped_walk_toward_pavement(Ped *p)
{
    int si, ci;
    switch (p->u7c) {
    case 0x19: si = 0x100, ci = 0x200; break;
    case 0x1a: si = 0x300, ci = 0x400; break;
    case 0x1b: si = 0x200, ci = 0x300; break;
    case 0x1c: si = 0, ci = 0x100; break;
    default: return;
    }
    int32_t x = p->spr.x, y = p->spr.y;
    do {
        y += g_sin[ci] * 0x40;
        x += g_sin[si] * 0x40;
    } while (ped_ground_type(bk(x), bk(y), bk(p->spr.z)) != 3);
    p->mode = 0x3e;
    p->walk_x = p->target_x = (int32_t)(((uint32_t)x & 0xffc00000u) + 0x200000);
    p->walk_y = p->target_y = (int32_t)(((uint32_t)y & 0xffc00000u) + 0x200000);
}

/* Ped_CheckCrossing 0x45a090: an ambient ped on a pavement block next to a crossing (block attr 2)
   picks one of the four directions in turn (0x74f103) to cross (action 0x15..0x18, state 3); when
   the traffic light of the block across lets peds go (Lights_Query 0x34 is 0 or 1) it starts across
   (action 0x19..0x1c, state 4, speed 2) toward the pavement beyond. Only every 11th call does
   anything (0x74f102). */
static void ped_check_crossing(Ped *p)
{
    int32_t cx = 0, cy = 0;
    int bz = bk(p->spr.z);
    if (map_test_block_attr(2, bk(p->spr.x), bk(p->spr.y), bz) && g_ped_74f102 == 0) {
        if (p->state != 3) {
            if (++g_ped_74f103 > 4) g_ped_74f103 = 1;
            static const int16_t dir[5][3] = { { 0 }, { 0x200, 0x300, 0x15 }, { 0x100, 0x200, 0x17 },
                                               { 0, 0x100, 0x16 }, { 0x300, 0x400, 0x18 } };
            const int16_t *d = dir[(int)g_ped_74f103];
            cy = g_sin[d[1]] * 0x40 + p->spr.y;
            cx = g_sin[d[0]] * 0x40 + p->spr.x;
            if (map_test_block_attr(2, bk(cx), bk(cy), bz)) {
                p->u7c = d[2];
                p->state = 3;
            }
        }
        int si = -1, ci = 0;
        switch (p->u7c) {
        case 0x15: ci = 0x300, si = 0x200; break;
        case 0x16: ci = 0x100, si = 0; break;
        case 0x17: ci = 0x200, si = 0x100; break;
        case 0x18: ci = 0x400, si = 0x300; break;
        }
        if (si >= 0) {
            cx = g_sin[si] * 0x40 + p->spr.x;
            cy = g_sin[ci] * 0x40 + p->spr.y;
        }
        int l = lights_query(0x34, bk(cx), bk(cy));
        if (l == 1 || l == 0) {
            switch (p->u7c) {
            case 0x15: p->u7c = 0x1b, p->spr.angle = 0x200; break;
            case 0x16: p->u7c = 0x1c, p->spr.angle = 0; break;
            case 0x17: p->u7c = 0x19, p->spr.angle = 0x100; break;
            case 0x18: p->u7c = 0x1a, p->spr.angle = 0x300; break;
            }
            p->walk_x = 0;
            p->mode = -1;
            p->state = 4;
            p->speed = 2;
            p->u78 = 0x11;
            ped_walk_toward_pavement(p);
        }
    }
    if (++g_ped_74f102 > 10) g_ped_74f102 = 0;
}

/* Ped_UpdateRiding 0x45cbb0: a ped attached to a car (kind 1: one pixel above it, speed 1), an object
   (kind 2: the speed of the object's rider ped for control 2 peds, else 1) or a ped (kind 3: its
   speed, and its animation when not walking) is placed at its offset (+0x54 across, +0x56 along)
   from the parent, facing the same way, and animated. */
void ped_update_riding(Ped *p)
{
    int32_t px = 0, py = 0;
    int16_t a = 0;
    if (p->attach_kind == 1) {
        const Car *c = car_get(p->attach_id);
        px = c->spr.x, py = c->spr.y, a = c->spr.angle;
        p->speed = 1;
        p->spr.zkey = c->spr.zkey - 1;
        p->spr.z = c->spr.z - 1;
    } else if (p->attach_kind == 2) {
        const Obj *o = obj_get(p->attach_id);
        a = o->spr.angle, px = o->spr.x, py = o->spr.y;
        if (p->control == 2 && o->attach_kind == 4) p->speed = g_peds[o->owner].speed;
        else p->speed = 1;
    } else if (p->attach_kind == 3) {
        const Ped *q = &g_peds[p->attach_id];
        px = q->spr.x, py = q->spr.y, a = q->spr.angle;
        p->speed = q->speed;
        if (q->anim > 0x10) p->anim = q->anim;
    }
    coll_remove(p, p->spr.unk20);
    int b = (a + 0x100) & 0x3ff;
    int32_t x = SIN(b) * p->u54 + px + SIN(a) * p->u56;
    p->spr.x = x;
    int32_t y = COS(b) * p->u54 + py + COS(a) * p->u56;
    p->spr.y = y;
    coll_insert(COLL_PED, p->id, p, p->spr.unk20, x, y);
    p->spr.angle = a;
    ped_animate(p);
}

/* the blood / run-over object: the ground under the new position, object 0x3f with the ped's id as
   its angle (the original pushes Map_GetGroundZ's extra arguments 0x3f, id and reuses them) */
static void blood(Ped *p, int32_t nx, int32_t ny)
{
    int32_t gz = ped_ground_z(nx, ny, p->spr.z);
    obj_create(p->spr.x, p->spr.y, gz - 1, 0x3f, p->id);
}

/* a ped in the water (ground type 1): a splash (object 0x36) and sound 0x12 once, ambient peds die,
   anim 0x30 / state 0x18, health - 3 */
static bool in_water(const Ped *p, int32_t x, int32_t y)
{
    return (ped_type_cache(bk(x), bk(y), bk(p->spr.z)) & 0x70) == 0x10;
}

/* Ped_Process 0x45a3b0: see docs/peds.md, "Ped_Process". The local names follow the original's
   variables: nx, ny the new position; go (0x28) the ped may move; blocked (0x24) a wall (1) or a slope
   (2) stopped it; car_hit (0x20) a car or train is in the way; wall (0x1c) the ground type seen by
   the wall test; ground (0x18) the ground type under the ped. */
void ped_process(Ped *p)
{
    int32_t nx = 0, ny = 0;
    int go = 1, blocked = 0, car_hit = 0;
    int wall = 0, ground, side_r = 0;
    bool turned = false;
    int16_t saved_angle = 0, speed0;
    int32_t far_y = 0;          /* local_8 */
    int slope;                  /* param_1 reused by the original */
    CollBox *box;

    if (p->anim != 0 && p->health == 0 && p->state != 0x17 && p->state != 0xc && p->state != 7 &&
        p->state != 0x18) {
        p->anim = 0x2d;
        p->speed = 0;
    }
    if (!ped_is_visible_recent(p) && p->speed == 0 && p->state == 3) return;
    if (p->u7c == 0x12 && p->speed == 0) p->speed = 4;
    {
        int a = p->anim, st = p->state;
        if ((a > 0x2b && a < 0x2e) || a == 0 || st == 0x17 || st == 0x15 || st == 0xc || st == 7 || st == 6)
            goto tail;
        if (p->speed == 0 && st != 10 && a > 0 && a < 0x11) p->anim = 0x88;
    }
    if (p->anim == 0x2b) p->speed = 0;
    if (p->player_ctl == 1) {
        police_cops_for_wanted((int16_t)player_find_by_ped(p->id));
        int limit = 200;   /* compared with a signed byte: never reached */
        if (p->state == 9 && (int8_t)++p->ufe > limit) {
            p->ufe = 0;
            p->state = 2;
        }
        if (p->state != 7) p->ufc = 0;
        if (p->carried > -1 && p->anim != 0x2b && p->state == 1) p->state = 2;
        p->target_ped = -1;
        if (p->u7c == 0x14 && (p->anim < 0xa9 || p->anim > 0xae) && p->firing == 0) p->u7c = 2;
        if ((int8_t)p->move_speed < p->speed) p->speed = (int8_t)p->move_speed;
        if (p->speed < -2) p->speed = -2;
    } else {
        if (p->speed > 4) p->speed = 4;
        if (p->carried > -1) {
            p->state = 1;
            p->u7c = 2;
            p->target_x = p->spr.x;
            p->target_y = p->spr.y;
        }
    }
    if (--p->u64 < 0) p->u64 = 0;

    /* action 0x14: punching what is in front */
    if (p->u7c == 0x14) {
        g_ped_push_count++;
        box = coll_build_box(p->spr.x + SIN(p->spr.angle) * 8, p->spr.y + COS(p->spr.angle) * 8, p->spr.z,
                             6, 6, 0, 10, &g_ped_box);
        CollHit *h = coll_query_box(box, COLL_PED, 1, p->id);
        if (h) {
            Ped *q = h->owner;
            if ((q->graphic != 1 || q->remap != 0) && q->attach_kind == 0 && q->state != 7) {
                if (g_ped_push_count > 12) g_ped_push_count = 0;
                if (q->state != 0x17 && q->state != 0xc) {
                    if (g_ped_push_count < 10 && (q->graphic != 1 || q->remap != 0)) {
                        if (q->anim < 0xaf) {
                            q->anim = 0xaf;
                            Snd_PlayAt(q->spr.x, q->spr.y, q->spr.z, 0x10);
                            q->speed = 0;
                        }
                    } else if (g_ped_push_count < 11 || (q->graphic == 1 && q->remap == 0)) {
                        q->anim = 0xa9;
                        if (q->u7c != 0x14) q->u80 = q->u7c;
                        q->u7c = 0x14;
                        q->spr.angle = (int16_t)((p->spr.angle - 0x200) & 0x3ff);
                        p->anim = 0xaf;
                        p->firing = 0;
                        Snd_PlayAt(p->spr.x, p->spr.y, p->spr.z, 0x10);
                        p->speed = 0;
                        p->u7c = p->u8b == 1 ? p->u80 : 2;
                        g_ped_push_count = 0;
                    }
                }
            }
        }
        coll_unlock();
        h = coll_query_box(box, COLL_OBJECT, 1, p->id);
        if (h) {
            const Obj *o = h->owner;
            if (o->type == 0x54) powerup_reveal(o->spr.x, o->spr.y);
        }
        coll_unlock();
    }
    /* a player walking to a car door gives up when the car moves */
    if (p->player_ctl == 1 && (p->state == 0xb || p->mode == 0xb) && p->car > -1 &&
        abs(car_get(p->car)->speed) > 2) {
        p->u7c = 2;
        p->state = 2;
        p->u78 = 8;
        p->anim = 0x88;
        p->speed = 0;
        p->mode = -1;
        p->walk_x = 0;
    }
    if (p->walk_x == 0 || p->state == 4 || p->state == 1 || p->state == 0xe) {
        if (p->state != 4) p->mode = -1;
    } else {
        ped_steer_to_target(p);
    }
    speed0 = p->speed;
    ground = ped_ground_type(bk(p->spr.x), bk(p->spr.y), bk(p->spr.z));
    /* railway tracks (block attr 1): after 21 frames on them the ped turns back (state 0x15) */
    if (style_requested() != 2 && (p->speed != 0 || p->player_ctl == 1)) {
        if (!map_test_block_attr(1, bk(p->spr.x), bk(p->spr.y), bk(p->spr.z))) {
            p->uec = 0;
        } else if ((int8_t)p->uec < 0x15) {
            p->uec++;
        } else {
            int st = p->state;
            if (st == 0x15 || st == 0x16 || st == 0xf || st == 0x10 || st == 0x11 || st == 0x12) {
                p->uec = 0;
            } else {
                p->state = 0x15;
                p->anim = 0xaf;
                p->speed = 0;
                p->spr.angle = (int16_t)((p->spr.angle - 0x200) & 0x3ff);
            }
        }
    }
    if (p->state == 0x16 && !map_test_block_attr(1, bk(p->spr.x), bk(p->spr.y), bk(p->spr.z))) p->state = 2;
    if (p->player_ctl == 1) {
        if (ground == 5) ped_step_out_of_building(p);
    } else if (ground == 3) {
        if (p->state != 4 && p->u78 != 0x10 && p->u8b != 1 && p->u78 != 1 && p->objective != 0x16 &&
            (p->graphic != 1 || p->remap != 0))
            ped_check_crossing(p);
    } else if (ground == 2) {
        int st = p->state;
        if (p->control == 0 && st != 4 && st != 1 && st != 3 && p->u7c != 0x13 && p->u7c != 0x11 &&
            p->u8b != 1 && st != 0x16) {
            p->state = 1;
            p->walk_x = p->target_x = p->spr.x;
            p->walk_y = p->target_y = p->spr.y;
        }
    } else if (ground == 5) {
        ped_step_out_of_building(p);
    }

    /* the next position */
    slope = (int)(map_get_type_at(g_game.map, p->spr.x, p->spr.y, p->spr.z) >> 8 & 0x3f);
    if (slope != 0) {
        ped_ground_z(p->spr.x, p->spr.y, p->spr.z - 0x200000);   /* (the result is not used) */
        if (p->player_ctl == 1) goto straight;
        CollHit *h = NULL;
        int lid = map_get_lid_below(p->spr.x, p->spr.y, p->spr.z);
        if (lid != 0x80 && lid != 0x32) {
            box = coll_build_box(p->spr.x, p->spr.y, p->spr.z, 0x20, 0x20, 0, 10, &g_ped_box);
            h = coll_query_box(box, COLL_CAR, 1, p->id);
        }
        coll_unlock();
        if (ground != 2 && !h) {
            ped_slope_adjust(slope, p);
            p->walk_x = 0;
            p->mode = -1;
        }
    }
    if (p->player_ctl == 1 || p->u7c == 0x12 || p->state == 0x14 || p->state == 5 || p->u7c == 0x13 ||
        p->mode == 0x3c || p->anim > 0xae || p->anim == 0x2b || (slope != 0 && p->state != 4))
        goto straight;
    ped_compute_step(&nx, &ny, p);
    goto stepped;
straight:
    nx = SIN(p->spr.angle) * speed0 + p->spr.x;
    ny = COS(p->spr.angle) * speed0 + p->spr.y;
stepped:

    /* run over: a car just ahead (and behind) */
    {
        int a = p->anim;
        if ((a < 0x11 || a == 0x88 || p->u7c == 0x10 || p->u7c == 8 || a == 0x2b || p->firing == 1) &&
            p->state != 0x14 && p->state != 0x17 && p->state != 9) {
            box = coll_build_box(SIN(p->spr.angle) + p->spr.x, COS(p->spr.angle) + p->spr.y, p->spr.z, 1, 1, 0, 10, &g_ped_box);
            CollHit *h = coll_query_box(box, COLL_CAR, 1, p->id);
            if (h) {
                coll_unlock();
                box = coll_build_box(SIN(p->spr.angle) + p->spr.x, COS(p->spr.angle) + p->spr.y, p->spr.z, 1, 1, 0, 10, &g_ped_box);
                h = coll_query_box(box, COLL_CAR, 1, p->id);
                if (h) {
                    coll_unlock();
                    box = coll_build_box(p->spr.x - SIN(p->spr.angle) * 2, p->spr.y - COS(p->spr.angle) * 2, p->spr.z, 1, 1, 0, 10,
                                         &g_ped_box);
                    h = coll_query_box(box, COLL_CAR, 1, p->id);
                    if (h) {
                        Car *c = h->owner;
                        int vt = c->vtype;
                        bool jump = false, slide = false;
                        if (p->player_ctl == 1) {
                            if (c->speed > 4) goto hit;
                            if (p->graphic == 1) goto ambient;
                            if (vt != 0 && vt != 1 && vt != 2 && vt != 0xe && CAR_I16(c, 0x30) < 0x14) jump = true;
                            else slide = true;
                        } else {
                        ambient:
                            if (c->speed < 5 && p->player_ctl != 1 && p->graphic != 1 && vt != 0xe) {
                                if (vt == 0 || vt == 1 || vt == 2 || CAR_I16(c, 0x30) > 0x13) slide = true;
                                else jump = true;
                            } else {
                            hit:
                                if (p->state == 0xb || c->vtype == 3) {
                                    p->speed = 0;
                                } else {
                                    police_report_crime(0, c->id, 3, p->spr.x, p->spr.y, p->spr.z);
                                    ny = p->spr.y;
                                    nx = p->spr.x;
                                    p->u5a = c->driver;
                                    if (p->anim == 0x2b) {
                                        blood(p, nx, ny);
                                        p->state = 2;
                                        p->u7c = 2;
                                        p->anim = 0x2d;
                                        Snd_PlayAt(p->spr.x, p->spr.y, p->spr.z, 0x11);
                                        p->speed = 0;
                                    } else {
                                        p->anim = 0x2d;
                                        blood(p, nx, ny);
                                        p->state = 2;
                                        p->mode = -1;
                                        p->walk_x = 0;
                                        p->walk_y = 0;
                                        Snd_PlayAt(p->spr.x, p->spr.y, p->spr.z, 0x11);
                                    }
                                }
                            }
                        }
                        if (jump) {   /* onto the car's roof (anim 0x73) */
                            if (p->u7c != 0x12) p->u80 = p->u7c;
                            p->anim = 0x73;
                            p->speed = 4;
                            p->spr.zkey = c->spr.zkey - 0x10000;
                        } else if (slide) {
                            p->anim = 0x92;
                        }
                    }
                }
            }
            coll_unlock();
        }
    }

    /* walls, map walls and slopes */
    if (p->state == 1 || p->state == 4)
        box = coll_build_box(p->spr.x + SIN(p->spr.angle) * 6, p->spr.y + COS(p->spr.angle) * 6, p->spr.z, 6, 6, 0, 10, &g_ped_box);
    else
        box = coll_build_box(nx, ny, p->spr.z, 4, 4, 0, 10, &g_ped_box);
    coll_compute_bounds(box);
    if ((slope == 0 || p->player_ctl == 1) && p->state != 10 && p->speed != 0) {
        int dummy;
        blocked = ped_check_wall_hit(box, p, &dummy, &wall, nx, ny);
        if (blocked == 1 && slope == 0) {
            nx = p->spr.x;
            ny = p->spr.y;
            go = 0;
        }
        coll_compute_bounds(box);
        if (blocked == 1 && p->player_ctl == 1 && slope != 0) blocked = 0;
        if (p->u8b == 0 && p->player_ctl != 1 && p->objective != 0x24 && p->state == 2 && wall == 4 && ground != 4) {
            go = 0;
            blocked = 1;
        }
        if (coll_map_walls(box, 0) == -1) {
            if (blocked == 0) {
                if (p->player_ctl == 1)
                    box = coll_build_box(nx, ny, p->spr.z, 4, 4, 0, 10, &g_ped_box);
                else
                    box = coll_build_box(p->spr.x + SIN(p->spr.angle) * 8, p->spr.y + COS(p->spr.angle) * 8, p->spr.z, 8, 8, 0,
                                         10, &g_ped_box);
                coll_compute_bounds(box);
            }
        } else {
            nx = p->spr.x;
            ny = p->spr.y;
            go = 0;
            blocked = 1;
        }
        if (coll_map_slopes(box, 1) != -1) {
            nx = p->spr.x;
            ny = p->spr.y;
            go = 0;
            blocked = 2;
        }
    } else {
        wall = 3;
    }
    if (style_requested() != 2 && p->state == 1 && p->u8b == 1 &&
        map_test_block_attr(1, bk(nx), bk(ny), bk(p->spr.z))) {
        blocked = 1;
        go = 0;
    }
    /* an ambient ped keeps to its ground type: only toward pavement 0x60 pixels on */
    if (p->player_ctl != 1 && p->speed != 0 && p->state != 4 && p->state != 1 && wall != 3 && wall != ground &&
        p->walk_x == 0 && p->carried == -1 && p->u7c != 10 && p->u7c != 9) {
        go = 0;
        if (wall != 4) {
            int bx = bk(SIN(p->spr.angle & 0x3ff) * 0x60 + p->spr.x), t = wall;
            if (bx > 0 && bx < 0xff) {
                int by = bk(COS(p->spr.angle & 0x3ff) * 0x60 + p->spr.y);
                if (by > 0 && by < 0xff) t = ped_ground_type(bx, by, bk(p->spr.z));
            }
            if (t == 3) go = 1;
        }
    }

    if ((p->anim < 0x47 || p->anim > 0x4a) && (p->speed != 0 || ped_is_visible_recent(p))) {
        int a = p->anim;
        if (a < 0x11 || (a > 0x40 && a < 0x45 && p->player_ctl == 1) || (a > 0x5d && a < 0x62) || a == 0x88) {
            /* other peds ahead */
            CollBox *b14 = coll_build_box(p->spr.x + SIN(p->spr.angle) * 8, p->spr.y + COS(p->spr.angle) * 8, p->spr.z,
                                          6, 6, 0, 10, &g_ped_box);
            for (CollHit *h = coll_query_box(b14, COLL_PED, 1, p->id); h; h = h->next) {
                Ped *q = h->owner;
                int st = p->state;
                if (!((q->anim == 0x88 || q->anim < 0x11) && st != 0x11 && q->state != 1 && st != 1 && st != 0xf &&
                      st != 0x10))
                    continue;
                bool stop = false;
                if (p->player_ctl == 1 && st != 4) {
                    if (q->u8b == 0) {
                        ped_avoid_ped(p, q);
                        if (p->objective != 0x16 && p->objective != 0x15) Snd_PlayScream(p->spr.x, p->spr.y, p->spr.z);
                        continue;
                    }
                    stop = true;
                } else if (st == 4) {
                    if (p->target_ped > -1) {
                        switch (q->objective) {
                        case 0x19: case 0x1c: case 0x1f: case 0x21:
                            p->u78 = 0xd;
                            p->u84 = 2;
                            stop = true;
                            break;
                        }
                    }
                }
                if (stop) p->speed = 0;
            }
            coll_unlock();

            /* the sides, then what is just ahead: cars, trains (kind 10), objects */
            int ang = p->spr.angle, bz = bk(p->spr.z);
            int ar = (ang + 0x100) & 0x3ff, al = (ang - 0x100) & 0x3ff;
            side_r = ped_ground_type(bk(p->spr.x + SIN(ar) * 0xc), bk(p->spr.y + COS(ar) * 0xc), bz);
            int side_l = ped_ground_type(bk(p->spr.x + SIN(al) * 0xc), bk(p->spr.y + COS(al) * 0xc), bz);
            CollBox *b13 = b14;
            if (p->player_ctl == 1 || p->u78 == 0xe || p->u78 == 0xf) {
                int32_t bx_, by_;
                if (p->player_ctl == 1) {
                    if (speed0 < 0) {
                        bx_ = p->spr.x + SIN(ang) * -6;
                        by_ = p->spr.y + COS(ang) * -6;
                    } else {
                        bx_ = p->spr.x + SIN(ang) * 6;
                        by_ = p->spr.y + COS(ang) * 6;
                    }
                    b13 = coll_build_box(bx_, by_, p->spr.z, 2, 4, 0, 10, &g_ped_box);
                } else {
                    bx_ = nx, by_ = ny;
                    b13 = coll_build_box(nx, ny, p->spr.z, 2, 4, 0, 10, &g_ped_box);
                }
                if (p->player_ctl == 1 && p->walk_x == 0) b13 = coll_build_box(bx_, by_, p->spr.z, 4, 4, 0, 10, &g_ped_box);
            }
            CollHit *h = coll_query_box(b13, 0x11, 1, p->id);
            coll_unlock();
            if (h && (p->anim < 0x11 || p->anim == 0x88) && p->u7c != 0x10) {
                h = coll_query_box(b13, COLL_CAR, 1, p->id);
                car_hit = 0;
                if (!h) {
                    coll_unlock();
                    h = coll_query_box(b13, COLL_KIND10, 1, p->id);
                    if (!h) {
                        coll_unlock();
                        h = coll_query_box(b13, COLL_OBJECT, 1, p->id);
                        if (h && p->state != 1 && p->state != 3 && p->state != 4) {
                            Obj *o = h->owner;
                            go = 1;
                            coll_unlock();
                            if (o->type == 0x41 || o->type == 0x42) go = 0;
                            else ped_avoid_object(p, o);
                        }
                    } else {   /* a train: its owner is the sprite */
                        const Sprite *t = h->owner;
                        car_hit = 1;
                        int d = (math_atan2(t->y - p->spr.y, t->x - p->spr.x) - t->angle) & 0x3ff;
                        p->u12 = (d < 0x100 || d > 0x300) ? 0x80 : -0x80;
                        if (p->walk_x > 0 && p->anim > 0x40 && p->anim < 0x46) {
                            int na;
                            if (p->u12 == 0x80 || (p->u12 == 0x100 && p->state == 1)) {
                                na = p->spr.angle + 0x80;
                            } else if (p->state == 0xe) {
                                go = 0;
                                goto train_done;
                            } else {
                                na = p->spr.angle - 0x80;
                            }
                            p->spr.angle = (int16_t)(na & 0x3ff);
                            if (p->spr.angle == p->prev_angle) p->spr.angle = (int16_t)((p->spr.angle - 400) & 0x3ff);
                            car_hit = 0;
                        }
                    train_done:;
                    }
                    coll_unlock();
                } else {   /* a car */
                    int16_t l10 = p->spr.angle;
                    nx = p->spr.x;
                    Car *c = h->owner;
                    ny = p->spr.y;
                    car_hit = 1;
                    coll_unlock();
                    int ca = (uint16_t)c->spr.angle & 0x3ff;
                    int t = math_atan2(COS(ca) * (c->door_dx + 4) + c->spr.y - p->spr.y,
                                       SIN(ca) * (c->door_dx + 4) + c->spr.x - p->spr.x);
                    int16_t l14 = (int16_t)(((uint16_t)c->spr.angle - (t & 0x3ff)) & 0x3ff);
                    uint16_t d = (uint16_t)l14;
                    if (d > 0x1a0 && d < 600) d = 0;
                    bool b4 = (p->walk_x != 0 && d < 0x1a0) || (p->player_ctl == 1 && p->walk_x != 0 && p->car != c->id);
                    if (CAR_I16(c, 0x30) < 0x14) {
                        if (p->speed > 2 && d > 0x60 && d < 0x3a0 && !b4 && p->walk_x != 0) {
                            saved_angle = p->spr.angle;
                            if (d < 0x200) p->spr.angle = (int16_t)((c->spr.angle - 0x100) & 0x3ff);
                            if (d > 0x200) p->spr.angle = (int16_t)((c->spr.angle + 0x100) & 0x3ff);
                            if (d != 0x200) turned = true;
                        }
                        if (CAR_I16(c, 0x30) > 0x13 || p->speed < 3 || ped_check_ahead(c, p) || d < 0x61 || d > 0x39f ||
                            p->objective == 0x27 || b4 || (b4 = true, p->walk_x == 0))
                            b4 = false;
                    } else {
                        b4 = false;
                    }
                    bool tested = true;
                    if (p->player_ctl == 1) {
                        if (p->speed > 2 && player_ctl_by_ped(p->id, 2) != 0) {
                            if (CAR_I16(c, 0x30) < 0x14) {
                                b4 = true;
                            } else {
                                b4 = false;
                                p->anim = 0x92;
                            }
                        }
                        tested = p->walk_x == 0 || p->car == c->id;
                    }
                    if (tested && b4 && p->graphic != 1) {   /* over the bonnet (anim 0x73) */
                        p->anim = 0x73;
                        if (p->turn != 0) p->turn = 0, p->u12 = 0;
                        p->spr.zkey = c->spr.zkey - 0x10000;
                        p->speed = p->player_ctl == 1 ? (int8_t)p->move_speed : 4;
                        p->u12 = (d < 0x100 || d > 0x300) ? 0x7f : -0x7f;
                        coll_unlock();
                        goto after_contacts;
                    }
                    if (p->u7c != 0x14) {
                        if (turned) p->spr.angle = saved_angle;
                        if (p->mode == 0x3c) p->walk_x = 0;
                        if (p->player_ctl != 1 && p->state == 2) go = 0;
                        if (p->walk_x != 0) {
                            int16_t s = p->u12;
                            if (abs(s) < 0x80) {
                                ped_avoid_car(p, c, l14);
                                coll_unlock();
                                goto after_contacts;
                            }
                            if (s == 0x80 || (s == 0x100 && p->state == 1) || p->state != 0xe) {
                                ped_avoid_car(p, c, l14);
                                car_hit = 0;
                            } else {
                                go = 0;
                            }
                            if (p->spr.angle == l10) p->spr.angle = (int16_t)((p->spr.angle + 0x40) & 0x3ff);
                        }
                    }
                    coll_unlock();
                }
            }
        after_contacts:;

            /* ambient peds keep to the pavement */
            int st = p->state;
            if (st != 4 && p->control != 2 && p->player_ctl != 1 && st != 1 && car_hit == 0 && st != 3 && st != 0xe &&
                p->objective != 0x26) {
                int16_t a2 = p->spr.angle;
                if (ground == 3 && a2 != 0x100 && a2 != 0x200 && a2 != 0x300 && a2 != 0) {
                    if (side_r != 3) {
                        if (p->u7c != 9 && p->u7c != 10) {
                            p->u84 = 4;
                            p->u7c = 9;
                            p->spr.angle = (int16_t)((a2 - 0x80) & 0x3ff);
                        }
                        go = 1;
                        p->walk_x = 0;
                        p->turn = 0;
                    }
                    if (side_l != 3) {
                        if (p->u7c != 9 && p->u7c != 10) {
                            int16_t a3 = p->spr.angle;
                            p->u84 = 4;
                            p->u7c = 10;
                            p->spr.angle = (int16_t)((a3 + 0x80) & 0x3ff);
                        }
                        p->walk_x = 0;
                        p->turn = 0;
                        go = 1;
                    }
                }
                /* a lid with a wall edge (lid tile 0x3e): keep off the half past it */
                if (map_get_lid_below(p->spr.x, p->spr.y, p->spr.z) == 0x3e) {
                    uint32_t tm = map_get_type_at(g_game.map, p->spr.x, p->spr.y, p->spr.z + 0x400000);
                    int px = (p->spr.x >> 16) & 0x3f, py = (p->spr.y >> 16) & 0x3f;
                    int turn_dir = 0;   /* 1: +0x80 (action 10), -1: -0x80 (action 9) */
                    int16_t s8;
                    switch (tm >> 14 & 3) {
                    case 0:
                        if (px > 0x20) {
                            move_to(p, nx, ny);
                            s8 = p->spr.angle;
                            if (s8 < 0x281 && s8 > 0x17f) turn_dir = 1;
                            else if ((s8 > 0x37f && s8 < 0x401) || (s8 >= 0 && s8 < 0x81)) turn_dir = -1;
                        }
                        break;
                    case 1:
                        if (py > 0x20) {
                            move_to(p, nx, ny);
                            s8 = p->spr.angle;
                            if (s8 < 0x181 && s8 > 0x7f) turn_dir = 1;
                            else if (s8 < 0x381 && s8 > 0x27f) turn_dir = -1;
                        }
                        break;
                    case 2:
                        if (px < 0x20) {
                            move_to(p, nx, ny);
                            s8 = p->spr.angle;
                            if (s8 < 0x281 && s8 > 0x17f && p->u7c != 9 && p->u7c != 10) {
                                p->u84 = 4;
                                p->u7c = 9;
                                p->spr.angle = (int16_t)((s8 - 0x80) & 0x3ff);
                            }
                            s8 = p->spr.angle;
                            if ((s8 > 0x37f && s8 < 0x401) || (s8 >= 0 && s8 < 0x81)) turn_dir = 1;
                        }
                        break;
                    case 3:
                        if (py < 0x20) {
                            move_to(p, nx, ny);
                            s8 = p->spr.angle;
                            if (s8 < 0x181 && s8 > 0x7f) turn_dir = -1;
                            else if (s8 < 0x381 && s8 > 0x27f) turn_dir = 1;
                        }
                        break;
                    }
                    if (turn_dir != 0 && p->u7c != 9 && p->u7c != 10) {
                        p->u7c = turn_dir > 0 ? 10 : 9;
                        p->u84 = 4;
                        p->spr.angle = (int16_t)((p->spr.angle + (turn_dir > 0 ? 0x80 : -0x80)) & 0x3ff);
                    }
                }
                int g = ground;
                if (g == 2) {
                    if (p->control != 0) goto ground_z;
                    if (p->u7c != 8 && p->u7c != 9 && p->u7c != 10) {
                        ped_step_to_pavement(p);
                        p->u78 = 0xc;
                        p->u84 = 0x14;
                    }
                }
                if (p->control == 0 && g == 3 && p->mode != 0x3c && p->u7c != 8 && p->u7c != 9 && p->u7c != 10) {
                    int32_t fx = SIN(p->spr.angle & 0x3ff) * 0x30 + p->spr.x;
                    int32_t fy = COS(p->spr.angle & 0x3ff) * 0x30 + p->spr.y;
                    int t = (int)far_y;   /* (the original reads its local 0x8 here, still 0) */
                    int fbx = bk(fx), fby = bk(fy);
                    if (fbx > 0 && fbx < 0xff && fby > 0 && fby < 0xff) t = ped_ground_type(fbx, fby, bk(p->spr.z));
                    bool detour = false;
                    if (slope == 0 || t != 5) {
                        if (t == 0) detour = !map_find_walkable_z(fx, fy, p->spr.z);
                        else detour = t != 3;
                    }
                    if (detour) {
                        /* off the pavement ahead: square the heading, look a block to each side and
                           walk there (mode 0x3c) where there is pavement, in one of four patterns */
                        switch (p->spr.angle / 128) {
                        case 0: case 7: p->spr.angle = 0; break;
                        case 1: case 2: p->spr.angle = 0x100; break;
                        case 3: case 4: p->spr.angle = 0x200; break;
                        case 5: case 6: p->spr.angle = 0x300; break;
                        }
                        int a1 = ((uint16_t)p->spr.angle - 0x100) & 0x3ff;
                        int32_t lx = SIN(a1) * 0x40 + p->spr.x, ly = COS(a1) * 0x40 + p->spr.y;
                        far_y = ly;
                        int left, right;
                        if (bk(lx) < 0 || bk(lx) > 0xff || bk(ly) < 0 || bk(ly) > 0xff) {
                            left = -1;
                        } else {
                            left = ped_ground_type(bk(lx), bk(ly), bk(p->spr.z));
                            if (left == 0 && map_find_walkable_z(lx, ly, p->spr.z)) left = 3;
                        }
                        int a2r = ((uint16_t)p->spr.angle + 0x100) & 0x3ff;
                        int32_t rx = SIN(a2r) * 0x40 + p->spr.x, ry = COS(a2r) * 0x40 + p->spr.y;
                        if (bk(rx) < 0 || bk(rx) > 0xff || bk(ry) < 0 || bk(ry) > 0xff) {
                            right = -1;
                        } else {
                            right = ped_ground_type(bk(rx), bk(ry), bk(p->spr.z));
                            if (right == 0 && map_find_walkable_z(rx, ry, p->spr.z)) right = 3;
                            if (right == 3 && left == 3) {   /* both: alternate (0x7284d1) */
                                if (g_ped_7284d1 == 1) left = -1;
                                else right = -1;
                                if (++g_ped_7284d1 > 1) g_ped_7284d1 = 0;
                            }
                        }
                        int pa = (uint16_t)p->spr.angle & 0x3ff;
                        for (int side = 0; side < 2; side++) {
                            if ((side == 0 ? left : right) != 3) continue;
                            int32_t wx = side == 0 ? lx : rx, wy = side == 0 ? ly : ry;
                            if (g_ped_72844d == 1) wx += SIN(pa) * 10, wy += COS(pa) * 5 * 2;
                            else if (g_ped_72844d == 2) wx += SIN(pa) * 0x1a, wy += COS(pa) * 0xd * 2;
                            else if (g_ped_72844d == 3) wx += SIN(pa) * 0x28, wy += COS(pa) * 0x28;
                            if (++g_ped_72844d > 4) g_ped_72844d = 1;
                            p->u12 = 0;
                            p->walk_x = wx;
                            p->walk_y = wy;
                            p->mode = 0x3c;
                        }
                    }
                }
            }
        }
    ground_z:;
            /* the ground under the new position */
            int32_t gz = ped_ground_z(nx, ny, p->spr.z - 0x200000);
            int32_t z = p->spr.z;
            if (gz < z - 0x100000 || z + 0x100000 < gz) {
                go = 0;
            } else {
                if (p->anim != 0) {
                    if (in_water(p, nx, ny) && p->attach_id == 0 && p->u7c != 0x13 && p->u7c != 0x11) {
                        if (p->state != 0x18) {
                            obj_create(p->spr.x, p->spr.y, z + 1, 0x36, p->spr.angle);
                            Snd_PlayAt(p->spr.x, p->spr.y, p->spr.z, 0x12);
                            if (p->player_ctl != 1) p->health = 0;
                        }
                        int8_t h = p->health;
                        p->anim = 0x30;
                        p->u78 = 8;
                        p->state = 0x18;
                        p->health = (int8_t)(h - 3);
                    } else if (p->state == 0x18) {
                        p->state = 2;
                        p->health = 100;
                        p->anim = 0x88;
                    }
                }
                if (p->anim < 0x73) p->spr.z = gz - 0x10000;
            }
            z = p->spr.z;
            if (z + 0x100000 < gz) {   /* falling */
                go = 1;
                if (p->anim < 0x11 || p->anim == 0x88) p->anim = 0x5e;
                if (p->state != 10) p->u74 = p->state;
                p->state = 10;
                p->spr.z = z + 0x20000;
                p->health = (int8_t)(p->health - 2);
                if (p->speed < 0) p->spr.angle = (int16_t)((p->spr.angle - 0x200) & 0x3ff);
                p->speed = 1;
            } else if (p->state == 10) {
                p->state = p->u74;
                if (p->health > 0) p->health = 100;
            }
            if (p->spr.angle == p->prev_angle) p->uf8++;
            if (nx == p->spr.x && ny == p->spr.y) p->uee++;
            else p->uee = 0;
            if (((p->uee > 8 && p->player_ctl != 1 && p->u7c != 0x13) || (p->uee > 4 && p->player_ctl == 1 && p->walk_x != 0)) &&
                p->speed != 0 && p->u7c != 0x11 && (p->u7c != 8 || p->state != 3) && p->firing != 1 && p->anim != 0x2b &&
                p->anim != 0x2d) {
                p->spr.angle = (int16_t)((p->spr.angle + 0x140) & 0x3ff);
                p->u12 = 0x80;
                p->uee = 0;
                if (p->state == 4) {
                    p->u78 = 0xd;
                    p->u84 = 0x14;
                }
            }
            if ((go == 1 || p->speed > 3 || p->state == 0xe) && car_hit == 0 && (blocked == 0 || p->state == 10)) {
                if (nx > 0 && ny > 0 && nx < 0x40000000 && ny < 0x40000000) move_to(p, nx, ny);
                goto tail;
            }
    }
    if (go != 0 || (p->speed > 3 && blocked == 0)) goto tail;
    /* stopped: turn */
    if (p->u7c == 0x12) p->spr.angle = (int16_t)((p->spr.angle + 0x80) & 0x3ff);
    if (p->player_ctl != 1) {
        int obj = p->objective;
        if (obj != 0x28 && obj != 0x27) {
            if (p->anim < 0x11) {
                int st = p->state;
                p->u78 = 0xc;
                if (st != 0xe) p->u7c = 2;
                if (st == 4 || st == 1) {
                    if (blocked) {
                        p->u78 = 0xd;
                        p->u84 = obj == 0x16 ? 2 : 100;
                    }
                } else {
                    p->u84 = 0x32;
                }
            }
            if (p->u7c != 0x12) p->u80 = 2;
        }
    }
    if (p->player_ctl == 1 || p->state == 1 || p->state == 4 || p->u7c != 2) {
        if (p->u7c != 0x12 && p->walk_x > 0) {
            ped_turn_at_obstacle(p, blocked);
            p->u12 = 0;
        }
    } else {
        p->u78 = 8;
        p->u84 = 3;
        p->spr.angle = (int16_t)((p->spr.angle + (car_hit == 1 ? 0x40 : -0x180)) & 0x3ff);
    }
    if (p->anim > 0x27 && p->anim < 0x2a) p->spr.angle = (int16_t)((p->spr.angle - 0x100) & 0x3ff);
    if (p->state != 0xe && p->player_ctl != 1 && p->state != 4 && p->state != 1) p->walk_x = 0, p->walk_y = 0;

tail:
    /* water, the depth key over slopes and dead peds, then the animation */
    if (in_water(p, p->spr.x, p->spr.y) && p->u7c != 0x13 && p->u7c != 0x11) {
        int st = p->state;
        if (st != 0xc) {
            if (st != 0x18) {
                if (p->carried > -1) obj_delete_wrapper(p->carried);
                obj_create(p->spr.x, p->spr.y, p->spr.z + 1, 0x36, p->spr.angle);
                Snd_PlayAt(p->spr.x, p->spr.y, p->spr.z, 0x12);
                if (p->player_ctl != 1) p->health = 0;
            }
            p->anim = 0x30;
            int8_t h = (int8_t)(p->health - 3);
            p->state = 0x18;
            p->health = h;
            if (h < 0) p->health = 0;
        }
    } else if (p->state == 0x18) {
        p->state = 2;
        p->anim = 0x88;
    }
    {
        int32_t gz = ped_ground_z(p->spr.x, p->spr.y, p->spr.z - 0x200000);
        int32_t z1 = gz - 0x10000;
        CollBox *b = coll_build_box(p->spr.x, p->spr.y, p->spr.z, 8, 8, 0, 10, &g_ped_box);
        for (int k = 0; k < 4; k++) {
            if (!(ped_type_cache(bk(b->x[k]), bk(b->y[k]), bk(p->spr.z)) & 0x80)) continue;
            int32_t g2 = ped_ground_z(b->x[k], b->y[k], p->spr.z - 0x400000);
            int32_t d = (z1 - g2) + 0x10000;
            if ((abs(d) & 0xffff0000) > 0x100000) break;
            if (p->car < 0 || (p->u7c != 0x11 && p->u7c != 0x13)) {
                p->spr.zkey = gz - 0x410000;
            } else if (car_info_is_convertible(p->car)) {
                int32_t zk = car_get(p->car)->spr.zkey - 0x400000;
                p->spr.zkey = zk;
                if (zk < 0) p->spr.zkey = 0x10000;
            } else {
                p->spr.zkey = gz - 0x410000;
            }
            if (p->spr.zkey < 0) p->spr.zkey = 0x10000;
            if (p->u7c != 0x11 && p->u7c != 0x13) {
                p->spr.z = z1;
                p->spr.zkey = gz - 0x40fff6;
                if (p->spr.zkey < 0) p->spr.zkey = 0x10000;
            }
            if (p->u7c == 0x12) {
                p->spr.zkey -= 0x400000;
                if (p->spr.zkey < 0) p->spr.zkey = 0x10000;
            }
            goto animate;
        }
        int a = p->anim;
        if (a < 0x73 && a != 0x39 && a != 0x3a && p->car > 0 && !car_info_is_convertible(p->car))
            p->spr.zkey = p->spr.z;
        if (p->anim > 0x2a && p->anim < 0x2e) {   /* lying dead: under live peds, by id */
            p->spr.z = z1;
            p->spr.zkey = p->id + 1 + z1;
            if (p->objective == 0x25) p->spr.zkey = gz - 0xffff;
        }
    }
animate:
    if (p->firing == 1 && p->player_ctl != 1) p->speed = 0;
    ped_animate(p);
}
