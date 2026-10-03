/* Emergency services, "sentinels" (0x419000-0x41e6ef; sentinel.h): the request queue for ambulances
   and wreck removal, the police dispatch list, the per-frame dispatcher Emergency_UpdateAll 0x419880
   (ported from the disassembly), the chasers, the warps, the AI driver Sentinel_DriveCar 0x41aed0
   with its route re-planning, stuck recovery and traffic-light override, and (at the end) the
   steering group 0x41e6f0-0x4227a0 (Sentinel_Steer 0x41f290 and the lane probes); the record table and
   Sentinel_Get / FindFree / ClearTable / Reset are ai.c's; the junction override table and its
   init / timers are lights.c's; Map_FindNearestRoad and Path_Find are path.c's. See docs/police.md. */
#include "sentinel.h"
#include "ambulance.h"
#include "car.h"
#include "carcoll.h"
#include "coll.h"
#include "dummy.h"
#include "game.h"
#include "gmath.h"
#include "lights.h"
#include "mission_obj.h"
#include "path.h"
#include "ped.h"
#include "player.h"
#include "police.h"
#include "route.h"
#include "traffic.h"
#include "train.h"
#include "trigger.h"
#include "wanted.h"
#include "stubs.h"
#include <string.h>
#include "../map.h"
#include <stdlib.h>

/* STUBS NEEDED (not ported anywhere yet; the caller's no-op result in brackets):
   int fire_engine_update(Sentinel *s);   FireEngine_Update 0x42f460 [0: don't drive on]
   void fire_engine_remove(Sentinel *s);  FireEngine_Remove 0x42e870 [nothing]
   int map_test_block_attr(int what, int bx, int by, int bz);   Map_TestBlockAttr 0x44b310 (stubs.c has a
                                          real body; declared here so this file needs no stubs.h) */
int fire_engine_update(Sentinel *s);        /* FireEngine_Update 0x42f460 */
void fire_engine_remove(Sentinel *s);       /* FireEngine_Remove 0x42e870 */
int map_test_block_attr(int what, int bx, int by, int bz);   /* Map_TestBlockAttr 0x44b310 */

/* The steering group (below) */
int sentinel_is_lane_clear(int bx, int by, int bz, Car *c, int dir);           /* 0x41e6f0 */
int sentinel_check_ahead(Sentinel *s, int bx, int by, int bz, int extra);      /* 0x41e890 */
int sentinel_check_obstacle(Sentinel *s, int bx, int by, int bz, int extra);   /* 0x41f030 */
void sentinel_steer(Sentinel *s, int bx, int by, int bz);                      /* 0x41f290 */
int sentinel_find_overtake_lane(Car *c, int bx, int by, int bz);               /* 0x421fb0 */
int sentinel_is_blocked_by_stopped_car(int bx, int by, int bz, Car *c, int dir);   /* 0x4223e0 */
void car_swap_positions(Car *a, Car *b);                                        /* 0x422500 */

/* MODULE GLOBALS */
SentRequest g_sent_requests[SENT_REQUESTS];         /* 0x50cab0 */
int16_t g_ambu_calls[AMBU_CALLS_MAX];               /* 0x505048 */
int16_t g_ambu_ncalls;                              /* 0x50584e */
PoliceDispatch g_police_dispatch[POLICE_DISPATCH_MAX];   /* 0x5058a8 */
int16_t g_police_ndispatch;                         /* 0x50f28e */
int16_t g_junction_list[JUNCTION_LIST_MAX];         /* 0x50586c */
int16_t g_junction_nlist;                           /* 0x50584a */
int16_t g_police_chasers[POLICE_CHASERS];           /* 0x505eec */
int16_t g_sent_bx, g_sent_by;                       /* 0x50caa0, 0x50caa2 */
int16_t g_sent_bz;                                  /* 0x50caa8 */
int16_t g_sent_half_len;                            /* 0x504f4c */
int16_t g_sent_node_dx, g_sent_node_dy;             /* 0x504f4a, 0x50584c */
int16_t g_sent_dest_dist;                           /* 0x505850 */
int32_t g_sent_req;                                 /* 0x504f60 */
int32_t g_sent_505848;                              /* 0x505848 (cleared by Sentinel_InitAll, never read) */
int16_t g_car_last_wreck;                           /* 0x4bde00 the last car Car_RegisterWreck queued */
int32_t g_sent_route_car;                           /* 0x75cd50 the car Sentinel_PlanRouteToTarget routes (a Car *; car id + 1 here) */

/* ---- the car module's accessors (0x40be00-0x40c0a0), as the field accesses they are ---- */
static inline bool c_turning(const Car *c) { return c->turn_delta != 0; }        /* Car_IsTurning 0x40be00 */
static inline bool c_moving(const Car *c) { return c->speed > 0; }               /* Car_IsMovingForward 0x40be60 */
static inline void c_begin_brake(Car *c) { c->brake = 1; c->thrust_in = 0; }     /* Car_BeginBrake 0x40be90 */
static inline void c_end_brake(Car *c) { c->brake = 0; }                        /* Car_EndBrake 0x40beb0 */
static inline void c_end_turn(Car *c)                                           /* Car_EndTurn 0x40c010 */
{
    c->turn_progress = 0, c->turn_delta = 0, c->turn_dirs = (int16_t)c->road_dirs;
}
static inline void c_end_turn_if_done(Car *c)                                   /* Car_EndTurnIfDone 0x40bfb0 */
{
    int p = c->turn_progress < 0 ? -c->turn_progress : c->turn_progress;
    if (c->turn_delta == 0 || (p > 0xff && (int16_t)c->road_dirs != c->turn_dirs)) c_end_turn(c);
}
static inline void c_save_pos(Car *c) { c->saved_x = c->spr.x, c->saved_y = c->spr.y; }   /* Car_SavePos 0x40c0a0 */
/* the turn every lane decision starts: from the current road direction, rate d (0x20 / -0x20) */
static void c_start_turn(Car *c, int d, int speed)
{
    c->turn_dirs = (int16_t)c->road_dirs;
    c->turn_progress = 0;
    c->turn_delta = (int16_t)d;
    c->u98 = (int16_t)d;
    c->speed = (int16_t)speed;
    c->lane_mode = 0;
}
/* the car's +0x120 (a pointer to its request record in the original): request index + 1, 0 none */
static void c_set_req(Car *c, int32_t v) { memcpy((uint8_t *)c + 0x120, &v, 4); }
static int32_t c_get_req(const Car *c) { int32_t v; memcpy(&v, (const uint8_t *)c + 0x120, 4); return v; }

/* the cached block type 0x55fab0 at block (x, y, z), linear as the original indexes it */
static inline uint8_t cache_at(int x, int y, int z) { return car_cache_at((z * 0x100 + y) * 0x100 + x); }

/* direction bits -> unit step and heading */
static void dir_step(int dirs, int *dx, int *dy)
{
    switch (dirs) {
    case 1: *dx = 0, *dy = -1; break;
    case 2: *dx = 0, *dy = 1; break;
    case 4: *dx = -1, *dy = 0; break;
    case 8: *dx = 1, *dy = 0; break;
    }
}

/* the front / rear wheel points of a car at (x, y) facing angle (half the length behind / ahead) */
static void c_set_axles(Car *c, int32_t x, int32_t y, int angle)
{
    int h = c->length >> 1;
    c->front_x = x - g_sin[angle] * h;
    c->front_y = y - g_sin[angle + 256] * h;
    c->rear_x = g_sin[angle] * h + x;
    c->rear_y = g_sin[angle + 256] * h + y;
}

/* Car_ReverseLane 0x4227a0 (the traffic module's; its only caller is Sentinel_HandleStuck): the road
   direction flipped (1 <-> 2, 4 <-> 8) with the heading of the new direction in +0x90 / +0x22c
   (other directions keep them), the turn cleared, the axle points recomputed from the sprite heading. */
static void car_reverse_lane(Car *c)
{
    c->turn_delta = 0;
    c->turn_progress = 0;
    int a = -1;
    switch (c->road_dirs) {
    case 1: c->road_dirs = 2; a = 0; break;
    case 2: c->road_dirs = 1; a = 0x200; break;
    case 4: c->road_dirs = 8; a = 0x100; break;
    case 8: c->road_dirs = 4; a = 0x300; break;
    }
    if (a >= 0) c->front_heading = (int16_t)a, c->next_heading = (int16_t)a;
    c_set_axles(c, c->spr.x, c->spr.y, c->spr.angle);
}

/* ======================================================================== the table */

/* Sentinel_InitAll 0x41abd0: the requests (own index, free, no crew), every record cleared, the
   queue and list counts 0, the junction overrides, the police car list all -1, the locations
   (route.c), the path search idle, no chasers. */
void sentinel_init_all(void)
{
    for (int i = 0; i < SENT_REQUESTS; i++) {
        SentRequest *r = &g_sent_requests[i];
        r->id = (int16_t)i;
        r->state = 0;
        r->u6 = 0;
        r->sentinel = -1;
    }
    sentinel_clear_table();
    g_junction_nlist = 0;
    g_ambu_ncrews = 0;
    g_police_ncars = 0;
    g_police_nobj = 0;
    g_ambu_ncalls = 0;
    g_police_ndispatch = 0;
    junction_init_overrides();
    for (int i = 0; i < POLICE_CARS_MAX; i++) g_police_cars[i] = -1;
    route_init_locations();
    g_path_owner = -1;
    for (int i = 0; i < POLICE_CHASERS; i++) g_police_chasers[i] = -1;
    g_sent_505848 = 0;
}

/* the station list of a record's kind (CMP locations: police 0, hospitals 6, fire stations 0x18) */
static const BlockXYZ *base_list(const Sentinel *s)
{
    switch (s->kind) {
    case SENT_AMBULANCE: return g_locations + 6;
    case SENT_POLICE: return g_locations;
    case SENT_FIRE: return g_locations + 0x18;
    case SENT_ROUTE: return NULL;   /* (the original then reads address 0) */
    default: game_fatal(-0x4a, 0x17f, s->kind); return NULL;
    }
}
/* the nearest (Chebyshev) of the 6 stations with x != 0 from block (bx, by) into +0x64 */
static void pick_nearest(Sentinel *s, int bx, int by)
{
    const BlockXYZ *b = base_list(s);
    if (!b) return;   /* port: kind 5 has no list */
    int best = 0x7fff;
    for (int i = 0; i < LOCATION_N; i++, b++) {
        if (b->x == 0) continue;
        int ax = bx - b->x, ay = by - b->y;
        if (ax < 0) ax = -ax;
        if (ay < 0) ay = -ay;
        int d = ax > ay ? ax : ay;
        if ((int16_t)d < (int16_t)best) {
            s->dest[0] = b->x, s->dest[1] = b->y, s->dest[2] = b->z;
            best = d;
        }
    }
}
/* Sentinel_PickNearestBase 0x4194e0: from the record's car */
void sentinel_pick_nearest_base(Sentinel *s)
{
    const Car *c = sentinel_car(s);
    pick_nearest(s, c->spr.x >> 22 & 0xff, c->spr.y >> 22 & 0xff);
}
/* Sentinel_PickNearestBaseTo 0x4195e0: from the block at q[2], q[3] */
void sentinel_pick_nearest_base_to(Sentinel *s, const uint8_t *q)
{
    pick_nearest(s, q[2], q[3]);
}

/* Police_AddChaser 0x4196d0: into the first free of the 4 chaser slots, state 0x32 */
void police_add_chaser(int s)
{
    for (int i = 0; i < POLICE_CHASERS; i++) {
        if (g_police_chasers[i] != -1) continue;
        Sentinel *r = sentinel_ptr(s);
        if (r) r->state = 0x32;
        g_police_chasers[i] = (int16_t)s;
        return;
    }
}

/* Police_UpdateChasers 0x419740: a chaser whose car has no driver, or whose driver isn't a player,
   goes home (car control 3, owner status 1, state 0xfe) and leaves its slot. */
void police_update_chasers(void)
{
    for (int i = 0; i < POLICE_CHASERS; i++) {
        int16_t n = g_police_chasers[i];
        if (n < 0) continue;
        Sentinel *r = sentinel_ptr(n);
        if (!r || r->car_id < 0) continue;   /* (port: Car_Get(-1)) */
        Car *c = car_get(r->car_id);
        if (c->driver >= 0 && ped_get(c->driver)->player_ctl == 1) continue;
        c->control = 3;
        r->state = 0xfe;
        c->owner_status = 1;
        for (int k = 0; k < POLICE_CHASERS; k++)
            if (g_police_chasers[k] == r->id) { g_police_chasers[k] = -1; break; }
    }
}

/* ======================================================================== warps */

/* Sentinel_WarpCar 0x419000: an AI car that nobody sees jumps to the centre of the block of (x, y, z)
   (ground height) if that block has a single road direction (on a junction: the direction bits other
   than the car's own), facing it (or its reverse if the car drives that way). 1: the car or the spot
   is on screen; 0: no single direction, or a car there that couldn't be swapped with; 2: warped (or
   swapped places with a dummy car standing there: Car_SwapPositions, undone if either box then hits
   a car). */
int sentinel_warp_car(Car *c, int32_t x, int32_t y, int32_t z)
{
    int32_t cy = (int32_t)(((uint32_t)y & 0xffc00000u) + 0x200000);
    int32_t cx = (int32_t)(((uint32_t)x & 0xffc00000u) + 0x200000);
    int dirs = cache_at(cx >> 22, cy >> 22, z >> 22) & 0xf;
    if (dirs != 0 && dirs != 1 && dirs != 2 && dirs != 4 && dirs != 8) dirs ^= c->road_dirs & 0xff;
    if (car_is_on_screen(c)) return 1;
    if (dirs != 1 && dirs != 2 && dirs != 4 && dirs != 8) return 0;
    int32_t ox = c->spr.x, oy = c->spr.y, oz = c->spr.z;
    c->spr.x = cx, c->spr.y = cy;
    int32_t gz = map_get_ground_z(g_game.map, cx, cy, z);
    c->spr.z = gz;
    if (car_is_on_screen(c)) {
        c->spr.x = ox, c->spr.y = oy, c->spr.z = oz;
        return 1;
    }
    CollHit *h = coll_query_block_all(cx, cy, c->spr.z, COLL_CAR);
    if (!h) {
        coll_remove(c, c->spr.unk20);
        int back = 0;
        switch (dirs) {
        case 1: back = 2; break;
        case 2: back = 1; break;
        case 4: back = 8; break;
        case 8: back = 4; break;
        }
        if (c->road_dirs == back) dirs = back;
        switch (dirs) {
        case 1: c->spr.angle = 0x200; break;
        case 2: c->spr.angle = 0; break;
        case 4: c->spr.angle = 0x300; break;
        case 8: c->spr.angle = 0x100; break;
        default: game_fatal(-0x127, 0, c->id);
        }
        coll_build_box(cx, cy, gz, c->half_w, c->half_l, c->spr.angle, c->depth, &c->box);
        car_sync_physics(c);
        car_stop_physics(c);
        c->impulse_state = 0;
        c->impulse_x = c->impulse_y = 0;
        c->front_heading = c->spr.angle;
        c_set_axles(c, cx, cy, c->spr.angle);
        c->spr.zkey = c->spr.z - c->z_offset;
        coll_insert(COLL_CAR, c->id, c, c->spr.unk20, cx, cy);
        coll_unlock();
        return 2;
    }
    /* a car there: only the first one of the list is looked at (the loop's "continue" result -10 is
       never produced) */
    int r;
    Car *o = (Car *)h->owner;
    c->spr.x = ox, c->spr.y = oy, c->spr.z = oz;
    if (o->control == CAR_CTL_DUMMY && !((uint16_t)o->script_line < 0x8000)) {   /* Car_IsZPositive 0x405910 */
        car_swap_positions(c, o);
        r = 2;
        if (coll_first_hit(&c->box_saved, COLL_CAR, c) != -1 || coll_first_hit(&o->box_saved, COLL_CAR, o) != -1) {
            car_swap_positions(o, c);
            r = 0;
        }
    } else {
        r = 0;
    }
    coll_unlock();
    return r;
}

/* Sentinel_WarpToNearestRoad 0x41a750: an AI car off screen moves to the nearest road block's centre
   (Map_FindNearestRoad), facing its direction (else keeping its heading), unless that spot is near a
   view or its box would hit a car or the map. The search's result is returned when the car isn't
   moved because of a view (1), 0 when blocked or not found. */
