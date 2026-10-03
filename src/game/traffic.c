/* Traffic (traffic.h, docs/traffic.md): the dummy drivers and the generator. Car fields are named in
   car.h; the type cache (0x55fab0) is read as the original indexes it, through car_cache_at. */
#include "traffic.h"
#include "../audio/audio.h"
#include "../exe.h"
#include "carcoll.h"
#include "event.h"
#include "game.h"
#include "gmath.h"
#include "lights.h"
#include "mission_obj.h"
#include "ped.h"
#include "player.h"
#include "stubs.h"
#include <string.h>

int16_t g_traffic_recycle;
uint8_t g_traffic_edge[4];

/* the type cache byte of block (bx, by, bz), at the original's linear index */
static uint8_t cache_b(int bx, int by, int bz) { return car_cache_at((bz * 0x100 + by) * 0x100 + bx); }
static bool road_kind(int k) { return k == 2 || k == 6 || k == 7; }
/* the give-way lid tiles (the ground lid under a point): 'Q', 'R', 'M' */
static bool give_way_lid(int8_t l) { return l == 'Q' || l == 'R' || l == 'M'; }
static int32_t ground(int32_t x, int32_t y, int32_t z) { return map_get_ground_z(g_game.map, x, y, z); }
static int8_t lid_below(int32_t x, int32_t y, int32_t z) { return (int8_t)map_get_lid_below(x, y, z); }
/* the car info record of the car's model (0x501574[0x4be178[model]]) as the AI reads it */
static int16_t info_s16(const Car *c, int o)
{
    const uint8_t *in = car_info_of_model(c->model);
    return in ? (int16_t)(in[o] | in[o + 1] << 8) : 0;
}
/* half of a + b with the 32-bit wrap of the original's add */
static int32_t avg2(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b) >> 1; }

/* ======================================================================================== probes */

/* Car_IsSpaceFree 0x407160 */
bool car_is_space_free(int32_t x, int32_t y, int32_t z, const Car *c)
{
    bool r = true;
    CollHit *h = coll_query_block(x, y, z, COLL_CAR, c->id);
    for (; h; h = h->next) {
        const Car *o = h->owner;
        if (o->id != c->id && o->speed != 0) { r = false; break; }
    }
    coll_unlock();
    return r;
}

/* Car_IsBlockedByCar 0x4071c0 (the list is read after the unlock, as in the original) */
bool car_is_blocked_by_car(int32_t x, int32_t y, int32_t z, const Car *c)
{
    if (c->model == 0x2f) return true;
    CollHit *h = coll_query_block(x, y, z, COLL_CAR, c->id);
    coll_unlock();
    for (; h; h = h->next) {
        const Car *o = h->owner;
        if (o->id != c->id) {
            if (o->active == 0) car_remove_if_offscreen(o->id);
            return false;
        }
    }
    return true;
}

/* Car_IsSpaceClearOfAll 0x407230 */
bool car_is_space_clear_of_all(int32_t x, int32_t y, int32_t z, const Car *c)
{
    bool r = true;
    for (CollHit *h = coll_query_block(x, y, z, 0, c->id); h; h = h->next) {
        if (h->kind == COLL_CAR) {
            if (((const Car *)h->owner)->id != c->id) r = false;
        } else if (h->kind == COLL_PED) {
            if (((const Ped *)h->owner)->id != c->driver) r = false;
        } else {
            r = false;
        }
    }
    coll_unlock();
    return r;
}

/* Car_CheckAhead 0x4072a0 */
bool car_check_ahead(int32_t x, int32_t y, Car *c)
{
    if (c->model == 0x2f) return true;
    bool r = true;
    CollHit *h = coll_query_car_box(c, x, y, COLL_CAR, c->id);
    while (h && ((const Car *)h->owner)->id == c->id) h = h->next;
    if (h) {
        const Car *o = h->owner;
        r = false;
        if (o->active == 0) car_remove_if_offscreen(o->id);
    }
    coll_unlock();
    return r;
}

/* Car_IsPathClear 0x415a50: only the first entry of each query counts */
bool car_is_path_clear(int32_t x, int32_t y, int32_t z, const Car *c)
{
    bool r = true;
    CollHit *h = coll_query_block(x, y, z, COLL_PED, c->id);
    if (h) {
        const Ped *p = h->owner;
        if (p->id != c->driver && p->health > 0) r = false;
    } else {
        coll_unlock();
        h = coll_query_block(x, y, z, COLL_OBJECT, c->id);
        if (h) {
            if (((const Obj *)h->owner)->weight == 3) r = false;
        } else {
            coll_unlock();
            h = coll_query_block(x, y, z, COLL_KIND8, c->id);
            if (!h) {
                coll_unlock();
                h = coll_query_block(x, y, z, COLL_KIND10, c->id);
            }
            if (h) r = false;
        }
    }
    coll_unlock();
    return r;
}

/* ================================================================================ horn and lanes */

/* Car_SetHorn 0x406e90: ordinary cars sound the horn (+0x136); models 4, 5 and 0x2a switch the siren
   state (+0x11a 1 off -> 2 on, lights started on the first use; the tank, 0x25, never). Then the
   first car within five blocks ahead (the pending-box query) starts a lane change. */
void car_set_horn(Car *c, int on)
{
    int16_t m = c->model;
    if (m < 4 || (m > 5 && m != 0x2a)) {
        c->horn_time = on ? 1 : 0;
    } else {
        switch (c->horn) {
        case 0:
            if (m != 0x25) {
                if (c->siren_state == 0) c->siren_state = 1, c->siren_tick = 0;
                if (c->horn == 0) c->horn = 1;
            }
            break;
        case 1: if (on) c->horn = 2; break;
        case 2: if (!on) c->horn = 1; break;
        default: game_fatal(-0x4a, 0x1cc, c->horn);
        }
    }
    int fh = c->front_heading & 0x3ff;
    for (int k = 1; k < 6; k++) {
        CollHit *h = coll_query_car_box(c, math_sin(fh) * k * 0x40 + c->spr.x, math_cos(fh) * k * 0x40 + c->spr.y,
                                        COLL_CAR, c->id);
        if (h) {
            car_start_lane_change(h->owner);
            coll_unlock();
            return;
        }
        coll_unlock();
    }
    coll_unlock();
}

/* Car_SetHornById 0x406fc0 */
void car_set_horn_by_id(int car, int on)
{
    Car *c = &g_cars[(int16_t)car];
    c->u12e = (int16_t)on;
    if (c->model != 0x25) car_set_horn(c, on);
}

/* Car_StartLaneChange 0x408330: a normal traffic car (owner status 1) heads for the other lane: the
   lane offset 0x37 or 7 by its direction, swapped when the block on its left is a road too; owner
   status 2 (Cars_UpdateAll speeds it up), lane mode 0, lane step 4 */
void car_start_lane_change(Car *c)
{
    if (c->owner_status != 1) return;
    int h = (c->front_heading - 0x100) & 0x3ff;
    uint8_t b = car_type_cache(math_sin(h) * 0x40 + c->spr.x, math_cos(h) * 0x40 + c->spr.y, c->spr.z);
    switch (c->road_dirs) {
    case 8: c->lane = 0x37; break;
    case 4: c->lane = 7; break;
    case 2: c->lane = 7; break;
    case 1: c->lane = 0x37; break;
    default: break;
    }
    int k = b >> 4 & 7;
    if (road_kind(k) && c->lane == 0x37) c->lane = 7;
    else if (road_kind(k) && c->lane == 7) c->lane = 0x37;
    c->owner_status = 2;
    c->lane_mode = 0;
    c->uc4 = 4;
}

/* ================================================================================ the dummy driver */

