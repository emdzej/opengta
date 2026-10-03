/* Ped_Animate 0x44fa70: the ped animation state machine (+0x18 anim). Every other call (+0x0a) the
   state advances; many states also move the ped (getting into / out of a car through its doors,
   being knocked back, sliding off a bonnet) or have side effects (footstep sounds, dying, blood).
   Then the depth key (+0x9c) is fixed up and the sprite frame chosen (Ped_UpdateSprite).

   The state groups (frame numbers as in the original):
     1..8        walk cycle (footsteps on 3 and 8 for players); speed > 2 switches to the run cycle
     9..0x10     run cycle (footsteps on 0xc and 0x10), back to walking below speed 3, standing at 0
     0x11..0x15  walking to the driver's door and opening it (0x74f0f4 counts the steps)
     0x16..0x19  getting out: stepping away and closing the door
     0x1a..0x1d  opening the door to get in (0x1c pulls the driver out: +0x40 is that ped)
     0x1e..0x22  climbing in, closing the door: the ped becomes the driver (0x22)
     0x23..0x26  getting out of a car on the other side, 0x26 thrown down (0x2b)
     0x27..0x2a  stumbling, 0x2a falls if a car is on it
     0x2b        lying down (knocked over), 0x2c / 0x2d dead
     0x2f..0x32  in water / drowning
     0x37..0x3a  climbing out of a convertible
     0x46..0x50  side steps and turns
     0x51..0x59  getting onto / off a bike or open vehicle (riding attach)
     0x5d        landing after a fall
     0x61        end of a fall
     0x62..0x72  pulling a driver out through the passenger side, a carried object (0x6d)
     0x73..0x78  jumping over a bonnet
     0x81..0x87  the rear door (buses, vans)
     0x89..0x91  shot, knocked back and dying (blood trail objects 0x4d)
     0x92        sliding along a car
     0x93..0x98  thrown out of a car by the player
     0xa9..0xb2  punched, knocked back
     0xe9..0xed  electrocuted (flashing remap), then dead */
#include "ped_internal.h"
#include "trigger.h"
#include "sentinel.h"
#include "wanted.h"

static int bk(int32_t v) { return v >> 22; }

/* the grid node follows a move into another 2 x 2-block cell */
static void move_to(Ped *p, int32_t x, int32_t y)
{
    if (((p->spr.x ^ x) & 0xff800000) != 0 || ((p->spr.y ^ y) & 0xff800000) != 0) {
        coll_remove(p, p->spr.unk20);
        coll_insert(COLL_PED, p->id, p, p->spr.unk20, x, y);
    }
    p->spr.x = x;
    p->spr.y = y;
}

/* a point of the car's frame: `along` pixels along the heading (sin / cos a), `across` along a + 0x100 */
static void car_point(const Car *c, int along, int across, int32_t *x, int32_t *y)
{
    int a = c->spr.angle & 0x3ff, b = (c->spr.angle + 0x100) & 0x3ff;
    *x = SIN(a) * along + SIN(b) * across + c->spr.x;
    *y = COS(a) * along + COS(b) * across + c->spr.y;
}

/* the depth key of a ped in a convertible: 4 layers... one block above the car's key, at least 0x10000 */
static void key_above(Ped *p, int32_t base)
{
    int32_t k = base - 0x40000;
    p->spr.zkey = k;
    if (k < 0) p->spr.zkey = 0x10000;
}

/* the blood object under a ped: object `type` on the ground, the ped id as its angle (the original
   pushes Map_GetGroundZ's extra arguments and reuses them for Obj_Create) */
static void ground_object(Ped *p, int type)
{
    int32_t gz = ped_ground_z(p->spr.x, p->spr.y, p->spr.z);
    obj_create(p->spr.x, p->spr.y, gz - 1, type, p->id);
}

static void snd(const Ped *p, int s) { Snd_PlayAt(p->spr.x, p->spr.y, p->spr.z, s); }

static int16_t info16(const Car *c, int off)
{
    const uint8_t *i = car_info_of_model(c->model);
    return i ? (int16_t)(i[off] | i[off + 1] << 8) : 0;
}

/* the snap of a heading to the nearest axis by eighths (0 / 7 -> 0, 1 / 2 -> 0x100, ...) */
static void snap_axis(Ped *p)
{
    switch (p->spr.angle / 128) {
    case 1: case 2: p->spr.angle = 0x100; break;
    case 3: case 4: p->spr.angle = 0x200; break;
    case 5: case 6: p->spr.angle = 0x300; break;
    default: p->spr.angle = 0; break;
    }
}

/* a sidestep of the ped by (dx, dy) unless a car is there (2 x 2 box) */
static void sidestep(Ped *p, int32_t dx, int32_t dy)
{
    int32_t x = p->spr.x + dx, y = p->spr.y + dy;
    CollBox *b = coll_build_box(x, y, p->spr.z, 2, 2, 0, 10, &g_ped_box);
    if (!coll_query_box_first(b, COLL_CAR, 1, p->id)) move_to(p, x, y);
}