int sentinel_warp_to_nearest_road(Car *c)
{
    if (car_is_on_screen(c)) return 0;
    uint8_t q[8] = { 0 };
    q[2] = (uint8_t)(c->spr.x >> 22), q[3] = (uint8_t)(c->spr.y >> 22), q[4] = (uint8_t)(c->spr.z >> 22);
    int found = map_find_nearest_road(q);
    if (!found) return found;
    int32_t cx = q[2] * 0x400000 + 0x200000, cy = q[3] * 0x400000 + 0x200000;
    int a;
    switch (cache_at(q[2], q[3], q[4]) & 0xf) {
    case 1: a = 0x200; break;
    case 2: a = 0; break;
    case 4: a = 0x300; break;
    case 8: a = 0x100; break;
    default: a = c->spr.angle;
    }
    if (pos_is_near_any_view(cx, cy)) return found;
    c->next_heading = (int16_t)a;
    c->next_x = cx, c->next_y = cy;
    coll_build_box(cx, cy, c->next_z - c->z_offset, c->half_w, c->half_l, a, c->depth, &c->box_saved);
    coll_compute_bounds(&c->box_saved);
    if (coll_first_hit(&c->box_saved, COLL_CAR, c) == -1 && coll_map_all(&c->box_saved, 0) == -1) {
        coll_remove(c, c->spr.unk20);
        c->impulse_state = 0;
        c->impulse_x = c->impulse_y = 0;
        c_set_axles(c, c->next_x, c->next_y, c->next_heading);
        car_update_ground(c);
        car_commit_move(c);
        coll_insert(COLL_CAR, c->id, c, c->spr.unk20, c->spr.x, c->spr.y);
        return found;
    }
    car_sync_physics(c);
    return 0;
}

/* ======================================================================== requests */

/* Ambulance_RequestForPed 0x41a1a0: an ambient ped (control 0) without a request gets one at its
   block moved onto the nearest road, and joins the call queue; none found: the request is dropped. */
void ambulance_request_for_ped(int ped)
{
    if (ped_get(ped)->control != 0) return;
    SentRequest *r = &g_sent_requests[(int16_t)ped];
    if (r->state != 0) return;
    const Ped *p = ped_get(ped);
    r->state = 1;
    r->sentinel = -1;
    r->state = 2;
    r->x = (uint8_t)(p->spr.x >> 22), r->y = (uint8_t)(p->spr.y >> 22), r->z = (uint8_t)(p->spr.z >> 22);
    if (map_find_nearest_road((uint8_t *)r)) {
        g_ambu_calls[g_ambu_ncalls++] = (int16_t)ped;
        return;
    }
    r->state = 0;
}

/* the queue removal both cancels share: the entry swapped with the last one; the loop goes on with
   the next index, so the entry moved into the hole isn't looked at again (as the original) */
static void drop_from_queue(int16_t id)
{
    for (int16_t i = 0; i < g_ambu_ncalls; i++) {
        if (g_ambu_calls[i] != id) continue;
        g_ambu_ncalls--;
        g_sent_requests[id].state = 0;
        g_ambu_calls[i] = g_ambu_calls[g_ambu_ncalls];
    }
}

/* Ambulance_CancelForPed 0x41a250: only when the crew the request names has the ped among its 10
   victims: the request is freed, and a crew still on its way (state 1) is called off (state 0xff);
   otherwise the ped leaves the call queue. */
void ambulance_cancel_for_ped(int ped)
{
    int16_t id = (int16_t)ped;
    SentRequest *r = &g_sent_requests[id];
    int16_t n = r->sentinel;
    if (n < 0) return;
    Sentinel *s = sentinel_ptr(n);
    if (!s) return;
    bool found = false;
    for (int i = 0; i < 10; i++)
        if (s->victims[i] == id) found = true;
    if (!found) return;
    n = r->sentinel;
    r->state = 0;
    if (n != -1 && sentinel_ptr(n)->state == 1) {
        r->sentinel = -1;
        sentinel_ptr(n)->state = 0xff;
        return;
    }
    drop_from_queue(id);
}

/* Ambulance_ClearRequest 0x41a3d0: the same without the victim check (ids >= 0x26c are wrecks) */
void ambulance_clear_request(int id)
{
    id = (int16_t)id;
    SentRequest *r = &g_sent_requests[id];
    int16_t n = r->sentinel;
    r->state = 0;
    if (n != -1 && sentinel_ptr(n)->state == 1) {
        sentinel_ptr(n)->state = 0xff;
        r->sentinel = -1;
        return;
    }
    drop_from_queue((int16_t)id);
}

/* Car_MarkForRemoval 0x41a360: a wreck of any model but the emergency / police ones (3, 4, 5, 0xf,
   0x10, 0x20) is queued once for removal (Car_RegisterWreck) */
void car_mark_for_removal(int car)
{
    Car *c = car_get(car);
    int m = c->model;
    if (m == 4 || m == 0x20 || m == 5 || m == 0xf || m == 0x10 || m == 3 || c->status == -1) return;
    SentRequest *r = &g_sent_requests[(int16_t)car + SENT_REQ_CAR_BASE];
    if (r->state != 0) return;
    r->state = 1;
    car_register_wreck(car, c);
}

/* Car_IsMarkedForRemoval 0x41a470 */
bool car_is_marked_for_removal(int car) { return g_sent_requests[car + SENT_REQ_CAR_BASE].state != 0; }

/* Car_RegisterWreck 0x405790: request car + 0x26c at the car's block, queued */
void car_register_wreck(int car, const Car *c)
{
    g_car_last_wreck = (int16_t)car;
    int id = (int16_t)car + SENT_REQ_CAR_BASE;
    SentRequest *r = &g_sent_requests[id];
    r->sentinel = -1;
    r->state = 2;
    r->x = (uint8_t)(c->spr.x >> 22), r->y = (uint8_t)(c->spr.y >> 22), r->z = (uint8_t)(c->spr.z >> 22);
    g_ambu_calls[g_ambu_ncalls++] = (int16_t)id;
}

/* Car_TryRemoveWreck 0x405800: a queued wreck that isn't burning and that nobody sees is deleted and
   its request cleared (-2); else -1 */
int car_try_remove_wreck(SentRequest *r)
{
    Car *c = car_get(r->id - SENT_REQ_CAR_BASE);
    if (!car_is_burning(c) && !car_is_on_screen(c)) {
        car_delete(r->id - SENT_REQ_CAR_BASE);
        ambulance_clear_request(r->id);
        return -2;
    }
    return -1;
}

/* ======================================================================== route planning */

/* Sentinel_PlanRouteToTarget 0x41a9d0: the crew's current victim (+0x24[+0x21]) is assigned to it
   and, when the path search is idle, a search (mode 3) from the car's block to the victim's starts:
   either end off the road gives up (state 0xff); a failed search state 4; the car's +0x120 names the
   request, +0x64 its block, the route node counter restarts; a search in progress keeps the state to
   return to in +0x1c. */
void sentinel_plan_route_to_target(Sentinel *s)
{
    Car *c = sentinel_car(s);
    int bx = c->spr.x >> 22, by = c->spr.y >> 22, bz = c->spr.z >> 22;
    int16_t v = s->victims[s->u21];
    SentRequest *r = &g_sent_requests[v];
    bool idle = g_path_owner < 0;
    g_sent_route_car = c->id + 1;
    r->sentinel = s->id;
    if (!idle) return;
    if ((cache_at((int16_t)bx, (int16_t)by, (int16_t)bz) & 0xf) == 0 || (cache_at(r->x, r->y, r->z) & 0xf) == 0) {
        s->state = 0xff;
        return;
    }
    s->u38 = 0;
    g_path_result = (int16_t)path_find(bx, by, bz, r->x, r->y, r->z, 3, s->id & 0xff);
    if (g_path_result == 0) s->state = 4;
    c_set_req(c, v + 1);
    s->dest[0] = r->x, s->dest[1] = r->y, s->dest[2] = r->z;
    c->counter119 = 0;
    if (g_path_result == 3) s->sub = s->state;
}

/* Sentinel_ReplanRoute 0x41da60: a new search (mode 3) from the car's block to +0x64 (a police car
   in state 1 to its route node first), both ends on road; if another controller holds the search the
   car brakes instead. */
static void sentinel_replan_route(Sentinel *s)
{
    Car *c = sentinel_car(s);
    int bx = c->spr.x >> 22 & 0xff, by = c->spr.y >> 22 & 0xff, bz = c->spr.z >> 22 & 0xff;
    if (s->kind == SENT_POLICE && s->state == 1) {
        s->dest[0] = s->u0c, s->dest[1] = s->u0d, s->dest[2] = s->u0e;
    }
    int x = s->dest[0], y = s->dest[1], z = s->dest[2];
    if ((cache_at(bx, by, bz) & 0xf) == 0 || (cache_at(x, y, z) & 0xf) == 0) return;
    if (g_path_owner >= 0) {
        c->brake = 1;
        return;
    }
    s->u38 = 0;
    c->speed = 0;
    g_path_result = (int16_t)path_find(bx, by, bz, x, y, z, 3, s->id & 0xff);
    c->counter119 = 1;
    s->u14 = 0;
    s->u16 = 0;
}

/* Sentinel_RecallRoute 0x41dff0: the controller owns the path search: continue it (Path_Find with
   zeros). 0 failed: +0x4a = 1, state 0xff; 1 found: back to the state saved in +0x1c and, unless the
   crew is out (+0x20), drive the new route from its first node (+0x44 = own slot, the old one kept in
   +0x48); 2 overflow: brake, +0x4a = 1, state 0xff; 3 not done: the saved state, stopped and braking.
   (The "adiag" debug log is left out.) */
static void sentinel_recall_route(Sentinel *s)
{
    Car *c = sentinel_car(s);
    g_path_result = (int16_t)path_find(0, 0, 0, 0, 0, 0, 0, s->id & 0xff);
    switch (g_path_result) {
    case 0:
        s->u4a = 1;
        s->state = 0xff;
        s->u38 = 0;
        return;
    case 1:
        if (s->sub != 0) s->state = s->sub;
        c->counter119 = 0;
        if (s->u20 != 0) return;
        c->owner_status = 1;
        if (s->u48 == -1) s->u48 = s->route;
        s->route = s->id;
        c->speed = 0;
        c_end_brake(c);
        c->input = 0;
        s->u4e = 0;
        s->u16 = 0;
        s->u14 = 0;
        s->u38 = 0;
        c->unk88 = 1;
        return;
    case 2:
        s->u4a = 1;
        c_begin_brake(c);
        s->state = 0xff;
        s->u38 = 0;
        return;
    case 3: {
        if (s->sub != 0) s->state = s->sub;
        bool fwd = c_moving(c);
        c->input = 0;
        if (fwd) c->speed = 0;
        c_begin_brake(c);
        return;
    }
    }
}

/* Sentinel_HandleStuck 0x41dba0: the recovery of a car that stopped (+0x12 counts; kinds other than
   1 / 2 always start over at 1):
   - 1: on screen, back out of the lane if the 5 blocks behind are free road and the block diagonally
     behind on the other side is too (speed -4, a reversing turn, +0x12 = 2, +0x4e = 1), else give up;
     off screen, turn round on the spot (Car_ReverseLane);
   - 2..10: wait until the road direction is the saved one (+0x13), then done;
   - 11..15: count, stopped; 16: if the block ahead is free (the same test twice), turn into it
     (+0x12 = 0x11) else stay stopped;
   - from 17: until the turn ends or passes 0x100, at most to 50; then done (+0x58 at most 4). */
static void sentinel_handle_stuck(Sentinel *s, int bx, int by, int bz)
{
    int dx = 0, dy = 0;
    if (s->kind != SENT_POLICE && s->kind != SENT_AMBULANCE) s->u12 = 1;
    Car *c = sentinel_car(s);
    dir_step(c->road_dirs, &dx, &dy);
    int8_t n = (int8_t)s->u12;
    if (n >= 0x11) {
        if (c->turn_delta == 0 || c->turn_progress > 0x100) {
            s->u4e = 0;
            c->turn_delta = 0;
            s->u12 = 0;
            if ((int8_t)s->u58 < 5) s->u58 = 4;
            return;
        }
        s->u12 = (uint8_t)(n + 1);
        if ((int8_t)(n + 1) > 0x32) {
            s->u12 = 0;
            c->turn_delta = 0;
            c->turn_progress = 0;
            s->u4e = 0;
        }
        return;
    }
    if (n > 10) {
        if (n != 0x10) {
            s->u12 = (uint8_t)(n + 1);
            c->speed = 0;
            return;
        }
        int32_t z = c->spr.z;
        int32_t y = dy * 0x400000 + c->spr.y, x = dx * 0x400000 + c->spr.x;
        bool a = car_is_space_free(x, y, z, c);
        bool b = car_is_space_free(x, y, z, c);
        if (b && a) {
            c->turn_progress = 0;
            c->turn_dirs = (int16_t)c->road_dirs;
            c->turn_delta = 0x20;
            c->u98 = 0x20;
            c->speed = 5;
            c->lane_mode = 0;
            s->u12 = 0x11;
            s->u13 = (uint8_t)c->road_dirs;
            return;
        }
        c->speed = 0;
        return;
    }
    if (n > 1) {
        if (c->road_dirs != (int16_t)(int8_t)s->u13) {
            s->u12 = (uint8_t)(n + 1);
            return;
        }
        s->u12 = 0;
        s->u4e = 0;
        return;
    }
    if (n != 1) return;
    if (!car_is_on_any_screen(c->id)) {
        car_reverse_lane(c);
        s->u12 = 0;
        s->u4e = 0;
        return;
    }
    bool ok = true;
    int ly = by - dy, lx = bx - dx;
    int32_t py = ly * 0x400000 + 0x200000, px = lx * 0x400000 + 0x200000;
    int ty = ly;
    for (int k = 1; k < 6; k++) {
        if (lx > 0 && lx < 0xff && ty > 0 && ty < 0xff) {
            uint8_t t = car_cache_at(bx + ((bz * 0x100 + by) * 0x100 - (dy * 0x100 + dx) * k));
            if ((px >> 22) < 1 || (px >> 22) > 0xff || (py >> 22) < 1 || (py >> 22) > 0xff) break;
            if (!car_is_space_clear_of_all(px, py, c->spr.z, c) || (t & 0x70) != 0x20) ok = false;
        }
        py -= dy * 0x400000;
        px -= dx * 0x400000;
        lx -= dx;
        ty -= dy;
    }
    if (car_is_space_clear_of_all((bx - dy - dx) * 0x400000 + 0x200000, (by - dy + dx) * 0x400000 + 0x200000, c->spr.z, c) &&
        ok && (cache_at(bx - dy - dx, by - dy + dx, bz) & 0x70) == 0x20) {
        c->speed = -4;
        c->turn_dirs = (int16_t)c->road_dirs;
        c->turn_progress = 0;
        c->turn_delta = -0x20;
        s->u12 = 2;
        s->u13 = (uint8_t)c->road_dirs;
        c->unkc0 = 1;
        s->u4e = 1;
        return;
    }
    s->u12 = 0;
    s->u4e = 0;
}

/* ======================================================================== traffic lights */

/* Sentinel_OverrideLights 0x41e1c0: an emergency vehicle holds the lights of the junctions on its
   way (+0x58 its progress, +0x59 the junction's saved mode, +0x5a whether it was forced, +0x5b / +0x5c
   the junction block, +0x5e the override record):
   - 0: up to min(+0x14, 5) blocks ahead (5 on a route, none without a route node) find a crossing
     block (Map_TestBlockAttr 2); take its override (timer 0x3c, owner, the mode); unless all its
     lights are red already, force them (Lights_Command 0x39) and stop the car's input (+0xc0);
   - 1: still a crossing within that range: when it is another junction (compared as signed bytes:
     never equal past block 127, as the original), take that one too and release the first; then 2;
     none: the junction's mode back (0x36), 0;
   - 2 -> 3 once off the crossing, 3 -> 4 on the next, 4 -> 0 off it again (the mode restored: 0x36,
     or 0x39 at the car's block when it had been forced) with the car's input back.
   While it holds a junction, a slow car (speed < 8) that isn't braking, stuck or with its crew out
   speeds up unless something is 1..3 blocks ahead (Sentinel_CheckAhead). The "adiag" log is left
   out. */