/* Car_DummyFollowRoad 0x408440 (control 0 cars, every frame they are active). The point ahead (half
   the length / 2, + 8 for buses, + the speed) decides:
   - a step the car can't take (more than 0x14 pixels of height off the slope's continuation, uphill
     only), a building (it slows by 1) or, for a control-8 car below speed 0x14, air: no decision;
   - off the road proper: lane mode 3 with a road on the left, else 4 with one on the right (step 8);
   - otherwise a turn (+0x96 = +-0x20 a frame) when the block ahead doesn't continue the direction but
     turns one way, or, for cars whose id has the direction bit, into a side road ahead whose corner
     is open; the turn is dropped when the car's direction continues on that side. Turning cars slow to
     their cruise speed. Owner states 2 / 3 (lane changes) slow the car down. */
void car_dummy_follow_road(Car *c)
{
    bool ok = true;
    int16_t fh = c->front_heading;
    /* (the original computes the ground z one block to the left here and drops it) */
    int16_t d = (int16_t)(c->half_l >> 1);
    if (c->vtype == 0) d = (int16_t)(d + 8);
    int16_t far = (int16_t)(d + c->speed);
    int32_t ax = math_sin(fh & 0x3ff) * far + c->spr.x, ay = math_cos(fh & 0x3ff) * far + c->spr.y;
    uint32_t t = map_get_type_at(g_game.map, c->spr.x, c->spr.y, c->spr.z);
    int32_t z = c->spr.z + map_slope_delta(t, c->spr.x, c->spr.y, ax, ay);
    int32_t gz = ground(ax, ay, z - 0x200000);
    if ((gz + 0x140000 <= z || z <= gz - 0x140000) && gz < z) ok = false;
    if (gz < 0) gz = 0x10000;
    uint8_t a = cache_b(ax >> 22, ay >> 22, gz >> 22);
    int ka = a >> 4 & 7;
    if (ka == 5) {
        c->speed--;
        ok = false;
    }
    if (ka == 0 && c->speed < 0x14 && c->control == 8) {
        ok = false;
        c->speed--;
        c->brake = 0;
    }
    int kt = (int)t >> 4 & 7;
    if (kt != 2) {
        int h = (c->front_heading - 0x100) & 0x3ff;
        int32_t lx = math_sin(h) * 0x40 + c->spr.x, ly = math_cos(h) * 0x40 + c->spr.y;
        int32_t g = ground(lx, ly, c->spr.z - 0x200000);
        if (g < 0) g = 0x10000;
        bool set = true;
        if ((cache_b(lx >> 22, ly >> 22, g >> 22) & 0x70) == 0x20) {
            c->lane_mode = 3;
        } else {
            h = (c->front_heading + 0x100) & 0x3ff;
            lx = math_sin(h) * 0x40 + c->spr.x, ly = math_cos(h) * 0x40 + c->spr.y;
            g = ground(lx, ly, c->spr.z - 0x200000);
            if (g < 0) g = 0x10000;
            if ((cache_b(lx >> 22, ly >> 22, g >> 22) & 0x70) == 0x20) c->lane_mode = 4;
            else set = false;
        }
        if (set) c->uc4 = 8;
    }
    uint16_t dirs = c->road_dirs;
    /* per direction: the opposite bit, the side bit, the block beside the one ahead (ox, oy) and
       the corner beyond it (+ px, py) */
    int side, opp, ox, oy, px, py;
    switch (dirs) {
    case 1: side = 8, opp = 2, ox = 1, oy = 0, px = 0, py = -1; break;
    case 2: side = 4, opp = 1, ox = -1, oy = 0, px = 0, py = 1; break;
    case 4: side = 1, opp = 8, ox = 0, oy = -1, px = -1, py = 0; break;
    default: side = 2, opp = 4, ox = 0, oy = 1, px = 1, py = 0; break;
    }
    if (ok && c->turn_delta == 0) {
        int ab = a & 0xf;
        if (t & (uint32_t)opp) c->lane_mode = 3;
        if (!road_kind(kt)) c->lane_mode = 4;
        bool turn = true;
        if (ab & dirs) {
            turn = false;
            if ((c->id & dirs) && (ab & side)) {
                int bx = c->spr.x >> 22, by = c->spr.y >> 22, bz = c->spr.z >> 22;
                if ((cache_b(bx + ox, by + oy, bz) & 0xf) == 0 && (cache_b(bx + ox + px, by + oy + py, bz) & 0x70) == 0x20) {
                    ab = side;
                    turn = true;
                }
            }
            if (!turn) c->ua6 = 0;
        }
        if (turn) {
            int16_t td = 0;
            switch (dirs) {
            case 8: td = ab == 2 ? -0x20 : ab == 1 ? 0x20 : 0; break;
            case 4: td = ab == 1 ? -0x20 : ab == 2 ? 0x20 : 0; break;
            case 2: td = ab == 8 ? 0x20 : ab == 4 ? -0x20 : 0; break;
            case 1: td = ab == 4 ? 0x20 : ab == 8 ? -0x20 : 0; break;
            default: break;
            }
            c->turn_delta = td;
            if (td != 0) {
                int h = (c->front_heading - 0x100) & 0x3ff;
                int32_t lx = math_sin(h) * 0x40 + c->spr.x, ly = math_cos(h) * 0x40 + c->spr.y;
                int32_t g = ground(lx, ly, c->spr.z - 0x200000);
                if (g < 0) g = 0x10000;
                uint8_t left = cache_b(lx >> 22, ly >> 22, g >> 22);
                h = (c->front_heading + 0x100) & 0x3ff;
                lx = math_sin(h) * 0x40 + c->spr.x, ly = math_cos(h) * 0x40 + c->spr.y;
                g = ground(lx, ly, c->spr.z - 0x200000);
                if (g < 0) g = 0x10000;
                uint8_t right = cache_b(lx >> 22, ly >> 22, g >> 22);
                uint8_t db = (uint8_t)dirs;
                if (c->turn_delta < 0 && (db & left)) c->turn_delta = 0;
                if (c->turn_delta > 0 && (db & right)) c->turn_delta = 0;
                if (c->cruise < c->speed) c->speed--;
                c->input = 0;
                c->ua6 = 0;
                c->u98 = c->turn_delta;
                c->turn_dirs = (int16_t)c->road_dirs;
            }
        }
    }
    if (c->owner_status == 2) {
        c->input = 0;
        if (c->cruise < c->speed) c->speed--;
    } else if (c->owner_status == 3) {
        c->speed--;
        c->input = 0;
        if (c->speed < 1) {
            c->input = 0;
            c->owner_status = 0;
            c->speed = 0;
        }
    }
}

/* the lane position of the car across its road: the pixel within the block (signed remainder) */
static int lane_pos(const Car *c, int dirs)
{
    if (dirs == 1 || dirs == 2) return (int16_t)(c->spr.x >> 16) % 64;
    return (int16_t)(c->spr.y >> 16) % 64;
}

/* Car_DummyKeepLane 0x415fe0: a car away from its lane offset (+0x10e, +- the step +0xc4 and one
   pixel) starts moving across (lane mode 1 / 2, which side by the direction); a lane move ends
   inside the step (and, for lane modes 3+, only once the car left the block it was saved in), an
   overtaking car (owner status 2) then returns to the normal lane 0x1f (status 3). While moving the
   side checks shift the front wheel. */
