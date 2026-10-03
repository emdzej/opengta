/* The Hells Angels gang and the hunting cars 0x431500-0x4325ff (gang.h). The gang lists are
   mission_obj.c's: g_gang_lists_b (0x513248) the gang's cars, g_gang_count_b (0x513298) their number,
   g_gang_lists_a (0x513270) a timer per player, g_gang_count_a (0x51329c) 1 once the gang hunts.
   docs/police.md. */
#include "gang.h"
#include "carinfo.h"
#include "coll.h"
#include "game.h"
#include "gmath.h"
#include "mission_obj.h"
#include "path.h"
#include "ped.h"
#include "player.h"
#include "sentinel.h"
#include "traffic.h"
#include <stdlib.h>

Hunt g_hunts[HUNT_MAX];

static int16_t info_s16(const Car *c, int off)
{
    const uint8_t *in = car_info_of_model(c->model);
    return in ? carinfo_s16(in, off) : 0;
}

/* the cached block type byte of block (x, y) at layer z, 0 outside the cache (the original reads
   whatever lies there) */
static uint8_t cache_at(int x, int y, int z)
{
    if (!g_game.map || z < 0 || z >= MAP_Z) return 0;
    return g_game.map->type_cache[z][y & 0xff][x & 0xff];
}

/* ---------------------------------------------------------------- the gang */

/* Gang_Init 0x431500 (also reached through Mission_ResetLists_thunk 0x478f90) */
void gang_init(void)
{
    for (int i = 0; i < 10; i++) g_gang_lists_a[i] = -1;
    for (int i = 0; i < 10; i++) g_gang_lists_b[i] = -1;
    g_gang_count_a = 0;
    g_gang_count_b = 0;
}

static void gang_check(int i)
{
    if (i < 0 || i > 9) game_fatal(-0x9a, 0xbc, i);
}

/* Gang_AddCar 0x431530: into the first free slot of ten (all taken: ignored) */
void gang_add_car(int car)
{
    for (int i = 0; i < 10; i++) {
        gang_check(i);
        if (g_gang_lists_b[i] != -1) continue;
        g_gang_count_b++;
        g_gang_lists_b[i] = car;
        return;
    }
}

/* Gang_Update 0x4315b0 (from Mission_UpdateTriggers), per player in a car of model 3 (a bike):
   - before the hunt: on a gang bike his timer (30 frames) runs; at 0 the gang hunts: every other gang
     car hunts his ped (Hunt_AddCarTarget mode 6, through 0x4763e0);
   - during the hunt every gang car is given one more Hunt_UpdateCar step (on top of Cars_UpdateAll's:
     the gang drives twice per frame while he is on a bike); a wrecked one (damage 0x65) leaves the
     hunt and the list.
   Not on a gang bike, or no gang at all, ends the whole update (the original returns, the other
   players aren't looked at). */
void gang_update(void)
{
    for (int n = (int8_t)player_first(); n >= 0; n = (int8_t)player_next(n)) {
        if (player_get_controlled_kind(n) != PLAYER_IN_CAR) continue;
        Car *c = car_get((int16_t)player_get_controlled_id(n));
        if (c->model != 3) continue;
        if (g_gang_count_a == 1) {
            for (int k = 0; k < (int8_t)g_gang_count_b; k++) {
                if (g_gang_lists_b[k] == -1) continue;
                Car *g = car_get((int16_t)g_gang_lists_b[k]);
                if (g->damage < 0x65) {
                    hunt_update_car(g);
                    continue;
                }
                for (int j = 0; j < 10; j++) {
                    gang_check(j);
                    if (g_gang_lists_b[j] != g->id) continue;
                    if (g_gang_count_a == 1) {
                        gang_check(j);
                        hunt_remove((int16_t)g_gang_lists_b[j]);
                        g_gang_lists_b[j] = -1;
                    }
                    break;
                }
            }
            continue;
        }
        bool other = true;
        if ((int16_t)g_gang_count_b < 1) return;
        for (int k = 0; k < (int16_t)g_gang_count_b; k++)
            if (c->id == g_gang_lists_b[k]) other = false;
        if (other) return;
        if (g_gang_lists_a[n] == -1) {
            g_gang_lists_a[n] = 0x1e;
        } else if (--g_gang_lists_a[n] == 0) {
            g_gang_lists_a[n] = -1;
            g_gang_count_a = 1;
            for (int k = 0; k < (int16_t)g_gang_count_b; k++) {
                int g = g_gang_lists_b[k];
                if (g <= -1 || g == c->id) continue;
                int target = c->driver > -1 ? c->driver : player_get_ped(n);
                car_unk_004318e0_wrap((int16_t)g, (int16_t)target, 6);
            }
        }
    }
}