void sentinel_override_lights(void *rec, Car *c)
{
    Sentinel *s = rec;
    int dx = 0, dy = 0;
    bool found = false;
    int n = 5;
    if (s->u14 < 5) n = s->u14;
    if (s->u4a < 1) {
        if (s->u0c == 0) n = -1;
    } else {
        n = 5;
    }
    dir_step(c->road_dirs, &dx, &dy);
    int x = c->spr.x >> 22, y = c->spr.y >> 22, z = c->spr.z >> 22;
    switch ((int8_t)s->u58) {
    case 0:
        for (; (int16_t)n >= 0; n--) {
            if ((int8_t)map_test_block_attr(2, x, y, z) == 1) {
                int g = (uint8_t)lights_query(LQ_GROUP, x, y);
                JunctionOvr *j = &g_junction_ovr[g];
                j->timer = 0x3c;
                j->mode = (uint8_t)lights_query(LQ_MODE, x, y);
                j->owner = (uint8_t)s->id;
                s->u59 = j->mode;
                s->u5e = (int16_t)g;
                if ((int8_t)lights_query(LQ_ALL_RED, x, y) == 0) {
                    s->u58 = 1;
                    s->u5a = s->u59;
                    lights_command(LQ_FORCE, 0, x, y);
                    s->u5b = (uint8_t)x;
                    s->u5c = (uint8_t)y;
                    c->unkc0 = 1;
                } else {
                    s->u59 = 0xff;
                    s->u58 = 1;
                }
                break;
            }
            x += dx;
            y += dy;
        }
        break;
    case 1:
        if ((int16_t)n >= 0) {
            for (int k = (n + 1) & 0xffff; k != 0; k--) {
                if ((int8_t)map_test_block_attr(2, x, y, z) == 1) found = true;
                y += dy;
                x += dx;
            }
            if (found) {
                if ((int8_t)map_test_block_attr(2, x, y, z) == 1) {
                    if ((int16_t)x != (int8_t)s->u5b && (int16_t)y != (int8_t)s->u5c && (int8_t)s->u59 >= 0) {
                        int g = (uint8_t)lights_query(LQ_GROUP, x, y);
                        JunctionOvr *j = &g_junction_ovr[(int16_t)g];
                        j->timer = 0x3c;
                        uint8_t m = (uint8_t)lights_query(LQ_MODE, x, y);
                        j->mode = m;
                        j->owner = (uint8_t)s->id;
                        s->u59 = m;
                        s->u5a = m;
                        lights_command(LQ_FORCE, 0, x, y);
                        s->u5c = (uint8_t)y;
                        s->u5b = (uint8_t)x;
                        g_junction_ovr[s->u5e].mode = 0xff;
                        s->u5e = (int16_t)g;
                    }
                    s->u58 = 2;
                }
                break;
            }
        }
        lights_command(LQ_GROUP_MODE, 0, s->u5b, s->u5c);
        s->u58 = 0;
        break;
    case 2:
        if ((int8_t)map_test_block_attr(2, x, y, z) == 0) s->u58 = 3;
        break;
    case 3:
        if ((int8_t)map_test_block_attr(2, x, y, z) == 1) s->u58 = 4;
        break;
    case 4:
        if ((int8_t)map_test_block_attr(2, x, y, z) == 0) {
            if ((int8_t)s->u59 >= 0) {
                if (s->u5a == 0) lights_command(LQ_GROUP_MODE, 0, s->u5b, s->u5c);
                else lights_command(LQ_FORCE, 0, x, y);
            }
            s->u58 = 0;
            c->input = c->accel;
        }
        break;
    }
    if ((int8_t)s->u58 > 0 && c->speed < 8 && c->brake == 0 && (int8_t)s->u12 == 0 && s->u20 == 0) {
        int r = (int16_t)sentinel_check_ahead(s, g_sent_bx, g_sent_by, g_sent_bz, 0);
        if (r == 0 || r > 3) c->speed++;
    }
}

/* ======================================================================== the dispatcher */

/* the target of a dispatch entry / criminal as a block: the car's or ped's position (q[2..4]) */
static void block_of(int kind, int id, uint8_t q[8], int line)
{
    switch (kind) {
    case 0: {
        const Car *c = car_get(id);
        q[2] = (uint8_t)(c->spr.x >> 22), q[3] = (uint8_t)(c->spr.y >> 22), q[4] = (uint8_t)(c->spr.z >> 22);
        break;
    }
    case 1: {
        const Ped *p = ped_get(id);
        q[2] = (uint8_t)(p->spr.x >> 22), q[3] = (uint8_t)(p->spr.y >> 22), q[4] = (uint8_t)(p->spr.z >> 22);
        break;
    }
    default: game_fatal(-0x4a, line, kind);
    }
}

/* a police car sent at a target block: states 0xc8, the pursuit, the destination (the nearest road
   block, if any), the distance (Chebyshev, block bytes), joined to the pursuit's cop list */
static void send_cop(Sentinel *s, int pursuit, uint8_t q[8])
{
    s->state = 0xc8;
    s->sub = 0xc8;
    s->pursuit = (int16_t)pursuit;
    if (map_find_nearest_road(q)) s->dest[0] = q[2], s->dest[1] = q[3], s->dest[2] = q[4];
    return;
}
static void cop_distance(Sentinel *s)
{
    const Car *c = sentinel_car(s);
    int ay = (int)(uint8_t)(c->spr.y >> 22) - s->dest[1], ax = (int)(uint8_t)(c->spr.x >> 22) - s->dest[0];
    if (ay < 0) ay = -ay;
    if (ax < 0) ax = -ax;
    s->dist = (uint8_t)(ax > ay ? ax : ay);
}

/* Emergency_UpdateAll 0x419880 (the decompiler fails on it; ported from the disassembly), per frame:
   1. Police_StartPursuit, Police_UpdatePursuits, Police_FlushObjectDeleteQueue;
   2. the call queue: a ped's call (id < 0x26c) gets a crew (Ambu_AssignVictim) when the emergency
      services are on, it has none and the path search is idle; a call with a crew leaves the queue
      (-1). A wreck (0x26c..0x3fb) is removed when nobody sees it (Car_TryRemoveWreck, which clears its
      own entry); the queue is then compacted;
   3. the police dispatch list: an entry without a car takes the nearest patrol car (spawning its car
      at the respawn block if the controller has none; that failing, it waits); with a car the
      controller goes for the target (states 0xc8, destination = the target's nearest road block) and
      joins the entry's pursuit; done entries (-1) are compacted out;
   4. each running pursuit whose criminal wants more units than it has (not on a train) gets patrol
      cars sent the same way, one per missing unit (none found: that unit is skipped this frame); a
      missing criminal record stops this phase;
   5. the roadblocks (g_junction_list): their timer counts down (400 while a car of theirs is on any
      screen); still running with a player wanted (level > 2): kept; else every car of it in state
      0x96 with control 3 that nobody sees is released (Cop_Release; the scan then restarts at index
      1, as the original), others leave the list; an empty roadblock is freed;
   6. Police_TickRadioReports, then (tail call) Police_UpdateChasers. */
void emergency_update_all(void)
{
    police_start_pursuit();
    police_update_pursuits();
    police_flush_object_delete_queue();

    /* 2. the call queue */
    for (int16_t i = 0; i < g_ambu_ncalls; i++) {
        int16_t id = g_ambu_calls[i];
        SentRequest *r = &g_sent_requests[id];
        if (id < SENT_REQ_CAR_BASE) {
            if (g_game.opt.emergency != 0 && r->sentinel == -1 && g_path_owner < 0) ambu_assign_victim(r);
            if (r->sentinel > -1) g_ambu_calls[i] = -1;
        } else if (id < SENT_REQUESTS) {
            car_try_remove_wreck(r);
        }
    }
    int16_t k = 0;
    for (int16_t i = 0; i < g_ambu_ncalls; i++)
        if (g_ambu_calls[i] != -1) g_ambu_calls[k++] = g_ambu_calls[i];
    g_ambu_ncalls = k;

    /* 3. the dispatch list */
    for (int16_t i = 0; i < g_police_ndispatch; i++) {
        PoliceDispatch *d = &g_police_dispatch[i];
        if (d->sentinel == -1) {
            bool ok = true;
            int32_t x = 0, y = 0;
            if (d->kind == 0) {
                const Car *c = car_get(d->target);
                x = c->spr.x, y = c->spr.y;
            } else if (d->kind == 1) {
                const Ped *p = ped_get(d->target);
                x = p->spr.x, y = p->spr.y;
            } else {
                game_fatal(-0x4a, 0x1c3, d->kind);
                ok = false;
            }
            if (ok) d->sentinel = (int16_t)police_find_nearest_car(x, y);
            if (d->sentinel > -1 && sentinel_ptr(d->sentinel)->car_id == -1 &&
                (int16_t)police_spawn_car_at_target(d->sentinel) == -1)
                d->sentinel = -1;
        }
        int16_t n = d->sentinel;
        if (n == -1) continue;
        Sentinel *s = sentinel_ptr(n);
        if (!s) continue;
        uint8_t q[8] = { 0 };
        block_of(d->kind, d->target, q, 0x1c4);
        send_cop(s, d->pursuit, q);
        d->active = -1;
        Pursuit *p = &g_pursuits[d->pursuit];
        p->cops[p->ncops] = s->id;
        p->ncops++;
        cop_distance(s);
    }
    k = 0;
    for (int16_t i = 0; i < g_police_ndispatch; i++)
        if (g_police_dispatch[i].active != -1) g_police_dispatch[k++] = g_police_dispatch[i];
    g_police_ndispatch = k;

    /* 4. the pursuits' missing units; `found` starts as the compacted dispatch count (as the
       original's register), which only matters for a criminal of an unknown kind (fatal) */
    int16_t found = k;
    for (int pi = 0; pi < PURSUITS; pi++) {
        Pursuit *p = &g_pursuits[pi];
        if (p->active <= 0 || p->criminal <= -1) continue;
        Criminal *cr = police_get_criminal(p->criminal);
        if (!cr) break;
        if (cr->cops <= p->ncops || cr->kind == 2) continue;
        for (int16_t u = p->ncops; u < cr->cops; u++) {
            int32_t x, y;
            if (cr->kind == 0) {
                const Car *c = car_get(cr->car);
                x = c->spr.x, y = c->spr.y;
                found = (int16_t)police_find_nearest_car(x, y);
            } else if (cr->kind == 1) {
                const Ped *pd = ped_get(cr->ped);
                x = pd->spr.x, y = pd->spr.y;
                found = (int16_t)police_find_nearest_car(x, y);
            } else {
                game_fatal(-0x4a, 0x1c5, cr->kind);
            }
            if (found <= -1) continue;
            Sentinel *s = sentinel_ptr(found);
            if (s->car_id == -1 && (int16_t)police_spawn_car_at_target(found) == -1) {
                found = -1;
                continue;
            }
            if (!s) continue;
            uint8_t q[8] = { 0 };
            block_of(cr->kind, cr->kind == 0 ? cr->car : cr->ped, q, 0x1c6);
            send_cop(s, p->id, q);
            cop_distance(s);
            p->cops[p->ncops] = s->id;
            p->ncops++;
        }
    }

    /* 5. the roadblocks */
    for (int16_t i = 0; i < g_junction_nlist; i++) {
        int16_t jn = g_junction_list[i];
        if (jn <= -1) continue;
        if (jn >= JUNCTION_OVR_MAX) game_fatal(-0x91, 0xa6, jn);
        JunctionOvr *j = &g_junction_ovr[jn];
        int16_t *cars = (int16_t *)(void *)j->u12;
        if (--j->u10 > 0 && player_any_wanted()) continue;
        for (int16_t m = 0; m < j->u5a; m++) {
            if (cars[m] != -1 && car_is_on_any_screen(car_get(cars[m])->id)) {
                j->u10 = 400;
                break;
            }
        }
        if (j->u10 > 0 && player_any_wanted()) continue;
        for (int16_t m = 0; m < j->u5a; m++) {
            if (cars[m] == -1) continue;
            Car *c = car_get(cars[m]);
            Sentinel *s = sentinel_ptr(c->sentinel);
            if (!s) continue;
            if (s->state == 0x96 && c->control == 3) {
                if (!car_is_on_screen(c)) {
                    cop_release(s);
                    m = 0;   /* (then incremented: the scan restarts at 1) */
                }
            } else {
                cars[m] = -1;
            }
        }
        int16_t kept = 0;
        if (j->u5a < 0) j->u5a = 0;
        for (int16_t m = 0; m < j->u5a; m++)
            if (cars[m] != -1) cars[kept++] = cars[m];
        j->u5a = kept;
        if (kept == 0) {
            j->u0e = 0;
            j->u5a = 0;
            g_junction_list[i] = -1;
        }
    }
    k = 0;
    for (int16_t i = 0; i < g_junction_nlist; i++)
        if (g_junction_list[i] != -1) g_junction_list[k++] = g_junction_list[i];
    g_junction_nlist = k;

    police_tick_radio_reports();
    police_update_chasers();
}

/* ======================================================================== the driver */

/* a lane decision of the wrong-way / overshoot logic: turn by d toward the lane, then either drive
   on (lane 2 dir blocked by a stopped car: brake off) or brake */
static void ww_turn(Car *c, int d, int bx, int by, int bz, int bdir)
{
    c_start_turn(c, d, 2);
    if (sentinel_is_blocked_by_stopped_car(bx, by, bz, c, bdir)) c_end_brake(c);
    else c_begin_brake(c);
}
/* ... or, the lane being taken, a slower turn (speed 5) whose success (the lane beside clear) moves
   on to the next route node */
static void ww_soft(Sentinel *s, Car *c, int d, int clear)
{
    c->lane_mode = 0;
    c->speed = 5;
    c->turn_progress = 0;
    c->turn_dirs = (int16_t)c->road_dirs;
    c->turn_delta = (int16_t)d;
    c->u98 = (int16_t)d;
    if (clear == 1) {
        c->counter119++;
        s->u16 = 0;
        s->u14 = 0;
    } else {
        c_begin_brake(c);
    }
}
/* the lane mode change of the wrong-way logic: the car's position saved */
static void ww_lane(Car *c, int mode)
{
    c->lane_mode = (int16_t)mode;   /* Car_SetLaneMode 0x40c060 */
    c->saved_x = c->spr.x;
    c->saved_y = c->spr.y;
}

/* Sentinel_DriveCar 0x41aed0 (and its thunk 0x42e860): one step of a car driven by a sentinel (car
   +0xd8), from Cars_UpdateAll for control 2 / 9 / 10 and 3 (when owned):
   1. the controller holding the path search only continues it (Sentinel_RecallRoute);
   2. state 0xff (dismissed): off screen and not burning the record goes (ambulance Ambu_Remove,
      police state 0xfe, fire engine FireEngine_Remove; dummies stay), else the car brakes to a stop;
   3. the look-ahead (0x20 ambulances, 0x24 police, the car's half length otherwise) gives the block
      ahead; the car's block goes to g_sent_bx / by / bz; a police car of a pursuit measures its
      distance to the criminal (+0x46) and, after a car criminal, counts down its car's +0x8e;
   4. on a route (+0x4a = 0): the current node (+0x44 slot, node +0x119 of the car) into +0xc..+0xe,
      the distances to it (+0x14, minimum +0x16) and to +0x64 (g_sent_dest_dist), the road
      directions around;
   5. the kind's handler (Ambu_Update, Cop_Update, FireEngine_Update, Dummy_Update): 0 ends the step,
      as does the path search being given to this controller;
   6. off a route (+0x4a > 0): Sentinel_Steer, a car that stopped off screen snaps to its road heading,
      a pursuer warps after a criminal 12 blocks from its destination, a car off the road warps back;
   7. on a route: reaching a node advances (the last: a route follower starts over, others U-turn
      or stop), the turns toward the next node (with lane and stopped-car checks), the stuck recovery,
      the warps back onto the road, the wrong-way and overshoot corrections, the dummies clearing
      12 blocks ahead of traffic, the others 2 blocks; turns at junctions, look-ahead for obstacles,
      overtaking and the lane changes; the speed. */