void car_dummy_keep_lane(Car *c)
{
    if (c->control == CAR_CTL_PHYSICS) return;
    bool skip = false;
    if (c->turn_delta != 0) {
        int16_t fh = c->front_heading;
        if ((int8_t)fh != 0 || fh == c->spr.angle || c->half_l > 0x20) skip = true;
    }
    if (!skip) {
        int dirs = c->road_dirs;
        bool along = dirs == 1 || dirs == 2 || dirs == 4 || dirs == 8;
        if (c->lane_mode != 0) {
            if (c->lane_mode < 3 || ((c->saved_x ^ c->spr.x) & 0xffc00000) || ((c->saved_y ^ c->spr.y) & 0xffc00000)) {
                if (along) {
                    int m = lane_pos(c, dirs);
                    if (m >= c->lane - c->uc4 && m <= c->uc4 + c->lane) {
                        if (c->owner_status == 2) {
                            c->lane = 0x1f;
                            c->owner_status = 3;
                        }
                        c->lane_mode = 0;
                    }
                }
            }
        } else {
            if (c->owner_status == 1) c->uc4 = 3;
            if (along) {
                int m = lane_pos(c, dirs);
                int lo = c->lane - c->uc4 - 1, hi = c->uc4 + 1 + c->lane;
                switch (dirs) {
                case 1:
                    if (m < lo) c->lane_mode = 1;
                    else if (hi < m) c->lane_mode = 2;
                    break;
                case 2: case 4:
                    if (m < lo) c->lane_mode = 2;
                    else if (hi < m) c->lane_mode = 1;
                    break;
                case 8:
                    if (m < lo) c->lane_mode = 1;
                    else if (hi < m) c->lane_mode = 2;
                    break;
                }
            }
        }
    }
    if (c->speed > 0) {
        int16_t lm = c->lane_mode;
        if (lm == 3 || lm == 1) car_lane_check_left(c, c->road_dirs, c->uc4);
        else if (lm == 2 || lm == 4) car_lane_check_right(c, c->road_dirs, c->uc4);
    }
}

/* Car_LaneCheckLeft 0x4162f0 / Car_LaneCheckRight 0x416480: the point 0x34 pixels to that side of
   the car's middle; a building there ends an overtake (status 3, lane 0x1f), no road there ends the
   lane move; a car there slows this one; if the side block allows the direction (any block counts
   inside the car's own block) or the car is overtaking, the front wheel shifts by `step` sideways. */
static void lane_check(Car *c, int dirs, int step, int side)
{
    int h = (c->front_heading + side) & 0x3ff;
    int32_t x = ((math_sin(h) * 0x34 - c->rear_x + c->front_x) >> 1) + c->rear_x;
    int32_t y = ((math_cos(h) * 0x34 - c->rear_y + c->front_y) >> 1) + c->rear_y;
    int bx = x >> 22, by = y >> 22;
    uint8_t t = cache_b(bx, by, c->spr.z >> 22);
    int k = t >> 4 & 7;
    if (k == 5 && c->owner_status == 2) {
        c->owner_status = 3;
        c->lane_mode = 0;
        c->lane = 0x1f;
    }
    if (!road_kind(k)) {
        c->lane_mode = 0;
        t = 0;
    }
    uint8_t bits = (bx == c->spr.x >> 22 && by == c->spr.y >> 22) ? 0xf : t;
    if (!car_check_ahead(x, y, c) && c->speed > 0) c->speed--;
    if ((((uint8_t)dirs & bits) || c->owner_status == 2) && c->owner_status != 3) {
        int32_t fx = math_sin(h) * (int16_t)step + c->front_x, fy = math_cos(h) * (int16_t)step + c->front_y;
        c->front_y = fy;
        c->next_x = avg2(c->rear_x, fx);
        c->next_y = avg2(c->rear_y, fy);
        c->front_x = fx;
        return;
    }
    c->lane_mode = 0;
}
void car_lane_check_left(Car *c, int dirs, int step) { lane_check(c, dirs, step, -0x100); }
void car_lane_check_right(Car *c, int dirs, int step) { lane_check(c, dirs, step, 0x100); }

/* Car_DummyDrive 0x416610 (every active car, before Car_Update). Bikes: the fallen frame, a wrecked
   (99) bike throws its rider. Then, for a car with a driver:
   - AI drivers (control 2 / 9 / 10, or +0xc0 set): a give-way lid ('Q' 'R' 'M') under the point
     ahead or the car, while on a screen, makes it wait (+0xdc = 2) for Car_DummyTurnRight;
   - dummies (not with the siren on, not a fallen bike): the box half a length ahead (at most 0x40)
     against cars and peds / heavy objects (a blocked tram rings, 0x136); then the probe +0x7c blocks
     further (1..3, cycling while free, shrinking while blocked: the car brakes, sometimes hoots or
     a ped screams); a red or amber light at the stop point (speed + 0x20 for long cars) of a junction
     not passed yet (+0xa8) stops it (+0xdc = 1) and so does a give-way lid ahead of a car standing on
     one (+0xdc = 2), both only on a screen; braking at the top speed hoots; free cars accelerate
     (car info +0x0e) up to their top speed (+ the bonus +0x8e) or the cruise speed while turning;
   - waiting cars: below speed 4 they stop; +0xdc 1 waits for green (Car_DummyCheckLights), 2 gives
     way (Car_DummyTurnLeft when the car's id picks the side road and it continues there, else
     Car_DummyTurnRight).
   Any input left runs Car_DummyThrottle; badly damaged cars (> 0x62) roll to a stop. */