/* the death of a shot ped (anims 0x8c / 0x91, also 0x2c / 0x2d): the kill is scored, the depth
   key goes below the live peds, blood, state 0x17 and an ambulance unless the ped is a cop or a
   mission ped; a player's car loses its driver (a police car without a script line becomes a
   chasing cop car, control 3). */
static void die_shot(Ped *p, int next)
{
    score_ped_killed(p, 1);
    p->spr.zkey += p->id * 2;
    ground_object(p, 0x3f);
    p->state = 0x17;
    if (p->control != 1 && p->u8b == 0) ambulance_request_for_ped(p->id);
    p->health = 0;
    p->walk_x = 0;
    p->turn = 0;
    if (p->player_ctl == 1 && p->car != -1) {
        Car *c = car_get(p->car);
        c->driver = -1;
        if (c->model == 4 && c->script_line < 0) c->control = 3;
    }
    p->anim = (int16_t)next;
    if (p->objective == 0x15 || p->objective == 0x16) ped_leave_group(p);
}

/* the knock-back of states 0x89..0x8b (dir -1, backward) and 0x8d..0x90 (dir +1): six pixels unless
   a building is under the box, leaving blood (object 0x4d) */
static void knock(Ped *p, int dir)
{
    int a = p->spr.angle & 0x3ff;
    int32_t x = p->spr.x + SIN(a) * 6 * dir, y = p->spr.y + COS(a) * 6 * dir;
    CollBox *b = coll_build_box(x, y, p->spr.z, 8, 8, 0, 10, &g_ped_box);
    bool wall = false;
    for (int k = 0; k < 3 && !wall; k++)
        wall = ped_ground_type(bk(b->x[k]), bk(b->y[k]), bk(p->spr.z)) == 5;
    if (!wall) {
        move_to(p, x, y);
        int32_t gz = ped_ground_z(x, y, p->spr.z);
        obj_create(p->spr.x, p->spr.y, gz - 1, 0x4d, p->id);
    }
    p->anim++;
    p->speed = 0;
}