void sentinel_drive_car(Car *c)
{
    int16_t sn = c->sentinel;
    if (sn < 0 || sn > 0x80) return;
    Sentinel *s = sentinel_ptr(sn);
    if (!s) return;
    g_sent_req = c_get_req(c);
    if (c->control != 3 || c->horn != 0) c->unkc0 = 1;
    if (g_path_owner == s->id) {
        sentinel_recall_route(s);
        return;
    }
    if (s->state == 0xff) {
        if (!car_is_on_screen(c) && !car_is_burning(c)) {
            switch (s->kind) {
            case SENT_AMBULANCE: ambu_remove(s); return;
            case SENT_POLICE: s->state = 0xfe; return;
            case SENT_FIRE: fire_engine_remove(s); return;
            case SENT_DUMMY: return;
            default: game_fatal(-0x4a, 0x180, s->kind); return;
            }
        }
        if (!c_moving(c)) return;
        c_begin_brake(c);
        c->accel = 0;
        c->input = 0;
        return;
    }

    /* 3. look-ahead */
    int wide = 0x20, narrow = 0x40;   /* [0x40] / [0x1c]: the turn speed flag (0x40: tight, speed 2) */
    switch (s->kind) {
    case SENT_AMBULANCE: g_sent_half_len = 0x20; break;
    case SENT_POLICE: g_sent_half_len = 0x24; break;
    case SENT_ROUTE: case SENT_DUMMY: case SENT_FIRE: g_sent_half_len = c->half_l; break;
    default: game_fatal(-0x4a, 0x181, s->kind);
    }
    int dx = 0, dy = 0, back = 0;
    switch (c->road_dirs) {
    case 1: dx = 0, dy = -1, back = 2; break;
    case 2: dx = 0, dy = 1, back = 1; break;
    case 4: dx = -1, dy = 0, back = 8; break;
    case 8: dx = 1, dy = 0, back = 4; break;
    }
    int px = c->spr.x >> 16, py = c->spr.y >> 16;
    int16_t bzw = (int16_t)((int16_t)(c->spr.z >> 16) >> 6);
    g_sent_bz = bzw;
    uint8_t ay = (uint8_t)((dy * g_sent_half_len + py) >> 6);
    uint8_t ax = (uint8_t)((dx * g_sent_half_len + px) >> 6);
    g_sent_bx = (int16_t)((int16_t)px >> 6);
    g_sent_by = (int16_t)((int16_t)py >> 6);
    uint8_t bzb = (uint8_t)bzw;
    int tx = 0, ty = 0, tz = 0;   /* the criminal's block ([0x34], [0x38], [0x44]) */
    if (s->kind == SENT_POLICE && s->pursuit != -1 && g_pursuits[s->pursuit].criminal > -1) {
        Criminal *cr = police_get_criminal(g_pursuits[s->pursuit].criminal);
        if (!cr) return;
        switch (cr->kind) {
        case 0: {
            const Car *t = car_get(cr->car);
            tx = (uint8_t)(t->spr.x >> 22), ty = (uint8_t)(t->spr.y >> 22), tz = (uint8_t)(t->spr.z >> 22);
            break;
        }
        case 1: {
            const Ped *p = ped_get(cr->ped);
            tx = p->spr.x >> 22, ty = p->spr.y >> 22, tz = p->spr.z >> 22;
            break;
        }
        case 2: {
            /* Ref_GetKind1PosRect 0x45fb60: the ridden train's position (Train_Command 7) */
            train_command(7, cr->train);
            const TrainBoardInfo *r = train_get_board_info();
            tx = r->x >> 22, ty = r->y >> 22, tz = r->z >> 22;
            break;
        }
        }
        int ey = g_sent_by - (int16_t)ty, ex = g_sent_bx - (int16_t)tx;
        if (ey < 0) ey = -ey;
        if (ex < 0) ex = -ex;
        s->dist = (uint8_t)(ex > ey ? ex : ey);
        if (cr->kind == 0 && s->u14 < 7) {
            Car *own = sentinel_car(s);
            if (own->u8e > 0) own->u8e--;
        }
    }

    /* 4. the route node */
    uint8_t next_x = 0, next_y = 0;          /* [0x30], [0x3c] */
    uint16_t node_dirs = 0, side_r = 0, side_l = 0, ahead_t = 0;   /* [0x60], [0x54], [0x58], [0x5c] */
    if (s->u4a == 0) {
        int cnt = c->counter119;
        const uint8_t *node = &g_path_slots[s->route][cnt * 3];
        s->u0c = node[0], s->u0d = node[1], s->u0e = node[2];
        /* quirk: whether a next node exists is read from the slot of the controller's own number,
           its coordinates from the route's slot (the two are the same for every caller but a route
           follower that keeps another slot) */
        if (g_path_slots[s->id][(cnt + 1) * 3] == 0) {
            g_sent_node_dx = 0;
            g_sent_node_dy = 0;
        } else {
            g_sent_node_dx = (int16_t)(node[0] - node[3]);
            g_sent_node_dy = (int16_t)(node[1] - node[4]);
        }
        int ey = g_sent_by - node[1], ex = g_sent_bx - node[0];
        if (ey < 0) ey = -ey;
        if (ex < 0) ex = -ex;
        int16_t d = (int16_t)(ex > ey ? ex : ey);
        s->u14 = d;
        if (d < s->u16) s->u16 = d;
        if (node[0] == 0) s->u16 = 0;
        if (s->u16 == 0) {
            s->u16 = d;
            s->u10 = 0;
            s->u11 = 0;
        }
        ey = g_sent_by - s->dest[1], ex = g_sent_bx - s->dest[0];
        if (ey < 0) ey = -ey;
        if (ex < 0) ex = -ex;
        g_sent_dest_dist = (int16_t)(ex > ey ? ex : ey);
        node_dirs = cache_at(node[0], node[1], node[2]) & 0xf;
        side_r = cache_at((uint8_t)(g_sent_bx + dy), (uint8_t)(g_sent_by - dx), bzb) & 0xf;
        side_l = cache_at((uint8_t)(g_sent_bx - dy), (uint8_t)(g_sent_by + dx), bzb) & 0xf;
        ahead_t = cache_at(ax, ay, bzb);
        next_x = node[3];
        next_y = node[4];
    }
    uint8_t cur_t = cache_at(g_sent_bx, g_sent_by, g_sent_bz);    /* [0x64] */
    int curdirs = cur_t & 0xf;                                    /* [0x2c] */
    uint8_t slope = cur_t >> 7;                                   /* [0x7c] */
    int dz = 0;
    if (g_sent_bz < 4) {
        /* Map_GetBlockInfo_thunk 0x471940 jumps to Map_GetTypeMap: its result is what is tested */
        uint32_t tm = map_get_type_map(g_game.map, (uint8_t)(g_sent_bx + dx), (uint8_t)(g_sent_by + dy), (uint8_t)(g_sent_bz + 1));
        if (path_is_down_ramp(tm, curdirs)) dz = 1;
    }
    if (dz == 0 && slope && (cache_at((uint8_t)(g_sent_bx + dx), (uint8_t)(g_sent_by + dy), (uint8_t)(g_sent_bz - 1)) & 0xf)) dz = -1;
    uint8_t ahead2_t = car_cache_at(((dz + g_sent_bz) * 0x100 + g_sent_by + dy) * 0x100 + g_sent_bx + dx);   /* [0x68] */

    /* 5. the kind's handler */
    int r;
    switch (s->kind) {
    case SENT_AMBULANCE:
        r = (int16_t)ambu_update(s);
        if (g_path_owner == s->id || r == 0) return;
        break;
    case SENT_POLICE:
        r = (int16_t)cop_update(s);
        if (g_path_owner == s->id || r == 0) return;
        break;
    case SENT_FIRE:
        r = (int16_t)fire_engine_update(s);
        if (g_path_owner == s->id || r == 0) return;
        break;
    case SENT_DUMMY:
        r = (int16_t)dummy_update((uint8_t *)s);
        if (g_path_owner == s->id || r == 0 || c->driver == -1) return;
        break;
    default:
        game_fatal(-0x4a, 0x182, s->kind);
    }

    /* 6. off a route */
    if (s->u4a > 0) {
        sentinel_steer(s, ax, ay, bzb);
        if (!car_is_on_any_screen(c->id) && c->speed == 0 && (s->state < 200 || s->state > 0xd1)) {
            switch (c->road_dirs) {
            case 1: c->spr.angle = 0x200; break;
            case 2: c->spr.angle = 0; break;
            case 4: c->spr.angle = 0x300; break;
            case 8: c->spr.angle = 0x100; break;
            }
            c->turn_delta = 0;
            s->u12 = 0;
        }
        if (s->pursuit == -1 || g_pursuits[s->pursuit].criminal <= -1) return;
        int ey = s->dest[1] - (int16_t)ty, ex = s->dest[0] - (int16_t)tx;
        if (ey < 0) ey = -ey;
        if (ex < 0) ex = -ex;
        if ((ex > ey ? ex : ey) >= 0xc) {
            int w = (int16_t)sentinel_warp_car(c, s->dest[0] << 22, s->dest[1] << 22, s->dest[2] << 22);
            if (w == 0 || w == 2) {
                s->dest[2] = (uint8_t)tz;
                if (w == 2) s->u14 = 0, s->u16 = 0;
                s->dest[0] = (uint8_t)tx;
                s->dest[1] = (uint8_t)ty;
            }
        }
        if (curdirs != 0) return;
        uint8_t q[8] = { 0 };
        q[2] = (uint8_t)(c->spr.x >> 22), q[3] = (uint8_t)(c->spr.y >> 22), q[4] = (uint8_t)(c->spr.z >> 22);
        if (!map_find_nearest_road(q)) return;
        int w = (int16_t)sentinel_warp_car(c, q[2] << 22, q[3] << 22, q[4] << 22);
        if (w < 0) return;
        if (w <= 1) {
            sentinel_warp_to_nearest_road(c);
            return;
        }
        if (w == 2) c->speed++;   /* Car_IncSpeed 0x40bf90 */
        return;
    }

    /* 7. on a route: the node reached? */
    if (s->u14 <= 2) {
        bool ahead_on = true;   /* [0x4c] */
        if (s->u14 > 0) {
            if ((ay == s->u0d && c->road_dirs >= 4) || (ax == s->u0c && c->road_dirs <= 2)) ahead_on = true;
            else if ((ay == s->u0d && c->road_dirs <= 2) || (ax == s->u0c && c->road_dirs >= 4)) ahead_on = false;
        }
        if ((ax == s->u0c && ay == s->u0d && bzb == s->u0e) ||
            (!ahead_on && ((ahead_t & node_dirs) & 0xf) != 0 && c->turn_delta == 0) ||
            (g_sent_bx == s->u0c && g_sent_by == s->u0d)) {
            s->u14 = 0;
            s->u16 = 0;
            s->u12 = 0;
            c->counter119++;
            s->u1a = 0;
            if (next_x == 0) {
                if (s->kind == SENT_ROUTE) {
                    /* a route follower starts its route over: the destination is the first node */
                    c->counter119 = 0;
                    const uint8_t *n0 = &g_path_slots[s->route][0];
                    s->dest[0] = n0[0], s->dest[1] = n0[1], s->dest[2] = n0[2];
                } else {
                    if (c->road_dirs & curdirs) return;
                    if (curdirs & back) return;
                    /* the last node, the road going on sideways only: turn round */
                    c->turn_progress = 0;
                    c->turn_dirs = (int16_t)c->road_dirs;
                    c->turn_delta = 0x20;
                    c->u98 = 0x20;
                    c->speed = 2;
                    c->lane_mode = 0;
                    s->u4e = 2;
                }
            } else {
                if ((g_sent_dest_dist <= 1 && ((cache_at(next_x, next_y, s->u0e) & 0xf) & back) == back) ||
                    (g_sent_dest_dist <= 5 && !car_check_ahead(next_x << 22, next_y << 22, c))) {
                    c->counter119++;
                    s->u14 = 0;
                    s->u16 = 0;
                    return;
                }
                if (s->kind == SENT_ROUTE && next_x == s->u0c && next_y == s->u0d) {
                    c->counter119++;
                    s->u14 = 0;
                    s->u16 = 0;
                    if (ahead_on && ++s->u0f == 4) {
                        s->state = 2;
                        s->u0f = 0;
                    }
                }
                int adx = next_x - ax, ady = next_y - ay;
                if (adx < 0) adx = -adx;
                if (ady < 0) ady = -ady;
                int16_t dxa = (int16_t)adx, dya = (int16_t)ady;
                int turnflag;
                if (dya + dxa == 1) {
                    c_end_turn(c);
                    if (c->u8e == 0 && c->speed > c->cruise) c->speed = c->cruise;   /* Car_SetCruiseSpeed 0x40bf80 */
                    turnflag = narrow;
                } else {
                    if (c->turn_delta == 0) {
                        c_end_turn(c);
                        if (c->u8e == 0 && c->speed > c->cruise) c->speed = c->cruise;
                    }
                    turnflag = wide;
                }
                int sp = turnflag == narrow ? 2 : 5;
                switch (c->road_dirs) {
                case 1:
                    if (dxa < dya || dxa < 2) break;
                    if (next_x > ax) {
                        c_start_turn(c, -0x20, sp);
                        if (sentinel_is_lane_clear(ax, ay, bzb, c, 8) != 1 || !sentinel_is_blocked_by_stopped_car(ax, ay, bzb, c, 4))
                            c_begin_brake(c);
                    }
                    if (next_x < ax) {
                        c_start_turn(c, 0x20, sp);
                        if (sentinel_is_lane_clear(ax, ay, bzb, c, 4) != 1 || !sentinel_is_blocked_by_stopped_car(ax, ay, bzb, c, 8))
                            c_begin_brake(c);
                    }
                    break;
                case 2:
                    if (dxa < dya || dxa < 2) break;
                    if (next_x > ax) {
                        c_start_turn(c, 0x20, sp);
                        if (sentinel_is_lane_clear(ax, ay, bzb, c, 8) != 1 || !sentinel_is_blocked_by_stopped_car(ax, ay, bzb, c, 4))
                            c_begin_brake(c);
                    }
                    if (next_x < ax) {
                        c_start_turn(c, -0x20, sp);
                        if (sentinel_is_lane_clear(ax, ay, bzb, c, 4) != 1 || !sentinel_is_blocked_by_stopped_car(ax, ay, bzb, c, 8))
                            c_begin_brake(c);
                    }
                    break;
                case 4:
                    if (dya < dxa || dya < 2) break;
                    if (next_y > ay) {
                        c_start_turn(c, 0x20, sp);
                        if (sentinel_is_lane_clear(ax, ay, bzb, c, 2) != 1 || !sentinel_is_blocked_by_stopped_car(ax, ay, bzb, c, 1))
                            c_begin_brake(c);
                    }
                    if (next_y < ay) {
                        c_start_turn(c, -0x20, sp);
                        if (sentinel_is_lane_clear(ax, ay, bzb, c, 1) != 1 || !sentinel_is_blocked_by_stopped_car(ax, ay, bzb, c, 2))
                            c_begin_brake(c);
                    }
                    break;
                case 8:
                    if (dya < dxa || dya < 2) break;
                    if (next_y > ay) {
                        c_start_turn(c, -0x20, sp);
                        if (sentinel_is_lane_clear(ax, ay, bzb, c, 2) != 1 || !sentinel_is_blocked_by_stopped_car(ax, ay, bzb, c, 1))
                            c_begin_brake(c);
                    }
                    if (next_y < ay) {
                        c_start_turn(c, 0x20, sp);
                        if (sentinel_is_lane_clear(ax, ay, bzb, c, 1) != 1 || !sentinel_is_blocked_by_stopped_car(ax, ay, bzb, c, 2))
                            c_begin_brake(c);
                    }
                    break;
                }
            }
        }
    }

    c_end_turn_if_done(c);
    if (s->u0c == 0 && !c_turning(c) && s->u4e == 0) c_begin_brake(c);
    if ((s->kind <= 2 || s->kind == SENT_FIRE) && (int8_t)s->u12 > 0) sentinel_handle_stuck(s, g_sent_bx, g_sent_by, g_sent_bz);

    /* off the road (no direction bits, not a slope; police excepted): back onto it */
    if ((cur_t & 0xf) == 0 && s->kind != SENT_POLICE && curdirs == 0 && !slope) {
        if (s->kind == SENT_DUMMY) {
            uint8_t q[8] = { 0 };
            q[2] = (uint8_t)(c->spr.x >> 22), q[3] = (uint8_t)(c->spr.y >> 22), q[4] = (uint8_t)(c->spr.z >> 22);
            if (map_find_nearest_road(q)) {
                int w = (int16_t)sentinel_warp_car(c, q[2] << 22, q[3] << 22, q[4] << 22);
                if (w >= 0) {
                    if (w <= 1) sentinel_warp_to_nearest_road(c);
                    else if (w == 2) c->speed++;
                }
            }
        } else {
            int w = (int16_t)sentinel_warp_car(c, s->dest[0] << 22, s->dest[1] << 22, s->dest[2] << 22);
            if (w == 0) {
                if (!sentinel_warp_to_nearest_road(c)) s->state = 0xff;
            } else if (w == 1) {
                sentinel_warp_to_nearest_road(c);
            }
        }
    }

    /* the turn-round (+0x4e 2 -> 3 -> 0) and the wrong-way / overshoot flags */
    switch ((int8_t)s->u4e) {
    case 0: {
        if (g_sent_dest_dist <= 3 && s->u16 < s->u14) c_begin_brake(c);
        if (!c_turning(c) && curdirs == back) {
            int16_t dyn = (int16_t)(s->u0d - g_sent_by), dxn = (int16_t)(s->u0c - g_sent_bx);
            if ((c->road_dirs == 1 && dyn > 0) || (c->road_dirs == 2 && dyn < 0) ||
                (c->road_dirs == 4 && dxn > 0) || (c->road_dirs == 8 && dxn < 0))
                s->u12 = 1;
        }
        if (s->u14 > s->u16 + 5) {
            if (curdirs == back) s->u12 = 1;
            else sentinel_replan_route(s);
        }
        if (ax != s->u0c && ay != s->u0d && curdirs == back) s->u1a = 1;
        break;
    }
    case 2:
        if (!c_turning(c)) s->u4e = 3;
        break;
    case 3:
        if (c->spr.angle % 256 == 0 || !c_moving(c)) s->u4e = 0;
        break;
    }

    /* the wrong way or past the node: get to the lane that leads there */
    if (s->u0c != 0 && !c_turning(c) && c_moving(c)) {
        if (s->u1a == 1 || s->kind == SENT_ROUTE) {
            sentinel_check_ahead(s, g_sent_bx - 2 * dx + dy, g_sent_by - 2 * dy - dx, g_sent_bz, 2);
            sentinel_check_ahead(s, g_sent_bx - 2 * dx - dy, g_sent_by - 2 * dy + dx, g_sent_bz, 2);
        }
        if (s->u14 < 10 || curdirs == back) {
            c->uc4 = 8;
            int nx = s->u0c, ny = s->u0d;
            int adx = g_sent_bx - nx, ady = g_sent_by - ny;
            if (adx < 0) adx = -adx;
            if (ady < 0) ady = -ady;
            int bx = g_sent_bx, by = g_sent_by, bz = g_sent_bz;
            switch (c->road_dirs) {
            case 1:
                if ((int16_t)ady == 0) {
                    if (nx < bx) {
                        if (sentinel_is_lane_clear(bx, by, bz, c, 4) == 1) ww_turn(c, 0x20, nx, by, bz, 8);
                        else ww_soft(s, c, 0x20, sentinel_is_lane_clear(bx, by - 1, bz, c, 4));
                    } else if (nx > bx) {
                        if (sentinel_is_lane_clear(bx, by, bz, c, 8) == 1) ww_turn(c, -0x20, nx, by, bz, 4);
                        else ww_soft(s, c, -0x20, sentinel_is_lane_clear(bx, by - 1, bz, c, 8));
                    }
                } else if (c->lane_mode <= 2) {
                    if (nx < bx && sentinel_check_ahead(s, bx - 1, by + 2, bz, 2) == 0) ww_lane(c, 4);
                    else if (nx > bx && sentinel_check_ahead(s, bx + 1, by + 2, bz, 2) == 0) ww_lane(c, 3);
                }
                break;
            case 2:
                if ((int16_t)ady == 0) {
                    if (nx < bx) {
                        if (sentinel_is_lane_clear(bx, by, bz, c, 4) == 1) ww_turn(c, -0x20, nx, by, bz, 8);
                        else ww_soft(s, c, -0x20, sentinel_is_lane_clear(bx, by + 1, bz, c, 4));
                    } else if (nx > bx) {
                        if (sentinel_is_lane_clear(bx, by, bz, c, 8) == 1) ww_turn(c, 0x20, nx, by, bz, back);
                        else ww_soft(s, c, 0x20, sentinel_is_lane_clear(bx, by + 1, bz, c, 8));
                    }
                } else if (c->lane_mode <= 2) {
                    if (nx > bx && sentinel_check_ahead(s, bx + 1, by - 2, bz, 2) == 0) ww_lane(c, 4);
                    else if (nx < bx && sentinel_check_ahead(s, bx - 1, by - 2, bz, 2) == 0) ww_lane(c, 3);
                }
                break;
            case 4:
                if ((int16_t)adx == 0) {
                    if (ny < by) {
                        if (sentinel_is_lane_clear(bx, by, bz, c, 1) == 1) ww_turn(c, -0x20, bx, ny, bz, 2);
                        else ww_soft(s, c, -0x20, sentinel_is_lane_clear(bx - 1, by, bz, c, 1));
                    }
                    if (ny > by) {
                        if (sentinel_is_lane_clear(bx, by, bz, c, 2) == 1) ww_turn(c, 0x20, bx, ny, bz, 1);
                        else ww_soft(s, c, 0x20, sentinel_is_lane_clear(bx - 1, by, bz, c, 2));
                    }
                } else if (c->lane_mode <= 2) {
                    if (ny > by && sentinel_check_ahead(s, bx + 2, by + 1, bz, 2) == 0) ww_lane(c, 4);
                    else if (ny < by && sentinel_check_ahead(s, bx + 2, by - 1, bz, 2) == 0) ww_lane(c, 3);
                }
                break;
            case 8:
                if ((int16_t)adx == 0) {
                    if (ny < by) {
                        if (sentinel_is_lane_clear(bx, by, bz, c, 1) == 1) ww_turn(c, 0x20, bx, ny, bz, 2);
                        else ww_soft(s, c, 0x20, sentinel_is_lane_clear(bx + 1, by, bz, c, 1));
                    }
                    if (ny > by) {
                        if (sentinel_is_lane_clear(bx, by, bz, c, 2) == 1) ww_turn(c, -0x20, bx, ny, bz, 1);
                        else ww_soft(s, c, -0x20, sentinel_is_lane_clear(bx + 1, by, bz, c, 2));
                    }
                } else if (c->lane_mode <= 2) {
                    if (ny > by && sentinel_check_ahead(s, bx - 2, by + 1, bz, 4) == 0) ww_lane(c, 3);
                    else if (ny < by && sentinel_check_ahead(s, bx - 2, by - 1, bz, 4) == 0) ww_lane(c, 4);
                }
                break;
            }
        }
    }

    /* clear the way: a dummy 12 blocks ahead (Car_RemoveInSquare radius 5), the others its block */
    if (s->kind == SENT_DUMMY) {
        int x = g_sent_bx, y = g_sent_by;
        for (int k = 0; k < 12; k++, x += dx, y += dy)
            car_remove_in_square(x << 6, y << 6, g_sent_bz << 6, 5, c->id, 0);
    } else {
        car_remove_in_square(g_sent_bx << 6, g_sent_by << 6, g_sent_bz << 6, 2, c->id, 1);
    }

    int16_t lc = 0;                 /* [0x1c] what is ahead (Sentinel_CheckAhead: blocks to it, 0 none) */
    uint8_t side_r_res = 1;         /* [0x3c] the same for the lane on the right ... */
    uint8_t side_l_res = 1;         /* [0x34] ... and on the left */
    bool turning_case = false;
    if (c_turning(c)) {
        /* in a turn: the lane it turns into (as the original: the mask doesn't depend on the turn's
           side, since Car_IsTurning is always 1 here: 4 / 8 / 2 / 1 for previous directions 1 / 2 / 4 / 8) */
        uint8_t m = 0;
        switch (c->prev_dirs) {
        case 1: m = 4; break;
        case 2: m = 8; break;
        case 4: m = 2; break;
        case 8: m = 1; break;
        }
        if ((uint8_t)curdirs & m) {
            if (sentinel_is_lane_clear(g_sent_bx, g_sent_by, g_sent_bz, c, m) == 1) {
                lc = 0;
            } else {
                if (sentinel_find_overtake_lane(c, (uint8_t)g_sent_bx, (uint8_t)g_sent_by, (uint8_t)g_sent_bz)) return;
                if (!car_is_on_screen(c)) sentinel_check_ahead(s, g_sent_bx, g_sent_by, g_sent_bz, -1);
                lc = 3;
                turning_case = true;
            }
        }
    } else {
        if ((ahead2_t & 0xf) == 0 && !slope) {
            /* the block ahead isn't road: turn into the current block's own direction */
            int d = 0;
            switch (c->road_dirs * 16 + curdirs) {
            case 0x14: d = 0x20; break;   /* 1, 4 */
            case 0x18: d = -0x20; break;  /* 1, 8 */
            case 0x24: d = -0x20; break;  /* 2, 4 */
            case 0x28: d = 0x20; break;   /* 2, 8 */
            case 0x41: d = -0x20; break;  /* 4, 1 */
            case 0x42: d = 0x20; break;   /* 4, 2 */
            case 0x81: d = 0x20; break;   /* 8, 1 */
            case 0x82: d = -0x20; break;  /* 8, 2 */
            }
            if (d != 0) {
                c_start_turn(c, d, 2);
                s->u16 = (int16_t)(s->u14 + 1);
            }
        }
        lc = (int16_t)sentinel_check_ahead(s, g_sent_bx, g_sent_by, g_sent_bz, 0);
        if ((car_cache_at(((g_sent_bz * 0x100 - dx) + g_sent_by) * 0x100 + dy + g_sent_bx) & 0x70) == 0x20)
            side_r_res = (uint8_t)sentinel_check_ahead(s, g_sent_bx - dx + dy, g_sent_by - dy - dx, g_sent_bz, 5);
        if ((car_cache_at((g_sent_by + dx + g_sent_bz * 0x100) * 0x100 - dy + g_sent_bx) & 0x70) == 0x20)
            side_l_res = (uint8_t)sentinel_check_ahead(s, g_sent_bx - dy - dx, g_sent_by - dy + dx, g_sent_bz, 5);
        if (lc == 0) {
            if (curdirs == back && side_l_res == 0) {
                c_save_pos(c);
                c->lane_mode = 3;   /* Car_SetLaneMode3 0x40be30 */
            }
        } else {
            mission_clear_row(g_sent_bx << 6, g_sent_by << 6, g_sent_bz << 6, c->id, lc, dx, dy);
        }
    }

    bool blocked;   /* 0x41d5d5 */
    if (turning_case) {
        blocked = c_moving(c);   /* 0x41d39c */
    } else if (lc == 0) {
        blocked = false;
    } else if (lc <= 2) {
        blocked = true;
    } else {
        blocked = c_moving(c);
    }
    if (!blocked && s->u20 != 0) blocked = true;
    if (!blocked) {
        /* 0x41d3ae: the way is free */
        if (s->u12 == 0 && c->udc != 2) {
            if (c->control == 3 && !car_is_on_screen(c) && !c_turning(c)) {
                c->speed = c->max_speed;
                c_end_brake(c);
            } else if (!c_moving(c)) {
                c->input = c->accel;
                c->udc = 0;
                c_end_brake(c);
            } else {
                if (c->speed < c->max_speed && (c->turn_delta == 0 || c->speed < c->cruise)) {
                    c->unkc0 = 1;
                    c->input = c->accel;
                }
                c_end_brake(c);
            }
        }
    } else if (!c_moving(c)) {
        /* 0x41d82a: stopped before something: overtake, or (unseen) push past it */
        if (sentinel_find_overtake_lane(c, (uint8_t)g_sent_bx, (uint8_t)g_sent_by, (uint8_t)g_sent_bz) == 1) return;
        if (!car_is_on_screen(c)) lc = (int16_t)sentinel_check_ahead(s, g_sent_bx, g_sent_by, g_sent_bz, -1);
    } else {
        /* 0x41d5e7: slow down for it, harder the closer it is */
        if (c->speed > 0x14) c->speed--;
        if (curdirs != 0) {
            if (lc <= 5 && c->speed > 8) c->speed--;
            if (lc <= 3) {
                if (c->speed > 8) {
                    c_begin_brake(c);
                    c->input = 0;   /* Car_ClearAccel 0x40c080 */
                } else {
                    c->speed--;
                }
            }
            if (lc <= 2) {
                c_begin_brake(c);
                c->input = 0;
            }
        }
        if (s->kind == SENT_POLICE && s->state == 1) lc = (int16_t)sentinel_check_obstacle(s, g_sent_bx, g_sent_by, g_sent_bz, 5);
        if (lc != 0 && s->kind != SENT_DUMMY) {
            /* a lane change to the side whose lane is clearer */
            uint8_t R = side_r_res, L = side_l_res;
            int rd = c->road_dirs;
            c_save_pos(c);
            c->uc4 = 8;
            bool to_a;
            if (R == 0 && L == 0) {
                if (side_r == rd) c->lane_mode = 4;
                else if (side_l == rd) c->lane_mode = 3;
                to_a = true;
            } else if (R == 0 && side_r == rd) {
                c->lane_mode = 4;
                to_a = true;
            } else {
                if (L == 0 && side_l == rd) c->lane_mode = 3;
                else if (R > L && (int16_t)R >= lc && side_r == rd) c->lane_mode = 4;
                else if (L > R && (int)L > lc + 2 && side_l == rd) c->lane_mode = 3;
                to_a = R == 0;
            }
            bool to_e;
            if (to_a) to_e = (int16_t)L <= lc || ((int16_t)R > lc && R > L);   /* 0x41d7ac -> 0x41d7b9 */
            else to_e = (int16_t)R > lc && R > L;                               /* 0x41d7b9 */
            if (to_e && s->kind != SENT_ROUTE &&
                (uint8_t)sentinel_check_ahead(s, g_sent_bx - 2 * dx + dy, g_sent_by - 2 * dy - dx, g_sent_bz, 10) == 0)
                c->lane_mode = 4;   /* Car_SetLaneMode4 0x40be20 */
        }
    }

    /* 0x41d881: in a lane change, slow and nothing close: speed up */
    if (c->lane_mode > 2 && c->speed < 8 && lc > 3) {
        c->speed++;
        c->input = c->accel;
        c_end_brake(c);
    }
}