void car_dummy_drive(Car *c)
{
    c->ube = 0;
    if (c->u13a == 0 && (c->control == 8 || c->control == 0) && c->speed < 0) {
        c->speed = 0;
        c->u1e = 0;
    }
    if (c->vtype == CAR_VT_BIKE) {
        if (c->status == 8) {
            sprite_set_frame(&c->spr, c->base_frame + 9);
            c->turret = 0;
        }
        if (c->damage == 99 && c->status != 7 && c->falling == 0) {
            ped_eject_driver(c);
            c->status = 7;
            sprite_set_frame(&c->spr, c->base_frame + 7);
            c->turret = 0;
            c->input = (int16_t)-info_s16(c, 0x10);
            if (c->u12c == 0) c->damage++;
        }
    }
    if (c->driver == -1) return;
    int16_t ctl = c->control;
    int lights = 0;   /* sVar10: 2 a light to stop for, 1 stopping for it */
    if ((c->unkc0 != 0 || ctl != 0) && (ctl != 3 || c->horn != 0)) {
        if (ctl == 10 || ctl == 2 || ctl == 9) {
            int fh = c->front_heading & 0x3ff;
            int32_t x = math_sin(fh) * c->half_l + c->spr.x, y = math_cos(fh) * c->half_l + c->spr.y;
            int32_t gz = ground(x, y, c->spr.z - 0x200000);
            int8_t la = lid_below(x, y, gz), lb = lid_below(c->spr.x, c->spr.y, c->spr.z);
            if ((give_way_lid(la) || give_way_lid(lb)) && c->turn_delta == 0 && car_is_on_screen(c)) {
                c->udc = 2;
                c->input = 0;
                g_car_forced_accel[c->id] = 0;
            }
            if (c->udc == 2) {
                car_dummy_turn_right(c);
                if (c->udc == 2 && c->turn_delta == 0) {
                    int16_t s = c->speed;
                    if (s > 0) c->brake = 1;
                    if (give_way_lid(lb) && s > 0) c->speed = (int16_t)(s - 1);
                }
            }
        }
        goto done;
    }
    if (c->horn != 2 && c->status != 7) {
        int16_t hl = c->half_l;
        bool b3 = true, b4 = true, b5 = false, b2;
        int h = c->front_heading & 0x3ff;
        if (hl > 0x40) hl = 0x40;
        int32_t x = math_sin(h) * hl + c->spr.x, y = math_cos(h) * hl + c->spr.y;
        int32_t gz = ground(x, y, c->spr.z - 0x200000);
        if (gz < 0) gz = 0x10000;
        int8_t lid = lid_below(x, y, gz);
        coll_build_box(x, y, gz, c->half_w, c->half_l, h, c->depth, &c->box_saved);
        bool ahead = car_check_ahead(x, y, c);
        if (!ahead && c->model == 9 && c->unkec != 0 && (int16_t)math_random() > 12000) c->horn_time = 0x28;
        bool clear = car_is_path_clear(x, y, gz, c);
        int16_t m = c->model;
        if (m == 0x2f) {
            ahead = clear = true;
            b2 = true;
        } else if (ahead && clear) {
            b2 = true;
        } else {
            if (c->speed > 0) c->speed--;
            if (c->control == 8) {
                if (c->speed == 0) c->owner_status = 0;
                else if (c->speed < 0) c->speed = 0;
            }
            b2 = false;
            b5 = true;
            c->input = 0;
            b3 = false;
        }
        if (c->control == 8) {
            if (m != 0x2f) {
                b2 = true;
                b4 = true;
                c->udc = 0;
                c->unkc0 = 0;
            }
        } else if (m != 0x2f) {
            if (b2) {
                int16_t dist = (int16_t)(hl + c->u7c * 0x40);
                x = math_sin(h) * dist + c->spr.x, y = math_cos(h) * dist + c->spr.y;
                if (x < 0 || y < 0 || x > 0x3fc00000 || y > 0x40000000) {
                    b2 = false;
                    c->u7c = 0;
                    goto probe_done;
                }
                int32_t g2 = ground(x, y, c->spr.z - 0x200000);
                if (g2 < 0) g2 = 0x10000;
                coll_build_box(x, y, g2, c->half_w, c->half_l, h, c->depth, &c->box_saved);
                if (ahead) ahead = car_is_blocked_by_car(x, y, g2, c);
                if (clear) clear = car_is_path_clear(x, y, g2, c);
                b2 = b3;
                if (!ahead || !clear) {
                    int16_t k = c->u7c;
                    if (k * 2 < c->speed) c->speed--;
                    if (k < 2) {
                        b2 = false;
                        if (c->speed > 5 && c->u124 > 0) c->speed = (int16_t)(c->speed - 2);
                    } else {
                        c->u7c = (int16_t)(k - 1);
                    }
                    int16_t s = c->speed;
                    if (s < 0) {
                        c->speed = 0;
                    } else if ((s > 10 && c->u7c < 4) || (s > 4 && c->u7c < 2) || (s > 0 && c->u7c == 0)) {
                        int r = (int16_t)math_random() % 100;
                        if (r > 25) {
                            if (r < 51) {
                                Snd_PlayScream(c->spr.x, c->spr.y, c->spr.z);
                            } else {
                                c->brake = 1;
                                c->horn_time = 0x28;
                            }
                        }
                    }
                    c->input = 0;
                    b5 = true;
                } else {
                    c->brake = 0;
                    if (c->u7c < 3) c->u7c++;
                    else c->u7c = 1;
                }
                /* the stop point of a traffic light */
                int16_t sd = (int16_t)((c->half_l < 0x21 ? 0 : 0x20) + c->speed);
                int32_t ly = math_cos(h) * sd + c->spr.y, lx = math_sin(h) * sd + c->spr.x;
                int32_t g3 = ground(lx, ly, c->spr.z - 0x200000);
                int lbx = lx >> 22, lby = ly >> 22;
                if (map_test_block_attr(2, lbx, lby, g3 >> 22)) {
                    uint16_t j = (uint8_t)lights_query(0x3a, lbx, lby);
                    if ((uint16_t)c->ua8 != j) {
                        c->ua8 = (int16_t)j;
                        int st = (int8_t)lights_query(0x34, lbx, lby);
                        if (st == 0 || st == 1) lights = 2;
                    }
                }
                int8_t lb = lid_below(c->spr.x, c->spr.y, c->spr.z);
                if (((c->turn_delta == 0 && give_way_lid(lid) && give_way_lid(lb)) || lights == 2) && car_is_on_screen(c)) {
                    if (lights == 2) {
                        c->udc = 1;
                        c->input = 0;
                        lights = 1;
                    } else {
                        c->udc = 2;
                        c->input = 0;
                    }
                    g_car_forced_accel[c->id] = 0;
                }
                if (c->udc == 0 || (give_way_lid(lid) && lights != 1)) {
                    if (c->udc == 2) {
                        int16_t s2 = c->speed;
                        if (s2 > 10) c->brake = 1;
                        if (s2 > 0) c->speed = (int16_t)(s2 - 1);
                    }
                    if (b2) goto probe_end;
                } else {
                    if (c->speed < 1) {
                        c->speed = 0;
                        c->input = 0;
                    } else {
                        int16_t s2 = --c->speed;
                        c->input = (int16_t)-info_s16(c, 0x10);
                        if (s2 > 8) c->speed = 8;
                    }
                    b2 = false;
                    b4 = false;
                }
                if (c->max_speed <= c->speed && c->lane_mode == 0) {
                    c->horn_time = 0x28;
                    c->brake = 1;
                }
            probe_end:;
            } else {
            probe_done:
                lights = 0;
            }
            if (c->u7c > 3 || c->u7c < 0) c->u7c = 0;
        }
        if (c->vtype == CAR_VT_2) {
            c->udc = 0;
            b2 = false;
        }
        if (!b4 || b2 || c->unkc0 != 0) {
            if (c->udc == 0 && lights != 1 && c->unkc0 == 0 && c->owner_status == 1) {
                if (c->turn_delta == 0) {
                    if (c->speed < c->u8e + c->max_speed) {
                        if (b5) goto waiting;
                        c->input = info_s16(c, 0x0e);
                    } else {
                        c->input = 0;
                        c->speed--;
                    }
                } else if (c->cruise < c->speed) {
                    c->input = 0;
                    c->speed = (int16_t)(c->cruise - 1);
                } else {
                    if (b5) goto waiting;
                    c->input = info_s16(c, 0x0e);
                }
                c->brake = 0;
            }
        } else if (c->turn_delta == 0) {
            if (c->speed < 0) c->input = 0;
        } else {
            if (c->speed > 0) c->speed--;
            c->input = 0;
        }
    }
waiting:
    if (c->udc != 0 && c->speed < 4) {
        c->speed = 0;
        c->input = 0;
        g_car_forced_accel[c->id] = 0;
    }
    if (c->udc == 1) {
        car_dummy_check_lights(c);
    } else if (c->udc == 2) {
        int dx, dy, want;
        switch (c->road_dirs) {
        case 1: dx = 0, dy = 1, want = 8; break;
        case 2: dx = 0, dy = -1, want = 4; break;
        case 4: dx = -1, dy = 0, want = 1; break;
        case 8: dx = 1, dy = 0, want = 2; break;
        default: dx = 0, dy = 0, want = 0x10; break;
        }
        if ((c->id & c->road_dirs) == 0 ||
            (cache_b((c->spr.x >> 22) + dx, (c->spr.y >> 22) + dy, c->spr.z >> 22) & 0xf) != want)
            car_dummy_turn_right(c);
        else
            car_dummy_turn_left(c);
    }
done:
    if (c->input != 0) car_dummy_throttle(c);
    if (c->damage > 0x62 && c->u102 > 3) {
        int16_t s = c->speed;
        if (s > 0) {
            c->speed = (int16_t)(s - 1);
            return;
        }
        if (s < 0) c->speed = (int16_t)(s + 1);
    }
}

/* Car_DummyThrottle 0x4170e0: the car info speeds (not for physics cars or model 4), then, with a
   driver sitting normally (anim 0 / 0x7f / 0x80), not braking, the engine on, kinematic and below 99
   damage: every 4th..8th frame (+0x102) the input accumulates in +0x1e and moves the speed by
   +0x1e / 16 (stopping at 0 when it changes sign), dummies at most cruising + 0xf above +0x8e. The
   speed is capped at the top speed (+ +0x8e). Then the wheelspin. */