/* ---------------------------------------------------------------- the hunt table */

static void hunt_check(int i)
{
    if (i < 0 || i > 0x13) game_fatal(-0x92, 0xa7, i);
}
static int hunt_find(int car)
{
    for (int i = 0; i < HUNT_MAX; i++)
        if (g_hunts[i].car == (int16_t)car) return i;
    return -1;
}

/* Hunt_Init 0x4317f0 */
void hunt_init(void)
{
    for (int i = 0; i < HUNT_MAX; i++) {
        Hunt *h = &g_hunts[i];
        h->car = -1, h->target = -1;
        h->mode = 0xff, h->u05 = 0;
        h->heading = -1;
        h->bx = h->by = h->bz = h->u0b = 0;
        h->timer = 0;
        h->turn = 0;
        h->fresh = 0;
        h->last_x = -1, h->last_y = -1;
    }
}

/* Hunt_Remove 0x431840: the car's record cleared (its last position, turn and +0x15 stay) and the
   car back to dummy control */
void hunt_remove(int car)
{
    int i = hunt_find(car);
    if (i == -1) return;
    hunt_check(i);
    Hunt *h = &g_hunts[i];
    Car *c = car_get((int16_t)car);
    h->car = -1, h->target = -1;
    h->mode = 0xff, h->u05 = 0;
    h->heading = -1;
    h->bx = h->by = h->bz = h->u0b = 0;
    h->timer = 0;
    c->control = CAR_CTL_DUMMY;
}

/* Hunt_AddCarTarget 0x4318e0: a car not hunting yet hunts ped `target` (mode); control 0x32. The
   record's index, -1 if it already hunts or the table is full. */
int hunt_add_car_target(int car, int target, int mode)
{
    if (hunt_find(car) != -1) return -1;
    int i = hunt_find(-1);
    if (i == -1) return -1;
    hunt_check(i);
    Hunt *h = &g_hunts[i];
    h->car = (int16_t)car;
    h->target = (int16_t)target;
    h->mode = (uint8_t)mode;
    h->fresh = 1;
    car_get((int16_t)car)->control = CAR_CTL_HUNT;
    return i;
}

/* Hunt_AddBlockTarget 0x4319b0 (mode 4) / Hunt_AddBlockTarget2 0x431ab0 (mode 10): the car drives
   to block (bx, by, bz) */
static int add_block_target(int car, int bx, int by, int bz, int mode)
{
    if (hunt_find(car) != -1) return -1;
    int i = hunt_find(-1);
    if (i == -1) return -1;
    hunt_check(i);
    Hunt *h = &g_hunts[i];
    h->car = (int16_t)car;
    h->target = -1;
    h->mode = (uint8_t)mode;
    h->fresh = 1;
    car_get((int16_t)car)->control = CAR_CTL_HUNT;
    hunt_check(i);
    h->bx = (uint8_t)bx, h->by = (uint8_t)by, h->bz = (uint8_t)bz;
    h->fresh = 1;
    return i;
}
int hunt_add_block_target(int car, int bx, int by, int bz) { return add_block_target(car, bx, by, bz, 4); }
int hunt_add_block_target2(int car, int bx, int by, int bz) { return add_block_target(car, bx, by, bz, 10); }

/* Hunt_SteerTowards 0x431bb0: turn the body toward `angle`. Near the top speed (within the car
   info's +0x14) the turn is a fixed ±0x20 step with the info's +0x0e as input (1 while turning; 0
   once the heading is within the step, which it then takes); slower, only the size of the turn is
   noted (+0x96) and 0 returned. */