/* ======================================================================== the steering group */
/* The sentinels' steering and look-ahead group (0x41e6f0-0x4227a0): lane probes ahead of a car
   (Sentinel_IsLaneClear 0x41e6f0, Sentinel_CheckAhead 0x41e890, Sentinel_CheckObstacle 0x41f030,
   Sentinel_IsBlockedByStoppedCar 0x4223e0), lane scans and lane changes (Map_ScanLaneLength
   0x421e50, Sentinel_FindOvertakeLane 0x421fb0), the place swap that unjams a car (Car_SwapPositions
   0x422500) and Sentinel_Steer 0x41f290, the step of a police car chasing a criminal: turn into a
   side road toward him, line up beside him, box him in, or follow the lane and brake for what is
   ahead. (See sentinel.h, docs/police.md.)

   Block probes read the cached block types (Map.type_cache, the original's 0x55fab0 [6][256][256])
   by a linear index (z * 0x10000 + y * 0x100 + x) where the original adds signed offsets to an int
   index, and by bytes where it adds bytes: so x - 1 at x = 0 is x 255 of the row before in the first
   case and x 255 of the same row in the second, as in the original. An index outside the table reads
   0 (the original reads the neighbouring memory). */
/* MODULE GLOBALS: none of its own; it reads and writes Sentinel_DriveCar's (sentinel.h): g_sent_bx,
   g_sent_by (0x50caa0 / 0x50caa2, int16), g_sent_bz (0x50caa8, int16), g_sent_node_dx,
   g_sent_node_dy (0x504f4a / 0x50584c, int16) and the pursuits' +0x38 counter (wanted.h). */

int sentinel_is_lane_clear(int bx, int by, int bz, Car *c, int dir);
int sentinel_check_ahead(Sentinel *s, int bx, int by, int bz, int extra);
int sentinel_check_obstacle(Sentinel *s, int bx, int by, int bz, int extra);
void sentinel_steer(Sentinel *s, int bx, int by, int bz);
int map_scan_lane_length(int bx, int by, int dir, int stop);
int sentinel_find_overtake_lane(Car *c, int bx, int by, int bz);
int sentinel_is_blocked_by_stopped_car(int bx, int by, int bz, Car *c, int dir);
void car_swap_positions(Car *a, Car *b);

/* ---------------------------------------------------------------- helpers */

static int lin(int x, int y, int z) { return (z * 0x100 + y) * 0x100 + x; }
/* the cached type byte at a linear index (0 outside the table) */
static uint8_t tcl(int i)
{
    if (!g_game.map || i < 0 || i >= MAP_Z * MAP_H * MAP_W) return 0;
    return (&g_game.map->type_cache[0][0][0])[i];
}
/* Map_GetBlockInfo_thunk 0x471940 calls Map_GetTypeMap 0x438900 and returns nothing, but its EAX is
   the type map, which the callers pass on to Path_IsUpRamp / Path_IsDownRamp. Past layer 5: 0. */
static uint32_t type_map_at(int x, int y, int z)
{
    x &= 0xff, y &= 0xff, z &= 0xff;
    if (!g_game.map || z >= MAP_Z) return 0;
    return map_get_type_map(g_game.map, x, y, z);
}
/* road direction bits -> unit step (dx, dy): 1 -y, 2 +y, 4 -x, 8 +x; others leave them */
/* (dir_step: as above) */
static Car *hit_car(const CollHit *h) { return car_get(((const Car *)h->owner)->id); }
static int i16abs(int v) { return abs((int16_t)v); }

/* the inlined car accessors (0x40be00-0x40c0a0) */
static void car_begin_brake(Car *c) { c->brake = 1; c->thrust_in = 0; }   /* Car_BeginBrake 0x40be90 */
static void car_end_brake(Car *c) { c->brake = 0; }                        /* Car_EndBrake 0x40beb0 */
static void car_save_pos(Car *c) { c->saved_x = c->spr.x, c->saved_y = c->spr.y; }   /* Car_SavePos 0x40c0a0 */

/* ---------------------------------------------------------------- probes */

/* Sentinel_IsLaneClear 0x41e6f0: the three blocks after (bx, by) in direction dir, at the car's
   height: another car there gives 0; when none is there but a kind-10 entity is, the car stops
   (speed and input 0, brake on) and the answer is 0 too; else 1. bz is not used. */
int sentinel_is_lane_clear(int bx, int by, int bz, Car *c, int dir)
{
    (void)bz;
    int dx = 0, dy = 0;
    dir_step(dir, &dx, &dy);
    int16_t x = (int16_t)bx, y = (int16_t)by;
    for (int n = 0; n < 3; n++) {
        bool stop = false;
        x = (int16_t)(x + dx), y = (int16_t)(y + dy);
        int32_t px = x * 0x400000 + 0x200000, py = y * 0x400000 + 0x200000;
        CollHit *h = coll_query_block(px, py, c->spr.z, COLL_CAR, c->id);
        if (h) {
            if (hit_car(h)->id != c->id) {
                coll_unlock();
                return 0;
            }
        } else {
            coll_unlock();
            if (coll_query_block(px, py, c->spr.z, COLL_KIND10, c->id)) {
                stop = true;
                c->speed = 0;
                c->brake = 1;
                c->input = 0;
            }
        }
        coll_unlock();
        if (stop) return 0;
    }
    return 1;
}