void car_dummy_throttle(Car *c)
{
    if (c->control != CAR_CTL_PHYSICS && c->model != 4) {
        c->accel = info_s16(c, 0x0e);
        c->braking = info_s16(c, 0x10);
        c->max_speed = info_s16(c, 0x0a);
        c->min_speed = info_s16(c, 0x0c);
    }
    bool seated = false;
    if (c->driver != -1) {
        int16_t a = ped_get(c->driver)->anim;
        if (a == 0 || a == 0x7f || a == 0x80) seated = true;
    }
    if (c->u12c == 2) c->brake = 1;
    int16_t *forced = &g_car_forced_accel[c->id];
    if (*forced != 0 && c->input == 0) c->input = *forced;
    if (c->brake == 0 && seated && c->keep_active == 0 && c->unk88 != 0 && c->physics == 0 && c->damage <= 0x62) {
        int16_t in = c->input, s = c->speed;
        if (in != 0 && c->udc == 0 && c->u102 > 3 &&
            ((in > 0 && s < c->u8e + c->max_speed) || (in < 0 && c->min_speed < s))) {
            if (c->falling == 0) {
                if (c->control != 0 || s - c->u8e <= c->uce + 0xf) c->u1e = (int16_t)(c->u1e + in);
                int16_t acc = c->u1e, s0 = c->speed;
                bool zero = false;
                if (acc < 1) {
                    if (acc < 0) {
                        int16_t ns = (int16_t)((acc >> 4) + s0);
                        c->speed = ns;
                        if (s0 > 0 && ns < 1) zero = true;
                    }
                } else {
                    int16_t ns = (int16_t)((acc >> 4) + s0);
                    c->speed = ns;
                    if (s0 < 0 && ns >= 0) zero = true;
                }
                if (zero) {
                    c->input = 0;
                    c->u1e = 0;
                    c->speed = 0;
                    *forced = 0;
                }
                if (c->u1e > 0x10) c->u1e &= 0xf;
                if (c->u1e < -0x10) c->u1e &= 0xf;
                if (c->speed == 0 && s0 != 0) {
                    c->u1e = 0;
                    c->input = 0;
                    *forced = 0;
                }
            }
            c->u102 = 0;
        }
        s = c->speed;
        if (s <= c->min_speed && c->u1e == 0 && c->input < 0) c->input = 0;
        if (c->u8e + c->max_speed <= s && c->u1e == 0 && c->input > 0) c->input = 0;
        if (c->control == 0 && s < 0) c->speed = 0;
    }
    int16_t mx = c->max_speed;
    if (mx < c->speed && c->u8e == 0) c->speed = mx;
    if (++c->u102 > 8) c->u102 = 4;
    if (c->u8e + mx < c->speed) c->speed--;
    car_update_wheelspin(c);
    if (c->control == 0 && c->uce + 0xf < c->speed - c->u8e) c->speed = (int16_t)(c->uce + 0xf);
}

/* the two rear corners of a car: rear wheel point +- a quarter of the width across */
static void rear_side(const Car *c, int side, int32_t *x, int32_t *y)
{
    int h = (c->spr.angle + side) & 0x3ff, k = c->cam_w >> 2;
    *x = math_sin(h) * k + c->rear_x;
    *y = math_cos(h) * k + c->rear_y;
}

/* Car_UpdateWheelspin 0x415b10: a car pulling away while braked (+0xb6 with input, at rest) spins its
   wheels (+0x100 up to half the top speed): smoke (object 0x43, chance 4767/32768 per frame after 5)
   at the rear wheel(s) and the tail wagging by 2 heading units; afterwards +0x100 counts down with
   tyre marks (objects 10 and 0xb) while above 5. Bikes rear up (+0xf0, frames base + 10..12) and fall
   over (status 7, the rider ejected) when they wheelie at full speed. */
void car_update_wheelspin(Car *c)
{
    int16_t b = c->brake;
    if (b == 1 && c->input > 0 && c->speed == 0 && c->keep_active == 0 && c->damage < 100 && c->unk88 != 0) {
        if (c->u100 < c->max_speed >> 1) {
            int16_t n = ++c->u100;
            if (n > 5 && (int16_t)math_random() > 28000) {
                if (c->vtype == CAR_VT_BIKE) {
                    obj_create(c->rear_x, c->rear_y, c->spr.z, 0x43, c->spr.angle);
                } else {
                    int32_t x, y, z = c->spr.z;
                    rear_side(c, 0x100, &x, &y);
                    obj_create(x, y, z, 0x43, c->spr.angle);
                    rear_side(c, -0x100, &x, &y);
                    obj_create(x, y, z, 0x43, c->spr.angle);
                }
            }
            int16_t r = (int16_t)math_random();
            if (r < 0x3e81 || c->spr.angle == 0) {
                int16_t a = c->spr.angle;
                if (a != 0 && c->front_heading - 5 < a - 2) c->next_heading = (int16_t)((a - 2) & 0x3ff);
            } else if (c->spr.angle + 2 < c->front_heading + 5) {
                c->next_heading = (int16_t)((c->spr.angle + 2) & 0x3ff);
            }
        } else {
            c->input = 0;
        }
    } else {
        int16_t w = c->u100;
        if (w < 1 || b != 0 || c->keep_active != 0) {
            if (w == 0 && b == 0 && c->status < 7 && c->keep_active == 0 && c->vtype == CAR_VT_BIKE && c->turret > 0) {
                if (c->max_speed - 3 < c->speed || c->speed < c->max_speed - 6) {
                    int16_t t = --c->turret;
                    if (t < 0xc) sprite_set_frame(&c->spr, (int16_t)((t >> 2) + c->base_frame) + 10);
                }
            }
        } else {
            c->u100 = (int16_t)(w - 1);
            if (c->vtype == CAR_VT_BIKE && c->turret < 0x3c && c->status < 7) {
                if (c->turret < 0xc) sprite_set_frame(&c->spr, (int16_t)((c->turret >> 2) + c->base_frame) + 10);
                c->turret++;
            }
            if (c->u100 > 5) {
                if (c->vtype == CAR_VT_BIKE) {
                    obj_create(c->rear_x, c->rear_y, c->spr.z, 10, c->spr.angle);
                    obj_create(c->rear_x, c->rear_y, c->spr.z, 0xb, c->spr.angle);
                } else {
                    int32_t x, y, z = c->spr.z;
                    rear_side(c, 0x100, &x, &y);
                    obj_create(x, y, z, 10, c->spr.angle);
                    obj_create(x, y, z, 0xb, c->spr.angle);
                    rear_side(c, -0x100, &x, &y);
                    obj_create(x, y, z, 10, c->spr.angle);
                    obj_create(x, y, z, 0xb, c->spr.angle);
                }
            }
        }
    }
    if (c->vtype == CAR_VT_BIKE &&
        ((c->turret > 0 && c->brake == 1 && c->speed > 0 && c->status < 7) || (c->status < 7 && c->speed < 3))) {
        c->turret = 0;
        sprite_set_frame(&c->spr, c->base_frame);
    }
    if (c->turret > 0xc && c->brake == 0 && c->speed == c->max_speed && c->status < 7) {
        c->turret = 0;
        c->status = 7;
        sprite_set_frame(&c->spr, c->base_frame + 7);
        c->next_heading = (int16_t)((c->spr.angle + 0x100) & 0x3ff);
        ped_eject_driver(c);
    }
}

/* the end of a wait at a junction: +0xdc cleared, the accelerator (car info +0x0e) for non-physics
   cars, +0xb6 and +0x124 cleared */
static void go_on(Car *c)
{
    c->udc = 0;
    if (c->control != CAR_CTL_PHYSICS) c->input = info_s16(c, 0x0e);
    c->brake = 0;
    c->u124 = 0;
}

/* Car_DummyTurnLeft 0x4173d0: up to three blocks ahead, the first road block that carries the
   crossing direction (8 / 4 / 1 / 2 for directions 1 / 2 / 4 / 8) is checked over five blocks along
   the crossing lane for moving cars; when all is free the car goes on. */