void ped_animate(Ped *p)
{
    bool zset = false;          /* the case set the depth key (0x4c bVar8) */
    bool zfall = false;         /* bVar7: carried into the fall-through cases */
    if (++p->anim_tick < 2) goto sprite;
    p->anim_tick = 0;
    Car *tc = p->car == -1 || p->car == 1000 ? NULL : car_get(p->car);   /* the car at entry */
    int a = p->anim;
    int32_t x, y;
    Car *c;
    switch (a) {
    /* ---- walking and running ---- */
    case 2: case 7:
        p->anim = (int16_t)(a + 1);
        if (p->speed > 2) p->anim = 0xb;
        break;
    case 3:
        if (p->player_ctl == 1) snd(p, 0x13);
        /* fall through */
    case 1: case 4: case 5: case 6:
        if (p->speed > 2) p->anim = 0xb;
        p->anim++;
        break;
    case 8:
        if (p->player_ctl == 1) snd(p, 0x13);
        p->anim = 1;
        if (p->speed > 2) p->anim = 0xb;
        break;
    case 9: case 10: case 0x1d: case 0x2e: case 0x33: case 0x34: case 0x35: case 0x36: case 0x3b:
    case 0x3c: case 0x3d: case 0x3e: case 0x3f: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45:
    case 0x5e: case 0x5f: case 0x60: case 0x62: case 0x65: case 0x69: case 0x6c: case 0x70: case 0x77:
    case 0x79: case 0x7a: case 0x7b: case 0x7c: case 0x7d: case 0xa9: case 0xaa: case 0xab: case 0xac:
    case 0xad: case 0xee: case 0xef: case 0xf0:
    next:
        p->anim = (int16_t)(a + 1);
        break;
    case 0xb: case 0xf: {
        int16_t s = p->speed;
        p->anim = s == 0 ? PED_ANIM_STAND : (int16_t)(a + 1);
        if (s < 3) p->anim = 1;
        break;
    }
    case 0xc:
        if (p->player_ctl == 1) snd(p, 0x14);
        /* fall through */
    case 0xd: case 0xe:
        p->anim++;
        break;
    case 0x10:
        if (p->player_ctl == 1) snd(p, 0x14);
        p->anim = 9;
        break;

    /* ---- to the driver's door and opening it ---- */
    case 0x11:
        if (g_ped_door_step != 0) g_ped_door_step = 0;
        p->u7c = 0x13;
        p->state = 7;
        p->u78 = 8;
        p->mode = -1;
        snd(p, 0);
        p->u0e = 0;
        /* fall through */
    case 0x12: case 0x13: case 0x14: case 0x15:
        if (p->anim == 0x15) g_ped_door_step += 4;
        c = car_get(p->car);
        CAR_I32(c, 0x244) = 1;
        car_point(c, c->door_dx - 1, c->door_dy - 4 + g_ped_door_step, &x, &y);
        move_to(p, x, y);
        car_open_door1_step(p->car);
        player_exit_car(p->id, p->car);
        if (p->anim != 0x11 && p->anim != 0x12) g_ped_door_step++;
        p->anim++;
        if (car_info_is_convertible(p->car)) {
            key_above(p, c->spr.zkey);
            zset = true;
        }
        break;

    /* ---- getting out: stepping away, closing the door ---- */
    case 0x16: case 0x17:
        if (p->speed != 0) p->speed = car_get(p->car)->speed;   /* Car_GetSpeedById 0x40bc00 */
        c = car_get(p->car);
        car_close_door1_step(p->car);
        p->anim++;
        car_point(c, c->door_dx - 4, c->door_dy, &x, &y);
        move_to(p, x, y);
        break;
    case 0x18:
        car_close_door1_step(p->car);
        p->anim++;
        break;
    case 0x19:
        p->turn = 0;
        if (tc && car_info_is_convertible(p->car)) {
            zset = true;
            p->spr.zkey = tc->spr.zkey - 0x40000;
            CAR_I32(tc, 0x244) = 0;
            if (p->spr.zkey < 0) p->spr.zkey = 0x10000;
        }
        if (car_close_door1_step(p->car)) {
            snd(p, 1);
            p->anim = PED_ANIM_STAND;
            p->u7c = 2;
            if (p->state != 9) p->state = 2;
            c = car_get(p->car);
            if (p->id == c->driver) car_set_driver_by_id(p->car, -1);
            p->speed = 0;
            car_point(c, c->door_dx - 4, c->door_dy + 2, &x, &y);
            move_to(p, x, y);
            p->spr.angle = (int16_t)((c->spr.angle + 0x80) & 0x3ff);
            CAR_I32(c, 0x244) = 0;
            CAR_I32(c, 0x248) = 0;
            g_ped_door_step = 0;
        }
        break;

    /* ---- opening the door to get in ---- */
    case 0x1a:
        p->mode = -1;
        c = car_get(p->car);
        CAR_I32(c, 0x244) = 1;
        if (car_open_door1_step(p->car)) {
            car_reset_siren99(car_get(p->car));
            if (car_get_door1(p->car) != 0) goto advance;
            p->anim = 0x1e;
        } else {
            int along;
            switch (g_ped_door_step) {
            case 2: snd(p, 0); /* fall through */
            case 4: case 6: case 8: along = c->door_dx - 4; break;
            default: along = c->door_dx - 2; break;
            }
            car_point(c, along, c->door_dy - 2, &x, &y);
            if (((p->spr.x ^ x) & 0xff800000) != 0 || ((p->spr.y ^ y) & 0xff800000) != 0) {
                coll_remove(p, p->spr.unk20);
                coll_insert(COLL_PED, p->id, p, p->spr.unk20, x, y);
            }
            if (g_ped_door_step == 4) p->spr.angle = (int16_t)((p->spr.angle + 0x80) & 0x3ff);
            g_ped_door_step += 2;
            p->spr.x = x;
            p->spr.y = y;
        }
        break;
    case 0x1b:
        g_ped_door_step = 0;
        p->anim = 0x1e;
        if (p->u40 != -1) p->anim = 0x1c;   /* there is a driver to pull out */
        break;
    case 0x1c:   /* pulling the driver (+0x40) out */
        if (p->u40 > -1) {
            c = car_get(p->car);
            CAR_I16(c, 0x136) = 0;
            car_point(c, c->door_dx - 2, c->door_dy - 6, &x, &y);
            Ped *q = &g_peds[p->u40];
            if (!car_info_is_convertible(p->car)) {
                ped_spawn_in_slot(x, y, p->spr.z + 1, 0, (c->spr.angle + 0x100) & 0x3ff, 0x93, p->u40, c->id);
                player_exit_car(p->u40, p->car);
                if (c->model == 4 && q->player_ctl != 1) {   /* out of a police car: a cop */
                    q->remap = 0;
                    sprite_set_remap(&q->spr, 0);
                    q->graphic = 1;
                }
                CAR_I32(c, 0x244) = 1;
                q->u7c = 0x13;
                q->car = p->car;
                if (p->state == 0xb) {
                    c->driver = -1;
                    q->state = 9;
                    Snd_PlayPedVoice(p->spr.x, p->spr.y, p->spr.z, 0x15, p->id);
                    CAR_I32(c, 0x244) = 0;
                }
                carphys_end(c->id);
            } else {
                move_to(q, x, y);
                CAR_I16(c, 0x88) = 0;
                CAR_I32(c, 0x244) = 1;
                q->u7c = 0x13;
                if (p->state == 0xb) {
                    q->state = 9;
                    Snd_PlayPedVoice(p->spr.x, p->spr.y, p->spr.z, 0x15, p->id);
                    CAR_I32(c, 0x244) = 0;
                }
                q->anim = 0x93;
                q->attach_kind = 0;
                q->spr.zkey = q->spr.z - 0x40000;
                q->spr.angle = (int16_t)((c->spr.angle + 0x100) & 0x3ff);
                police_update_criminal_target(0, p->car, 1, q->id);
                zset = true;
                q->car = p->car;
                player_exit_car(p->u40, p->car);
                carphys_end(c->id);
            }
        }
        p->anim++;
        if (p->objective == 0x26) p->anim = 0x2e;
        break;

    /* ---- climbing in and closing the door ---- */
    case 0x1e: case 0x1f: case 0x20: case 0x21: {
        static const int16_t in[4] = { 4, 8, 0xc, 0xe };
        p->speed = in[a - 0x1e];
        c = car_get(p->car);
        car_point(c, c->door_dx, c->door_dy - p->speed, &x, &y);
        p->speed = 0;
        move_to(p, x, y);
        if (car_info_is_convertible(p->car)) {
            zfall = true;
            key_above(p, c->spr.zkey);
        }
        p->anim++;
    }
        /* fall through */
    case 0x22:
        zset = zfall;
        if (car_close_door1_step(p->car)) {
            p->anim = 0;
            if (p->u40 != -2) car_set_driver_by_id(p->car, p->id);
            car_set_owner_status(p->car, 1);
            c = car_get(p->car);
            if (p->u40 != -2) {
                c->driver = p->id;
                if (CAR_I16(c, 0x88) == 0) CAR_I16(c, 0x80) = 4;
            }
            if (!car_info_is_convertible(p->car)) {
                coll_remove(p, p->spr.unk20);
                p->anim = 0;
            } else {   /* sitting visibly in the convertible */
                p->attach_kind = 1;
                p->attach_id = p->car;
                p->anim = 0x80;
                p->u56 = 0;
                p->u54 = 6;
            }
            snd(p, 1);
            p->state = 7;
            if (CAR_I16(c, 0x88) == 0) {
                snd(p, 5);
                if (CAR_I16(c, 0x88) == 0) CAR_I16(c, 0x80) = 4;
            }
            CAR_I16(c, 0xc0) = 0;
            if (car_info_is_convertible(p->car)) {
                zset = true;
                key_above(p, c->spr.zkey);
            }
            if (p->player_ctl == 1) {
                player_enter_car(p->id, p->car);
                c = car_get(p->car);
                if (c->physics == 0) carphys_begin(c->id);
            }
            CAR_I32(c, 0x244) = 0;
            p->u40 = -1;
        }
        break;

    /* ---- out of the far side ---- */
    case 0x23: case 0x24: case 0x25: {
        static const int8_t across[3] = { 0, 2, 4 };
        c = car_get(p->car);
        car_point(c, c->door_dx - 2, c->door_dy + across[a - 0x23], &x, &y);
        move_to(p, x, y);
        p->anim++;
        if (a == 0x25 && car_info_is_convertible(p->car)) {
            key_above(p, c->spr.zkey);
            zset = true;
        }
        break;
    }
    case 0x26:
        p->speed = 0;
        p->anim = 0x2b;
        snd(p, 0xf);
        police_update_criminal_target(0, p->car, 1, p->id);
        p->turn = 0;
        if (car_info_is_convertible(p->car)) {
            key_above(p, p->spr.z);
            zset = true;
        }
        break;

    /* ---- stumbling and lying down ---- */
    case 0x27: case 0x5a:
        if (p->speed == 0) p->speed = 1;
        /* fall through */
    case 0x28: case 0x29: case 0x5b: case 0x5c:
        p->anim = (int16_t)(a + 1);
        if (p->speed > 2) p->speed--;
        break;
    case 0x2a: {
        CollBox *b = coll_build_box(p->spr.x, p->spr.y, p->spr.z, 1, 1, 0, 10, &g_ped_box);
        p->anim = coll_query_box_first(b, COLL_CAR, 1, p->id) ? 0x27 : 0x2b;
        coll_unlock();
        break;
    }
    case 0x2b:
        if (++p->idle_count > 0x14) {
            p->idle_count = 0;
            p->speed = 0;
            p->spr.angle = (int16_t)((p->spr.angle + 0x100) & 0x3ff);
            p->anim = PED_ANIM_STAND;
        }
        break;
    case 0x2c: case 0x2d:   /* dead */
        p->turn = 0;
        if (p->health != 0) {
            score_ped_killed(p, 1);
            if (p->objective == 0x15 || p->objective == 0x16) ped_leave_group(p);
            p->state = 0x17;
            if (p->control != 1 && p->u8b == 0) ambulance_request_for_ped(p->id);
            p->health = 0;
            p->spr.zkey += p->id;
            p->walk_x = 0;
            p->turn = 0;
            if (p->player_ctl == 1 && p->car != -1) {
                c = car_get(p->car);
                if (c->driver == p->id) {
                    c->driver = -1;
                    if (c->model == 4) c->control = 3;
                }
            }
        }
        break;

    /* ---- water ---- */
    case 0x2f:
        if (p->state != 9 && p->state != 5) p->anim = 1;
        break;
    case 0x30:
        if (p->carried > -1) obj_delete_wrapper(p->carried);
        if (p->idle_count > 5) {
            p->anim++;
            p->idle_count = 0;
        }
        p->idle_count++;
        break;
    case 0x31:
        if (p->idle_count > 5) {
            p->anim = 0x30;
            p->idle_count = 0;
        }
        p->idle_count++;
        break;
    case 0x32:
        p->anim = 0x2b;
        break;

    /* ---- out of a convertible ---- */
    case 0x37:
        snd(p, 0x10);
        if (p->state != 0xb) p->anim = 0x51;
        break;
    case 0x39:
        p->spr.angle = (int16_t)((p->spr.angle + 0x100) & 0x3ff);
        p->spr.zkey = p->spr.z - 0x80000;
        p->attach_kind = 0;
        p->attach_id = 0;
        car_get(p->car)->driver = -1;
        x = p->spr.x + SIN(p->spr.angle & 0x3ff) * 6;
        y = p->spr.y + COS(p->spr.angle & 0x3ff) * 6;
        move_to(p, x, y);
        p->anim++;
        break;
    case 0x3a: {
        int16_t ang = p->spr.angle;
        p->anim = 0xaf;
        x = p->spr.x + SIN(ang & 0x3ff) * 6;
        y = p->spr.y + COS(ang & 0x3ff) * 6;
        move_to(p, x, y);
        p->speed = 2;
        p->state = 2;
        p->u7c = 2;
        p->spr.angle = (int16_t)((ang - 0x200) & 0x3ff);
        police_update_criminal_target(0, p->car, 1, p->id);
        c = car_get(p->car);
        CAR_I32(c, 0x244) = 0;
        if (p->player_ctl == 1) player_exit_car(p->id, p->car);
        carphys_end(c->id);
        break;
    }
    case 0x40: case 0x7e:   /* gone (into a building / vehicle) */
        p->anim = 0;
        coll_remove(p, p->spr.unk20);
        break;

    /* ---- side steps and turns ---- */
    case 0x46:
    walk:
        p->anim = 1;
        break;
    case 0x47:
        p->anim = (int16_t)(a + 1);
        p->spr.angle = (int16_t)((p->spr.angle - 0x80) & 0x3ff);
        /* fall through */
    case 0x48:
        p->anim = 1;
        p->spr.angle = (int16_t)((p->spr.angle - 0x80) & 0x3ff);
        break;
    case 0x49:
        p->anim = (int16_t)(a + 1);
        p->spr.angle = (int16_t)((p->spr.angle + 0x80) & 0x3ff);
        break;
    case 0x4a:
        p->anim = 1;
        p->spr.angle = (int16_t)((p->spr.angle + 0x80) & 0x3ff);
        break;
    case 0x4b: case 0x4c: {
        int d = (p->spr.angle + 0x100) & 0x3ff;
        sidestep(p, SIN(d), COS(d));
        p->anim++;
        coll_unlock();
        break;
    }
    case 0x4d: {
        int d = (p->spr.angle + 0x100) & 0x3ff;
        sidestep(p, SIN(d), COS(d));
        p->anim = 1;
        snap_axis(p);
        coll_unlock();
        break;
    }
    case 0x4e: case 0x4f: {
        int d = (p->spr.angle - 0x100) & 0x3ff;
        sidestep(p, SIN(d) * 3, COS(d) * 3);
        p->anim++;
        coll_unlock();
        break;
    }
    case 0x50: {
        int d = (p->spr.angle - 0x100) & 0x3ff;
        sidestep(p, SIN(d) * 3, COS(d) * 3);
        p->anim = 1;
        snap_axis(p);
        coll_unlock();
        break;
    }

    /* ---- onto / off an open vehicle (riding it) ---- */
    case 0x51: case 0x52: case 0x53: case 0x54:
        p->spr.zkey = p->spr.z - 0x80000;
        p->attach_kind = 1;
        p->attach_id = p->car;
        p->anim = (int16_t)(a + 1);
        break;
    case 0x55:
        p->anim = 0x7f;
        car_set_status(p->car, 0);
        car_set_owner_status(p->car, 1);
        p->attach_kind = 1;
        p->attach_id = p->car;
        p->u56 = -6;
        p->u54 = 0;
        if (p->player_ctl == 1) {
            police_update_criminal_target(1, p->id, 0, p->car);
            player_enter_car(p->id, p->car);
            c = car_get(p->car);
            CAR_I32(c, 0x244) = 0;
            c->driver = p->id;
            if (c->physics == 0) carphys_begin(c->id);
        }
        break;
    case 0x56: case 0x57:
        car_set_status(p->car, 8);
        /* fall through */
    case 0x58:
        p->anim++;
        if (p->player_ctl != 1) move_to(p, SIN(p->spr.angle & 0x3ff) + p->spr.x, COS(p->spr.angle & 0x3ff) + p->spr.y);
        break;
    case 0x59:
        p->anim = PED_ANIM_STAND;
        p->attach_id = 0;
        p->attach_kind = 0;
        c = car_get(p->car);
        p->state = 2;
        if (p->speed < 3) p->speed = 0;
        else p->anim = 0x27;
        player_exit_car(p->id, p->car);
        CAR_I32(c, 0x244) = 0;
        carphys_end(c->id);
        p->u7c = 2;
        p->car = -1;
        break;

    /* ---- landing ---- */
    case 0x5d:
        if ((ped_type_cache(bk(p->spr.x), bk(p->spr.y), bk(p->spr.z)) & 0x70) == 0x10) {
            /* in water: the original's test "a < 0x30 and a > 0x31" can never hold */
            p->health = 100;
        } else {
            snd(p, 0x19);
            ground_object(p, 0x3f);
            p->anim = 0x2d;
            coll_unlock();
        }
        break;
    case 0x61:
        p->anim = PED_ANIM_STAND;
        if (p->state == 10) p->anim = 0x5e;
        else p->state = p->u74;
        break;

    /* ---- pulling a driver out of the passenger side ---- */
    case 100:
        if (p->u48 == 0) {
            car_reset_siren99(car_get(p->car));
            if (car_open_door1_step(p->car)) p->anim++;
        }
        break;
    case 0x66:
        if (p->u40 > -1) {
            c = car_get(p->car);
            car_point(c, c->door_dx - 2, c->door_dy - 6, &x, &y);
            int16_t qi = p->u40;
            Ped *q = &g_peds[qi];
            if (!car_info_is_convertible(p->car)) {
                if (q->graphic == 1 && q->remap == 0) {   /* a cop driver gets out by himself */
                    p->anim = 0x5a;
                    p->spr.angle = (int16_t)((p->spr.angle - 0x100) & 0x3ff);
                    ped_update_sprite(p->id);
                } else {
                    if (q->player_ctl == 1) {
                        /* (the original inserts the node without removing it first) */
                        if (((q->spr.x ^ x) & 0xff800000) != 0 || ((q->spr.y ^ y) & 0xff800000) != 0)
                            coll_insert(COLL_PED, q->id, q, q->spr.unk20, x, y);
                        q->spr.x = x;
                        q->spr.y = y;
                    } else {
                        ped_spawn_in_slot(p->spr.x, p->spr.y, p->spr.z + 1, 0, (p->spr.angle - 0x100) & 0x3ff, 0x93, qi, c->id);
                    }
                    q->u7c = 0x13;
                    CAR_I32(c, 0x244) = 1;
                    q->anim = 0x93;
                    q->spr.zkey = q->spr.z - 0x40000;
                }
            } else {
                q->attach_kind = 0;
                q->spr.angle = (int16_t)((p->spr.angle - 0x100) & 0x3ff);
                q->u7c = 0x13;
                CAR_I32(c, 0x244) = 1;
                q->anim = 0x93;
                q->spr.zkey = q->spr.z - 0x40000;
            }
            player_exit_car(qi, p->car);
        }
        /* (with no driver, +0x40 = -1, the original reads the record before the table) */
        if (p->u40 > -1 && g_peds[p->u40].player_ctl == 1) player_exit_car(p->u40, p->car);
        p->anim++;
        break;
    case 0x67:
        if (car_close_door1_step(p->car)) {
            snd(p, 1);
            c = car_get(p->car);
            car_point(c, c->door_dx - 4, c->door_dy + 4, &x, &y);
            move_to(p, x, y);
            p->speed = 0;
            p->idle_count = 0;
            p->anim = 0x62;
            p->spr.angle = (int16_t)(c->spr.angle & 0x3ff);
            if (p->u40 > -1) g_peds[p->u40].u48 = 1;
        }
        break;
    case 0x68:
        if (++p->idle_count > 4) {
            p->idle_count = 0;
            p->anim = (int16_t)(a + 1);
            p->speed = 0;
        }
        break;
    case 0x6a:
        p->anim = 0x62;
        p->idle_count = 0;
        p->speed = 0;
        break;
    case 0x6b:
        if (car_open_door2_step(p->car)) p->anim++;
        break;
    case 0x6d:
        p->u4e = (int16_t)obj_create_attached(p->id, 4, 4, 0, 0x18);
        if (p->u4e != -1) p->anim++;
        break;
    case 0x6e: case 0x72:
        if (car_close_door2_step(p->car)) p->anim = 1;
        break;
    case 0x6f:
        car_open_door2_step(p->car);
        p->anim++;
        break;
    case 0x71:
        if (car_open_door2_step(p->car)) {
            obj_delete(p->u4e);
            p->anim++;
            p->u4e = -1;
        }
        break;

    /* ---- over a bonnet ---- */
    case 0x73:
        p->u7c = 0x12;
        /* fall through */
    case 0x74: case 0x75: case 0x76: {
        CollBox *b = coll_build_box(p->spr.x + SIN(p->spr.angle & 0x3ff) * 2, p->spr.y + COS(p->spr.angle & 0x3ff) * 2,
                                    p->spr.z, 6, 6, 0, 10, &g_ped_box);
        if (!coll_query_box_first(b, COLL_CAR, 1, p->id)) {
            p->anim = 0xd;
            p->u7c = p->u8b == 1 ? p->u80 : 2;
        } else if (a != 0x76) {
            p->anim++;
        }
        if (a == 0x76 && p->u8b == 0) p->state = 2;
        coll_unlock();
        break;
    }
    case 0x78: {
        int16_t s = p->u12;
        p->anim = 0xd;
        p->u7c = 2;
        if (s != 0 && p->player_ctl == 1) p->turn = s;
        break;
    }

    /* ---- the rear door ---- */
    case 0x81:
        if (car_open_rear_door_step(p->car)) {
            car_reset_siren99(car_get(p->car));
            if (car_get_door1(p->car) != 0) goto advance;
            p->anim = 0x82;
        } else {
            c = car_get(p->car);
            int16_t ra = info16(c, 0xb6), rb = info16(c, 0xb8);   /* the rear door offsets */
            int along;
            switch (g_ped_door_step) {
            case 2: snd(p, 0); /* fall through */
            case 4: case 6: case 8: along = ra + 4; break;
            default: along = ra + 2; break;
            }
            car_point(c, along, rb - 2, &x, &y);
            if (((p->spr.x ^ x) & 0xff800000) != 0 || ((p->spr.y ^ y) & 0xff800000) != 0) {
                coll_remove(p, p->spr.unk20);
                coll_insert(COLL_PED, p->id, p, p->spr.unk20, x, y);
            }
            if (g_ped_door_step == 4) p->spr.angle = (int16_t)((p->spr.angle + 0x80) & 0x3ff);
            g_ped_door_step += 2;
            p->spr.x = x;
            p->spr.y = y;
        }
        break;
    case 0x82: case 0x83: case 0x84: case 0x85: case 0x86: {
        static const int16_t in[5] = { 4, 8, 0xc, 0x10, 0x12 };
        p->speed = in[a - 0x82];
        c = car_get(p->car);
        car_point(c, info16(c, 0xb6) + 2, info16(c, 0xb8) - p->speed, &x, &y);
        move_to(p, x, y);
        p->speed = 0;
        c->spr.zkey++;   /* (the car's depth key: the original adds one each frame) */
        p->anim++;
        zfall = true;
    }
        /* fall through */
    case 0x87:
        zset = zfall;
        if (car_close_rear_door_step(p->car)) {
            p->anim = 0;
            coll_remove(p, p->spr.unk20);
            c = car_get(p->car);
            p->state = 6;
            p->u78 = 8;
            p->u7c = 2;
            snd(p, 1);
            if (CAR_I16(c, 0x88) == 0) snd(p, 5);
        }
        break;
    case 0x88:
        if (p->speed != 0) goto walk;
        break;

    /* ---- shot ---- */
    case 0x89: case 0x8a: case 0x8b:
        knock(p, -1);
        break;
    case 0x8c:
        if (p->health != 0) die_shot(p, 0x2d);
        break;
    case 0x8d: case 0x8e: case 0x8f: case 0x90:
        knock(p, 1);
        break;
    case 0x91:
        if (p->health != 0) die_shot(p, 0x2c);
        break;
    case 0x92: {   /* sliding along a car */
        CollBox *b = coll_build_box(p->spr.x, p->spr.y, p->spr.z, 4, 4, 0, 10, &g_ped_box);
        if (!coll_query_box(b, COLL_CAR, 1, p->id)) {
            p->anim = PED_ANIM_STAND;
        } else {
            move_to(p, p->spr.x + SIN(p->spr.angle & 0x3ff) * 2, p->spr.y + COS(p->spr.angle & 0x3ff) * 2);
        }
        coll_unlock();
        break;
    }

    /* ---- thrown out of a car ---- */
    case 0x93: case 0x94: case 0x96: case 0x97: {
        static const int8_t along[4] = { -2, -2, -6, -8 }, across[4] = { -6, 0, 4, 4 };
        int k = a == 0x93 ? 0 : a == 0x94 ? 1 : a == 0x96 ? 2 : 3;
        c = car_get(p->car);
        car_point(c, c->door_dx + along[k], c->door_dy + across[k], &x, &y);
        move_to(p, x, y);
        p->anim++;
        break;
    }
    case 0x95:
        c = car_get(p->car);
        car_point(c, c->door_dx - 4, c->door_dy + 4, &x, &y);
        move_to(p, x, y);
        p->anim++;
        p->spr.angle = (int16_t)((p->spr.angle - 0x100) & 0x3ff);
        break;
    case 0x98: {
        Car *cc = tc;
        p->speed = 0;
        p->anim = 0x2b;
        snd(p, 0xf);
        police_update_criminal_target(0, p->car, 1, p->id);
        p->turn = 0;
        if (p->state == 9) {
            p->walk_x = -1;
            p->u48 = 0x1e;
            cc = car_get(p->car);
            cc->driver = -1;
        } else {
            p->state = 2;
        }
        if (p->car > -1) {
            cc = car_get(p->car);
            if (cc->driver == p->id) cc->driver = -1;
        }
        p->car = -1;
        p->u7c = 2;
        if (cc) CAR_I32(cc, 0x244) = 0;
        break;
    }

    /* ---- punched ---- */
    case 0xae:
        p->anim = PED_ANIM_STAND;
        if (p->u8b == 1) {
            p->speed = 0;
            p->u7c = p->u80;
        } else {
            p->u7c = 2;
        }
        break;
    case 0xaf: case 0xb0: case 0xb1: {   /* knocked back two pixels unless something is behind */
        int ang = p->spr.angle & 0x3ff;
        CollBox *b = coll_build_box(p->spr.x + SIN(ang) * -2, p->spr.y + COS(ang) * -2, p->spr.z, 8, 8, 0, 10, &g_ped_box);
        bool ok = true, building = false;
        for (int k = 0; k < 3; k++)
            if (ped_ground_type(bk(b->x[k]), bk(b->y[k]), bk(p->spr.z)) == 5) {
                ok = false;
                building = true;
                break;
            }
        b = coll_build_box(p->spr.x + SIN(ang) * 6, p->spr.y + COS(ang) * 6, p->spr.z, 6, 6, 0, 10, &g_ped_box);
        coll_compute_bounds(b);
        if (coll_map_walls(b, 0) == -1) {
            b = coll_build_box(p->spr.x, p->spr.y, p->spr.z, 4, 4, 0, 10, &g_ped_box);
            coll_compute_bounds(b);
            if (coll_map_walls(b, 0) != -1) ok = false;
        } else {
            ok = false;
        }
        if (!building) {
            b = coll_build_box(p->spr.x, p->spr.y, p->spr.z, 8, 8, 0, 10, &g_ped_box);
            coll_compute_bounds(b);
        }
        if (coll_map_slopes(b, 1) == -1 && ok) {
            ang = p->spr.angle & 0x3ff;
            move_to(p, p->spr.x + SIN(ang) * -2, p->spr.y + COS(ang) * -2);
        }
        p->speed = 0;
    }
    advance:
        p->anim++;
        break;
    case 0xb2:
        p->walk_x = 0;
        p->turn = 0;
        p->anim = 0x2b;
        if (p->state == 0x15) p->anim = 0xe9;
        break;

    /* ---- electrocuted ---- */
    case 0xe9:
        p->anim = (int16_t)(a + 1);
        p->idle_count = 0;
        sprite_set_remap(&g_peds[p->id].spr, 0);
        snd(p, 0x1b);
        break;
    case 0xea:
        p->idle_count++;
        p->anim = (int16_t)(a + 1);
        break;
    case 0xeb:
        if (p->idle_count > 10) goto next;
        p->anim = 0xea;
        break;
    case 0xec:
        p->anim = 0xed;
        break;
    case 0xed:
        p->firing = 0;
        if (p->health != 0) {
            score_ped_killed(p, 3);
            if (p->objective == 0x15 || p->objective == 0x16) ped_leave_group(p);
            p->state = 0x17;
            p->health = 0;
            p->spr.zkey += p->id;
            p->walk_x = 0;
            p->turn = 0;
        }
        break;
    case 0xf1:
        p->anim = PED_ANIM_STAND;
        break;
    }

    /* the depth key: dead or knocked-down peds a little above the ground by id (a player's just
       above), riding / sitting states from the car; on a slope one layer up */
    {
        int an = p->anim;
        if (an == 0x2c || an == 0x2d || an == 0x2b || p->health == 0) {
            p->spr.zkey = p->spr.z + p->id * 2;
            if (p->objective == 0x25) p->spr.zkey = p->spr.z + 1;
        } else if (!zset && (map_get_type_at(g_game.map, p->spr.x, p->spr.y, p->spr.z) >> 8 & 0x3f) == 0) {
            bool from_car;
            if ((an < 0x51 || an > 0x56) && an != 0x39 && an != 0x3a) {
                if (an != 0x80 && an != 0x7f && (an < 0x73 || an > 0x79)) {
                    p->spr.zkey = p->spr.z;
                    goto slope;
                }
                from_car = an == 0x80 || an == 0x7f;
            } else {
                from_car = an == 0x80 || an == 0x7f;
            }
            if (from_car && p->car >= 0 && p->car < CAR_MAX) p->spr.zkey = car_get(p->car)->spr.zkey - 1;
        }
    slope:
        if ((ped_type_cache(bk(p->spr.x), bk(p->spr.y), bk(p->spr.z)) & 0x80) && p->anim != 0x80 && p->anim != 0x7f &&
            !zset && p->u7c != 0x12 && p->u7c != 0x11 && p->u7c != 0x13)
            p->spr.zkey = p->spr.z - 0x400000;
    }
sprite:
    ped_update_sprite(p->id);
}