/* Sentinel_CheckAhead 0x41e890: walks the lane of s's car from block (bx, by, bz), up and down ramps,
   for at most (the car's speed when chasing, else +0x14) + extra blocks (kept to extra + 5 .. extra
   + 10) and returns the step where something blocks it (0: nothing; 1 at once when the start isn't
   road; the walk only runs on a route (+0x0c) or a chase (+0x4a)):
   - a car whose box overlaps the car's box moved there: with extra -1 a hidden traffic car (or an
     idle AI car) swaps places with it instead (back if either then overlaps something; 0); a traffic
     car in the same lane loses the car's +0xdc, a traffic car going its way or across (seen from a
     side lane: the same way) gets owner status 4; the criminal's own car doesn't count (0); from a
     side lane the step - 2 (at least 1); a slower car or one coming the other way or turning blocks
     (an oncoming moving car at half the distance), a faster one is passed if the car goes over 5;
   - a ped, unless s is a police car out of state 1;
   - following the route, the walk turns where the route turns (the next node, +0x119; g_sent_node_*
     get the node's step).
   The collision queries look at the block before the step (they lag one block behind the road test),
   as in the original. */
int sentinel_check_ahead(Sentinel *s, int bx, int by, int bz, int extra)
{
    Car *c = sentinel_car(s);
    int16_t result = 0, v = 0, dirbit = 0;
    int ddx = 0, ddy = 0;
    bool lateral = false;
    extra = (int16_t)extra;
    int16_t lim = (int16_t)((s->u4a ? c->speed : s->u14) + extra);
    if (lim < 5) lim = (int16_t)(extra + 5);
    else if (lim > 10) lim = (int16_t)(extra + 10);
    uint8_t x = (uint8_t)bx, y = (uint8_t)by, z = (uint8_t)bz;
    if ((tcl(lin(x, y, z)) & 0xf) == 0) return 1;
    switch (c->road_dirs) {
    case 1: ddy = -1, dirbit = 2; lateral = g_sent_bx != x; break;
    case 2: ddy = 1, dirbit = 1; lateral = g_sent_bx != x; break;
    case 4: ddx = -1, dirbit = 8; lateral = g_sent_by != y; break;
    case 8: ddx = 1, dirbit = 4; lateral = g_sent_by != y; break;
    }
    if (s->u0c == 0 && s->u4a == 0) return 0;   /* (without unlocking: nothing was queried) */
    uint8_t px = x, py = y;
    for (int16_t i = 1; i < lim; i++) {
        int32_t qz = c->spr.z;
        y = (uint8_t)(y + ddy), x = (uint8_t)(x + ddx);
        int32_t qx = px * 0x400000 + 0x200000, qy = py * 0x400000 + 0x200000;
        px = x, py = y;
        uint8_t t = tcl(lin(x, y, z));
        int dz = 0;
        if (z < 4 && path_is_down_ramp(type_map_at(x, y, z + 1), c->road_dirs)) dz = 1;
        else if ((t & 0x80) && (tcl(lin(x, y, (uint8_t)(z - 1))) & 0xf)) dz = -1;
        uint8_t d = tcl(lin(x, y, z + dz)) & 0xf;
        if (d == 0) {
            result = 0;
            break;
        }
        CollHit *h = coll_query_block(qx, qy, qz, COLL_CAR, c->id);
        if (h) {
            CollBox box;
            coll_build_box(qx, qy, qz, c->half_w, c->half_l, c->next_heading, c->depth, &box);
            if (coll_entity_vs_box(COLL_CAR, h->owner, &box) != -1) {
                Car *o = hit_car(h);
                if (c->id != o->id) {
                    if (extra == -1 && !car_is_on_screen(o) && (o->control == 0 || (o->control == 3 && o->horn == 0))) {
                        car_swap_positions(c, o);
                        if (coll_first_hit(&c->box_saved, COLL_CAR, c) != -1 || coll_first_hit(&o->box_saved, COLL_CAR, o) != -1)
                            car_swap_positions(o, c);
                        coll_unlock();
                        return 0;
                    }
                    if (c->road_dirs != o->road_dirs || o->control == 0) {
                        if (c->road_dirs == o->road_dirs && c->udc != 0) c->udc = 0;
                        if (o->control == 0 && c->speed > 0) {
                            if (lateral) {
                                if (c->road_dirs == o->road_dirs && !(s->kind == SENT_POLICE && s->state == 1)) o->owner_status = 4;
                            } else if (o->road_dirs == dirbit) {
                                o->owner_status = 4;
                            }
                        }
                    }
                    if (s->u4a > 0 && s->pursuit >= 0 && g_pursuits[s->pursuit].criminal > -1) {
                        const Criminal *cr = police_get_criminal(g_pursuits[s->pursuit].criminal);
                        if (!cr) return 0;   /* (unreachable: the index is not negative; the list stays locked) */
                        if (cr->kind == 0 && cr->car == o->id) {
                            result = 0;
                            break;
                        }
                    }
                    if (lateral) {
                        result = (int16_t)(i - 2);
                        if (result < 1) result = 1;
                        break;
                    }
                    if (!(o->speed >= c->speed && o->road_dirs != dirbit && o->turn_delta == 0 && c->speed > 5)) {
                        if (o->road_dirs == dirbit && o->speed != 0) {
                            result = (int16_t)((i >> 1) + 1);
                            break;
                        }
                        result = i;
                        if (result < 1) result = 1;
                        break;
                    }
                }
            }
        }
        coll_unlock();
        if (coll_query_block(qx, qy, qz, COLL_PED, c->driver) && (s->kind != SENT_POLICE || s->state == 1)) result = i;
        coll_unlock();
        if (result != 0) break;
        if (s->u14 <= 1) {
            if (s->u4a == 0) break;
            continue;
        }
        if (s->u4a != 0) continue;
        /* following the route: turn with it at its current node (+0x0c..+0x0e) */
        uint8_t nx = s->u0c, ny = s->u0d, nz = s->u0e;
        uint8_t nd = tcl(lin(nx, ny, nz)) & 0xf;
        if ((x != nx || y != ny) && (d != nd || abs(x - nx) + abs(y - ny) > 2)) continue;
        int k = 3 * (s->id * 0x55 + c->counter119) + 3;
        const uint8_t *path = &g_path_slots[0][0];
        int nnx = k < PATH_SLOTS * PATH_SLOT_SIZE - 1 ? path[k] : 0;
        int nny = k < PATH_SLOTS * PATH_SLOT_SIZE - 1 ? path[k + 1] : 0;
        int sw = v;
        if (nnx == 0) {
            sw = nd;   /* (the node's own bits; v is kept) */
        } else {
            g_sent_node_dx = (int16_t)(nnx - nx);
            g_sent_node_dy = (int16_t)(nny - ny);
            int ax = i16abs(g_sent_node_dx), ay = i16abs(g_sent_node_dy);
            int16_t cd = (int16_t)c->road_dirs;
            if (cd <= 2 && ax > ay) v = g_sent_node_dx > 0 ? 8 : 4;
            else if (ax < ay) v = g_sent_node_dy > 0 ? 2 : 1;
            else if (ax == ay) v = cd;
            /* (|dx| > |dy| on an x road keeps v from the last node) */
            sw = v;
        }
        switch (sw) {
        case 1: ddx = 0, ddy = -1, dirbit = 2; break;
        case 2: ddx = 0, ddy = 1, dirbit = 1; break;
        case 4: ddx = -1, ddy = 0, dirbit = 8; break;
        case 8: ddx = 1, ddy = 0, dirbit = 4; break;
        }
    }
    coll_unlock();
    return result;
}

/* Sentinel_CheckObstacle 0x41f030: like CheckAhead for a police car in state 1, but it follows ramps
   and only a car without a driver (a parked or abandoned car) counts: the step it is found at, 0 if
   none (or the lane ends), 1 if the start isn't road. The steps are speed + extra, kept to extra + 4
   .. extra + 10; the queries lag one block, as in CheckAhead. */
int sentinel_check_obstacle(Sentinel *s, int bx, int by, int bz, int extra)
{
    Car *c = sentinel_car(s);
    extra = (int16_t)extra;
    int16_t lim = (int16_t)(c->speed + extra);
    if (lim < 4) lim = (int16_t)(extra + 4);
    else if (lim > 10) lim = (int16_t)(extra + 10);
    int dx = 0, dy = 0;
    dir_step(c->road_dirs, &dx, &dy);
    int16_t x = (int16_t)bx, y = (int16_t)by, z = (int16_t)bz;
    if ((tcl(lin(x, y, z)) & 0xf) == 0) return 1;
    if (s->u0c == 0 && s->u4a == 0) return 0;
    if (lim <= 1) return 0;
    for (int16_t i = 1;;) {
        int32_t qz = c->spr.z;
        int32_t qx = x * 0x400000 + 0x200000, qy = y * 0x400000 + 0x200000;
        x = (int16_t)(x + dx), y = (int16_t)(y + dy);
        int down = z < 4 ? path_is_down_ramp(type_map_at(x, y, z + 1), c->road_dirs) : 0;
        int up = path_is_up_ramp(type_map_at(x, y, z), c->road_dirs);
        if (down) z++;
        if (up) z--;
        if ((tcl(lin(x, y, z)) & 0xf) == 0) return 0;
        CollHit *h = coll_query_block(qx, qy, qz, COLL_CAR, c->id);
        if (h) {
            Car *o = hit_car(h);
            if (c->id != o->id && o->driver == -1) {
                coll_unlock();
                return i;
            }
        }
        coll_unlock();
        if (++i >= lim) return 0;
    }
}

/* Map_ScanLaneLength 0x421e50: from (bx, by) at the driven car's layer (g_sent_bz) along dir while
   the blocks keep a dir bit, until one has exactly the bits `stop`, or (after 3 blocks) one has a bit
   of (start bits - dir - stop), or 100 blocks: 1 if it didn't end on `stop`. */
int map_scan_lane_length(int bx, int by, int dir, int stop)
{
    uint8_t x = (uint8_t)bx, y = (uint8_t)by, st = (uint8_t)stop;
    int d = dir & 0xff;
    int dx = 0, dy = 0;
    dir_step(d, &dx, &dy);
    int zrow = g_sent_bz * 0x100;
    uint16_t cur = tcl((zrow + y) * 0x100 + x) & 0xf;
    uint16_t other = (uint16_t)(cur - d - st);
    int16_t n = 0;
    bool done = false;
    while (d & cur) {
        if (cur == st || done) break;
        x = (uint8_t)(x + dx), y = (uint8_t)(y + dy);
        cur = tcl((zrow + y) * 0x100 + x) & 0xf;
        n++;
        if (n > 2 && ((other & cur) != 0 || n > 100)) done = true;
    }
    return cur != st;
}

/* Sentinel_FindOvertakeLane 0x421fb0: steps sideways from (bx, by) up to 10 blocks (along the block's
   own direction where it differs from the start's and is a single one, else the car's), trying the
   car there (it must stay off screen): a block with road bits and no car behind, at or ahead of the
   spot is a candidate (the last one wins). If the car would be off screen there too and the block is
   road, the car's next position (+0x220) and heading (from the block's bits; several bits: its own
   angle, speed 0) are set, its speed becomes its cruise speed, and 1 is returned. */
int sentinel_find_overtake_lane(Car *c, int bx, int by, int bz)
{
    uint8_t x = (uint8_t)bx, y = (uint8_t)by;
    int zrow = (bz & 0xff) * 0x100;
    int32_t wx = c->spr.x, wy = c->spr.y, z = c->spr.z;
    uint8_t base = tcl((y + zrow) * 0x100 + x) & 0xf;
    uint8_t last = base;
    int dx = 0, dy = 0;
    bool found = false;
    int32_t fx = 0, fy = 0;
    int16_t n = 0;
    do {
        n++;
        if (last == 0) break;
        uint8_t b = tcl((y + zrow) * 0x100 + x) & 0xf;
        if (b == 0) break;
        if (b == base || (b != 1 && b != 2 && b != 4 && b != 8)) dir_step(c->road_dirs, &dx, &dy);
        else dir_step(b, &dx, &dy);
        x = (uint8_t)(x + dx), y = (uint8_t)(y + dy);
        int32_t sx = c->spr.x, sy = c->spr.y;
        wx += dx * 0x400000, wy += dy * 0x400000;
        c->spr.x = wx, c->spr.y = wy;
        if (!car_is_on_screen(c)) {
            last = tcl((y + zrow) * 0x100 + x) & 0xf;
            if (last != 0) {
                CollHit *h = coll_query_block(wx - dx * 0x400000, wy - dy * 0x400000, z, COLL_CAR, c->id);
                coll_unlock();
                if (!h) {
                    h = coll_query_block(wx, wy, z, COLL_CAR, c->id);
                    coll_unlock();
                    if (!h) {
                        h = coll_query_block(wx + dx * 0x400000, wy + dy * 0x400000, z, COLL_CAR, c->id);
                        coll_unlock();
                        if (!h) found = true, fx = wx, fy = wy;
                    }
                }
            }
        } else {
            last = 0;
        }
        c->spr.x = sx, c->spr.y = sy;
    } while (n < 10);
    if (!found || car_is_on_screen(c)) return 0;
    int32_t sx = c->spr.x, sy = c->spr.y;
    c->spr.x = fx, c->spr.y = fy;
    if (!car_is_on_screen(c)) {
        uint8_t t = tcl(lin((uint8_t)(fx >> 22), (uint8_t)(fy >> 22), (uint8_t)(c->spr.z >> 22)));
        if (t & 0xf) {
            int16_t h;
            c->speed = c->cruise;
            switch (t & 0xf) {
            case 1: h = 0x200; break;
            case 2: h = 0; break;
            case 4: h = 0x300; break;
            case 8: h = 0x100; break;
            default: h = c->spr.angle, c->speed = 0; break;
            }
            c->spr.x = sx, c->spr.y = sy;
            c->next_x = fx, c->next_y = fy;
            c->next_heading = h;
            return 1;
        }
    }
    c->spr.x = sx, c->spr.y = sy;
    return 0;
}

/* Sentinel_IsBlockedByStoppedCar 0x4223e0: the first block of the four after (bx, by) in direction
   dir that holds another car decides: 1 if that car stands (speed < 1), 0 if it moves; 1 if there is
   none. bz is not used. */
int sentinel_is_blocked_by_stopped_car(int bx, int by, int bz, Car *c, int dir)
{
    (void)bz;
    int dx = 0, dy = 0;
    dir_step(dir, &dx, &dy);
    int16_t x = (int16_t)bx;
    int32_t y = by;
    for (int n = 0; n < 4; n++) {
        y += dy, x = (int16_t)(x + dx);
        CollHit *h = coll_query_block(x * 0x400000 + 0x200000, (int16_t)y * 0x400000 + 0x200000, c->spr.z, COLL_CAR, c->id);
        if (h) {
            Car *o = hit_car(h);
            if (o->id != c->id) {
                int16_t sp = o->speed;
                coll_unlock();
                return sp < 1;
            }
        }
        coll_unlock();
    }
    return 1;
}

/* Car_SwapPositions 0x422500: a and b trade positions and headings (as their next position, then
   committed): turns and lane changes cleared, boxes rebuilt at the new spots, the wheel points put
   half a length before and after, pending impulses dropped, the ground found again, both back into
   the grid. */
void car_swap_positions(Car *a, Car *b)
{
    coll_remove(a, a->spr.unk20);
    coll_remove(b, b->spr.unk20);
    int32_t ax = a->spr.x, az = a->spr.z;
    int16_t aang = a->spr.angle;
    a->next_x = b->spr.x, a->next_y = b->spr.y, a->next_z = b->spr.z;
    a->next_heading = b->spr.angle;
    a->front_heading = a->next_heading;
    a->turn_delta = 0, a->lane_mode = 0;
    b->next_y = a->spr.y;
    b->next_heading = aang, b->front_heading = aang;
    b->next_x = ax, b->next_z = az;
    b->turn_delta = 0, b->lane_mode = 0;
    Car *cs[2] = { a, b };
    for (int k = 0; k < 2; k++) {
        Car *c = cs[k];
        coll_build_box(c->next_x, c->next_y, c->next_z - c->z_offset, c->half_w, c->half_l, c->next_heading, c->depth, &c->box_saved);
    }
    for (int k = 0; k < 2; k++) {
        Car *c = cs[k];
        int h = c->next_heading, half = c->length >> 1;
        c->front_x = c->next_x - math_sin(h) * half;
        c->front_y = c->next_y - math_cos(h) * half;
        c->rear_x = math_sin(h) * half + c->next_x;
        c->rear_y = math_cos(h) * half + c->next_y;
    }
    for (int k = 0; k < 2; k++) {
        Car *c = cs[k];
        c->impulse_state = 0;
        c->impulse_x = 0, c->impulse_y = 0;
    }
    car_update_ground(a);
    car_update_ground(b);
    car_commit_move(a);
    car_commit_move(b);
    coll_insert(COLL_CAR, a->id, a, a->spr.unk20, a->spr.x, a->spr.y);
    coll_insert(COLL_CAR, b->id, b, b->spr.unk20, b->spr.x, b->spr.y);
}

/* ---------------------------------------------------------------- Sentinel_Steer */

/* the start of a turn into a side road: the turn takes the road bits as its "from" bits, turns by
   `delta` (+0x20 one way, -0x20 the other) at speed 5, out of any lane change */