void car_dummy_turn_left(Car *c)
{
    int8_t dx = 0, dy = 0, sx = 0, sy = 0;
    uint8_t want;
    switch (c->road_dirs) {
    case 1: dy = -1, sx = 1, want = 8; break;
    case 2: dy = 1, sx = -1, want = 4; break;
    case 4: dx = -1, sy = -1, want = 1; break;
    case 8: dx = 1, sy = 1, want = 2; break;
    default: want = 0xf; break;
    }
    bool free = true, found = false;
    uint8_t x = (uint8_t)(c->spr.x >> 22), y = (uint8_t)(c->spr.y >> 22);
    int bz = c->spr.z >> 22 & 0xff;
    for (int i = 0; i < 3; i++) {
        uint8_t ny = (uint8_t)(y + dy), nx = (uint8_t)(x + dx);
        x = nx;
        uint8_t t = cache_b(nx, ny, bz);
        if ((t & 0x70) == 0x20) {
            if (want & t) {
                found = true;
                uint8_t xx = nx, yy = ny;
                for (int j = 0; j < 5; j++) {
                    uint8_t t2 = cache_b(xx, yy, bz);
                    if ((t2 & 0x70) == 0x20 && (want & t2))
                        free = car_is_space_free(xx * 0x400000 + 0x200000, yy * 0x400000 + 0x200000, c->spr.z, c);
                    xx = (uint8_t)(xx + sx);
                    yy = (uint8_t)(yy + sy);
                    if (!free) goto out;
                }
            }
            if (!free || found) break;
        }
        y = ny;
    }
out:
    if (free) go_on(c);
}

/* Car_DummyTurnRight 0x417640: up to four blocks ahead, at the first road block the crossing lane of
   the far side (if this block carries it: four blocks that way, then stop looking) or else of the
   near side (four blocks the other way) is checked for moving cars; when all is free the car goes
   on. */
void car_dummy_turn_right(Car *c)
{
    int8_t dx = 0, dy = 0, axs = 0, ays = 0, bxs = 0, bys = 0;
    uint8_t want_a, want_b;
    switch (c->road_dirs) {
    case 1: dy = -1, axs = -1, want_a = 8, bxs = 1, want_b = 4; break;
    case 2: dy = 1, axs = 1, want_a = 4, bxs = -1, want_b = 8; break;
    case 4: dx = -1, ays = 1, want_a = 1, bys = -1, want_b = 2; break;
    case 8: dx = 1, ays = -1, want_a = 2, bys = 1, want_b = 1; break;
    default: want_a = want_b = 0xf; break;
    }
    bool free = true;
    uint8_t x = (uint8_t)(c->spr.x >> 22), y = (uint8_t)(c->spr.y >> 22);
    int bz = c->spr.z >> 22 & 0xff;
    for (int i = 0; i < 4; i++) {
        uint8_t ny = (uint8_t)(y + dy), nx = (uint8_t)(x + dx);
        uint8_t t = cache_b(nx, ny, bz);
        if ((t & 0x70) == 0x20) {
            uint8_t xx = nx, yy = ny;
            if ((want_b & t) == 0) {
                for (int j = 0; j < 4; j++) {
                    yy = (uint8_t)(yy + ays);
                    xx = (uint8_t)(xx + axs);
                    uint8_t t2 = cache_b(xx, yy, bz);
                    if ((t2 & 0x70) == 0x20 && (want_a & t2))
                        free = car_is_space_free(xx * 0x400000 + 0x200000, yy * 0x400000 + 0x200000, c->spr.z, c);
                    if (!free) goto out;
                }
            } else {
                for (int j = 0; j < 4 && free; j++) {
                    yy = (uint8_t)(yy + bys);
                    xx = (uint8_t)(xx + bxs);
                    uint8_t t2 = cache_b(xx, yy, bz);
                    if ((t2 & 0x70) == 0x20 && (want_b & t2))
                        free = car_is_space_free(xx * 0x400000 + 0x200000, yy * 0x400000 + 0x200000, c->spr.z, c);
                }
                i = 4;
            }
            if (!free) break;
        }
        x = nx;
        y = ny;
    }
out:
    if (free) go_on(c);
}

/* Car_DummyCheckLights 0x417980: a car waiting at a light (+0xdc = 1) on a junction block goes when
   the light shows 3 (green). Quirk kept: the check for crossing traffic before it moves reads the
   type cache at block (0, 0) of the car's layer and probes positions relative to the map's corner,
   not the car (and its result doesn't change what follows). Then +0xa6 = 0x104, +0xa8 = the junction,
   and a car at rest starts with speed 1. */
void car_dummy_check_lights(Car *c)
{
    uint16_t junction = 0;
    int bx = c->spr.x >> 22, by = c->spr.y >> 22;
    if (map_test_block_attr(2, bx, by, c->spr.z >> 22)) {
        junction = (uint8_t)lights_query(0x3a, bx, by);
        if ((int8_t)lights_query(0x34, bx, by) != 3) return;
    }
    if (c->speed != 0) return;
    uint16_t turn = 0;
    for (int n = 1; n < 13; n++) {
        uint8_t t = car_cache_at((c->spr.z >> 22) * 0x10000);
        if (!road_kind(t >> 4 & 7)) break;
        uint8_t d = t & 0xf;
        switch (c->road_dirs) {
        case 2: if (d & 8) turn = 0xff00; if (d & 4) turn = 0x100; break;
        case 4: if (d & 1) turn = 0x100; if (d & 2) turn = 0xff00; break;
        case 1: if (d & 8) turn = 0x100; if (d & 4) turn = 0xff00; break;
        default: if (d & 1) turn = 0xff00; if (d & 2) turn = 0x100; break;
        }
        int a = (uint16_t)(c->front_heading + turn) & 0x3ff;
        bool blocked = false;
        for (int k = 0; k < 12; k++)
            if (!car_is_space_free(math_sin(a) * k * 0x40, math_cos(a) * k * 0x40, c->spr.z, c)) { blocked = true; break; }
        if (blocked) break;
    }
    c->ua6 = 0x104;
    c->ua8 = (int16_t)junction;
    if (c->speed == 0) {
        c->udc = 0;
        c->speed = 1;
        c->input = c->accel;
        if (c->control != CAR_CTL_PHYSICS) c->input = info_s16(c, 0x0e);
        c->brake = 0;
        c->u124 = 0;
    }
}

/* ================================================================================== the generator */

/* Traffic_FindRecyclable 0x417b70: from 0x504f38 on, the first pool car (+0x139 = 1; a car marked at
   +0x140 is put back in the pool 10 frames later) that isn't an emergency model or 0xb / 0xc and
   either is free with nobody about it, or is an idle unscripted dummy nobody sees, and whose driver
   isn't dead (state 0xc) and wasn't seen lately. Past the table the search restarts at 0 next time. */
int traffic_find_recyclable(void)
{
    for (int16_t i = g_traffic_recycle;; i++) {
        if (i > 399) {
            g_traffic_recycle = 0;
            return -1;
        }
        Car *c = &g_cars[i];
        bool emergency = car_is_emergency_model(c->model);
        if (c->u140 > 0 && (uint32_t)c->u140 + 10 <= g_frame) {
            c->unk139 = 1;
            c->u140 = 0;
        }
        if (emergency || c->unk139 != 1 || c->model == 0xb || c->model == 0xc) continue;
        bool cand;
        if (c->status == -1) {
            bool a = c->script_line != -1 || car_is_burning(c) || c->damage < 100 || c->control != 0;
            bool b = c->status != -1 || c->script_line != -1 || c->unkec != 0 || c->active != 0 || c->sinking != 0 ||
                     car_is_burning(c) || player_is_car_view_target(i);
            cand = !(a && b);
        } else {
            cand = c->control == 0 && c->script_line == -1 && c->driver != -1 && !car_is_on_screen(c) && !car_is_burning(c);
        }
        if (!cand) continue;
        if (c->status == -1) {
            if (c->driver == -1) {
                const Ped *p = ped_get(i + PED_DRIVER_FIRST);
                if (p->state == 0xc) continue;
                if (p->health == 0 || !ped_is_visible_recent(p)) return i;
            } else {
                if (ped_get(c->driver)->state != 0xc) return i;
            }
        } else {
            const Ped *p = ped_get(c->driver);
            if (p->state != 0xc && !ped_is_visible_recent(p)) return i;
        }
    }
}

