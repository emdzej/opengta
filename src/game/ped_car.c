/* Peds and cars (0x44f510-0x44f8cf, 0x454dd0, 0x45d3d0-0x45f97f, 0x45fc60-0x46076f): drivers
   created in and thrown out of cars, the player getting in (Ped_TryEnterCar walks the ped to the
   driver's door, Ped_FinishEnterCar hands the car over) and out (Ped_PlayerExitCar), AI drivers
   leaving, the walk targets to a car's doors, and the respawn of a player at a police station or
   hospital (or a network respawn point). The getting in / out animations themselves are Ped_Animate's
   (ped_anim.c). See docs/peds.md. */
#include "ped_internal.h"
#include "carinfo.h"
#include "mission_obj.h"
#include "mission_ops.h"
#include "route.h"
#include "trigger.h"
#include "fire.h"
#include "path.h"
#include "police.h"
#include "train.h"
#include "wanted.h"
#include <stdlib.h>

/* Ped_CreateInSlot 0x454090 (ped_spawn.c) */
void ped_create_in_slot(int32_t x, int32_t y, int32_t z, int angle, int slot);
/* Ped_EjectDriver 0x44f510 (called by the car collision code) */
void ped_eject_driver(Car *c);
int map_find_pavement_along_road(int *bx, int *by, int *bz, int16_t *angle, int n);
void player_respawn_multi(Ped *p);

static int bk(int32_t v) { return v >> 22; }

/* the ped's grid node follows a move to (x, y) */
static void move_to(Ped *p, int32_t x, int32_t y)
{
    if (((p->spr.x ^ x) & 0xff800000) != 0 || ((p->spr.y ^ y) & 0xff800000) != 0) {
        coll_remove(p, p->spr.unk20);
        coll_insert(COLL_PED, p->id, p, p->spr.unk20, x, y);
    }
    p->spr.x = x;
    p->spr.y = y;
}

/* a ped's sprite set up afresh at its position: the first ped frame, the ped palette, its remap */
static void ped_sprite_reinit(Ped *p, int angle)
{
    sprite_init(&p->spr, p->spr.x, p->spr.y, p->spr.z, angle, sprite_group_base(SPRITE_GROUP_PED));
    sprite_set_palette(&p->spr, sprite_ped_palette());
    sprite_set_remap(&p->spr, p->remap);
}

/* the car's controls released when its driver leaves */
static void car_release_controls(Car *c)
{
    c->input = 0;
    c->ubc = 0;
    c->u1e = 0;
    c->turn_delta = 0;
    c->driver = -1;
}

/* Ped_EjectDriver 0x44f510: the driver of a car is thrown out (a bike falling over, a car hit hard;
   not the model 0x2f, the hunter car): back on its feet at the car's position one pixel up, at speed
   8, health 100, falling (anim 0x27); a player ped walks on (state 2), others go to state 1. The car
   loses its driver and control. A bike's or open car's driver had no grid node: it gets one again. */
void ped_eject_driver(Car *c)
{
    if (c->model == 0x2f) return;
    if (c->driver == -1) return;
    Ped *d = &g_peds[c->driver];
    d->attach_kind = 0;
    d->speed = 8;
    d->health = 100;
    if (c->vtype == 3 || car_info_is_convertible(c->id)) coll_remove(d, d->spr.unk20);
    d->spr.x = c->spr.x;
    d->spr.y = c->spr.y;
    d->spr.angle = c->spr.angle;
    d->spr.z = c->spr.z - 0x10000;
    d->u5a = -1;
    d->spr.zkey = c->spr.zkey - 0x10000;
    player_exit_car(c->driver, c->id);
    d->car = c->id;
    coll_insert(COLL_PED, c->driver, d, d->spr.unk20, d->spr.x, d->spr.y);
    d->anim = 0x27;
    d->objective = 0x19;
    d->state = d->player_ctl == 1 ? 2 : 1;
    d->attach_id = 0;
    d->attach_kind = 0;
    d->u78 = 8;
    d->u7c = 2;
    d->u84 = 0;
    ped_update_sprite(d->id);
    police_update_criminal_target(0, d->car, 1, c->driver);
    d->car = -1;
    c->driver = -1;
    c->control = 0;
    c->u244 = 0;
    c->script_held = 0;
    g_peds_active++;
}

/* Ped_CreateCarDriver 0x44f7a0: the driver slot of the car (car + 200) made a fresh ped sitting in
   it (state 7, riding the car: attach kind 1), one pixel above it; in a closed car it is hidden
   (anim 0x7f, offset -6 along), in an open one visible at the wheel (anim 0x80, offset 6 across). */
void ped_create_car_driver(Car *c)
{
    int id = (int16_t)(c->id + PED_DRIVER_FIRST);
    ped_reset(id);
    Ped *p = &g_peds[id];
    p->state = 7;
    p->spr.x = c->spr.x;
    p->spr.y = c->spr.y;
    p->spr.angle = c->spr.angle;
    p->spr.z = c->spr.z - 1;
    p->car = c->id;
    c->driver = (int16_t)id;
    p->spr.zkey = c->spr.zkey - 1;
    if (!car_info_is_convertible(c->id)) {
        p->anim = 0x7f;
        p->u56 = -6;
        p->u54 = 0;
    } else {
        p->u56 = 0;
        p->u54 = 6;
        p->anim = 0x80;
    }
    ped_update_sprite(id);
    p->health = 100;
    g_peds_active++;
    coll_insert(COLL_PED, id, p, p->spr.unk20, p->spr.x, p->spr.y);
    p->attach_kind = 1;
    p->attach_id = c->id;
    p->objective = 0x19;
    p->control = 0;
    p->weapon = 0;
    p->state = 7;
    p->u78 = 8;
    p->u7c = 2;
    p->u84 = 0;
    sprite_set_remap(&p->spr, p->remap);
}

/* Ped_SitInCar 0x454dd0: a ped at the passenger door (the car info's second door, +0xb6 / +0xb8)
   takes the car over, unless it moves faster than 2. Unless +0x40 says otherwise (-2, -3) the old
   driver is noted in +0x40, gets the car as its own and loses control (a player gets out), and the
   ped becomes the driver. The ped moves to the door, faces the car's way and starts the sit-down
   animation (anim 0x81, state 6, action 0x11) a pixel above the car; on a bike (vtype 3) it is the
   bike's frames (anim 0x51, or 0x33 throwing the old rider off with 0x39). */