static void steer_turn(Car *c, int delta)
{
    c->turn_dirs = c->road_dirs;
    c->turn_progress = 0;
    c->turn_delta = (int16_t)delta;
    c->u98 = (int16_t)delta;
    c->speed = 5;
    c->lane_mode = 0;
}
static void steer_stop(Car *c) { c->input = 0; c->brake = 1; }
/* a lane change (Car_StartLaneChange's fields): mode, 6 frames, from the current position */
static void steer_lane(Car *c, int mode)
{
    c->lane_mode = (int16_t)mode;
    c->uc4 = 6;
    c->saved_x = c->spr.x, c->saved_y = c->spr.y;
}
/* the block diagonally behind on the right of the driven car's block (cx - dx - dy, cy + dx - dy):
   road there lets the car give up on the target ahead (+0x12 = 1, Sentinel_HandleStuck) */
static bool diag_road(int dx, int dy)
{
    return (tcl(g_sent_bz * 0x10000 + (g_sent_by + dx - dy) * 0x100 + g_sent_bx - dx - dy) & 0xf) != 0;
}
/* lined up beside the target: counted in the pursuit (+0x38) */
static void steer_beside(Sentinel *s, int angle)
{
    s->u4b = 1;
    g_pursuits[s->pursuit].lined_up++;
    s->u4c = (int16_t)angle;
}

/* Sentinel_Steer 0x41f290: one step of a police car (s, kind 2, +0x4a > 0) after its criminal of the
   pursuit +0x6c, at the look-ahead block (bx, by) and layer bz Sentinel_DriveCar computed (the
   driven car's own block is g_sent_bx / _by / _bz).
   1. The target: the criminal's ped (on foot) or train, in pixels relative to the car (rel); in a
      car: with +0x4a = 1 the car itself, with 3 a point beside it (64 pixels ahead, 192 when it goes
      over 5) on the side the lanes beside it are freer (two CheckAheads from two blocks behind it on
      each side), if the point is road; +0x4b = 1 when the car is past that point. (+0x4a 1 keeps the
      target in pixels where the others are 16.16: the comparisons with the car's x / y below then
      mix units, as in the original.)
   2. Unless turning or past the target: the length of the straight lane ahead (+0x14, 4..8), the
      two side lanes one block behind (CheckAhead 5), then by the car's direction a turn into a side
      road toward the target (Sentinel_IsLaneClear, Map_ScanLaneLength; braking when it must turn at
      once), else a lane change toward it, and when close beside it: lined up (+0x4b, the angle to it
      +0x4c, braking when within 64 pixels) or, far past it, giving up (+0x12) where a road leads
      back. The four directions are not symmetric (see the quirks in the code).
   3. A dead end ahead turns the car into the free side; a new turn at a junction is remembered in
      the ring +0x76 / +0x77 (10 x {x, y, turn}) and a turn there again is cancelled.
   4. Lined up (+0x4b > 0): stop beside the target (speed at most 13, reverse input, turn toward the
      free side, brake). Else: the stuck handler, the cars in the 2-block square around removed, the
      lane ahead checked (CheckAhead, the side lanes, Mission_ClearRow), the lane the car's last turn
      leads into (IsLaneClear, FindOvertakeLane), throttle and brakes by the distance to what blocks,
      a lane change away from it, and the traffic lights (Sentinel_OverrideLights). */