/* Traffic_RespawnCar 0x417d90 */
bool traffic_respawn_car(int n, int32_t x, int32_t y, int32_t z, int dir)
{
    int16_t h = 0;
    int8_t sx = 0, sy = 0;   /* the walk back for the junction just passed */
    switch ((int16_t)dir) {
    case 1: h = 0x200, sy = 1; break;
    case 2: h = 0, sy = -1; break;
    case 4: h = 0x300, sx = 1; break;
    case 8: h = 0x100, sx = -1; break;
    default: break;
    }
    n = (int16_t)n;
    Car *c = &g_cars[n];
    int32_t cx = (int32_t)(((uint32_t)x & 0xffc00000) + 0x200000), cy = (int32_t)(((uint32_t)y & 0xffc00000) + 0x200000);
    const SpriteInfo *si = c->spr.info;
    int w = (si ? (si->w > si->h ? si->w : si->h) : 0) >> 1;
    int t = (cy >> 16) - w, l = (cx >> 16) - w;
    for (int p = player_first(); p > -1; p = player_next(p)) {
        const int32_t *r = player_get_view_rect(p);
        bool ox = l < r[0] ? r[0] < (cx >> 16) + w : l < r[1];
        if (ox) {
            bool oy = t < r[2] ? r[2] < (cy >> 16) + w : t < r[3];
            if (oy) return false;
        }
    }
    CollBox box;
    coll_build_box(cx, cy, z - c->z_offset, c->half_w + 4, c->half_l + 4, h, c->depth, &box);
    if ((int16_t)coll_any_object_at(cx, cy, &box) != -1) return false;
    if (c->status == -1) {
        if (c->driver == -1) {
            Ped *p = ped_get(n + PED_DRIVER_FIRST);
            if (p->anim > 0 && p->state != 0xc) coll_remove(p, p->spr.unk20);
            c->driver = (int16_t)(n + PED_DRIVER_FIRST);
            ambulance_cancel_for_ped(p->id);
            if (c->unk88 == 0) c->unk88 = 1;
        } else if (c->vtype == CAR_VT_BIKE || car_info_is_convertible(n)) {
            Ped *p = ped_get(c->driver);
            if (p->health > 0) coll_remove(p, p->spr.unk20);
            ambulance_cancel_for_ped(p->id);
        }
    } else {
        coll_remove(c, c->spr.unk20);
        Ped *p = ped_get(c->driver);
        p->attach_kind = 0;
        if (p->anim != 0) ped_remove(p->id);
    }
    c->road_dirs = (uint16_t)dir;
    c->turn_delta = 0;
    c->turn_progress = 0;
    c->spr.angle = h;
    c->spr.z = z;
    c->spr.zkey = z - c->z_offset;
    c->front_heading = h;
    int32_t ky = math_cos(h) * (c->length >> 1), kx = math_sin(h) * (c->length >> 1);
    c->rear_y = ky + cy;
    c->front_x = cx - kx;
    c->script_line = -1;
    c->spr.x = cx;
    c->spr.y = cy;
    c->front_y = cy - ky;
    c->rear_x = kx + cx;
    c->status = 0;
    c->owner_status = 1;
    c->control = 0;
    c->u124 = 0;
    c->script_held = 0;
    c->u244 = 0;
    car_repair(c);
    Ped *d = ped_get(c->driver);
    d->health = 100;
    d->anim = 0;
    d->car = (int16_t)n;
    coll_insert(COLL_CAR, n, c, c->spr.unk20, c->spr.x, c->spr.y);
    if (c->vtype == CAR_VT_BIKE || car_info_is_convertible(n)) ped_create_car_driver(c);
    else if (c->vtype == CAR_VT_CAR) car_assign_cycle_remap(n);
    coll_build_box(cx, cy, z - c->z_offset, c->half_w, c->half_l, h, c->depth, &c->box);
    car_sync_physics(c);
    car_stop_physics(c);
    c->impulse_state = 0;
    c->impulse_x = 0;
    c->impulse_y = 0;
    car_update_ground(c);
    car_commit_move(c);
    /* the junction behind it (up to 10 blocks back along the road) counts as passed */
    c->ua8 = -1;
    uint8_t bx = (uint8_t)(c->spr.x >> 22), by = (uint8_t)(c->spr.y >> 22), bz = (uint8_t)(c->spr.z >> 22);
    for (int i = 0; bx != 0 && by != 0 && (cache_b(bx, by, bz) & 0xf); ) {
        if (map_test_block_attr(2, bx, by, bz)) c->ua8 = (int16_t)(uint8_t)lights_query(0x3a, bx, by);
        bx = (uint8_t)(bx + sx);
        by = (uint8_t)(by + sy);
        if (++i > 9) break;
    }
    return true;
}

/* Traffic_AngleToDir 0x418390 */
int traffic_angle_to_dir(int angle)
{
    int16_t a = (int16_t)angle;
    if (a > 0x37f || a < 0x80) return 2;
    if (a < 0x180) return 8;
    if (a < 0x280) return 1;
    return 4;
}

/* the edge scanners: a road block of the lane direction (and none across it), wide sprites (more
   than 0x40 pixels tall) only with road all round */
static bool lane_ok(uint8_t b, int dir, bool along_x)
{
    int16_t d = (int16_t)dir;
    bool valid = along_x ? d == 1 || d == 2 : d == 4 || d == 8;   /* (other directions never match) */
    return (b & 0x70) == 0x20 && valid && (b & (uint8_t)dir) && (b & (along_x ? 0xc : 3)) == 0;
}
static bool wide_ok(const Car *c, int bx, int by, int bz)
{
    return !c->spr.info || c->spr.info->h < 0x41 || traffic_is_road_3x3(bx, by, bz);
}
/* one row (vertical directions) or column (horizontal) of blocks, x / y 16.16, stepping a block */
static bool scan_edge(int car, int dir, int32_t x, int32_t y, bool along_x, int32_t end, bool top)
{
    const Car *c = &g_cars[(int16_t)car];
    do {
        if (top ? (x < 1 || y < 1) : (x < 0 || y < 0)) return false;
        int bx = x >> 22, by = y >> 22, lim = top ? 0xfe : 0xff;
        if (bx > lim || by > lim) return false;
        int32_t gz = ground(x, y, 0x10000);
        if (gz < 0) gz = 0x10000;
        uint8_t b = cache_b(bx, by, gz >> 22);
        if (lane_ok(b, dir, along_x) && wide_ok(c, bx, by, gz >> 22) && traffic_respawn_car(car, x, y, gz, dir)) return true;
        if (along_x) x += 0x400000;
        else y += 0x400000;
    } while ((along_x ? x : y) < end);
    return false;
}
/* Traffic_TrySpawnTopEdge 0x418970 (the row above the rect; x from left + 0x40) */
bool traffic_try_spawn_top(const int32_t *r, int car, int dir, int skip)
{
    if ((int16_t)dir == (int16_t)skip) return false;
    return scan_edge(car, dir, (r[0] + 0x40) * 0x10000, (r[2] - 0x40) * 0x10000, true, (r[1] - 0x40) * 0x10000, true);
}
/* Traffic_TrySpawnBottomEdge 0x418b40 (the row below) */
bool traffic_try_spawn_bottom(const int32_t *r, int car, int dir, int skip)
{
    if ((int16_t)dir == (int16_t)skip) return false;
    return scan_edge(car, dir, (r[0] + 0x40) * 0x10000, (r[3] + 0x40) * 0x10000, true, (r[1] - 0x40) * 0x10000, false);
}
/* Traffic_TrySpawnLeftEdge 0x418c80 (the column left of the rect; y from top + 0x40) */
bool traffic_try_spawn_left(const int32_t *r, int car, int dir, int skip)
{
    if ((int16_t)dir == (int16_t)skip) return false;
    return scan_edge(car, dir, (r[0] - 0x40) * 0x10000, (r[2] + 0x40) * 0x10000, false, (r[3] - 0x40) * 0x10000, false);
}
/* Traffic_TrySpawnRightEdge 0x418dc0 (the column right of it) */
bool traffic_try_spawn_right(const int32_t *r, int car, int dir, int skip)
{
    if ((int16_t)dir == (int16_t)skip) return false;
    return scan_edge(car, dir, (r[1] + 0x40) * 0x10000, (r[2] + 0x40) * 0x10000, false, (r[3] - 0x40) * 0x10000, false);
}