void ped_sit_in_car(Ped *p)
{
    Car *c = car_get(p->car);
    const uint8_t *info = car_info_of_model(c->model);
    int16_t ox = info ? carinfo_s16(info, 0xb6) : 0, oy = info ? carinfo_s16(info, 0xb8) : 0;
    coll_unlock();
    if (c->speed > 2) return;
    int a = c->spr.angle & 0x3ff, b = (c->spr.angle + 0x100) & 0x3ff;
    int32_t x = SIN(a) * ox + SIN(b) * oy + c->spr.x;
    int32_t y = COS(a) * ox + COS(b) * oy + c->spr.y;
    int old = -1;
    if (p->u40 != -2 && p->u40 != -3) {
        police_update_criminal_target(1, p->id, 0, c->id);
        int d = c->driver;
        old = d;
        if (d > -1) {
            if (g_peds[d].player_ctl == 1) player_exit_car(d, c->id);
            p->u40 = (int16_t)d;
            g_peds[d].car = c->id;
            c->driver = p->id;
            if (c->unk88 == 0) c->enter_delay = 4;
        }
    }
    p->car = c->id;
    p->spr.angle = c->spr.angle;
    if (((p->spr.x ^ x) & 0xff800000) != 0 || ((p->spr.y ^ y) & 0xff800000) != 0) {
        coll_remove(p, p->spr.unk20);
        coll_insert(COLL_PED, p->id, p, p->spr.unk20, x, y);
    }
    if (c->unk88 == 0) c->enter_delay = 4;
    if (p->player_ctl != 1) c->unkc0 = 0;
    /* (a player-controlled branch that would set the control to 0 can't be reached) */
    if (p->u40 == -2) c->driver = -1;
    p->spr.x = x;
    p->spr.y = y;
    p->walk_x = 0;
    p->walk_y = 0;
    p->speed = 0;
    p->anim = 0x81;
    p->spr.zkey = c->spr.z + 0x10000;
    p->state = 6;
    p->u78 = 0xf;
    p->u7c = 0x11;
    if (c->vtype == 3) {
        c->status = 8;
        p->anim = 0x51;
        if (old != -1) {
            p->anim = 0x33;
            p->spr.zkey = c->spr.z - 0x10000;
            g_peds[old].anim = 0x39;
        }
    }
}

/* Ped_PlayerExitCar 0x45d3d0: the enter / exit key in a car (or on a train). Not while getting in
   (action 0x13), not faster than 4, not while the car ignores the player (+0x244). In the hunter car
   (model 0x2f) the player goes back to the car of its mission slot. The driver's door must be free:
   a box beside it clear of cars, map walls and slopes, on ground that is neither air nor a building.
   - Blocked: the driver climbs out over the bonnet (anim 0x73) at once, half the car's length ahead.
   - Free: the door opens (anim 0x11, action 0x13: Ped_Animate does the rest; a bike: anim 0x56) and
     the ped is placed 8 pixels inside the door.
   On a train the ped steps off when the platform side allows it. The player's +0x1a4 remembers the
   car. The ped gets its sprite back (player-controlled, not firing). */