void sentinel_steer(Sentinel *s, int bx, int by, int bz)
{
    Car *c = sentinel_car(s);
    int dx = 0, dy = 0, back = 0;
    int32_t rx = 0, ry = 0, rz = 0;          /* local_60, local_5c, local_1c */
    int32_t tgt_x = 0, tgt_y = 0;            /* local_24, local_20 */
    Car *tcar = NULL;                        /* local_14 */
    c->cruise = 6;
    switch (c->road_dirs) {
    case 1: dx = 0, dy = -1, back = 2; break;
    case 2: dx = 0, dy = 1, back = 1; break;
    case 4: dx = -1, dy = 0, back = 8; break;
    case 8: dx = 1, dy = 0, back = 4; break;
    }
    /* (the original reads the pursuit record before the table for +0x6c = -1; DriveCar only steers
       cars of a pursuit) */
    int16_t crim_i = s->pursuit >= 0 && s->pursuit < PURSUITS ? g_pursuits[s->pursuit].criminal : -1;
    if (crim_i < 0) {
        s->u4a = 0;
        return;
    }
    const Criminal *crim = police_get_criminal(crim_i);
    if (!crim) return;
    if (crim->kind == 1) {
        const Ped *p = ped_get(crim->ped);
        rx = (int16_t)(c->spr.x >> 16) - (int16_t)(p->spr.x >> 16);
        ry = (int16_t)(c->spr.y >> 16) - (int16_t)(p->spr.y >> 16);
        rz = (int16_t)(c->spr.z >> 16) - (int16_t)(p->spr.z >> 16);
        tgt_x = p->spr.x, tgt_y = p->spr.y;
    } else if (crim->kind == 2) {
        const int32_t *t = ref_get_kind1_pos_rect(crim->train);
        rx = (int16_t)(c->spr.x >> 16) - (int16_t)(t[0] >> 16);
        ry = (int16_t)(c->spr.y >> 16) - (int16_t)(t[1] >> 16);
        rz = (int16_t)(c->spr.z >> 16) - (int16_t)(t[2] >> 16);
        tgt_x = t[0], tgt_y = t[1];
    } else if (crim->kind == 0) {
        tcar = car_get(crim->car);
        if (s->u4a == 1) {
            tgt_x = (int16_t)(tcar->spr.x >> 16);   /* (pixels) */
            tgt_y = (int16_t)(tcar->spr.y >> 16);
            rx = (int16_t)(c->spr.x >> 16) - tgt_x;
            ry = (int16_t)(c->spr.y >> 16) - tgt_y;
        } else if (s->u4a == 3) {
            int tdx = 0, tdy = 0;
            dir_step(tcar->road_dirs, &tdx, &tdy);
            uint8_t tx = (uint8_t)(tcar->spr.x >> 22), ty = (uint8_t)(tcar->spr.y >> 22), tz = (uint8_t)(tcar->spr.z >> 22);
            int16_t side_a = 1, side_b = 1;   /* local_3c, local_34 */
            if ((tcl(lin(tx + tdy, ty - tdx, tz)) & 0x70) == 0x20)
                side_a = (int16_t)sentinel_check_ahead(s, tx - 2 * tdx + tdy, ty - 2 * tdy - tdx, tz, 1);
            if ((tcl(lin(tx - tdy, ty + tdx, tz)) & 0x70) == 0x20)
                side_b = (int16_t)sentinel_check_ahead(s, tx - 2 * tdx - tdy, ty - 2 * tdy + tdx, tz, 1);
            int off = 0;
            if (c->road_dirs == tcar->road_dirs) {
                if (side_a == 0) off = -0x40;
                else if (side_b == 0) off = 0x40;
                else if (side_b < side_a) off = side_a > 3 ? -0x40 : 0;
                else if (side_b > side_a && side_b > 3) off = 0x40;
                /* already level with or ahead of the target car on the axis: no side offset */
                switch (c->road_dirs) {
                case 1: if (c->spr.y < tcar->spr.y) off = 0; break;
                case 2: if (c->spr.y > tcar->spr.y) off = 0; break;
                case 4: if (c->spr.x < tcar->spr.x) off = 0; break;
                case 8: if (c->spr.x > tcar->spr.x) off = 0; break;
                }
            }
            int ahead = tcar->speed > 5 ? 0xc0 : 0x40;
            int pxl = (int16_t)(tcar->spr.x >> 16), pyl = (int16_t)(tcar->spr.y >> 16);
            int ox = tdx * ahead - off * tdy + pxl;
            int oy = tdy * ahead + pyl + off * tdx;
            int oz = (int16_t)(tcar->spr.z >> 16) - 1;
            if ((tcl(lin(ox >> 6 & 0xff, oy >> 6 & 0xff, oz >> 6 & 0xff)) & 0xf) == 0) ox = pxl, oy = pyl;
            int cpx = (int16_t)(c->spr.x >> 16), cpy = (int16_t)(c->spr.y >> 16);
            rx = cpx - ox, ry = cpy - oy;
            rz = (int16_t)(c->spr.z >> 16) - oz;
            int a1 = abs(dy * 0x40 - oy + cpy), a2 = abs(dx * 0x40 - ox + cpx);
            int ahead_d = a2 > a1 ? a2 : a1;
            int here_d = abs((int)rx) > abs((int)ry) ? abs((int)rx) : abs((int)ry);
            if (ahead_d > here_d) s->u4b = 1;
            tgt_x = tcar->spr.x, tgt_y = tcar->spr.y;
        }
    }

    /* ---- the blocks around the look-ahead block */
    int px = bx & 0xff, py = by & 0xff, pz = bz & 0xff;
    uint8_t here = tcl(lin(g_sent_bx & 0xff, g_sent_by & 0xff, g_sent_bz & 0xff));   /* bVar19 */
    uint16_t cur = here & 0xf;                                                      /* uVar25 */
    int cv = 0;   /* slope ahead: the layer offset */
    if (g_sent_bz < 4 && (tcl(lin((uint8_t)(dx + px), (uint8_t)(dy + py), pz)) & 0xf) == 0 &&
        (tcl(lin((uint8_t)(dx + px), (uint8_t)(dy + py), (uint8_t)(pz + 1))) & 0xf) != 0) {
        cv = 1;
    } else if (g_sent_bz > 0 && (tcl(lin((uint8_t)(dx + px), (uint8_t)(dy + py), (uint8_t)(pz - 1))) & 0xf) != 0) {
        cv = -1;
    }
    if (px + dx <= 0 || px + dx >= 0xff || py + dy <= 0 || py + dy >= 0xff) dx = dy = 0;
    uint8_t b1 = tcl(lin(px + dx, py + dy, pz + cv));                                                   /* ahead */
    uint8_t b2 = tcl(lin((uint8_t)(dy + dx + px), (uint8_t)(dy - dx + py), (uint8_t)(cv + pz))) & 0xf;  /* ahead, side */
    uint16_t r32 = tcl(lin((uint8_t)(dy + px), (uint8_t)(py - dx), pz)) & 0xf;                          /* bVar3: one side */
    uint16_t r50 = tcl(lin((uint8_t)(px - dy), (uint8_t)(dx + py), pz)) & 0xf;                          /* bVar4: the other */
    int16_t l48 = 0, l44 = 0;
    bool near = false;
    if (i16abs(c->turn_progress) > 0x100 && c->road_dirs != c->turn_dirs) {
        c->turn_delta = 0;
        c->turn_progress = 0;
    }
    int ax = 0, ay = 0;
    if (c->turn_delta == 0 && s->u4b == 0) {
        /* the straight lane ahead */
        uint8_t lx = (uint8_t)px, ly = (uint8_t)py, run = 1;
        do {
            if ((tcl(lin(lx, ly, pz)) & 0xf) != c->road_dirs) break;
            lx = (uint8_t)(lx + dx), ly = (uint8_t)(ly + dy);
            run++;
        } while (run < 9);
        s->u14 = run;
        if (run < 4) s->u14 = 4;
        int16_t side_a = 1, side_b = 1;   /* local_3c, local_34: the side lanes from two blocks behind */
        if ((tcl(lin(g_sent_bx + dy, g_sent_by - dx, g_sent_bz)) & 0x70) == 0x20)
            side_a = (int16_t)sentinel_check_ahead(s, dy - 2 * dx + g_sent_bx, g_sent_by - 2 * dy - dx, g_sent_bz, 5);
        if ((tcl(lin(g_sent_bx - dy, g_sent_by + dx, g_sent_bz)) & 0x70) == 0x20)
            side_b = (int16_t)sentinel_check_ahead(s, g_sent_bx - 2 * dx - dy, dx - 2 * dy + g_sent_by, g_sent_bz, 5);
        switch (c->road_dirs) {
        case 1:
            near = abs((int)ry) < 0x200 && abs((int)rx) < 0x100;
            if (rx > 0 && (r32 == cur || (near && (r32 == 1 || r32 == 2))) && side_a == 0) l44 = 4, l48 = 1;
            else if (rx < 0 && (r50 == cur || (near && (r50 == 1 || r50 == 2))) && side_b == 0) l48 = 3, l44 = 2;
            else if (c->lane_mode == 0) l44 = 2, l48 = 1;
            ax = abs((int)rx), ay = abs((int)ry);
            if ((ax < 0x100 && ry < -0x200) || ax > 0x100) goto c1_fd4f;
            if (cur == 4) goto c1_fd59;
            if (cur != 8) goto c1_lane;
        c1_fd4f:
            if (cur < 4) goto c1_scan;
        c1_fd59:
            if (ax <= ay) goto c1_scan;
            if (rx > 0x80 && r32 != 0 && (cur & 4)) {
                /* (this side tests the ahead-side block against the reverse bit; the others test the
                   side block) */
                if (sentinel_is_lane_clear(px, py, pz, c, 4) == 1 && (b2 & back) == 0) { steer_turn(c, 0x20); cv = 4; goto c1_post; }
            } else if (rx < -0x80 && r50 != 0 && (cur & 8)) {
                if (sentinel_is_lane_clear(px, py, pz, c, 8) == 1 && r50 != back) { steer_turn(c, -0x20); cv = 8; goto c1_post; }
            }
        c1_scan:
            if (((cur & 4) && rx > 0 && map_scan_lane_length(px, py, 4, 2) == 1) || cur == 4) {
                if (sentinel_is_lane_clear(px, py, pz, c, 4) == 1) { if (r32 == back) goto c1_lane; }
                else { if (cur != 4) goto c1_lane; steer_stop(c); }
                steer_turn(c, 0x20); cv = 4; goto c1_post;
            }
            if (((cur & 8) && rx < 0 && map_scan_lane_length(px, py, 8, 2) == 1) || cur == 8) {
                if (sentinel_is_lane_clear(px, py, pz, c, 8) == 1) { if (r50 == back) goto c1_lane; }
                else { if (cur != 8) goto c1_lane; steer_stop(c); }
                steer_turn(c, -0x20); cv = 8; goto c1_post;
            }
        c1_lane:
            if (c->lane_mode <= 2) {
                if (rx > 8 && side_a == 0 && r32 != 0) steer_lane(c, l44);
                else if (rx < -8 && side_b == 0 && r50 != 0) steer_lane(c, l48);
            }
            cv = 0;
        c1_post:
            /* (this direction tests the car's turn, the others +0x4b) */
            if (c->turn_delta == 0 && s->u12 == 0 && ax <= 0x100) {
                if (ry < 8 && ry > -8) {
                    steer_beside(s, c->spr.x > tgt_x ? 0x300 : 0x100);
                    if (ax < 0x40) steer_stop(c);
                } else if (ax < 0x100 && ry < -0x200 && diag_road(dx, dy)) {
                    s->u12 = 1;
                }
            }
            back = 2;
            break;
        case 2:
            near = abs((int)ry) < 0x200 && abs((int)rx) < 0x100;
            if (rx < 0 && (r32 == cur || (near && (r32 == 2 || r32 == 1))) && side_a == 0) l44 = 4, l48 = 1;
            else if (rx > 0 && (r50 == cur || (near && (r50 == 2 || r50 == 1))) && side_b == 0) l44 = 2, l48 = 3;
            else if (c->lane_mode == 0) l44 = 2, l48 = 1;
            ax = abs((int)rx), ay = abs((int)ry);
            if ((ax < 0x100 && ry > 0x200) || ax > 0x100) goto c2_b4;
            if (cur == 4) goto c2_be;
            if (cur != 8) goto c2_lane;
        c2_b4:
            if (cur < 4) goto c2_scan;
        c2_be:
            if (ax <= ay) goto c2_scan;
            if (rx > 0x80 && r50 != 0 && (cur & 4)) {
                if (sentinel_is_lane_clear(px, py, pz, c, 4) == 1 && r50 != back) { steer_turn(c, -0x20); cv = 4; goto c2_post; }
            } else if (rx < -0x80 && r32 != 0 && (cur & 8)) {
                if (sentinel_is_lane_clear(px, py, pz, c, 8) == 1 && r32 != back) { steer_turn(c, 0x20); cv = 8; goto c2_post; }
            }
        c2_scan:
            if (((cur & 4) && rx > 0 && map_scan_lane_length(px, py, 4, 1) == 1) || cur == 4) {
                if (sentinel_is_lane_clear(px, py, pz, c, 4) == 1) { if (r50 == back) goto c2_lane; }
                else { if (cur != 4) goto c2_lane; steer_stop(c); }
                steer_turn(c, -0x20); cv = 4; goto c2_post;
            }
            if (((cur & 8) && rx < 0 && map_scan_lane_length(px, py, 8, 1) == 1) || cur == 8) {
                if (sentinel_is_lane_clear(px, py, pz, c, 8) == 1) { if (r32 == back) goto c2_lane; }
                else { if (cur != 8) goto c2_lane; steer_stop(c); }
                steer_turn(c, 0x20); cv = 8; goto c2_post;
            }
        c2_lane:
            if (c->lane_mode <= 2) {
                if (rx > 8 && side_b == 0 && r50 != 0) steer_lane(c, l48);
                else if (rx < -8 && side_a == 0 && r32 != 0) steer_lane(c, l44);
            }
            cv = 0;
        c2_post:
            if (s->u4b == 0 && s->u12 == 0 && ax <= 0x100) {
                int lo = -8;   /* (a variable: the test is the original's, it can never hold) */
                if (ry > 8 && ry < lo) {
                    /* (never: the original tests ry > 8 and ry < -8, so a car going +y never lines up) */
                    steer_beside(s, c->spr.x < tgt_x ? 0x100 : 0x300);
                    if (ax < 0x40) steer_stop(c);
                } else if (ax < 0x100 && ry > 0x200 && diag_road(dx, dy)) {
                    s->u12 = 1;
                }
            }
            back = 1;
            break;
        case 4:
            near = abs((int)rx) < 0x200 && abs((int)ry) < 0x100;
            if (ry < 0 && (r32 == cur || (near && (r32 == 4 || r32 == 8))) && side_a == 0) l44 = 4, l48 = 1;
            else if (ry > 0 && (r50 == cur || (near && (r50 == 4 || r50 == 8))) && side_b == 0) l44 = 2, l48 = 3;
            else if (c->lane_mode == 0) l44 = 2, l48 = 1;
            ax = abs((int)ry), ay = abs((int)rx);
            if ((ax < 0x100 && rx < -0x200) || ax > 0x100) {
                if (cur > 2) goto c4_scan;
            } else if (cur > 2) {
                goto c4_lane;
            }
            if (ax <= ay) goto c4_scan;
            if (ry > 0x80 && r50 != 0 && (cur & 1)) {
                if (sentinel_is_lane_clear(px, py, pz, c, 1) == 1 && r50 != back) { steer_turn(c, -0x20); cv = 1; goto c4_post; }
            } else if (ry < -0x80 && r32 != 0 && (cur & 2)) {
                if (sentinel_is_lane_clear(px, py, pz, c, 2) == 1 && r32 != back) { steer_turn(c, 0x20); cv = 2; goto c4_post; }
            }
        c4_scan:
            if (((cur & 1) && ry > 0 && map_scan_lane_length(px, py, 1, 8) == 1) || cur == 1) {
                if (sentinel_is_lane_clear(px, py, pz, c, 1) == 1) { if (r50 == back) goto c4_lane; }
                else { if (cur != 1) goto c4_lane; c->brake = (int16_t)cur, c->input = 0; }
                steer_turn(c, -0x20); cv = 1; goto c4_post;
            }
            if (((cur & 2) && ry < 0 && map_scan_lane_length(px, py, 2, 8) == 1) || cur == 2) {
                /* (no braking when the lane isn't clear, unlike the other cases) */
                if (sentinel_is_lane_clear(px, py, pz, c, 2) != 1 && cur != 2) goto c4_lane;
                if (r32 == back) goto c4_lane;
                steer_turn(c, 0x20); cv = 2; goto c4_post;
            }
        c4_lane:
            if (c->lane_mode <= 2) {
                if (ry > 8 && r50 != 0 && side_b == 0) steer_lane(c, l48);
                else if (ry < -8 && r32 != 0 && side_a == 0) steer_lane(c, l44);
            }
            cv = 0;
        c4_post:
            if (s->u4b == 0 && s->u12 == 0 && ax <= 0x100) {
                if (rx < 8 && rx > -8) {
                    steer_beside(s, c->spr.y < tgt_y ? 0x3ff : 0x200);
                    if (ax < 0x40) steer_stop(c);
                } else if (ax < 0x100 && rx < -0x200 && diag_road(dx, dy)) {
                    s->u12 = 1;
                }
            }
            back = 8;
            break;
        case 8:
            near = abs((int)rx) < 0x200 && abs((int)ry) < 0x100;
            if (ry > 0 && (r32 == cur || (near && (r32 == 8 || r32 == 4))) && side_a == 0) l44 = 4, l48 = 1;
            else if (ry < 0 && (r50 == cur || (near && (r50 == 8 || r50 == 4))) && side_b == 0) l48 = 3, l44 = 2;
            else if (c->lane_mode == 0) l44 = 2, l48 = 1;
            ax = abs((int)ry), ay = abs((int)rx);
            if ((ax < 0x100 && rx > 0x200) || ax > 0x100) {
                if (cur > 2) goto c8_e57;
            } else if (cur > 2) {
                goto c8_lane;
            }
            /* (this direction skips when the other axis is not farther, the others when it is) */
            if (ay <= ax) goto c8_e57;
            if (ry < -0x80 && r50 != 0 && (cur & 2)) {
                if (sentinel_is_lane_clear(px, py, pz, c, 2) == 1 && r50 != back) { steer_turn(c, -0x20); cv = 2; goto c8_post; }
                goto c8_e57;
            }
            if (ry > 0x80 && r32 != 0) {
                if (!(cur & 1)) goto c8_e81;
                if (sentinel_is_lane_clear(px, py, pz, c, 1) == 1 && r32 != back) { steer_turn(c, 0x20); cv = 1; goto c8_post; }
            }
        c8_e57:
            if ((cur & 1) && ry > 0 && map_scan_lane_length(px, py, 1, 4) == 1) goto c8_f1c;
        c8_e81:
            if (cur == 1) goto c8_f1c;
            if (((cur & 2) && ry < 0 && map_scan_lane_length(px, py, 2, 4) == 1) || cur == 2) {
                if (sentinel_is_lane_clear(px, py, pz, c, 2) == 1) { if (r50 == back) goto c8_lane; }
                else { if (cur != 2) goto c8_lane; steer_stop(c); }
                steer_turn(c, -0x20); cv = 2; goto c8_post;
            }
            goto c8_lane;
        c8_f1c:
            if (sentinel_is_lane_clear(px, py, pz, c, 1) == 1) {
                if (r32 == back) goto c8_lane;
                steer_turn(c, 0x20);
            } else {
                if (cur != 1) goto c8_lane;
                /* (braking, it turns the other way: -0x20) */
                c->brake = (int16_t)cur, c->input = 0;
                steer_turn(c, -0x20);
            }
            cv = 1;
            goto c8_post;
        c8_lane:
            if (c->lane_mode <= 2) {
                if (ry > 8 && r32 != 0 && side_a == 0) steer_lane(c, l44);
                else if (ry < -8 && r50 != 0 && side_b == 0) steer_lane(c, l48);
            }
            cv = 0;
        c8_post:
            if (s->u4b == 0 && s->u12 == 0 && ax <= 0x100) {
                if (rx > -8 && rx < 8) {
                    steer_beside(s, c->spr.y > tgt_y ? 0x200 : 1);
                    if (ax < 0x40) { steer_stop(c); c->unkc0 = 1; }
                } else if (ax < 0x100 && rx > 0x200 && diag_road(dx, dy)) {
                    s->u12 = 1;
                }
            }
            back = 4;
            break;
        default:
            cv = 0;
        }
    } else {
        cv = 0;
    }

    /* ---- a dead end ahead: into the free side */
    if ((b1 & 0xf) == 0 && c->turn_delta == 0) {
        if (r32 != 0) steer_turn(c, 0x20), c->speed = 2;
        else if (r50 != 0) steer_turn(c, -0x20), c->speed = 2;
    }
    /* ---- the junction ring: the same turn near a remembered one is cancelled, a new one remembered */
    if (cv != 0) {
        bool again = false;
        for (int i = 0; i < 10; i++) {
            const uint8_t *e = &s->u77[3 * i];
            if (e[0] < px + 4 && e[0] > px - 4 && e[1] < py + 4 && e[1] > py - 4 && e[2] == cv && (c->road_dirs & cur)) {
                c->turn_delta = 0;
                again = true;
                break;
            }
        }
        if (!again) {
            if (c->speed >= 10) c->speed = 10;
            if (++s->u76 == 10) s->u76 = 0;
            uint8_t *e = &s->u77[3 * s->u76];
            e[0] = (uint8_t)bx, e[1] = (uint8_t)by, e[2] = (uint8_t)cv;
        }
    }
    if ((int8_t)tcl(lin(g_sent_bx, g_sent_by, g_sent_bz)) < 0 && c->speed > 0xf) c->speed -= 2;   /* on a slope */
    /* ---- the target car coming the other way beside us */
    if (crim->kind == 0 && s->u4b == 0 && near && tcar->road_dirs == back && tcar->speed != 0) {
        if (c->speed < 9) {
            s->u12 = 1;
        } else {
            switch (tcar->road_dirs) {
            case 1: s->u4c = 0x200; break;
            case 2: s->u4c = 0; break;
            case 4: s->u4c = 0x300; break;
            case 8: s->u4c = 0x100; break;
            }
            s->u4b = 1;
        }
    }
    if (abs((int)rz) > 0x20) s->u4b = 0;

    if ((int8_t)s->u4b > 0) {
        /* ---- lined up: stop beside the target, nose toward the free side */
        if (c->speed > 0xd) c->speed = 0xd;
        c->input = -10;
        if (c->turn_delta == 0) {
            int which = 0;   /* 1: L4ef (r50 free: -0x20, else +0x20), 2: L51f (r32 free: +0x20, else -0x20), 3 +, 4 - */
            c->u98 = 0;
            int d = c->road_dirs;
            if (crim->kind == 0 && tcar->road_dirs == back) {
                switch (d) {
                case 1: which = rx > 0 ? 4 : 2; break;
                case 2: which = rx > 0 ? 3 : 1; break;
                case 4: which = ry > 0 ? 3 : 1; break;
                case 8: which = ry > 0 ? 4 : 2; break;
                }
            } else {
                switch (d) {
                case 1: which = rx < 0 ? 4 : 2; break;
                case 2: which = rx < 0 ? 3 : 1; break;
                case 4: which = ry < 0 ? 3 : 1; break;
                case 8: which = ry < 0 ? 4 : 2; break;
                }
            }
            if (which == 1) which = r50 != 0 ? 4 : 3;
            else if (which == 2) which = r32 != 0 ? 3 : 4;
            if (which == 3) c->turn_delta = 0x20;
            else if (which == 4) c->turn_delta = -0x20;
            if (c->brake == 1) {
                if (c->speed < 0xd) c->speed = 0;
                else c->speed--;
            }
        }
        c->brake = 1;
        if (c->speed == 0) s->u4b = 0;
        if (s->u4b != 0) return;
        int m = abs((int)rx) > abs((int)ry) ? abs((int)rx) : abs((int)ry);
        if (m < 0x100) s->u4b = 1;
        return;
    }

    /* ---- driving on */
    if ((int8_t)s->u12 > 0) {
        if (s->u12 == 1 && cur != 1 && cur != 2 && cur != 4 && cur != 8) {
            s->u12 = 0;
        } else {
            c->unkc0 = 1;
            sentinel_handle_stuck(s, g_sent_bx, g_sent_by, g_sent_bz);
        }
    }
    car_remove_in_square(g_sent_bx << 6, g_sent_by << 6, g_sent_bz << 6, 2, c->id, s->kind != SENT_DUMMY);
    int16_t right = 1, left = 1;   /* local_44, local_48: the side lanes from one block behind */
    int16_t block = (int16_t)sentinel_check_ahead(s, g_sent_bx, g_sent_by, g_sent_bz, 0);
    if ((tcl(lin(g_sent_bx + dy, g_sent_by - dx, g_sent_bz)) & 0x70) == 0x20)
        right = (int16_t)sentinel_check_ahead(s, dy - dx + g_sent_bx, g_sent_by - dx - dy, g_sent_bz, 5);
    if ((tcl(lin(g_sent_bx - dy, g_sent_by + dx, g_sent_bz)) & 0x70) == 0x20)
        left = (int16_t)sentinel_check_ahead(s, g_sent_bx - dx - dy, dx - dy + g_sent_by, g_sent_bz, 5);
    if (block != 0) {
        mission_clear_row(g_sent_bx << 6, g_sent_by << 6, g_sent_bz << 6, c->id, block, dx, dy);
    } else if (cur == back && left == 0) {
        c->saved_x = c->spr.x, c->saved_y = c->spr.y;   /* Car_GetX / Car_GetY */
        c->lane_mode = 3;                               /* Car_SetLaneMode3 0x40be30 */
    }
    /* the lane the last turn leads into (by the previous road bits, +0xd4) */
    int want = 0;
    bool turning = c->turn_delta != 0;                  /* Car_IsTurning 0x40be00 */
    switch (c->prev_dirs) {                             /* Car_GetPrevRoadDir 0x40be80 */
    case 1: want = turning ? 4 : 8; break;
    case 2: want = turning ? 8 : 4; break;
    case 4: want = turning ? 2 : 1; break;
    case 8: want = turning ? 1 : 2; break;
    }
    if ((want & cur) == 0) {
        if (block == 0) goto plain;
        if (block <= 2) goto moving;
        goto check_moving;
    }
    if (sentinel_is_lane_clear(g_sent_bx, g_sent_by, g_sent_bz, c, want) == 1) {
        block = 0;
        goto plain;
    }
    if (sentinel_find_overtake_lane(c, g_sent_bx, g_sent_by, g_sent_bz) != 0) return;
    if (!car_is_on_screen(c)) sentinel_check_ahead(s, g_sent_bx, g_sent_by, g_sent_bz, -1);
    block = 3;
check_moving:
    if (c->speed > 0) goto moving;                      /* Car_IsMovingForward 0x40be60 */
plain:
    if (s->u20 != 0) goto moving;
    if (s->u12 != 0) goto speed_up;
    if (!car_is_on_screen(c) && c->turn_delta == 0) {
        c->speed = c->max_speed;                        /* Car_SetSpeed (Car_GetMaxSpeed) */
        car_end_brake(c);
    } else if (c->speed <= 0) {
        c->input = c->accel;
        c->udc = 0;
        car_end_brake(c);
    } else {
        if (c->speed < c->max_speed && (c->turn_delta == 0 || c->speed < c->cruise)) {
            c->unkc0 = 1;
            c->input = c->accel;
        }
        car_end_brake(c);
    }
    goto speed_up;
moving:
    if (c->speed <= 0) {
        if (sentinel_find_overtake_lane(c, g_sent_bx, g_sent_by, g_sent_bz) == 1) return;
        if (!car_is_on_screen(c)) block = (int16_t)sentinel_check_ahead(s, g_sent_bx, g_sent_by, g_sent_bz, -1);
        goto speed_up;
    }
    if (cur != 0) {
        if (block <= 5) {
            if (c->speed > 0x14) c->input = 0;          /* Car_ClearAccel 0x40c080 */
            car_begin_brake(c);
            if (c->speed > 3) c->speed -= 3;
        }
        if (block <= 3) {
            if (c->speed > 8) {
                car_begin_brake(c);
                c->input = 0;
                c->speed--;
            } else {
                c->speed--;                             /* Car_DecSpeed 0x40bfa0 */
            }
        }
        if (block <= 2) {
            if (c->control == 3 && c->horn == 1) c->horn = 2;
            car_begin_brake(c);
            c->input = 0;
        } else if (c->control == 3 && c->horn == 2) {
            c->horn = 1;
        }
    }
    if (s->kind == SENT_POLICE && s->state == 1) block = (int16_t)sentinel_check_obstacle(s, g_sent_bx, g_sent_by, g_sent_bz, 5);
    if (block == 0) goto speed_up;
    car_save_pos(c);
    c->uc4 = 8;
    /* a lane change away from what blocks: 4 to the side of r32, 3 to the side of r50 */
    if (right == 0) {
        if (left == 0) {
            if (r32 == c->road_dirs) c->lane_mode = 4;
            else if (r50 == c->road_dirs) c->lane_mode = 3;
            goto lane_b;
        }
        if (r32 == c->road_dirs) {
            c->lane_mode = 4;
            goto lane_b;
        }
    }
    if (left == 0 && r50 == c->road_dirs) {
        c->lane_mode = 3;
    } else if (left < right) {
        if (right >= block && r32 == c->road_dirs) c->lane_mode = 4;
    } else if (left > right) {
        if (left > block + 2 && r50 == c->road_dirs) c->lane_mode = 3;
    }
    if (right != 0) goto lane_c;
lane_b:
    if (left <= block) goto lane_back;
lane_c:
    if (right <= block || right <= left) goto speed_up;
lane_back:
    /* nothing better on the sides: change lane when the lane two blocks back on the right is free */
    if (s->kind != SENT_ROUTE && sentinel_check_ahead(s, dy - 2 * dx + g_sent_bx, g_sent_by - 2 * dy - dx, g_sent_bz, 10) == 0)
        c->lane_mode = 4;
speed_up:
    if (c->lane_mode > 2 && c->speed < 8 && block > 3) {
        c->speed++;                                     /* Car_IncSpeed 0x40bf90 */
        c->input = c->accel;                            /* Car_SetAccelFromInfo 0x40c090 */
        car_end_brake(c);
    }
    sentinel_override_lights(s, c);
}