int hunt_steer_towards(Car *c, int angle)
{
    int16_t a = c->spr.angle, t = (int16_t)angle;
    uint16_t d = (uint16_t)((t - a) & 0x3ff);
    if (d > 0x200) d = (uint16_t)-d;
    if (c->speed >= c->max_speed - info_s16(c, 0x14)) {
        if (t - c->turn_delta <= a && a <= t + c->turn_delta) {
            c->brake = 0;
            c->turn_delta = 0;
            c->input = 0;
            c->spr.angle = t;
            return 0;
        }
        int16_t td;
        if ((int16_t)d < 1) {
            if ((int16_t)d >= 0) return 1;
            td = -0x20;
        } else {
            td = 0x20;
        }
        c->turn_progress = 0;
        c->turn_delta = td;
        c->u98 = td;
        c->input = info_s16(c, 0xe);
        return 1;
    }
    if (c->turn_delta == 0) {
        if ((int16_t)d < 1) {
            if ((int16_t)d >= 0) return 0;
            d = (uint16_t)-d;
        }
        c->turn_delta = (int16_t)d;
        c->turn_progress = 0;
    }
    return 0;
}

/* Car_SteerTowards 0x40bc70 (car module, not ported elsewhere: the hunters' copy). The front wheels
   (+0x90) turn toward `angle`: above the cruise speed (+0x104) the car slows by 2; below the top
   speed less the info's +0x14 only the turn direction (±0x20) is set; at speed it turns in steps of
   ±0x20 with the brake on (+0xb6) until within a step. Returns 1 while still off the heading. */
static int car_steer_towards(Car *c, int angle)
{
    int16_t a = c->front_heading, t = (int16_t)angle;
    uint16_t d = (uint16_t)((t - a) & 0x3ff);
    if (d > 0x200) d = (uint16_t)-d;
    if (a == t) {
        c->speed = 0;
        c->input = 0;
    }
    if (c->cruise < c->speed) c->speed -= 2;
    if (c->speed < c->max_speed - carinfo_s16(c->info, 0x14)) {
        if (c->turn_delta == 0) {
            if ((int16_t)d > 0) {
                c->turn_progress = 0;
                c->turn_delta = 0x20;
                return 0;
            }
            if ((int16_t)d < 0) {
                c->turn_delta = -0x20;
                c->turn_progress = 0;
            }
        }
        return 0;
    }
    if (t - c->turn_delta <= a && a <= t + c->turn_delta) {
        c->front_heading = t;
        c->brake = 0;
        c->turn_delta = 0;
        c->input = 0;
        return 0;
    }
    if ((int16_t)d < 1) {
        if ((int16_t)d >= 0) return a != t;
        c->turn_delta = -0x20;
    } else {
        c->turn_delta = 0x20;
    }
    c->turn_progress = 0;
    c->brake = 1;
    c->u98 = c->turn_delta;
    c->input = carinfo_s16(c->info, 0xe);
    return a != t;
}

/* Hunt_ProbeAhead 0x431cb0: the road direction bits at the point `speed` pixels ahead along the front
   wheels (written to *x, *y), from the layers a block and 20 / 4 pixels above the car down to its own;
   none there: the bits of the next block along the car's road direction (+0xa2) on its layer, else
   on the ground layer below it (one layer down onto a down ramp, one up from a slope), and the car's
   own bits when that is empty too. On a slope a car faster than 15 slows by 1. */