void ped_player_exit_car(int32_t ref[2], int ped)
{
    Ped *pp = &g_peds[(int16_t)ped];
    Car *c = NULL;
    int32_t lx = 0, ly = 0, lz = 0;
    int16_t ldx = 0, ldy = 0;
    int carid = 0;
    int who = 0;
    bool go = true;
    if (pp->u7c == 0x13) return;
    if (ref[0] == 0) {
        c = car_get((int16_t)ref[1]);
        if (abs(c->speed) > 4) return;
        if (c->u244 == 1) return;
        if (c->model == 0x2f) {
            c->driver = -1;
            c->control = 0;
            int n = player_find_by_ped(ped);
            c = car_get(mission_get_player_slot(n));
            player_set_controlled(n, 0, c->id);
            player_set_view_target(n, 0, c->id);
            mission_clear_player_slot(n);
            if (c->model == 0x2f) return;
        }
        if (player_find_by_ped(ped) > -1) player_set_field1a4(player_find_by_ped(ped), c->id);
        int a = c->spr.angle & 0x3ff, b = (c->spr.angle + 0x100) & 0x3ff;
        /* (the original subtracts 2 from the 16.16 sum: two 65536ths of a pixel) */
        int32_t dx = SIN(a) * c->door_dx - 2 + c->spr.x;
        int32_t dy = COS(a) * c->door_dx - 2 + c->spr.y;
        CollBox *bx = coll_build_box(SIN(b) * (c->door_dy + 2) + dx, COS(b) * (c->door_dy + 2) + dy, c->spr.z, 4, 4, 0, 10,
                                     &g_ped_box);
        coll_compute_bounds(bx);
        CollHit *hit = coll_query_box_first(bx, COLL_CAR, COLL_CAR, c->id);
        uint8_t t = 0;
        for (int k = 0; k < 4; k++) t = ped_type_cache(bk(bx->x[k]), bk(bx->y[k]), bk(c->spr.z));   /* the last corner */
        CollBox *b2 = coll_build_box(SIN(b) * c->door_dy + dx, COS(b) * c->door_dy + dy, c->spr.z, 8, 8, 0, 10, &g_ped_box);
        coll_compute_bounds(b2);
        int g = coll_map_walls(b2, 0) == -1 ? (t & 0x70) >> 4 : 5;
        coll_compute_bounds(b2);
        int s = coll_map_slopes(b2, 1);
        if (s == -1 && g != 5 && g != 0 && !hit) {
            who = c->driver;
            carid = c->id;
            pp->mode = -1;
            if (c->driver > -1) {
                police_update_criminal_target(0, c->id, 1, c->driver);
                go = true;
                coll_unlock();
                goto out;
            }
            /* no driver: who stays -1, nothing happens below */
        } else {
            /* blocked: over the bonnet */
            who = c->driver;
            Ped *d = &g_peds[who];
            carid = c->id;
            d->mode = -1;
            police_update_criminal_target(0, c->id, 1, who);
            car_siren_off(c);
            car_release_controls(c);
            if (c->speed == 0) {
                c->unk88 = 0;
                c->enter_delay = 0;
            }
            int32_t x = SIN(a) * (c->half_l >> 1) + c->spr.x;
            int32_t y = COS(a) * (c->half_l >> 1) + c->spr.y;
            if (car_info_is_convertible(d->car) || d->anim == 0x7f) coll_remove(d, d->spr.unk20);
            d->spr.x = x;
            d->spr.y = y;
            d->spr.z = c->spr.z - 0x10000;
            coll_build_box(SIN(a) * (c->half_l + 8) + c->spr.x, COS(a) * (c->half_l + 8) + c->spr.y, c->spr.z, 4, 4, 0, 10,
                           &g_ped_box);   /* (built, not used) */
            d->player_ctl = 1;
            d->firing = 0;
            d->spr.angle = (int16_t)((c->spr.angle + 0x100) & 0x3ff);
            ped_sprite_reinit(d, d->spr.angle);
            d->spr.zkey = c->spr.zkey - 0x10000;
            bool open = d->anim == 0x7f || car_info_is_convertible(carid);
            coll_insert(COLL_PED, who, d, d->spr.unk20, d->spr.x, d->spr.y);
            if (open) d->attach_kind = 0, d->attach_id = 0;
            d->anim = 0x73;
            d->spr.angle = c->spr.angle;
            ped_update_sprite(who);
            d->speed = 4;
            d->state = 2;
            d->u7c = 2;
            d->u78 = 8;
            player_exit_car(who, c->id);
            go = false;
        }
        coll_unlock();
    } else {
        int st = pp->state;
        if (train_check_platform_sides(pp->train) == -1) return;
        int r = train_command(5, pp->train);
        if (r == 0) police_update_criminal_target(2, (int16_t)ref[1], 1, ped);
        go = r == 0 && st == 0x12;
        who = ped;
    }
out:;
    uint16_t ang = 0;
    if (!go) return;
    if ((int16_t)who < 0) return;
    Ped *d = &g_peds[(int16_t)who];
    if (d->anim != 0 && d->anim != 0x73 && d->attach_kind == 0) return;
    if (d->health < 1) return;
    if (ref[0] == 0) {
        car_siren_off(c);
        car_release_controls(c);
        if (c->speed == 0) {
            c->unk88 = 0;
            c->enter_delay = 0;
        }
        ly = c->spr.y;
        lz = c->spr.z;
        c->control = 0;
        lx = c->spr.x;
        ldy = c->door_dy;
        ang = (uint16_t)c->spr.angle;
        ldx = c->door_dx;
        d->anim = 0x11;
        d->car = c->id;
        if (c->vtype == 3) {
            d->anim = 0x56;
            d->u7c = 0x13;
            c->status = 8;
        } else {
            car_reset_siren99(c);
            car_open_door1_step(c->id);
        }
        d->u7c = 0x13;
        d->state = 2;
        d->u78 = 8;
    } else if (ref[0] == 1) {
        if (train_is_boarded(pp->train) == 1) train_command(2, pp->train);
        train_command(7, pp->train);
        int t = pp->train;
        if (train_get_door_offsets(0) == 0) train_update_door_sprites(pp->train);
        train_load_passengers(t, ped);
        player_set_on_foot(ped);
        ang = (uint16_t)pp->spr.angle;
        pp->speed = 2;
        who = ped;
        pp->anim = 0x41;
    }
    /* (the anim tested is the player's ped's, the one placed the car's driver's: the same ped) */
    if (pp->anim != 0x41) {
        Ped *w = &g_peds[(int16_t)who];
        int a = ang & 0x3ff;
        int32_t ox = lx + SIN(a) * ldx;
        if (car_info_is_convertible(w->car) || w->anim == 0x56) coll_remove(w, w->spr.unk20);
        if (pp->anim != 0x41) {
            int b = (ang + 0x100) & 0x3ff;
            int dd = ldy;
            if (w->anim == 0x56) ang = (uint16_t)((ang + 0x100) & 0x3ff);
            else dd -= 8;
            w->spr.x = SIN(b) * dd + ox;
            w->spr.y = COS(b) * dd + ly + COS(a) * ldx;
            w->spr.z = lz;
        }
    }
    d = &g_peds[(int16_t)who];
    d->player_ctl = 1;
    d->firing = 0;
    ped_sprite_reinit(d, ang);
    if (d->anim != 0x41) {
        bool open = d->anim == 0x56 || car_info_is_convertible(carid);   /* (car 0 on a train) */
        coll_insert(COLL_PED, (int16_t)who, d, d->spr.unk20, d->spr.x, d->spr.y);
        if (open) d->attach_kind = 0, d->attach_id = 0;
    }
    ped_update_sprite(d->id);
    if (d->anim == 0x41 && d->player_ctl == 1) d->anim = 1;
}

/* Ped_FinishEnterCar 0x45ddc0: the ped at the door takes the car over. The car's control follows the
   ped's (a cop's control 1 makes it a police car, 3; a pursuing cop car loses its cop, Cop_Dismiss),
   physics on, brake and inputs off, the door animation starts (anim 0x1a; a bike: 0x51, or 0x33
   throwing the old rider off), and a car taken from a driver is a crime (5). The original takes the
   car and the ped id; the port's argument order is (ped, car). */
void ped_finish_enter_car(Ped *p, int car)
{
    Car *c = car_get((int16_t)car);
    int ped = p->id;
    int old = c->driver;
    c->owner_status = 1;
    police_update_criminal_target(1, ped, 0, c->id);
    c->unkc0 = 1;
    if (c->control == 3 && p->objective != 0x28) cop_dismiss(c, ped);
    c->control = p->control == 1 ? 3 : p->control != 0;
    c->brake = 0;
    c->u1e = 0;
    c->input = 0;
    g_car_forced_accel[c->id] = 0;   /* Car_SetForcedAccel 0x4082b0 */
    p->anim = 0x1a;
    if (c->vtype == 3) {
        c->status = 8;
        p->anim = 0x51;
        if (old != -1) {
            p->anim = 0x33;
            p->spr.zkey = c->spr.z - 0x40000;
            g_peds[old].anim = 0x39;
        }
    }
    c->driver = (int16_t)ped;
    if (old != -1) police_report_crime(0, c->id, 5, c->spr.x, c->spr.y, c->spr.z);
    if (c->unk88 == 0) c->enter_delay = 4;
}