/* Traffic_IsRoad3x3 0x418ab0: the eight neighbours are road (the centre isn't tested) */
bool traffic_is_road_3x3(int bx, int by, int bz)
{
    int i = (bz * 0x100 + by) * 0x100 + bx;
    static const int d[8] = { 1, -1, 0x100, -0x100, -0x101, 0xff, -0xff, 0x101 };
    for (int k = 0; k < 8; k++)
        if ((car_cache_at(i + d[k]) & 0x70) != 0x20) return false;
    return true;
}

/* the fallback in the player's own direction (no skip); an unknown direction is fatal -0x4a */
static bool spawn_dir(const int32_t *r, int car, int pdir)
{
    switch ((int16_t)pdir) {
    case 1: return traffic_try_spawn_top(r, car, 1, -1);
    case 2: return traffic_try_spawn_bottom(r, car, 2, -1);
    case 4: return traffic_try_spawn_left(r, car, 4, -1);
    case 8: return traffic_try_spawn_right(r, car, 8, -1);
    default: game_fatal(-0x4a, 0x178, pdir); return false;
    }
}

/* Traffic_SpawnAroundView 0x4183d0, once a frame per player with the number of dummies near its view
   (Cars_UpdateAll): below a quota by wanted level (7; 6 at level 1, 4 at 2, 3 at 3, 2 at 4) a
   recyclable pool car goes to an edge of the view. Each call starts at the next edge of a five-step
   cycle (0 top, 1 bottom, 2 left, 3 right, 4 the player's direction first); edges spawn cars heading
   into the view (the top edge downwards, ...). With police due at the wanted level (Police_CopsForWanted)
   the edge in the player's own direction is skipped at first; the player's direction (the camera
   target's heading) is tried as a last resort, at low wanted levels. */
void traffic_spawn_around_view(int player, int near)
{
    int lvl = player_get_wanted_level(player);
    switch (lvl) {
    case 1: if (near >= 6) return; break;
    case 2: if (near >= 4) return; break;
    case 3: if (near > 2) return; break;
    case 4: if (near > 1) return; break;
    default: if (near >= 7) return; break;
    }
    int cops = police_cops_for_wanted(player);
    int car = traffic_find_recyclable();
    if ((int16_t)car == -1) return;
    g_traffic_recycle = (int16_t)(car + 1);
    uint8_t e = (uint8_t)(g_traffic_edge[player] + 1);
    g_traffic_edge[player] = e == 5 ? 0 : e;
    const int32_t *r = player_get_view_rect(player);
    CameraTarget tg = player_view_target(player);
    int pdir = traffic_angle_to_dir(tg.angle);
    int skip = cops == 0 ? -1 : pdir;
    switch (g_traffic_edge[player]) {
    case 0:
        if (!traffic_try_spawn_top(r, car, 2, skip) && !traffic_try_spawn_bottom(r, car, 1, skip) &&
            !traffic_try_spawn_left(r, car, 8, skip) && !traffic_try_spawn_right(r, car, 4, skip) && lvl < 4)
            spawn_dir(r, car, pdir);
        break;
    case 1:
        if (!traffic_try_spawn_bottom(r, car, 1, skip) && !traffic_try_spawn_left(r, car, 8, skip) &&
            !traffic_try_spawn_right(r, car, 4, skip)) {
            if (lvl < 3) {
                if ((int16_t)pdir != 1 && (int16_t)pdir != 2 && (int16_t)pdir != 4 && (int16_t)pdir != 8) {
                    game_fatal(-0x4a, 0x178, pdir);
                    traffic_try_spawn_top(r, car, 2, skip);
                    return;
                }
                if (spawn_dir(r, car, pdir)) return;
            }
            traffic_try_spawn_top(r, car, 2, skip);
        }
        break;
    case 2:
        if (traffic_try_spawn_left(r, car, 8, skip)) return;
        if (traffic_try_spawn_right(r, car, 4, skip)) return;
        if (lvl < 2 && spawn_dir(r, car, pdir)) return;
        if (!traffic_try_spawn_top(r, car, 2, skip)) traffic_try_spawn_bottom(r, car, 1, skip);
        break;
    case 3:
        if (traffic_try_spawn_right(r, car, 4, skip)) return;
        if (!spawn_dir(r, car, pdir) && !traffic_try_spawn_top(r, car, 2, skip) && !traffic_try_spawn_bottom(r, car, 1, skip))
            traffic_try_spawn_left(r, car, 8, skip);
        break;
    case 4:
        if (lvl < 3 && spawn_dir(r, car, pdir)) return;
        if (!traffic_try_spawn_top(r, car, 2, skip) && !traffic_try_spawn_bottom(r, car, 1, skip) &&
            !traffic_try_spawn_left(r, car, 8, skip))
            traffic_try_spawn_right(r, car, 4, skip);
        break;
    }
}

/* Traffic_PrimeCarPool 0x418f00: n cars of the city's shuffled model row (0x504ce0 + (style - 1) *
   200, the cycle 0x504f40 wrapping after 99) are created at block (1, 1, 1) with a driver and deleted
   at once, sound effects suspended: their slots stay reserved for the traffic (+0x139 = 1). */
void traffic_prime_car_pool(int n)
{
    int first = g_cars_count;
    Snd_SuspendSfx();
    int end = (int16_t)n + first;
    for (int i = first; (int16_t)i < end; i++) {
        int row = style_requested() - 1;
        int k = g_traffic_cycle;
        g_traffic_cycle = (int8_t)(g_traffic_cycle + 1);
        if (g_traffic_cycle > 99) g_traffic_cycle = 0;
        int model = row >= 0 && row < 3 ? g_traffic_models[row][k] : 0;
        car_delete(car_spawn_with_driver(0x400000, 0x400000, 0x400000, model));
    }
    Snd_RestoreSfx();
}

/* Traffic_InitModelTables 0x418f80: three rows of 100 car models (0x4ac108, read from the exe), each
   shuffled by swapping every entry with a random one of its row (Math_Random % 100); the cycle, the
   recycle search and the edge cycles start at 0. */
void traffic_init_model_tables(void)
{
    const uint8_t *src = exe_data(0x4ac108, sizeof g_traffic_models);
    if (!src) game_fatal(-2, 0, 0x4ac108);
    for (int i = 0; i < 300; i++) (&g_traffic_models[0][0])[i] = (uint16_t)(src[2 * i] | src[2 * i + 1] << 8);
    for (int r = 0; r < 3; r++)
        for (int i = 0; i < 100; i++) {
            int j = (int16_t)math_random() % 100;
            uint16_t t = g_traffic_models[r][i];
            g_traffic_models[r][i] = g_traffic_models[r][j];
            g_traffic_models[r][j] = t;
        }
    g_traffic_cycle = 0;
    g_traffic_recycle = 0;
    memset(g_traffic_edge, 0, sizeof g_traffic_edge);
}