int hunt_probe_ahead(Car *c, int32_t *x, int32_t *y)
{
    *x = math_sin(c->front_heading) * c->speed + c->spr.x;
    int32_t py = math_cos(c->front_heading) * c->speed + c->spr.y;
    *y = py;
    int32_t z = c->spr.z;
    int bx = *x >> 22, by = py >> 22;
    int v = cache_at(bx, by, (z - 0x140000) >> 22);
    if (v == 0) v = cache_at(bx, by, (z - 0x40000) >> 22);
    if (v == 0) v = cache_at(bx, by, z >> 22);
    if (v != 0) return v & 0xf;
    int dx = 0, dy = 0;
    switch (c->road_dirs) {
    case 1: dy = -1; break;
    case 2: dy = 1; break;
    case 4: dx = -1; break;
    case 8: dx = 1; break;
    }
    int cx = (uint8_t)((c->spr.x >> 22) + dx), cy = (uint8_t)((c->spr.y >> 22) + dy);
    int w = cache_at(cx, cy, z >> 22);
    int r = w & 0xf;
    if (r == 0) {
        if (w != 0) return r;
        int32_t g = map_get_ground_z(g_game.map, c->spr.x, c->spr.y, c->spr.z - 0x100);
        int gl = g >> 22;
        bool slope = false;
        if (cache_at(cx, cy, gl) & 0x80) {
            if (c->speed > 0xf) c->speed--;
            slope = true;
        }
        bool down = gl < 5 && path_is_down_ramp(map_get_type_map(g_game.map, cx, cy, gl + 1), c->road_dirs);
        int dz = gl < 4 && down ? 1 : slope && (cache_at(cx, cy, gl - 1) & 0xf) != 0 ? -1 : 0;
        w = cache_at(cx, cy, gl + dz);
        r = w & 0xf;
    }
    if (w == 0) r = c->road_dirs;
    return r;
}

/* the steering angle difference of the hunter's heading + off to the target, folded to 0..0x200 */
static int16_t fold(int a)
{
    uint16_t d = (uint16_t)(a & 0x3ff);
    if (d > 0x200) d = (uint16_t)(-d & 0x3ff);
    return (int16_t)d;
}

/* Hunt_UpdateCar 0x431f70: one step of a hunting car (control 0x32, a live driver), from its
   record:
   1. the target block: mode 4 / 10 the record's block; else the target ped's (in a car: the car's,
      whose speed it may match); a target in a car is chased at full speed (bikes 3 / 0x29 to the
      top, model 0x2f 3 a frame to 20 below it, others up to 10 above the target car's speed),
      otherwise the hunter slows to 7;
   2. off the road and out of sight it warps onto the nearest road (Sentinel_WarpCar);
   3. a started timer (mode 3 at the target) counts; at 30 the car is destroyed (MisCar_Destroy);
   4. a turn in progress (+0x18) finishes when the heading is within 0x20 of the aim (+6), or the
      car snaps to its road (turns 1 / 2); while the wheels still turn it waits (mode 3 cars that
      haven't moved give up the turn);
   5. within 2 blocks of the target it turns toward it (from 0x80 off: ±0x40, then aiming ±0x60);
   6. otherwise it follows the roads: the road bits ahead give the exits to the left (heading -
      0x100), the right (+ 0x100) and ahead; it takes the exit whose direction is nearest the
      target's (ahead when nothing is nearer), matching the speed of a car in its way (unless that
      car can be removed off screen, Car_RemoveAtBlock; block targets just go at 5);
   7. a car that hasn't moved since the last step stops (speed 0), unless it could push the car in
      its way off screen; the position is kept. */