/* the train part of Ped_TryEnterCar: walk to a train door in two legs (modes 0x37, 0x38: 0x38 px
   then 0x10 px off its side), then board (anim 0x3b, state 0x12). `h` lists the trains (kind 10)
   found; (x, y) is the last probe point. */
static void try_board_train(Ped *p, CollHit *h, int32_t x, int32_t y)
{
    int ped = p->id;
    uint16_t dir = 0;
    for (;;) {
        const Sprite *t = h->owner;
        int32_t tx = t->x, ty = t->y;
        int ta = (uint16_t)t->angle;
        int s19 = (ta - 0x100) & 0x3ff;
        int32_t ux = SIN(s19) * 0x20 + tx, uy = COS(s19) * 0x20 + ty;
        int32_t ex = tx + SIN(ta & 0x3ff) * -2, ey = ty + COS(ta & 0x3ff) * -2;
        int m = p->mode;
        if (m != 0x37 && m != 0x38) {
            dir = (uint16_t)((math_atan2(ty - p->spr.y, tx - p->spr.x) - ta) & 0x3ff);
            int u = (ta - 0x100) & 0x3ff, s = (dir == 0 || dir > 0x1ff) ? -0x38 : 0x38;
            y = ey + COS(u) * s;
            x = ex + SIN(u) * s;
            p->state = 0x11;
        }
        if (m == 0x37) {
            dir = (uint16_t)((math_atan2(ty - p->spr.y, tx - p->spr.x) - ta) & 0x3ff);
            int u = (ta - 0x100) & 0x3ff, s = (dir == 0 || dir > 0x1ff) ? -0x38 : 0x38;
            x = ex + SIN(u) * s;
            y = ey + COS(u) * s;
        }
        if (m == 0x38 || (m == 0x37 && p->state == 0xf)) {
            dir = (uint16_t)((math_atan2(ty - p->spr.y, tx - p->spr.x) - ta) & 0x3ff);
            int u = (ta - 0x100) & 0x3ff, s = (dir == 0 || dir > 0x1ff) ? -0x10 : 0x10;
            x = SIN(u) * s + ex;
            y = COS(u) * s + ey;
        }
        int train = h->id / 4;
        if (train_command(5, train) == 0) {
            m = p->mode;
            if (m == 0x37) {
                if (p->state == 0xf) {
                    p->speed = 2;
                    ped_set_destination(p, x, y, dir, 0x38);
                    return;
                }
            } else if (m != 0x38) {
                ped_set_destination(p, x, y, dir, 0x37);
                p->speed = 4;
                p->accel = 1;
                return;
            }
            if (m == 0x38 && p->state == 0x10) {
                uint16_t ua = (uint16_t)t->angle;
                uint16_t dd = (uint16_t)((math_atan2(ty - p->spr.y, tx - p->spr.x) - ua) & 0x3ff);
                int16_t turn;
                if (dd < 0x200 || dd > 0x3ff) {
                    turn = 0x100;
                } else {
                    int u = (ua - 0x100) & 0x3ff;
                    uy = ty + COS(u) * -0x20;
                    ux = tx + SIN(u) * -0x20;
                    turn = -0x100;
                }
                p->spr.angle = (int16_t)((ua + turn) & 0x3ff);
                move_to(p, ux, uy);
                p->mode = 0;
                g_ped_74f0f8 = (int16_t)ped;
                train_command(9, train);
                p->car = -1;
                police_update_criminal_target(1, ped, 2, train);
                p->anim = 0x3b;
                p->state = 0x12;
                p->train = (uint8_t)train;
                player_board_train(ped);
            }
            return;
        }
        if (train_command(5, train) != 0) {   /* (asked again) */
            p->state = 2;
            p->speed = 0;
            p->mode = -1;
            p->walk_x = 0;
            p->target_x = 0;
            return;
        }
        h = h->next;
        if (!h) return;
    }
}

/* Ped_TryEnterCar 0x45def0: the enter key on foot (also AI peds through 0x74f0e0), only walking or
   standing, without a walk target, not in state 9. Probes spiral out ahead (7 rings of 12 pixels,
   17 angles of 0x20 either side): a train (kind 10) first sends the ped to its door; a car, or the
   car the ped already walks to (u78 0xb), is then looked up with a wider spiral (24 pixels) and its
   driver's door point checked: no fire / burning / held / scripted / wrecked car, no carried
   object, a door side clear of cars, walls and buildings, speed within -4..8, no cop at the wheel.
   Within 8 pixels (32 for speed 2) of the door point the ped gets in at once (state 7, action 0x11;
   a player's car is handed over, Ped_FinishEnterCar), else it walks there (mode 0xb); the driver of
   a taken car is noted in +0x40. */
void ped_try_enter_car(int32_t ref[2])
{
    int ped = (int16_t)ref[1];
    Ped *p = &g_peds[ped];
    bool search_cars = true;
    p->uee = 0;
    if ((p->state == 9 || p->walk_x != 0 || p->anim < 1 || p->anim > 0x10) && p->anim != 0x88) return;
    CollHit *h = NULL;
    int32_t px = 0, py = 0;
    for (int r = 1; r < 8; r++) {
        for (int k = 0; k < 0x11; k++) {
            int off = k * 0x20;
            for (int side = 0; side < 2; side++) {
                int u = ((uint16_t)p->spr.angle + (side ? -off : off)) & 0x3ff;
                px = p->spr.x + SIN(u) * r * 0xc;
                py = p->spr.y + COS(u) * r * 0xc;
                CollBox *b = coll_build_box(px, py, p->spr.z, 2, 2, 0, 10, &g_ped_box);
                h = coll_query_box(b, COLL_KIND10, 1, ped);
                if (h) {
                    if (p->u78 != 0xb) {
                        search_cars = false;
                        try_board_train(p, h, px, py);
                    }
                    goto searched;
                }
                coll_unlock();
                h = coll_query_box(b, COLL_CAR, 1, ped);
                if (h) goto searched;
                coll_unlock();
            }
        }
    }
searched:
    coll_unlock();
    if (!search_cars) return;
    h = NULL;
    if (p->u78 != 0xb) {
        for (int r = 1; r < 8; r++)
            for (int k = 0; k < 0x11; k++)
                for (int side = 0; side < 2; side++) {
                    int u = ((uint16_t)p->spr.angle + (side ? k * -0x20 : k * 0x20)) & 0x3ff;
                    CollBox *b = coll_build_box(p->spr.x + SIN(u) * r * 0x18, p->spr.y + COS(u) * r * 0x18, p->spr.z, 2, 2, 0,
                                                10, &g_ped_box);
                    h = coll_query_box_first(b, COLL_CAR, 1, ped);
                    if (h) goto found;
                    coll_unlock();
                }
    }
    if (!h && p->u78 != 0xb) goto done;
found:
    coll_unlock();
    Car *c;
    bool have = true;
    if (p->u78 == 0xb) {
        c = car_get(p->car);
        if (crane_is_car_held(p->car)) return;
        if (c->speed > 4) {
            p->u78 = 8;
            p->car = -1;
            p->walk_x = 0;
        }
    } else {
        c = h->owner;
    }
    {
        int a = c->spr.angle & 0x3ff, b = (c->spr.angle + 0x100) & 0x3ff;
        int32_t fx = SIN(a) * c->door_dx + c->spr.x, fy = COS(a) * c->door_dx + c->spr.y;
        int32_t dx = SIN(b) * (c->door_dy - 4) + fx, dy = COS(b) * (c->door_dy - 4) + fy;
        int l8 = c->door_dy + 2;
        if ((c->model == 0x2a && fire_has_objects(c->id) == 1) || c->burning > 0 || c->u244 == 1 ||
            c->script_held == 1 || p->carried > -1 || c->damage > 99)
            have = false;
        CollBox *bx = coll_build_box(l8 * SIN(b) + fx, l8 * COS(b) + fy, c->spr.z, 4, 4, 0, 10, &g_ped_box);
        CollHit *lc = coll_query_box_first(bx, COLL_CAR, COLL_CAR, c->id);
        coll_unlock();
        uint8_t t = 0;
        for (int k = 0; k < 3; k++) t = ped_type_cache(bk(bx->x[k]), bk(bx->y[k]), bk(c->spr.z));
        CollBox *b2 = coll_build_box(SIN(b) * c->door_dy + fx, COS(b) * c->door_dy + fy, c->spr.z, 8, 8, 0, 10, &g_ped_box);
        coll_compute_bounds(b2);
        bool blocked = !(coll_map_walls(b2, 0) == -1 && (t >> 4 & 7) != 5) || lc != NULL;
        if (c->speed < -4 || c->speed > 8 || c->keep_active != 0 || blocked) have = false;
        int d = c->driver;
        /* a cop at the wheel (not a player): no (the original tests ids above 0) */
        if (d > 0 && g_peds[d].player_ctl != 1 && g_peds[d].graphic == 1 && g_peds[d].remap == 0) goto done;
        if (!have) goto done;
        if (dx - 0x100000 < p->spr.x && p->spr.x < dx + 0x100000 && dy - 0x100000 < p->spr.y && p->spr.y < dy + 0x100000)
            p->speed = 2;
        if (dx - 0x80000 < p->spr.x && p->spr.x < dx + 0x80000 && dy - 0x80000 < p->spr.y && p->spr.y < dy + 0x80000 &&
            p->anim < 0x11) {
            p->spr.angle = c->spr.angle;
            c->udc = 0;
            c->brake = 0;
            move_to(p, dx, dy);
            p->spr.z = c->spr.z;
            p->speed = c->speed;
            d = c->driver;
            if (d > 0 && (g_peds[d].graphic != 1 || g_peds[d].remap != 0)) p->u40 = (int16_t)d;
            p->car = c->id;
            if (p->player_ctl == 1) ped_finish_enter_car(p, c->id);
            p->u78 = 8;
            p->u7c = 0x11;
            c->u244 = 1;
            p->state = 7;
            coll_unlock();
            return;
        }
        d = c->driver;
        if (d < 1) {
            p->car = c->id;
            int an = math_atan2(c->spr.y - p->spr.y, c->spr.x - p->spr.x);
            ped_set_destination(p, dx, dy, an & 0x3ff, 0xb);
        } else if (g_peds[d].graphic != 1 || g_peds[d].remap != 0) {
            int an = math_atan2(c->spr.y - p->spr.y, c->spr.x - p->spr.x);
            ped_set_destination(p, dx, dy, an & 0x3ff, 0xb);
            p->u40 = c->driver;
            p->car = c->id;
        }
        p->speed = 4;
        p->accel = 1;
    }
done:
    coll_unlock();
}

/* Ped_DriverLeaveCar 0x45ed10: an AI driver gets out (car speed at most 8, not the hunter car). With
   the driver's door side clear (no car, wall, building or air under the box) the door opens and the
   driver steps out 8 pixels in from the door (anim 0x11, action 0x13; a bike: anim 0x56 beside it,
   half the speed) keeping the car's speed; else the driver jumps out over the bonnet (anim 0x73).
   1 if the driver got out the door. Quirks kept: with the door side clear and no driver the checks
   below run on ped 0; the bonnet branch writes the ped of id -1 when there is no driver (the port
   skips it). */