void hunt_update_car(Car *c)
{
    if (c->control != CAR_CTL_HUNT || c->driver == -1) return;
    if (ped_get(c->driver)->health == 0) return;
    c->owner_status = 1;
    int hi = hunt_find(c->id);
    if (hi == -1) return;
    hunt_check(hi);
    Hunt *h = &g_hunts[hi];
    c->cruise = 5;
    c->physics = 0;
    c->unk88 = 1;

    int tx, ty, tspeed = 0;
    bool chase = false;
    if (h->mode == 4 || h->mode == 10) {
        tx = h->bx, ty = h->by;
    } else {
        const Ped *tp = ped_get(h->target);
        int pn = (int16_t)player_find_by_ped(tp->id);
        int kind = pn >= 0 ? player_get_controlled_kind(pn) : PLAYER_ON_FOOT;   /* port: no player */
        if (kind == PLAYER_IN_CAR) {
            const Car *tc = car_get(tp->car);
            tspeed = tc->speed;
            tx = (uint8_t)(tc->spr.x >> 22), ty = (uint8_t)(tc->spr.y >> 22);
            chase = true;
        } else {
            tx = (uint8_t)(tp->spr.x >> 22), ty = (uint8_t)(tp->spr.y >> 22);
        }
    }
    if (chase) {
        if (c->model == 3 || c->model == 0x29) {
            if (c->speed < c->max_speed) c->speed++;
        } else if (c->model == 0x2f) {
            if (c->speed < c->max_speed - 0x14) c->speed += 3;
        } else if (c->speed < c->max_speed && tspeed != -1 && c->speed <= tspeed + 10) {
            c->speed++;
        }
    } else if (c->speed > 7) {
        c->speed--;
    }

    int bx = (uint8_t)(c->spr.x >> 22), by = (uint8_t)(c->spr.y >> 22), bz = (uint8_t)(c->spr.z >> 22);
    int t = cache_at(bx, by, bz) >> 4 & 7;
    if (t != 2 && t != 6 && t != 7 && !car_is_on_screen(c)) {
        uint8_t q[8] = { 0 };
        q[2] = (uint8_t)bx, q[3] = (uint8_t)by, q[4] = (uint8_t)bz;
        if (map_find_nearest_road(q)) {
            sentinel_warp_car(c, q[2] << 22, q[3] << 22, q[4] << 22);
            return;
        }
    }
    if ((int8_t)h->timer > 0) h->timer++;
    if (h->timer == 0x1e && !car_is_wrecked(c->id)) mis_car_destroy(c->id);
    int ddy = abs(ty - by), ddx = abs(tx - bx);
    int d = ddx > ddy ? ddx : ddy;
    if (h->mode == 3 && d == 0 && h->timer == 0) h->timer = 1;

    if (h->turn != 0) {
        int16_t aim = h->heading;
        if (aim < 0 || c->spr.angle < aim - 0x20 || aim + 0x20 < c->spr.angle) {
            if (h->turn == 1 || h->turn == 2) car_snap_heading(c);
        } else {
            c->spr.angle = aim;
            c->turn_delta = 0;
            h->heading = -1;
        }
        if (c->turn_delta != 0) {
            if (h->last_x != c->spr.x || h->last_y != c->spr.y || h->mode != 3) return;
            h->turn = 0;
        }
        h->turn = 0;
    }

    int32_t gx = tx * 0x400000, gy = ty * 0x400000;
    if (d < 3) {
        int a = math_atan2(gy - c->spr.y + 0x200000, gx - c->spr.x + 0x200000);
        uint16_t raw = (uint16_t)(a - c->spr.angle);
        int16_t diff = fold(a - c->spr.angle);
        c->turn_dirs = (int16_t)traffic_angle_to_dir(c->spr.angle);
        if (diff < 0x81) return;
        if ((raw & 0x3ff) > 0x1ff) {
            if (hunt_steer_towards(c, (c->spr.angle - 0x40) & 0x3ff) != 0) return;
            h->turn = 4;
            h->heading = (int16_t)((c->spr.angle - 0x60) & 0x3ff);
        } else {
            if (hunt_steer_towards(c, (c->spr.angle + 0x40) & 0x3ff) != 0) return;
            h->turn = 3;
            h->heading = (int16_t)((c->spr.angle + 0x60) & 0x3ff);
        }
        return;
    }

    int32_t px, py;
    int dirs = hunt_probe_ahead(c, &px, &py);
    if (h->mode == 4 || h->mode == 10) {
        c->speed = 5;
    } else if (h->mode != 3) {
        int32_t qx, qy;
        if (hunt_probe_ahead(c, &qx, &qy) == 0 || !car_remove_at_block(qx >> 16, qy >> 16, c->id, 1, 1)) {
            int pbx = (uint8_t)(px >> 22), pby = (uint8_t)(py >> 22);
            bool found = false;
            for (CollHit *e = coll_query_cars(pbx << 22, pby << 22, c->id); e; e = e->next) {
                const Car *o = e->owner;
                if (o->id == c->id || o->spr.x >> 22 != pbx || o->spr.y >> 22 != pby) continue;
                coll_unlock();
                if (o->speed >= 0) c->speed = o->speed;
                found = true;
                break;
            }
            if (!found) coll_unlock();
        }
    }

    /* the exits: p = the heading + 0x100 side, m = - 0x100, a = ahead */
    bool exit_p = false, exit_m = false, ahead = false;
    int16_t dp = -1, dm = -1, da = -1;
    int target_a = math_atan2(gy - py, gx - px);
    switch (traffic_angle_to_dir(c->spr.angle)) {
    case 1: exit_m = dirs & 8, exit_p = dirs & 4, ahead = dirs & 1; break;
    case 2: exit_m = dirs & 4, exit_p = dirs & 8, ahead = dirs & 2; break;
    case 4: ahead = dirs & 4, exit_m = dirs & 1, exit_p = dirs & 2; break;
    case 8: ahead = dirs & 8, exit_p = dirs & 1, exit_m = dirs & 2; break;
    case 3: case 5: case 6: case 7: break;
    default: goto done;
    }
    /* the angle differences (all three exits open: none computed) */
    if (!(exit_p && exit_m && ahead)) {
        if (exit_p) dp = fold(target_a - (uint16_t)(c->spr.angle + 0x100));
        if (exit_m) dm = fold(target_a - (c->spr.angle - 0x100));
        if (ahead) da = fold(target_a - (uint16_t)c->spr.angle);
        /* a single exit to one side: take it */
        if (!ahead || dp != -1) {
            if (dp >= 0 && dm == -1) {
                if (da == -1) {
                    c->turn_dirs = (int16_t)traffic_angle_to_dir(c->spr.angle);
                    car_steer_towards(c, (c->spr.angle + 0x100) & 0x3ff);
                    h->turn = 1;
                    goto done;
                }
                goto pick;
            }
        } else if (dm == -1) {
            c->brake = 0;   /* only ahead */
            goto done;
        }
        if (dm >= 0 && dp == -1 && da == -1) {
            c->turn_dirs = (int16_t)c->road_dirs;
            car_steer_towards(c, c->spr.angle - 0x100);
            h->turn = 2;
            goto done;
        }
    }
pick:
    /* two or three exits: the one nearest the target's direction */
    if (!exit_p) {
        if (!exit_m || !ahead) goto three;
        if (dm < da) {
            c->turn_dirs = (int16_t)c->road_dirs;
            car_steer_towards(c, (c->spr.angle - 0x100) & 0x3ff);
            h->turn = 2;
            goto done;
        }
        if (dm <= da) goto done;
        c->turn_dirs = (int16_t)c->road_dirs;
        c->unkc0 = 0;
        goto straight;
    }
    if (exit_m) {
        if (ahead) goto three;
        if (dm <= dp) {
            if (dm < dp) {
                c->turn_dirs = (int16_t)c->road_dirs;
                car_steer_towards(c, c->spr.angle - 0x100);
                h->turn = 2;
            }
            goto done;
        }
        c->turn_dirs = (int16_t)c->road_dirs;
        car_steer_towards(c, (c->spr.angle + 0x100) & 0x3ff);
        h->turn = 1;
        goto done;
    }
    if (!ahead) goto three;
    if (da <= dp) {
        if (dp <= da) goto done;
        c->turn_dirs = (int16_t)c->road_dirs;
        goto straight;
    }
    goto right;
three:
    if (dm <= dp || da <= dp || dp < 0) {
        if (dm < da && dm < dp && dm >= 0) {
            c->turn_dirs = (int16_t)c->road_dirs;
            car_steer_towards(c, c->spr.angle - 0x100);
            h->turn = 2;
            goto done;
        }
        if (dp <= da || dm <= da || da < 0) goto done;
        goto straight;
    }
right:
    c->turn_dirs = (int16_t)c->road_dirs;
    car_steer_towards(c, c->spr.angle + 0x100);   /* (not masked here) */
    h->turn = 1;
    goto done;
straight:
    c->brake = 0;
    h->turn = 0;
done:
    c->cruise = 5;
    if (h->fresh == 1) h->fresh = 0;
    if (c->spr.x == h->last_x && c->spr.y == h->last_y && h->mode != 4 && h->mode != 10 && h->mode != 3) {
        int32_t qx, qy;
        if (hunt_probe_ahead(c, &qx, &qy) == 0 || !car_remove_at_block(qx >> 16, qy >> 16, c->id, 1, 1))
            c->speed = 0;
    }
    h->last_x = c->spr.x;
    h->last_y = c->spr.y;
}