int ped_driver_leave_car(int car)
{
    int8_t spd = 0;
    Car *c = car_get(car);
    if (c->speed > 8) return 0;
    if (c->model == 0x2f) return 0;
    int16_t ddx = c->door_dx;
    int a = c->spr.angle & 0x3ff, b = (c->spr.angle + 0x100) & 0x3ff;
    CollBox *bx = coll_build_box(SIN(a) * ddx + SIN(b) * (c->door_dy + 2) + c->spr.x,
                                 COS(a) * ddx + COS(b) * (c->door_dy + 2) + c->spr.y, c->spr.z, 4, 4, 0, 10, &g_ped_box);
    coll_compute_bounds(bx);
    bool blocked = coll_query_box_first(bx, COLL_CAR, COLL_CAR, c->id) != NULL;
    int16_t hi = (int16_t)(ddx >> 15);   /* the high half of the angle the sprite gets (unused) */
    int wall_t;
    for (int k = 0; k < 4 && !blocked; k++) {
        int t = ped_ground_type(bk(bx->x[k]), bk(bx->y[k]), bk(c->spr.z));
        wall_t = t;
        if (coll_map_walls(bx, 0) != -1) {
            wall_t = 5;
            blocked = true;
        } else if (t == 5 || t == 0) {
            blocked = true;
        }
    }
    (void)hi;
    int who;
    int carid = c->id;
    if (!blocked) {
        spd = (int8_t)c->speed;
        int d = c->driver;
        who = 0;
        if (d > -1) {
            g_peds[d].u0e = 0;
            police_update_criminal_target(0, c->id, 1, c->driver);
            who = d;
        }
    } else {
        who = c->driver;
        if (who >= 0) {
            Ped *dp = &g_peds[who];
            dp->u0e = 0;
            int32_t py = COS(a) * (c->half_l >> 1) + c->spr.y, px = SIN(a) * (c->half_l >> 1) + c->spr.x;
            if (car_info_is_convertible(dp->car) || dp->anim == 0x56 || c->vtype == 3) coll_remove(dp, dp->spr.unk20);
            int32_t fy = COS(a) * (c->half_l + 8) + c->spr.y, fx = SIN(a) * (c->half_l + 8) + c->spr.x;
            dp->spr.x = px;
            dp->spr.y = py;
            dp->spr.z = c->spr.z;
            CollBox *b3 = coll_build_box(fx, fy, c->spr.z, 4, 4, 0, 10, &g_ped_box);
            int dummy;
            if (ped_check_wall_hit(b3, dp, &dummy, &wall_t, fx, fy)) dp->spr.angle = 0x100;
            else dp->spr.angle = c->spr.angle;
            dp->player_ctl = 0;
            dp->firing = 0;
            dp->id = (int16_t)who;
            dp->spr.zkey = c->spr.zkey - 0x10000;   /* (Sprite_Init right after sets it to z again) */
            ped_sprite_reinit(dp, dp->spr.angle);
            bool open = dp->anim == 0x56 || dp->anim == 0x7f || car_info_is_convertible(carid);
            coll_insert(COLL_PED, who, dp, dp->spr.unk20, dp->spr.x, dp->spr.y);
            if (open) dp->attach_kind = 0, dp->attach_id = 0;
            dp->anim = 0x73;
            ped_update_sprite(who);
            dp->speed = 4;
        }
        c->input = 0;
        c->brake = 1;
        c->ubc = 0;
        c->u1e = 0;
        c->turn_delta = 0;
        c->driver = -1;
        c->u244 = 0;
        if (c->sentinel >= 0 && c->sentinel == g_path_owner) g_path_owner = -1;
    }
    coll_unlock();
    if (who < 0) return 0;
    Ped *d = &g_peds[who];
    if (!((d->anim == 0 || d->attach_kind != 0) && d->health > 0)) return 0;
    car_release_controls(c);
    if (c->sentinel >= 0 && c->sentinel == g_path_owner) g_path_owner = -1;
    if (c->speed == 0) c->unk88 = 0;
    int32_t ly = c->spr.y, lz = c->spr.z, lx = c->spr.x;
    int16_t ldx = c->door_dx, ldy = c->door_dy;
    uint16_t ang = (uint16_t)c->spr.angle;
    d->anim = 0x11;
    d->car = c->id;
    if (c->vtype == 3) {
        d->anim = 0x56;
        c->status = 8;
        if (spd > 6) spd >>= 1;
    } else {
        car_reset_siren99(c);
        car_open_door1_step(c->id);
    }
    int ua = ang & 0x3ff;
    int32_t sx = SIN(ua);
    ly += COS(ua) * ldx;
    if (car_info_is_convertible(d->car) || d->anim == 0x56) coll_remove(d, d->spr.unk20);
    if (d->anim != 0x41) {
        int u = (ang + 0x100) & 0x3ff;
        int32_t ox, oy;
        if (d->anim == 0x56) {
            int k = ldy + 2;
            oy = COS(u) * k;
            ox = SIN(u) * k;
            ang = (uint16_t)((ang + 0x100) & 0x3ff);
        } else {
            int k = ldy - 8;
            ox = SIN(u) * k;
            oy = COS(u) * k;
        }
        d->spr.x = ox + lx + sx * ldx;
        d->spr.y = oy + ly;
        d->spr.z = lz;
    }
    if (d->control == -1) d->control = 0;
    d->id = (int16_t)who;
    d->firing = 0;
    c->u244 = 1;
    d->u7c = 0x13;
    d->player_ctl = 0;
    ped_sprite_reinit(d, ang);
    bool open = d->anim == 0x56 || car_info_is_convertible(carid);
    coll_insert(COLL_PED, who, d, d->spr.unk20, d->spr.x, d->spr.y);
    if (open) d->attach_kind = 0, d->attach_id = 0;
    ped_update_sprite(who);
    d->speed = spd;
    if (spd == 0) d->accel = 0;
    if (d->anim == 0x41 && d->player_ctl == 1) d->anim = 1;
    return 1;
}

/* Ped_SendToCarDoor1 0x45f4a0 / Ped_SendToCarDoor2 0x45f850: walk target at door 1 (car info
   +0xae / +0xb0) or door 2 (+0xb6 / +0xb8) of the car, the ped facing the car; objective 0x35 (door
   1, unless 0x27 / 0x28) or 0x34, state 4, u78 0xe / 0xf. A car without that door is fatal (-0xe0). */
static void send_to_door(Ped *p, int car, int door)
{
    Car *c = car_get((int16_t)car);
    const uint8_t *info = car_info_of_model(c->model);
    if (!info || carinfo_s16(info, 0xac) < door) {
        game_fatal(-0xe0, 0xfe, 0);
        return;
    }
    int16_t ox = carinfo_s16(info, door == 1 ? 0xae : 0xb6), oy = carinfo_s16(info, door == 1 ? 0xb0 : 0xb8);
    int a = c->spr.angle & 0x3ff, b = (c->spr.angle + 0x100) & 0x3ff;
    int32_t x = SIN(b) * oy + c->spr.x + SIN(a) * ox;
    int32_t y = COS(b) * oy + c->spr.y + COS(a) * ox;
    p->spr.angle = (int16_t)math_atan2(c->spr.y - p->spr.y, c->spr.x - p->spr.x);
    p->target_x = x;
    p->walk_x = x;
    p->target_y = y;
    p->walk_y = y;
    if (door == 1) {
        if (p->objective != 0x27 && p->objective != 0x28) p->objective = 0x35;
    } else {
        p->objective = 0x34;
    }
    p->state = 4;
    p->u78 = door == 1 ? 0xe : 0xf;
    p->u7c = 2;
    p->car = (int16_t)car;
}
void ped_send_to_car_door1(Ped *p, int car) { send_to_door(p, car, 1); }
void ped_send_to_car_door2(Ped *p, int car) { send_to_door(p, car, 2); }

/* Ped_RespawnBesideCar 0x45f980: the accelerate key of a player whose ped is gone (anim 0) for more
   than 100 frames, and not dead: the ped is made again beside the car it left (+0x5c) once that car
   has stopped, at its rear wheel point facing across it, and the cameras on the car follow the ped.
   A standing player ped (anims 0x63, 0x88) just starts walking. `fast` is not used. */
void ped_respawn_beside_car(int fast, int ped)
{
    (void)fast;
    Ped *p = &g_peds[ped];
    if (p->player_ctl != 1) return;
    if (p->anim == 99 || p->anim == 0x88) p->anim = 1;
    if (p->anim != 0 || p->idle_count <= 100 || p->state == 0x17 || p->state == 0xc) return;
    Car *c = car_get(p->u5c);
    if (c->speed != 0) return;
    ped_create_in_slot(c->rear_x, c->rear_y, c->spr.z, (c->spr.angle + 0x100) & 0x3ff, ped);
    player_retarget_camera(0, c->id, 2, ped);
    p->u5a = -1;
    p->health = 100;
}

/* Ped_ExitCarAtParkPoint 0x45fc60: the player gets out at the mission's park exit point (the garage
   of PARK, Mission_GetParkExitPos) instead of beside the car: from a car (not the hunter car, not
   wrecked; a stopped car becomes mission-free, owner 0) or a train it is on (state 0x12). The ped
   stands there (anim 0x88; a bike: 0x56) with the car's speed and becomes what the player controls
   (the reference turns into {2, ped}). From a train the original reads the car record at address 0
   (it would crash); the port keeps the ped's z there. */
void ped_exit_car_at_park_point(int32_t ref[2])
{
    Car *c = NULL;
    int16_t ang = 0;
    int8_t spd;
    int who;
    if (ref[0] == 0) {
        c = car_get((int16_t)ref[1]);
        if (c->model == 0x2f) return;
        if (c->damage > 99) return;
        who = c->driver;
        spd = (int8_t)c->speed;
        police_update_criminal_target(0, c->id, 1, who);
    } else {
        Ped *q = &g_peds[(int16_t)ref[1]];
        int st = q->state;
        spd = (int8_t)train_command(5, q->train);
        who = (uint8_t)train_command(8, q->train);
        police_update_criminal_target(2, (int16_t)ref[1], 1, who);
        if (st != 0x12) return;
    }
    who = (int16_t)who;
    if (who < 0) return;
    Ped *d = &g_peds[who];
    if (!((d->anim == 0 || d->attach_kind != 0) && d->health > 0)) return;
    if (ref[0] == 0) {
        if (spd == 0) car_set_owner_status(c->id, 0);
        car_siren_off(c);
        car_release_controls(c);
        carphys_end(c->id);
        if (c->speed == 0) c->unk88 = 0;
        ang = c->spr.angle;
        c->control = 0;
        d->anim = 0x88;
        d->car = c->id;
        if (c->vtype == 3) {
            d->anim = 0x56;
            c->status = 8;
            if (spd > 6) spd >>= 1;
        } else {
            car_reset_siren99(c);
            car_open_door1_step(c->id);
        }
    }
    if (car_info_is_convertible(d->car) || d->anim == 0x56) coll_remove(d, d->spr.unk20);
    int32_t px, py;
    mission_get_park_exit_pos(&px, &py, &ang);
    d->spr.x = px << 16;
    d->spr.y = py << 16;
    if (c) d->spr.z = c->spr.z;
    d->anim = 0x88;
    player_exit_car(d->id, d->car);
    if (c) {
        c->u244 = 0;
        c->owner_status = 0;
    }
    d->spr.angle = ang;
    d->id = (int16_t)who;
    d->player_ctl = 1;
    d->firing = 0;
    ped_sprite_reinit(d, ang);
    coll_insert(COLL_PED, who, d, d->spr.unk20, d->spr.x, d->spr.y);
    d->attach_kind = 0;
    d->attach_id = 0;
    ped_update_sprite(who);
    d->speed = spd;
    if (spd == 0) d->accel = 0;
    ref[0] = 2;
    ref[1] = who;
    d->state = 2;
    d->u7c = 2;
    d->u78 = 8;
    d->mode = -1;
}

/* Map_FindPavementAlongRoad 0x45ffb0: from a service location (block x, y, z) leave along the road
   direction of the block under it (one direction bit; another is fatal -0xad) and walk up to 15
   blocks for a pavement block without a car on it; the heading goes to *angle. The row is cleared
   first (Mission_ClearRow with n). 1 found (the block in *bx, *by), 0 not. */
int map_find_pavement_along_road(int *bx, int *by, int *bz, int16_t *angle, int n)
{
    int dx = 0, dy = 0;
    int dirs = ped_type_cache(*bx, *by, *bz - 1) & 0xf;
    switch (dirs) {
    case 1: dx = 0, dy = -1, *angle = 0x200; break;
    case 2: dx = 0, dy = 1, *angle = 0; break;
    case 4: dx = -1, dy = 0, *angle = 0x300; break;
    case 8: dx = 1, dy = 0, *angle = 0x100; break;
    default: game_fatal(-0xad, 0xc1, dirs);
    }
    *bx += dx;
    *by += dy;
    mission_clear_row(*bx, *by, *bz, -1, n, dx, dy);
    bool found = false;
    for (int i = 0;;) {
        if (*bx < 0 || *bx > 0xff || *by < 0 || *by > 0xff) return 0;
        if ((ped_type_cache(*bx, *by, *bz) & 0x70) == 0x30) {
            coll_unlock();
            found = true;
            for (CollHit *h = coll_query_cars(*bx << 22, *by << 22, -1); h; h = h->next) {
                const Car *c = h->owner;
                if (c->spr.x >> 22 == *bx && c->spr.y >> 22 == *by) found = false;
            }
            coll_unlock();
        }
        if (found) return 1;
        *bx += dx;
        *by += dy;
        if (++i > 0xe) return 0;
    }
}

/* the ped placed at its respawn point (+0x20, +0x24, +0x28) on foot under the player's control */
static void respawn_place(Ped *p, int16_t ang)
{
    p->attach_kind = 0;
    p->spr.x = p->u20;
    p->spr.y = p->u24;
    int32_t z = (int32_t)(((uint32_t)p->u28 & 0xffc00000u) + 0x3f0000);
    p->spr.z = z;
    p->spr.zkey = z;
    int16_t carried = p->carried;
    p->spr.angle = ang;
    p->attach_id = 0;
    p->walk_x = 0;
    p->anim = 3;
    p->speed = 0;
    p->player_ctl = 1;
    p->turn = 0;
    p->accel = 0;
    p->firing = 0;
    p->anim_tick = 0;
    p->idle_count = 0;
    p->u48 = 1;
    if (carried > -1) obj_delete_wrapper(carried);
    if (p->car != -1) {
        Car *c = car_get(p->car);
        c->u244 = 0;
        c->script_held = 0;
        if (c->driver == p->id) c->driver = -1;
    }
    p->u7c = 2;
    p->state = 2;
    p->mode = 0;
    p->car = -1;
    p->u48 = 0;
    p->u78 = 8;
    p->objective = 0x25;
}

/* Player_RespawnAtStation 0x4601a0: after a bust (kind 0, police stations) or death (kind 1,
   hospitals) the ped is put on the pavement found along the road from a location sorted by distance
   (the larger of the x / y block distances). Quirks: the count of locations read is the hospitals'
   for both kinds; the nearest location (index 0 after sorting) is never tried; no pavement found is
   fatal (-0xae). */
void player_respawn_at_station(Ped *p, int kind)
{
    int16_t order[6], dist[6];
    for (int i = 0; i < 6; i++) order[i] = dist[i] = -1;
    int bx = (p->spr.x >> 22) & 0xff, by = (p->spr.y >> 22) & 0xff;
    const uint8_t *base = (const uint8_t *)g_locations + (kind == 1 ? 0x12 : 0);
    const uint8_t *l = base;
    int n = 0;
    for (int i = 0; i < g_hospitals && i < 6; i++, l += 3) {
        int d = abs(by - l[1]), d2 = abs(bx - l[0]);
        if (d2 <= d) d2 = d;
        order[i] = (int16_t)i;
        dist[i] = (int16_t)d2;
        n = i + 1;
    }
    int last = n - 1;
    for (;;) {
        bool sorted = true;
        if (last < 1) break;
        for (int j = 0; j < last; j++) {
            int16_t o = order[j];
            if (dist[order[j + 1]] < dist[o]) {
                order[j] = order[j + 1];
                order[j + 1] = o;
                sorted = false;
            }
        }
        if (sorted) break;
    }
    int found = 0;
    int16_t ang = 0;
    int x = 0, y = 0, z = 0;
    for (int s = 1; s < n && found == 0; s++) {
        l = base;
        if (order[s] > 0) l += order[s] * 3;
        p->u20 = l[0];
        p->u24 = l[1];
        z = l[2];
        x = p->u20;
        y = p->u24;
        p->u28 = z;
        found = map_find_pavement_along_road(&x, &y, &z, &ang, 0xf);
    }
    if (found != 1) game_fatal(-0xae, 0xdb, found);
    p->u20 = x * 0x400000 + 0x200000;
    p->u24 = y * 0x400000 + 0x200000;
    p->u28 = z * 0x400000 + 0x3f0000;
    if (p->anim != 0 && p->state != 0x18) coll_remove(p, p->spr.unk20);
    respawn_place(p, ang);
    coll_insert(COLL_PED, p->id, p, p->spr.unk20, p->spr.x, p->spr.y);
}

/* Player_RespawnMulti 0x460500: a network game's respawn: from the next hospital on, the first whose
   pavement point could be cleared of cars (Player_ChooseRespawnPoint picks the point,
   Car_RemoveInSquare clears it), the players a quarter block apart in x. */
void player_respawn_multi(Ped *p)
{
    const uint8_t *hosp = (const uint8_t *)g_locations + 0x12;
    const uint8_t *l = hosp;
    int16_t ang = 0;
    int i = -1, x = 0, y = 0, z;
    if (g_hospitals < 1) return;   /* (the original reads address 0 here) */
    for (;;) {
        if (++i == g_hospitals) i = 0;
        for (int k = 0; k < g_hospitals; k++) {
            int r = (int8_t)player_choose_respawn_point(p, i);
            l = hosp;
            for (int s = 0; s < r; s++) l += 3;
            int lx = l[0], ly = l[1], lz = l[2];
            map_find_pavement_along_road(&lx, &ly, &lz, &ang, 0xf);
            if (car_remove_in_square(lx << 6, ly << 6, l[2] << 6, 3, -1, 0)) break;
        }
        p->u20 = l[0];
        p->u24 = l[1];
        p->u28 = l[2];
        x = l[0], y = l[1], z = l[2];
        int r = map_find_pavement_along_road(&x, &y, &z, &ang, 0xf);
        if (r == 1) break;
        if (r != 0) return;
    }
    int n = player_find_by_ped(p->id);
    p->u20 = (n + x * 4) * 0x100000 + 0x80000;
    p->u24 = y * 0x400000 + 0x200000;
    p->u28 = l[2] * 0x400000 + 0x3f0000;
    coll_remove(p, p->spr.unk20);
    p->spr.x = p->u20;
    p->spr.z = p->spr.zkey = p->u28;
    int16_t car = p->car;
    p->spr.y = p->u24;
    p->spr.angle = ang;
    p->attach_kind = 0;
    p->attach_id = 0;
    p->walk_x = 0;
    p->anim = 3;
    p->speed = 0;
    p->player_ctl = 1;
    p->turn = 0;
    p->accel = 0;
    p->anim_tick = 0;
    p->idle_count = 0;
    p->u48 = 1;
    if (car != -1) {
        Car *c = car_get(car);
        c->u244 = 0;
        c->script_held = 0;
        if (c->driver == p->id) c->driver = -1;
    }
    p->car = -1;
    p->u48 = 0;
    p->mode = 0;
    p->u7c = 2;
    p->u78 = 8;
    p->state = 2;
    p->objective = 0x25;
    p->u5a = -1;
    /* Player_ClearKillFlag 0x4643d0: the frag of the player whose ped this is processed again */
    if (n != -1) g_players[n].frag_done = 0;
    coll_insert(COLL_PED, p->id, p, p->spr.unk20, p->spr.x, p->spr.y);
}
