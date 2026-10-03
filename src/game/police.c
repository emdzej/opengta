/* The police module 0x464e20-0x46a09f (police.h): the criminal record screen, the patrol cars, the
   pursuit groups' cops, roadblocks and the police controller state machine Cop_Update 0x466f10.
   Controllers are sentinel records of kind 2 (sentinel.h); the criminal records and pursuit groups are
   wanted.h's. See docs/police.md for the states. */
#include "police.h"
#include "../audio/audio.h"
#include "../exe.h"
#include "../hud/hud.h"
#include "../render/sprite.h"
#include "../text.h"
#include "car.h"
#include "coll.h"
#include "game.h"
#include "gmath.h"
#include "lights.h"
#include "mission_obj.h"
#include "obj.h"
#include "path.h"
#include "ped.h"
#include "player.h"
#include "route.h"
#include "sentinel.h"
#include "wanted.h"
#include <stdio.h>
#include <string.h>

/* from ped_internal.h (which includes stubs.h, whose old police stubs clash until integration) */
bool ped_is_near_screen(const Ped *p);       /* Ped_IsNearScreen 0x4536d0 */
void ped_set_destination(Ped *p, int32_t x, int32_t y, int angle, int mode);   /* Ped_SetDestination 0x45f780 */

/* STUBS NEEDED (not ported elsewhere yet) */
bool obj_is_on_screen(const Obj *o);         /* Obj_IsOnScreen 0x44c2f0 (stubs.h) */
void obj_remove_moving(int obj);             /* Obj_RemoveMoving 0x44eb30 (stubs.h) */
int map_test_block_attr(int what, int bx, int by, int bz);   /* Map_TestBlockAttr 0x44b310 (stubs.h) */
/* Ref_GetKind1PosRect 0x45fb60: the position record of train n {x, y, z, ?, speed} (16.16); trains
   aren't ported: a stub returning a record of zeros will do */
const int32_t *ref_get_kind1_pos_rect(int train);

/* MODULE GLOBALS */
int16_t g_police_cars[POLICE_CARS_MAX];
int16_t g_police_ncars;
int16_t g_police_obj_queue[POLICE_OBJ_QUEUE_MAX];
int16_t g_police_nobj;
static char pv_502f78[0x100];               /* 0x502f78 the criminal record text */

/* ---------------------------------------------------------------- helpers */

/* the cached block type 0x55fab0 [z][y][x] (outside the table 0: the original reads past it) */
static uint8_t tc(int x, int y, int z)
{
    if (!g_game.map || (unsigned)x >= MAP_W || (unsigned)y >= MAP_H || (unsigned)z >= MAP_Z) return 0;
    return g_game.map->type_cache[z][y][x];
}
static int blk(int32_t v) { return v >> 22; }
/* |a - b| in blocks the way the original takes it: the difference shifted, its sign removed */
static int dblk(int32_t a, int32_t b)
{
    int32_t d = a - b;
    return d < 0 ? -(d >> 22) : d >> 22;
}
static int imax(int a, int b) { return a > b ? a : b; }
static int iabs(int a) { return a < 0 ? -a : a; }

/* the criminal record a pursuit group chases (+0x0a); the original indexes the table with whatever
   the controller holds, -1 included (port: -1 outside the four groups) */
static int pursuit_crim(int p) { return p >= 0 && p < PURSUITS ? g_pursuits[p].criminal : -1; }
static Pursuit *pursuit_of(const Sentinel *s) { return s->pursuit >= 0 && s->pursuit < PURSUITS ? &g_pursuits[s->pursuit] : NULL; }
static Criminal *crim_of(const Sentinel *s) { return police_get_criminal(pursuit_crim(s->pursuit)); }

/* the junction override / roadblock record (lights.h) and its car list (shorts at +0x12) */
static JunctionOvr *rb_get(int j) { return &g_junction_ovr[j]; }
static int rb_car(const JunctionOvr *j, int i)
{
    int16_t v;
    if (i < 0 || i >= ROADBLOCK_CARS) return j->u5a;   /* (the original's index past the list reads +0x5a) */
    memcpy(&v, j->u12 + 2 * i, 2);
    return v;
}
static void rb_set_car(JunctionOvr *j, int i, int v)
{
    int16_t s = (int16_t)v;
    if (i < 0 || i >= ROADBLOCK_CARS) return;   /* port: no write past the list */
    memcpy(j->u12 + 2 * i, &s, 2);
}

/* a ped animation "walking or standing" (frames 1..0x10, or 0x88) */
static bool anim_walking(int a) { return (a != 0 && a < 0x11) || a == 0x88; }

/* the criminal's position: kind 0 his car, 1 him; other kinds leave *x, *y as they are */
static void crim_pos(const Criminal *c, int32_t *x, int32_t *y)
{
    if (c->kind == 0) {
        const Car *t = car_get(c->car);
        *x = t->spr.x, *y = t->spr.y;
    } else if (c->kind == 1) {
        const Ped *t = ped_get(c->ped);
        *x = t->spr.x, *y = t->spr.y;
    }
}

/* the controller's cop on foot (+0x60). The states that use it expect one; with -1 the original reads
   the record before the ped table: the port hands out a scratch record instead. */
static Ped *cped(const Sentinel *s)
{
    static Ped scratch;
    if (s->u60 >= 0 && s->u60 < PED_MAX) return ped_get(s->u60);
    memset(&scratch, 0, sizeof scratch);
    scratch.id = -1;
    return &scratch;
}

static void close_door1(int car) { while (!car_close_door1_step(car)) {} }

/* the player's kill / crime counters (Player_GetKills 0x462910, Player_ResetKills 0x462930): shorts
   at +0xfc (current) of the player record */
static int16_t kills_get(int n, int k)
{
    int16_t v;
    memcpy(&v, (const uint8_t *)g_players[n].stats + 2 * k, 2);
    return v;
}

/* ---------------------------------------------------------------- the object queue, the record */

/* Police_FlushObjectDeleteQueue 0x464e20: the queued objects (roadblock barriers) that stand still off
   screen are removed; the queue is compacted. */
void police_flush_object_delete_queue(void)
{
    int n = g_police_nobj;
    for (int i = 0; i < n; i++) {
        if (g_police_obj_queue[i] == -1) continue;
        Obj *o = obj_get(g_police_obj_queue[i]);
        if (o->speed != 0 || obj_is_on_screen(o)) continue;
        obj_remove_moving(o->id);
        g_police_obj_queue[i] = -1;
    }
    g_police_nobj = 0;
    for (int i = 0; i < n; i++)
        if (g_police_obj_queue[i] != -1) g_police_obj_queue[g_police_nobj++] = g_police_obj_queue[i];
}

/* Police_ShowCriminalRecord 0x464ec0: the FXT "crimes" text followed by "%s %d %s, " (0x4b2264) for
   each of player 0's crime counters 2..9 that isn't 0 (keys crimeRTA, HAR, HIJ, CAR, GTA, SHO, MUR,
   BAN), the second-to-last character made a full stop (the last ", " becomes ". "; with no crime at
   all the text's own second-to-last letter: kept), as subtitle 4; the counters cleared. The original
   sprintfs the buffer into itself. */
void police_show_criminal_record(void)
{
    static const uint32_t keys[8] = { 0x4b05ac, 0x4b05a0, 0x4b0594, 0x4b0588, 0x4b2258, 0x4b057c, 0x4b0570, 0x4b0564 };
    snprintf(pv_502f78, sizeof pv_502f78, "%s", text_get(exe_str(0x4b0540)));
    for (int k = 2; k <= 9; k++) {
        int n = kills_get(0, k);
        if (n == 0) continue;
        char tmp[sizeof pv_502f78];
        snprintf(tmp, sizeof tmp, exe_str(0x4b2264), pv_502f78, n, text_get(exe_str(keys[k - 2])));
        memcpy(pv_502f78, tmp, sizeof tmp);
    }
    size_t len = strlen(pv_502f78);
    if (len >= 2) pv_502f78[len - 2] = '.';   /* (len < 2: the original writes before the buffer) */
    hud_show_subtitle(4, pv_502f78);
    memset(g_players[0].stats, 0, 10 * sizeof(int16_t));   /* Player_ResetKills(0): ten shorts */
}

/* ---------------------------------------------------------------- controllers */

/* Cop_ResetToPatrol 0x4650e0: siren off (+0xc6 = 9 back to 0); a car that isn't a patrol car (+0x04
   != 1) is released. A patrol car with its crew aboard gets its driver slot back, heads for its
   route's first node (states 6 / 6, door closed, Cop_PathToRouteStart); a cop on foot is called back
   (state 0xbe, then 0xfe), revived if dead (health 100, standing). */
void cop_reset_to_patrol(Sentinel *s)
{
    Car *c = sentinel_car(s);
    s->u12 = 0;
    car_siren_off(c);
    if (c->sinking == 9) c->sinking = 0;
    if (s->u04 != 1) {
        cop_release(s);
        return;
    }
    if (s->u60 < 0) {
        c->driver = (int16_t)(c->id + 200);
        car_siren_off(c);
        if (c->sinking == 9) c->sinking = 0;
        s->u14 = 0;
        s->state = 6;
        s->sub = 6;
        s->u16 = 0;
        c->brake = 0;
        c->unkc0 = 0;
        close_door1(s->car_id);
        cop_path_to_route_start(s);
        return;
    }
    Ped *p = ped_get(s->u60);
    s->state = 0xbe;
    c->u244 = 0;
    if (p->health == 0) {
        p->health = 100;
        p->anim = 0x88;
    }
    s->sub = 0xfe;
}

/* Cop_CheckPedState 0x4651c0: a cop on foot (+0x60) that died, or whose ped is in state 5, 0x14 or
   0x15, leaves its pursuit (the criminal's record back to "started" 1, the controller 0xfe); during an
   arrest (states 0xd5..0xda) the criminal is also let go (state 2). 0 when dropped. States 0x32 and
   0xfe and a sub state 0xfe are left alone. */
int cop_check_ped_state(int si)
{
    Sentinel *s = sentinel_ptr(si);
    if (s->u60 == -1 || s->state == 0x32 || s->state == 0xfe || s->sub == 0xfe) return 1;
    const Ped *p = ped_get(s->u60);
    if (p->health != 0 && p->state != 5 && p->state != 0x14 && p->state != 0x15) return 1;
    if (pursuit_crim(s->pursuit) < 0) return 1;
    Criminal *cr = crim_of(s);
    if (!cr) return 0;
    cr->started = 1;
    if (s->state >= 0xd5 && s->state <= 0xda) ped_get(cr->ped)->state = 2;
    pursuit_remove_cop(s);
    s->state = 0xfe;
    return 0;
}

/* the driver a new police car gets: a cop (control 1, graphic 1, own remap) sitting in it */
static void seat_cop(Ped *p, const Car *c, int car_id)
{
    p->control = 1;
    p->u14 = (int16_t)car_id;
    p->graphic = 1;
    p->remap = 0;
    sprite_init(&p->spr, c->spr.x, c->spr.y, c->spr.z, c->spr.angle, sprite_group_base(SPRITE_GROUP_PED));
}

/* Police_SpawnPatrolCars 0x465300: one patrol car (model 4 on the road at the route's first node) per
   0xff route, the routes being the path slots from 50 + the 0xfe count on, while there are fewer than
   100: controller state 1, +0x04 = 1 (a patrol car), +0x08 and +0x44 its route, listed in 0x50f2a8.
   The controller is taken before the car: a full table is fatal in Sentinel_Get, as in the original;
   a car that can't be placed leaves the route unused but still counted. */
void police_spawn_patrol_cars(void)
{
    int fe, ff;
    const uint8_t *end;
    route_get_counts(&fe, &ff, &end);
    if (ff < 1) return;
    int slot = fe + PATH_ROUTE_FIRST;
    for (int i = 0; i < ff; i++) {
        if (g_police_ncars >= 100) continue;
        int si = (int16_t)sentinel_find_free();
        const uint8_t *node = g_path_slots[(int16_t)slot];
        int car = (int16_t)car_spawn_on_road(node[0] * 0x400000 + 0x200000, node[1] * 0x400000 + 0x200000,
                                             node[2] * 0x400000 + 0x3e0000, 4, 1);
        if (car >= 0) {
            Sentinel *s = sentinel_ptr(si);
            Car *c = car_get(car);
            c->owner_status = 1;
            c->control = 3;
            c->driver = (int16_t)(c->id + 200);
            c->counter119 = 1;
            c->sentinel = (int16_t)si;
            c->cruise = 6;
            c->unk139 = 0;
            s->u04 = 1;
            s->u21 = 0;
            s->u22 = 0;
            s->state = 1;
            s->route = (int16_t)slot;
            s->u08 = (int16_t)slot;
            s->u60 = -1;
            s->car_id = (int16_t)car;
            s->u38 = 0;
            sentinel_set_car(s, c);
            s->kind = SENT_POLICE;
            seat_cop(ped_get(c->driver), c, s->car_id);
            g_police_cars[g_police_ncars++] = s->id;
        }
        slot++;
    }
}

/* Police_SpawnCarAtTarget 0x4654c0: at the respawn block 0x74f850 (the first police station) */
int police_spawn_car_at_target(int s)
{
    uint32_t b = g_player_respawn_block;
    return police_spawn_car(s, (int)(b >> 8 & 0xff), (int)(b & 0xff), (int)(b >> 16 & 0xff));
}

/* the heading (0..1023) of road direction bits 1, 2, 4, 8 (others: unchanged) */
static void heading_of_dirs(Car *c, int dirs)
{
    switch (dirs) {
    case 1: c->spr.angle = 0x200; break;
    case 2: c->spr.angle = 0; break;
    case 4: c->spr.angle = 0x300; break;
    case 8: c->spr.angle = 0x100; break;
    }
}
/* the wheel points of a car facing `a`: front = centre - dir * length / 2, rear = centre + (the
   length is negative, so the front is ahead) */
static void set_wheels(Car *c, int a)
{
    int hl = c->length >> 1;
    c->front_x = c->spr.x - math_sin(a) * hl;
    c->front_y = c->spr.y - math_cos(a) * hl;
    c->rear_x = math_sin(a) * hl + c->spr.x;
    c->rear_y = math_cos(a) * hl + c->spr.y;
}

/* Police_SpawnCar 0x4654f0: a police car (model 4) on the road at the block's centre for controller s.
   Its driver slot must be free (anim 0 and dead) or unused (control -1), else the car is deleted.
   Facing the block's road direction, wheels placed; the controller: state 1, +0x44 its own number, no
   cop on foot (+0x60 -1), +0x48 -1, kind 2. Patrol car count >= 100: none. */
int police_spawn_car(int si, int bx, int by, int bz)
{
    if (g_police_ncars > 99) return -1;
    int car = (int16_t)car_spawn_on_road(bx * 0x400000 + 0x200000, by * 0x400000 + 0x200000,
                                         bz * 0x400000 + 0x3e0000, 4, 1);
    if (car < 0) return car;
    Sentinel *s = sentinel_ptr(si);
    Car *c = car_get(car);
    c->unk139 = 0;
    Ped *p = ped_get(c->id + 200);
    if ((p->anim != 0 || p->health != 0) && p->control != -1) {
        car_delete(c->id);
        return -1;
    }
    c->sentinel = (int16_t)si;
    c->owner_status = 0;
    c->control = 3;
    c->counter119 = 1;
    c->cruise = 6;
    c->driver = (int16_t)(c->id + 200);
    p->control = 1;
    p->graphic = 1;
    p->remap = 0;
    sprite_init(&p->spr, c->spr.x, c->spr.y, c->spr.z, c->spr.angle, sprite_group_base(SPRITE_GROUP_PED));
    p->u14 = s->car_id;   /* (the controller's old car: +0x1e is set below) */
    int dirs = tc(bx, by, bz) & 0xf;
    c->road_dirs = (uint16_t)dirs;
    heading_of_dirs(c, dirs);
    c->front_heading = c->spr.angle;
    set_wheels(c, c->spr.angle);
    s->route = s->id;
    sentinel_set_car(s, c);
    s->u21 = 0;
    s->u22 = 0;
    s->state = 1;
    s->car_id = (int16_t)car;
    s->u38 = 0;
    s->u60 = -1;
    s->u48 = -1;
    s->kind = SENT_POLICE;
    return car;
}

/* Pursuit_RemoveCop 0x465760: the controller leaves its pursuit group (the list compacted, the lead
   cleared if it was the lead) and goes home (state 0xfe). */
void pursuit_remove_cop(Sentinel *s)
{
    Pursuit *p = pursuit_of(s);
    if (!p) {   /* port: the original indexes the table with -1 */
        s->state = 0xfe;
        return;
    }
    for (int i = 0; i < p->ncops; i++) {
        if (p->cops[i] == s->id) {
            p->cops[i] = -1;
            break;
        }
    }
    int n = 0;
    if (p->ncops != 0) {
        for (int i = 0; i != p->ncops && i < PURSUIT_COPS; i++)
            if (p->cops[i] != -1) p->cops[n++] = p->cops[i];
    }
    p->ncops = (int8_t)n;
    if (p->lead == s->id) p->lead = -1;
    s->state = 0xfe;
}

/* Cop_Dismiss 0x465870: a cop car taken by `ped`: its controller leaves the pursuit and becomes a
   chaser (Police_AddChaser); its cop on foot, if alive, goes after the thief with a pistol
   (objective 0x1e, state 4). */
void cop_dismiss(Car *c, int ped)
{
    Sentinel *s = sentinel_ptr(c->sentinel);
    pursuit_remove_cop(s);
    police_add_chaser(s->id);
    if (s->u60 < 0) return;   /* port: Ped_Get(-1) in the original */
    Ped *p = ped_get(s->u60);
    if (p->health == 0) return;
    p->objective = 0x1e;
    ped_set_weapon(p->id, 1);
    p->state = 4;
    p->u78 = 8;
    p->u7c = 2;
    p->target_ped = (int16_t)ped;
    p->firing = 1;
}

/* Roadblock_RemoveCar 0x4658f0: the controller's car leaves its roadblock's list (+0x5e); a roadblock
   left without cars is freed (+0x0e, its entry in 0x50586c). */
void roadblock_remove_car(Sentinel *s)
{
    if (s->u5e < 0 || s->u5e >= JUNCTION_OVR_MAX) return;   /* (callers check >= 0) */
    JunctionOvr *j = rb_get(s->u5e);
    int n = j->u5a;
    if (n < 1) return;
    int i = 0;
    while (rb_car(j, i) != s->car_id) {
        if (++i >= j->u5a) return;   /* not in the list: +0x5e stays */
    }
    rb_set_car(j, i, -1);
    for (; i < j->u5a; i++) rb_set_car(j, i, rb_car(j, i + 1));
    rb_set_car(j, i, -1);   /* (the slot past the old last one, as the original) */
    j->u5a--;
    int k = 0;
    while (k < g_junction_nlist && g_junction_list[k] != s->u5e) k++;
    /* not found: the original uses the entry past the list's end */
    int jj = k < JUNCTION_LIST_MAX ? g_junction_list[k] : -1;
    if (jj >= 0 && jj < JUNCTION_OVR_MAX) {
        JunctionOvr *r = rb_get(jj);
        if (r->u5a < 1) {
            r->u0e = 0;
            r->u5a = 0;
            g_junction_list[k] = -1;
        }
    }
    s->u5e = -1;
}

/* Cop_Release 0x465a40: the controller and its car go: the barrier object (+0x6a) to the delete queue
   (more than 99 queued: fatal -0xa2), out of the pursuit and the roadblock, the cop on foot removed,
   the path search given up, the car deleted, the record reset. */
void cop_release(Sentinel *s)
{
    if (s->u6a != -1) {
        g_police_obj_queue[g_police_nobj++] = obj_get(s->u6a)->id;
        if (g_police_nobj > 99) game_fatal(-0xa2, 0xcc, -1);
        s->u6a = -1;
    }
    if (s->pursuit >= 0) pursuit_remove_cop(s);
    if (s->u5e >= 0) roadblock_remove_car(s);
    if (s->u60 >= 0) {
        Ped *p = ped_get(s->u60);
        if (p->health != 0) ped_remove(p->id);
    }
    if (g_path_owner == s->id) g_path_owner = -1;
    if (car_get(s->car_id)->status != -1) car_delete(s->car_id);
    sentinel_reset(sentinel_ai(s));
}

/* Police_FindNearestCar 0x465b20: of the patrol cars in states 1, 4, 6 the one nearest (x, y)
   (Manhattan distance in blocks), -1 */
int police_find_nearest_car(int32_t x, int32_t y)
{
    int best = 0x7fff, r = -1;
    for (int i = 0; i < g_police_ncars; i++) {
        Sentinel *s = sentinel_ptr(g_police_cars[i]);
        if (s->state != 1 && s->state != 4 && s->state != 6) continue;
        const Car *c = sentinel_car(s);
        int d = (iabs(x - c->spr.x) + iabs(y - c->spr.y)) >> 22;
        if ((int16_t)d < (int16_t)best) {
            r = g_police_cars[i];
            best = d;
        }
    }
    return r;
}

/* Pursuit_FindFarthestCop 0x465bc0: the cop of the group farthest (Manhattan, blocks) from the
   criminal; each new farthest one gets the distance in +0x46. -1 if none (or no criminal). */
int pursuit_find_farthest_cop(int pi)
{
    Criminal *cr = police_get_criminal(pursuit_crim(pi));
    if (!cr) return -1;
    int32_t tx = 0, ty = 0;
    crim_pos(cr, &tx, &ty);
    Pursuit *p = &g_pursuits[pi];
    int best = 0, r = -1;
    for (int i = 0; i < p->ncops; i++) {
        Sentinel *s = sentinel_ptr(p->cops[i]);
        const Car *c = sentinel_car(s);
        int d = (iabs(ty - c->spr.y) + iabs(tx - c->spr.x)) >> 22;
        if ((int16_t)best < (int16_t)d) {
            r = s->id;
            s->dist = (uint8_t)d;
            best = d;
        }
    }
    return r;
}

/* Pursuit_FindNearestCop 0x465cf0: the cop of the group nearest the criminal, -1 */
int pursuit_find_nearest_cop(int pi)
{
    Criminal *cr = police_get_criminal(pursuit_crim(pi));
    if (!cr) return -1;
    int32_t tx = 0, ty = 0;
    crim_pos(cr, &tx, &ty);
    Pursuit *p = &g_pursuits[pi];
    int best = 9999999, r = -1;
    for (int i = 0; i < p->ncops; i++) {
        Sentinel *s = sentinel_ptr(p->cops[i]);
        const Car *c = sentinel_car(s);
        int d = (int16_t)((iabs(ty - c->spr.y) + iabs(tx - c->spr.x)) >> 22);
        if (d < best) {
            r = s->id;
            best = d;
        }
    }
    return r;
}

/* Cop_HasClearLine 0x465e30: from the cop car toward the criminal, n (at most 15) steps of a block:
   0 if one lands in a building block (type 5), else 1; -1 without a criminal. A criminal on a train
   (or of another kind) gives the direction of (0, 0). */
int cop_has_clear_line(Sentinel *s, int n)
{
    const Car *c = sentinel_car(s);
    Criminal *cr = crim_of(s);
    if (!cr) return -1;
    int32_t dx = 0, dy = 0;
    if (cr->kind == 0 || cr->kind == 1) {
        int32_t tx = 0, ty = 0;
        crim_pos(cr, &tx, &ty);
        dx = tx - c->spr.x, dy = ty - c->spr.y;
    } else if (cr->kind == 2) {
        ref_get_kind1_pos_rect(cr->train);   /* (fetched and not used) */
    }
    int a = (int16_t)math_atan2(dy, dx);
    if ((int16_t)n > 0xf) n = 0xf;
    for (int i = 0; i < (int16_t)n; i++) {
        int x = (math_sin(a) * i * 0x40 + c->spr.x) >> 22;
        int y = (i * 0x40 * math_cos(a) + c->spr.y) >> 22;
        if ((tc(x, y, c->spr.z >> 22) & 0x70) == 0x50) return 0;
    }
    return 1;
}

/* Pursuit_UpdateLead 0x465f90: a cop within 16 blocks becomes the group's lead if there is none;
   the group's +0x06 is cleared (in every case but a close cop without a clear line). */
void pursuit_update_lead(Sentinel *s)
{
    Criminal *cr = crim_of(s);
    if (!cr) return;
    if (cr->kind == 2) ref_get_kind1_pos_rect(cr->train);   /* (fetched and not used) */
    Pursuit *p = &g_pursuits[s->pursuit];
    if (s->dist < 0x10) {
        int r = (int16_t)cop_has_clear_line(s, s->dist);
        if (p->lead == -1) {
            p->lead = s->id;
            p->u06 = 0;
        }
        if (r != 1) return;
    }
    p->u06 = 0;
}

/* Cop_PathToRouteStart 0x466070: a patrol car (+0x04 = 1; others go home, 0xfe) asks the path finder
   (mode 3) for the way from its block to its route's first node, if it stands on a road. */
void cop_path_to_route_start(Sentinel *s)
{
    if (s->u04 != 1) {
        s->state = 0xfe;
        return;
    }
    Car *c = sentinel_car(s);
    s->u38 = 0;
    c->counter119 = 1;
    s->route = s->id;
    s->u48 = s->u08;
    int bx = blk(c->spr.x) & 0xff, by = blk(c->spr.y) & 0xff, bz = blk(c->spr.z) & 0xff;
    if ((tc(bx, by, bz) & 0xf) == 0) return;
    const uint8_t *node = g_path_slots[s->u08];
    s->dest[0] = node[0], s->dest[1] = node[1], s->dest[2] = node[2];
    g_path_result = (int16_t)path_find(bx, by, bz, s->dest[0], s->dest[1], s->dest[2], 3, (uint8_t)s->route);
}

/* Cop_PathToTarget 0x466180: the way (mode 2) from the car's block to the respawn block (the police
   station); off the road the controller is released. */
void cop_path_to_target(Sentinel *s)
{
    s->u38 = 0;
    s->route = s->id;
    Car *c = sentinel_car(s);
    c->counter119 = 1;
    int bz = blk(c->spr.z), by = blk(c->spr.y), bx = blk(c->spr.x);
    if ((tc(bx, by, bz) & 0xf) == 0) {
        cop_release(s);
        return;
    }
    uint32_t b = g_player_respawn_block;
    s->dest[0] = (uint8_t)(b >> 8), s->dest[1] = (uint8_t)b, s->dest[2] = (uint8_t)(b >> 16);
    g_path_result = (int16_t)path_find(bx, by, bz, s->dest[0], s->dest[1], s->dest[2], 2, (uint8_t)s->route);
}

/* the step (dx, dy) of road direction bits 1 (-y), 2 (+y), 4 (-x), 8 (+x); others (0, 0) */
static void dir_step(int dirs, int *dx, int *dy)
{
    *dx = 0, *dy = 0;
    switch (dirs) {
    case 1: *dy = -1; break;
    case 2: *dy = 1; break;
    case 4: *dx = -1; break;
    case 8: *dx = 1; break;
    }
}

/* Roadblock_TryPlaceAhead 0x466230: with the criminal in a car at wanted level 3 or more and the
   mission's roadblocks on (header value 3, 0x505efa), the 11 blocks ahead of his car along its road
   direction are searched for a junction (Map_TestBlockAttr 2). At the first one, if its traffic light
   exists: a free junction (+0x0e 0) gets a roadblock at each vertex of the CMP set named by the
   light's angle, while fewer than 0x14 roadblocks stand (then +0x0e = 1, timer 400, listed in
   0x50586c); one that has a roadblock already gets its timer back to 400. */
void roadblock_try_place_ahead(Sentinel *s)
{
    Criminal *cr = crim_of(s);
    if (!cr) return;
    if (player_get_wanted_level_by_ped(cr->ped) < 3) return;
    if (g_mission_var505efa == 0) return;
    if (cr->kind != 0) return;
    const Car *t = car_get(cr->car);
    int dx, dy;
    dir_step(t->road_dirs, &dx, &dy);
    int z = t->spr.z;
    int x = t->spr.x >> 22, y = t->spr.y >> 22;
    for (int n = 10; n >= 0; n--) {
        if ((int8_t)map_test_block_attr(2, x, y, z >> 22) == 1) {
            int ji = (uint8_t)lights_query(0x3a, x, y);
            JunctionOvr *j = rb_get(ji);
            if (j->obj < 0) return;
            if ((unsigned)j->obj_angle >= ROADBLOCK_SETS) return;   /* port: the original indexes past the sets */
            const RoadblockSet *set = &g_roadblock_sets[j->obj_angle];
            const BlockXYZ *v = set->v;
            bool placed = false;
            for (int k = set->n; k != 0; k--, v++) {
                if (j->u0e == 0) {
                    if (g_junction_nlist < 0x14) {
                        roadblock_spawn(v, ji, s->pursuit);
                        placed = true;
                    }
                } else if (j->u0e == 1) {
                    j->u10 = 400;
                }
            }
            if (placed) {
                j->u0e = 1;
                g_junction_list[g_junction_nlist++] = j->id;
                j->u10 = 400;
            }
            return;
        }
        x += dx;
        y += dy;
    }
}

/* Roadblock_Spawn 0x466450: from vertex v, across the road (perpendicular to its direction bits:
   1 -x, 2 +x, 4 +y, 8 -y; style 2 starts one block back) the road blocks (type 2) up to 6 are counted.
   Unless the first is near a screen, every other one gets a police car (Police_SpawnCar on a fresh
   controller) turned across the road, its cop standing on the next block (state 0x96: waiting at the
   roadblock, +0x5e = the junction, the car listed in the junction's cars) and a barrier object (type
   0x18) beside him. Quirks kept: Car_RemoveInSquare gets the block coordinates >> 6 (so it clears the
   map's corner, not the block); a criminal record missing ends it after the car; a controller or car
   that can't be had advances one block instead of two; for direction bits other than 1, 2, 4, 8 the
   angles and offsets of the previous car are used. */
void roadblock_spawn(const BlockXYZ *v, int ji, int pursuit)
{
    int z = v->z, x = v->x, y = v->y;
    int sx = 0, sy = 0;
    int dir = (tc(x, y, z) & 0xf) - 1;
    int dx, dy;
    switch (dir) {
    case 0: dx = -1, dy = 0; break;
    case 1: dx = 1, dy = 0; break;
    case 3: dx = 0, dy = 1; break;
    case 7: dx = 0, dy = -1; break;
    default: dx = 0, dy = 0; break;
    }
    if (style_requested() == 2) {
        x -= dx;
        y -= dy;
    }
    int count = 0;
    bool first = false;
    for (int i = 0; i < 6; i++) {
        if ((tc(x, y, z) & 0x70) != 0x20) break;
        count++;
        if (!first) {
            first = true;
            sx = x, sy = y;
        }
        x += dx;
        y += dy;
    }
    if (pos_is_near_screen(sx << 22, sy << 22) || count <= 0) return;
    JunctionOvr *j = rb_get(ji);
    int angle = 0;
    int32_t ox = 0, oy = 0;
    for (int k = 0; k < count; k += 2) {
        int si = (int16_t)sentinel_find_free();
        if (si != -1) {
            car_remove_in_square(sx >> 6, sy >> 6, z >> 6, 0, -1, 1);
            int car = (int16_t)police_spawn_car(si, sx, sy, z);
            if (!police_get_criminal(pursuit_crim(pursuit))) return;
            if (car != -1) {
                rb_set_car(j, j->u5a, car);
                j->u5a++;
                switch (dir) {
                case 0: angle = 0x2f8; break;
                case 1: angle = 0xfa; break;
                case 3: angle = 6; break;
                case 7: angle = 0x206; break;
                }
                Car *c = car_get(car);
                c->spr.angle = (int16_t)angle;
                c->front_heading = (int16_t)angle;
                c->owner_status = 1;
                c->sentinel = (int16_t)si;
                int dirs = tc(sx, sy, z) & 0xf;
                c->road_dirs = (uint16_t)dirs;
                switch (dirs) {
                case 1: c->spr.angle = 0x2f8; break;
                case 2: c->spr.angle = 0xfa; break;
                case 4: c->spr.angle = 6; break;
                case 8: c->spr.angle = 0x1fc; break;
                }
                int a = c->spr.angle;
                c->next_heading = (int16_t)a;
                c->front_heading = (int16_t)a;
                set_wheels(c, a);
                c->next_x = c->spr.x, c->next_y = c->spr.y, c->next_z = c->spr.z;
                coll_build_box(c->spr.x, c->spr.y, c->spr.z, c->half_w, c->half_l, a, 0x18, &c->box_saved);
                car_commit_move(c);
                sx += dx;
                sy += dy;
                Sentinel *s = sentinel_ptr(si);
                switch (dir) {
                case 0: ox = 0x200000, oy = 0x80000; break;
                case 1: ox = 0x200000, oy = 0x180000; break;
                case 3: ox = 0x80000, oy = 0x200000; break;
                case 7: ox = 0x180000, oy = 0x200000; break;
                }
                Ped *p = ped_get(c->driver);
                int32_t pz = z * 0x400000 + 0x3f0000;
                ped_spawn_in_slot(sx * 0x400000 + ox, sy * 0x400000 + oy, pz, 0, 0, 0x62, c->driver, c->id);
                s->u60 = p->id;
                s->pursuit = (int16_t)pursuit;
                s->state = 0x96;
                s->u20 = 1;
                s->u5e = (int16_t)ji;
                s->u6f = 4;
                s->u48 = -2;
                p->speed = 0;
                p->graphic = 1;
                p->control = 1;
                p->u14 = s->car_id;
                p->car = (int16_t)car;
                p->u78 = 8;
                p->u7c = 8;
                p->walk_x = 0;
                p->walk_y = 0;
                p->objective = 0x20;
                p->target_x = 0;
                p->state = 3;
                p->target_ped = -1;
                p->firing = 0;
                bool set = true;
                switch (dir) {
                case 0: angle = 0; break;
                case 1: angle = 0x200; break;
                case 3: angle = 0x100; break;
                case 7: angle = 0x300; break;
                default: set = false;
                }
                if (set) ox = 0x200000, oy = 0x200000;
                int o = (int16_t)obj_create(sx * 0x400000 + ox, sy * 0x400000 + oy, pz, 0x18, angle);
                if (o != -1) {
                    obj_get(o)->spr.angle = (int16_t)angle;
                    s->u6a = (int16_t)o;
                }
            }
        }
        sy += dy;
        sx += dx;
    }
}

/* Pursuit_RecallCops 0x466a50: every cop of the group goes back: one out of sight (car and cop on
   foot) returns at once (a live cop on foot that isn't lying down is put back in as the driver;
   state 4), the others walk back (0xbe, then 4). A dead cop on foot out of sight ends it before the
   record is cleared. Then the group's criminal record is cleared (Police_ClearCriminal). The "near a
   screen" flag of a cop on foot carries over to the next cop whose car has no driver and no cop on
   foot (as the original). */
void pursuit_recall_cops(Sentinel *s)
{
    Pursuit *p = pursuit_of(s);
    if (!p) return;   /* port: -1 */
    int n = p->ncops;
    bool near = false;
    for (int i = 0; i < n; i++) {
        Sentinel *cs = sentinel_ptr(p->cops[i]);
        Car *c = sentinel_car(cs);
        bool on = car_is_on_screen(c);
        Ped *q = NULL;
        bool out;
        if ((cs->u20 == 1 && cs->u60 >= 0) || (c->driver < 0 && cs->u60 >= 0)) {   /* (port: no ped -1) */
            q = ped_get(cs->u60);
            near = ped_is_near_screen(q);
            out = true;
        } else {
            if (c->driver >= 0) {
                q = ped_get(c->driver);
                near = false;
            }
            out = false;
        }
        if (!on && !near) {
            if (out) {
                if (q->health == 0) return;
                if (q->anim != 0) {
                    c->driver = q->id;
                    ped_remove(q->id);
                    cs->u60 = -1;
                    cs->u20 = 0;
                }
            }
            cs->state = 4;
        } else {
            cs->state = 0xbe;
            cs->sub = 4;
        }
    }
    police_clear_criminal(g_pursuits[s->pursuit].criminal);
}

/* Cop_RecallOne 0x466bb0: the same for one controller */
void cop_recall_one(Sentinel *s)
{
    Car *c = sentinel_car(s);
    bool on = car_is_on_screen(c);
    Ped *q = NULL;
    bool near = false, out = false;
    if ((s->u20 == 1 && s->u60 >= 0) || (c->driver < 0 && s->u60 >= 0)) {   /* (port: no ped -1) */
        q = ped_get(s->u60);
        near = ped_is_near_screen(q);
        out = true;
    } else if (c->driver >= 0) {
        q = ped_get(c->driver);
    }
    if (!on && !near) {
        if (out) {
            if (q->health == 0) return;
            if (q->anim != 0) {
                c->driver = q->id;
                ped_remove(q->id);
                s->u60 = -1;
                s->u20 = 0;
            }
        }
        s->state = 4;
        return;
    }
    s->state = 0xbe;
    s->sub = 4;
}

/* Police_FindNearestTarget 0x466c70: of the four criminal records with a running pursuit (+0x0e = 1)
   the one nearest the cop car (larger of the block distances), -1. A record of another kind is
   measured at the previous record's position. */
int police_find_nearest_target(Sentinel *s)
{
    const Car *c = sentinel_car(s);
    int32_t tx = 0, ty = 0;
    int r = -1;
    uint8_t best = 0xff;
    for (int i = 0; i < CRIMINALS; i++) {
        Criminal *cr = police_get_criminal(i);
        if (cr->started != 1) continue;
        crim_pos(cr, &tx, &ty);
        int ax = iabs(c->spr.x - tx), ay = iabs(c->spr.y - ty);
        int d = ay < ax ? ax : ay;
        if ((uint8_t)(d >> 22) < best) {
            r = i;
            best = (uint8_t)(d >> 22);
        }
    }
    return r;
}

/* Cop_JoinNearestPursuit 0x466d90: with a criminal being chased nearby, the pursuit group of the same
   number (the record index is used as the group index, as in the original) loses its farthest cop if
   this car is nearer the criminal than that one. This car itself doesn't join here. */
void cop_join_nearest_pursuit(Sentinel *s)
{
    int t = (int16_t)police_find_nearest_target(s);
    if (t < 0) return;
    Criminal *cr = police_get_criminal(g_pursuits[t].criminal);
    if (!cr) return;
    int32_t tx = 0, ty = 0;
    crim_pos(cr, &tx, &ty);
    const Car *c = sentinel_car(s);
    int d = (int16_t)((iabs(ty - c->spr.y) + iabs(tx - c->spr.x)) >> 22);
    int f = (int16_t)pursuit_find_farthest_cop(t);
    if (f > -1 && d < sentinel_ptr(f)->dist) pursuit_remove_cop(sentinel_ptr(f));
}

/* Cop_SetSpeedByWanted 0x466e80: the car's top speed by the chased criminal's wanted level: 1 0x1e,
   2 0x23, 3 / 4 0x26, else (or not chasing) 0x19. */
void cop_set_speed_by_wanted(Sentinel *s)
{
    Car *c = sentinel_car(s);
    if (!c) return;
    if (s->pursuit > -1 && pursuit_crim(s->pursuit) > -1) {
        Criminal *cr = crim_of(s);
        if (!cr) return;
        switch (player_get_wanted_level_by_ped(cr->ped)) {
        case 1: c->max_speed = 0x1e; return;
        case 2: c->max_speed = 0x23; return;
        case 3: case 4: c->max_speed = 0x26; return;
        }
    }
    c->max_speed = 0x19;
}

/* ---------------------------------------------------------------- Cop_Update */

/* the cop car stops dead (state 0xf9) */
static int cop_go_home_now(Sentinel *s, int ret) { s->state = 0xf9; return ret; }
/* chase: the controller follows the criminal directly (state 0xcb), from the car's block */
static void chase_from_here(Sentinel *s, const Car *c)
{
    s->u4a = 1;
    s->state = 0xcb;
    s->u0c = (uint8_t)(c->spr.x >> 22);
    s->u0d = (uint8_t)(c->spr.y >> 22);
    s->u0e = (uint8_t)(c->spr.z >> 22);
}
/* the cop on foot goes for the criminal: stand and shoot (objective 0x20, state 3) */
static void cop_stand_and_shoot(Ped *p, int target)
{
    p->objective = 0x20;
    p->target_ped = (int16_t)target;
    p->speed = 0;
    p->state = 3;
    p->u7c = 8;
    p->u78 = 8;
    ped_set_weapon(p->id, 1);
}
/* the cop walks back to his car (objective 0x28) */
static void cop_back_to_car(Ped *p, int car)
{
    p->objective = 0x28;
    ped_send_to_car_door1(p, car);
}
/* take the car's road position of a door: car centre + door offset rotated by the heading, the door's
   y offset pushed out by `out` pixels */
static void door_point(const Car *c, int out, int32_t *x, int32_t *y)
{
    int a = c->spr.angle & 0x3ff, b = (c->spr.angle + 0x100) & 0x3ff;
    *x = math_sin(a) * c->door_dx + math_sin(b) * (c->door_dy + out) + c->spr.x;
    *y = math_cos(a) * c->door_dx + math_cos(b) * (c->door_dy + out) + c->spr.y;
}

/* state 0x96 (a cop at a roadblock): the car leaves the roadblock's list (swapped with the last) and
   takes the place of the group's farthest cop */
static int roadblock_cop_joins(Sentinel *s, bool clear_last)
{
    JunctionOvr *j = rb_get(s->u5e);
    int n = j->u5a;
    for (int i = 0; i < n; i++) {
        if (rb_car(j, i) != s->car_id) continue;
        rb_set_car(j, i, rb_car(j, n - 1));
        if (clear_last) rb_set_car(j, j->u5a - 1, -1);
        j->u5a--;
        break;
    }
    int f = (int16_t)pursuit_find_farthest_cop(s->pursuit);
    if (f > -1) {
        pursuit_remove_cop(sentinel_ptr(f));
        Pursuit *p = &g_pursuits[s->pursuit];
        p->cops[p->ncops] = s->id;
        p->ncops++;
    }
    return 0;
}

/* Cop_Update 0x466f10: one step of a police controller (states in docs/police.md). 1 = the driving
   code (Sentinel_DriveCar) steers the car on, 0 = not this frame. */
int cop_update(Sentinel *s)
{
    Car *c = sentinel_car(s);
    int ret = 0;
    int ddx = 0, ddy = 0;   /* the car's road direction as a block step */
    switch (c->road_dirs) {
    case 1: ddy = -1; break;
    case 2: ddy = 1; break;
    case 4: ddx = -1; break;
    case 8: ddx = 1; break;
    }
    if (s->u60 > -1) cop_check_ped_state(s->id);
    if (c->damage > 99) s->state = 0xfe;
    cop_set_speed_by_wanted(s);
    int st = s->state;
    switch (st) {
    case 1:   /* patrolling its route */
        s->u4a = 0;
        car_siren_off(sentinel_car(s));
        cop_join_nearest_pursuit(s);
        c->unk88 = 1;
        c->brake = 0;
        c->unkc0 = 0;
        if (c->sinking == 9) c->sinking = 0;
        close_door1(s->car_id);
        if (s->u0c != 0) return 1;
        s->u14 = 0;
        s->u16 = 0;
        c->counter119 = 1;
        return 0;
    case 2:   /* back to the station */
        if (!car_is_on_screen(sentinel_car(s))) {
            cop_release(s);
            return 0;
        }
        if (g_path_owner > -1) {
            c->brake = 1;
            return 0;
        }
        car_siren_off(c);
        s->u4a = 1;
        s->state = 3;
        s->u14 = 0;
        s->u16 = 0;
        cop_path_to_target(s);
        return 1;
    case 3:
        if (!car_is_on_screen(sentinel_car(s))) {
            cop_release(s);
            return 0;
        }
        s->state = 0xfe;
        return 0;
    case 4: {   /* back to its patrol route */
        if (s->u04 != 1) {
            s->state = 2;
            return 0;
        }
        s->u48 = s->u08;
        if (car_is_on_screen(sentinel_car(s))) {
            if (g_path_owner > -1) {
                c->brake = 1;
                return 0;
            }
            car_siren_off(c);
            s->u14 = 0;
            s->state = 6;
            s->sub = 6;
            s->u16 = 0;
            c->brake = 0;
            cop_path_to_route_start(s);
            return 1;
        }
        Car *k = sentinel_car(s);
        const uint8_t *node = g_path_slots[s->u48];
        /* quirk: the node's block bytes go to the query as 16.16 coordinates (the map's corner) */
        if (coll_query_block(node[0], node[1], node[2], COLL_CAR, -1)) {
            coll_unlock();
            return 0;
        }
        s->u14 = 0;
        s->u16 = 0;
        s->route = s->u48;
        s->state = 1;
        s->sub = 1;
        k->counter119 = 1;
        int nx = node[0], ny = node[1], nz = node[2];
        s->u48 = -1;
        coll_unlock();
        sentinel_warp_car(k, nx * 0x400000 + 0x200000, ny * 0x400000 + 0x200000, nz * 0x400000 + 0x3e0000);
        return 1;
    }
    case 5:
        if (g_path_owner > -1) {
            c->brake = 1;
            return 0;
        }
        car_siren_off(c);
        s->state = 3;
        s->sub = 3;
        s->u14 = 0;
        s->u16 = 0;
        cop_path_to_target(s);
        return 0;
    case 6: {   /* driving to the route start; off screen it jumps there */
        s->u48 = s->u08;
        if (!car_is_on_screen(sentinel_car(s))) {
            c = sentinel_car(s);
            const uint8_t *node = g_path_slots[s->u48];
            if (!coll_query_block(node[0], node[1], node[2], COLL_CAR, -1)) {   /* (quirk as in state 4) */
                int nz = node[2], nx = node[0], ny = node[1];
                coll_unlock();
                int r = (int16_t)sentinel_warp_car(c, nx * 0x400000 + 0x200000, ny * 0x400000 + 0x200000,
                                                   nz * 0x400000 + 0x3e0000);
                if (r == 0 || r == 1) {
                    s->u0c = 1;
                } else if (r == 2) {
                    c->spr.zkey = c->spr.z;
                    s->u0c = 0;
                }
            } else {
                coll_unlock();
            }
        }
        ret = 1;
        if (s->u0c == 0) {
            c->spr.zkey = c->spr.z;
            s->route = s->u48;
            s->u48 = -1;
            s->u14 = 0;
            s->u16 = 0;
            s->state = 1;
            s->sub = 1;
            c->counter119 = 1;
            return 1;
        }
        break;
    }
    default:
        game_fatal(-0xf5, 0xa6, st);
    case 10:
        sentinel_override_lights(s, c);
        if (s->u0c != 0 &&
            (c->speed > 3 || (int16_t)(iabs(g_sent_by - s->dest[1]) + iabs(g_sent_bx - s->dest[0])) > 3))
            return 1;
        s->state = 0xb;
        s->u14 = 0;
        s->u16 = 0;
        return 0;
    case 0xb:
        if (c->turn_delta == 0) {
            s->state = s->u04 == 1 ? 4 : 2;
            return 0;
        }
        break;
    case 0x32: case 0x33: case 199:
        break;
    case 0x6e: {   /* the crew gets out */
        Car *k = sentinel_car(s);
        k->input = 0;
        k->speed = 0;
        k->unkc0 = 1;
        if (k->driver != -1) {
            s->u60 = k->driver;
            Ped *p = ped_get(k->driver);
            p->health = 100;
            p->u14 = s->car_id;
            ped_driver_leave_car(s->car_id);
            s->u20 = 1;
            s->state = 0x6f;
            return 0;
        }
        s->state = 0xfe;
        return 0;
    }
    case 0x6f: {   /* getting out */
        Ped *p = cped(s);
        Snd_PlayPedVoice(p->spr.x, p->spr.y, p->spr.z, 0x12, p->id);
        bool on = car_is_on_screen(sentinel_car(s));
        bool near = on;
        if (p->anim > 0) near = ped_is_near_screen(p);
        if (anim_walking(p->anim)) {
            s->state = s->sub;
            p->graphic = 1;
            p->remap = 0;
            sprite_set_remap(&p->spr, 0);
            p->walk_x = 0;
            p->walk_y = 0;
            p->u48 = 1;
            p->speed = 0;
            s->u6f = 4;
            return 0;
        }
        if (on || near) break;
        int32_t x, y;
        door_point(c, 0x14, &x, &y);
        if (p->anim == 0) {
            ped_spawn_in_slot(x, y, c->spr.z, 0, 0, 0x62, c->driver, c->id);
            close_door1(s->car_id);
            c->u244 = 0;
        } else {
            p->anim = 0x88;
            coll_remove(p, p->spr.unk20);
            p->spr.y = y;
            p->spr.x = x;
            coll_insert(COLL_PED, p->id, p, p->spr.unk20, x, y);
            c->u244 = 0;
            close_door1(s->car_id);
        }
        s->state = s->sub;
        p->u48 = 1;
        s->u6f = 4;
        p->speed = 0;
        p->walk_x = 0;
        p->walk_y = 0;
        p->remap = 0;
        p->graphic = 1;
        sprite_set_remap(&p->spr, 0);
        p->u14 = s->car_id;
        return 0;
    }
    case 0x96: {   /* a cop at a roadblock, waiting for the criminal */
        Car *k = sentinel_car(s);
        if (pursuit_crim(s->pursuit) < 0) break;
        Criminal *cr = crim_of(s);
        if (!cr) {
            s->state = 0xfe;
            return 0;
        }
        if (s->u60 == -1) {
            s->sub = 0x96;
            s->state = 0xf8;
            return roadblock_cop_joins(s, false);
        }
        Ped *p = cped(s);
        ped_set_weapon(p->id, player_get_wanted_level_by_ped(cr->ped) == 4 ? 2 : 1);
        if (p->health == 0) {
            Car *k2 = sentinel_car(s);
            s->state = 0xfe;
            cped(s)->player_ctl = 0;
            k2->control = 3;
            return 0;
        }
        if (cr->kind == 0) {
            const Car *t = car_get(cr->car);
            int d = imax(dblk(k->spr.x, t->spr.x), dblk(k->spr.y, t->spr.y));
            s->dist = (uint8_t)d;
            if (t->speed != 0) {
                p->objective = 0x20;
                p->state = 3;
                p->u78 = 8;
                p->target_ped = s->dist > 5 ? -1 : cr->ped;
                return 0;
            }
            if ((uint8_t)d > 5) return 0;
            Snd_PlayPedVoice(p->spr.x, p->spr.y, p->spr.z, 0x13, p->id);
        } else if (cr->kind == 1) {
            const Ped *t = ped_get(cr->ped);
            int d = imax(dblk(k->spr.x, t->spr.x), dblk(k->spr.y, t->spr.y));
            s->dist = (uint8_t)d;
            if ((uint8_t)d > 5) return 0;
        } else {
            return 0;
        }
        s->sub = 0x96;
        s->state = 0xf8;
        return roadblock_cop_joins(s, true);
    }
    case 0xbe: {   /* the cop on foot walks back to the car */
        if (s->u60 == -1) {
            s->state = s->sub;
            s->u20 = 0;
            return 0;
        }
        Ped *p = cped(s);
        if (p->carried < 0) {
            if (p->anim != 0) {
                p->u48 = 0;
                p->anim = 3;
                p->firing = 0;
                p->objective = 0x28;
                ped_send_to_car_door1(p, s->car_id);
            }
            s->state = 0xbf;
            return 0;
        }
        break;
    }
    case 0xbf: {   /* ... and gets in (out of sight: at once) */
        if (s->u60 == -1) {
            s->state = s->sub;
            s->u20 = 0;
            return 0;
        }
        Ped *p = cped(s);
        if (p->carried >= 0) break;
        Car *k = sentinel_car(s);
        if (p->anim == 0) {
            s->u20 = 0;
            p->car = k->id;
            s->u60 = -1;
            s->state = s->sub;
            return 0;
        }
        if (!car_is_on_screen(k) && !ped_is_near_screen(p)) {
            k->unk88 = 1;
            k->driver = p->id;
            ped_remove(p->id);
            p->state = 7;
            p->car = k->id;
            p->objective = 0x26;
            s->u60 = -1;
            s->u20 = 0;
            s->state = s->sub;
            close_door1(s->car_id);
            k->u244 = 0;
            return 0;
        }
        break;
    }
    case 200: {   /* dispatched at a criminal: plan the way */
        if (pursuit_crim(s->pursuit) < 0) {
            s->state = 0xfe;
            return 0;
        }
        Criminal *cr = crim_of(s);
        if (!cr) {
            s->state = 0xfe;
            return 0;
        }
        sentinel_car(s)->brake = 0;
        if (cr->started == 3 || cr->started == 5) return cop_go_home_now(s, 1);
        car_siren_on(c);
        if (g_pursuits[s->pursuit].lead != -1) {
            sentinel_car(s)->brake = 0;
            s->u4a = 1;
            s->state = 0xcb;
            if (s->route > 0x31 && s->u48 < 0) s->u48 = s->route;
            s->u0c = (uint8_t)(c->spr.x >> 22);
            s->u0d = (uint8_t)(c->spr.y >> 22);
            s->u0e = (uint8_t)(c->spr.z >> 22);
            return 0;
        }
        int bx = c->spr.x >> 22 & 0xff, by = c->spr.y >> 22 & 0xff, bz = c->spr.z >> 22 & 0xff;
        int dx = s->dest[0], dy = s->dest[1], dz = s->dest[2];
        if (s->dist < 0xf || tc(dx, dy, dz & 0xf) == 0 || s->u4a != 0) {
            if (s->route > 0x31 && s->u48 < 0) s->u48 = s->route;
            s->route = s->id;
            sentinel_car(s)->brake = 0;
            s->state = 0xcb;
            s->sub = 0xcb;
            s->u0c = (uint8_t)(c->spr.x >> 22);
            s->u0d = (uint8_t)(c->spr.y >> 22);
            s->u0e = (uint8_t)(c->spr.z >> 22);
            s->u4a = 1;
            return 1;
        }
        if ((tc(bx, by, bz) & 0xf) == 0 || (tc(dx, dy, dz) & 0xf) == 0) {
            s->state = 0xff;
            return 0;
        }
        if (g_path_owner > -1) {
            if (c->speed < 1) return 0;
            c->input = 0;
            c->speed--;
            return 0;
        }
        if (s->route > 0x31 && s->u48 < 0) s->u48 = s->route;
        s->u38 = 0;
        s->route = s->id;
        c->counter119 = 1;
        c->brake = 1;
        g_path_result = (int16_t)path_find(bx, by, bz, dx, dy, dz, 3, (uint8_t)s->route);
        if (g_path_result != 1) {
            if (g_path_result != 3) return 0;
            s->state = 0xc9;
            s->sub = 0xc9;
            return 0;
        }
        s->state = 0xca;
        s->sub = 0xca;
        c->brake = 0;
        c->unkc0 = 0;
        s->u14 = 0;
        s->u16 = 0;
        return 1;
    }
    case 0xc9: {   /* waiting for the path search */
        if (pursuit_crim(s->pursuit) < 0) {
            s->state = 0xfe;
            return 0;
        }
        Criminal *cr = crim_of(s);
        if (!cr) {
            s->state = 0xfe;
            return 0;
        }
        if (cr->started == 3 || cr->started == 5) return cop_go_home_now(s, 1);
        if (g_pursuits[s->pursuit].lead != -1) {
            chase_from_here(s, c);
            return 1;
        }
        if (g_path_owner == s->id) return 0;
        s->state = 0xca;
        s->sub = 0xca;
        return 1;
    }
    case 0xca: {   /* following the path to the criminal */
        if (pursuit_crim(s->pursuit) < 0) {
            s->state = 0xfe;
            return 0;
        }
        Criminal *cr = crim_of(s);
        if (!cr) {
            s->state = 0xfe;
            return 0;
        }
        if (cr->started == 3 || cr->started == 5) return cop_go_home_now(s, 1);
        pursuit_update_lead(s);
        sentinel_override_lights(s, c);
        if (g_pursuits[s->pursuit].lead != -1) {
            chase_from_here(s, c);
            return 1;
        }
        if (s->u0c == 0) {
            s->state = 4;
            return 0;
        }
        if (s->dist < 0xf) {
            if (cr->kind == 2) ref_get_kind1_pos_rect(cr->train);   /* (fetched and not used) */
            s->u0d = (uint8_t)(c->spr.y >> 22);
            s->state = 0xcb;
            s->sub = 0xcb;
            s->u0c = (uint8_t)(c->spr.x >> 22);
            s->u0e = (uint8_t)(c->spr.z >> 22);
            s->u4a = 1;
            if (s->u20 == 1) s->u20 = 0;
        }
        return 1;
    }
    case 0xcb: {   /* chasing */
        if (pursuit_crim(s->pursuit) < 0) {
            s->state = 0xfe;
            return 0;
        }
        Criminal *cr = crim_of(s);
        if (!cr) {
            s->state = 0xfe;
            return 0;
        }
        if (cr->started == 3 || cr->started == 5) return cop_go_home_now(s, 0);
        sentinel_override_lights(s, c);
        Pursuit *pu = &g_pursuits[s->pursuit];
        if (pu->lead == -1) {
            pu->lead = s->id;
            pu->u06 = 0;
        } else if (s->id == pu->lead) {
            pursuit_update_lead(s);
            roadblock_try_place_ahead(s);
        }
        if (s->dist < 5) s->state = 0xd1;
        else s->u4a = 1;
        if ((tc(c->spr.x >> 22, c->spr.y >> 22, c->spr.z >> 22) & 0xf) == 0 && !car_is_on_screen(c))
            sentinel_warp_to_nearest_road(c);
        return 1;
    }
    case 0xd0: {   /* the cop on foot gets back in */
        const Ped *p = cped(s);
        if (p->anim == 0) {
            s->u20 = 0;
            s->u60 = -1;
            s->state = 4;
            return 0;
        }
        break;
    }
    case 0xd1: {   /* close behind */
        sentinel_override_lights(s, c);
        s->u4a = 3;
        if (pursuit_crim(s->pursuit) < 0) break;
        Criminal *cr = crim_of(s);
        if (!cr) {
            s->state = 0xfe;
            return 0;
        }
        Car *k = sentinel_car(s);
        if ((tc(k->spr.x >> 22, k->spr.y >> 22, k->spr.z >> 22) & 0xf) == 0 && !car_is_on_screen(k))
            sentinel_warp_to_nearest_road(k);
        if (cr->kind == 0) {
            const Car *t = car_get(cr->car);
            if (k->speed == 0 && t->speed == 0 && ((uint32_t)(t->spr.z ^ k->spr.z) & 0xffc00000u) == 0) {
                s->state = 0x6e;
                s->sub = 0xd3;
                return 1;
            }
            if (s->id == g_pursuits[s->pursuit].lead) {
                pursuit_update_lead(s);
                roadblock_try_place_ahead(s);
            }
            if (s->dist > 4) {
                s->u4a = 1;
                s->state = 0xcb;
                return 1;
            }
            if (s->dist < 3 && t->speed < 4) {
                s->state = 0xd2;
                return 1;
            }
            s->u4a = 3;
        } else if (cr->kind == 1) {
            s->state = 0xd2;
            s->u4a = 1;
            return 1;
        }
        return 1;
    }
    case 0xd2: {   /* alongside: stop and get out, or chase on */
        if (pursuit_crim(s->pursuit) < 0) return 0;
        pursuit_update_lead(s);
        ret = 1;
        Criminal *cr = crim_of(s);
        if (!cr) {
            s->state = 0xfe;
            return 1;
        }
        const Car *t = NULL;
        const Ped *tp = NULL;
        uint8_t tx = 0, ty = 0, tz = 0;
        if (cr->kind == 0) {
            t = car_get(cr->car);
            ty = (uint8_t)(t->spr.y >> 22);
            tz = (uint8_t)(t->spr.z >> 22);
            s->u6e = (uint8_t)t->speed;
            tx = (uint8_t)(t->spr.x >> 22);
            if (s->id == g_pursuits[s->pursuit].lead) roadblock_try_place_ahead(s);
        } else if (cr->kind == 1) {
            tp = ped_get(cr->ped);
            ty = (uint8_t)(tp->spr.y >> 22);
            tx = (uint8_t)(tp->spr.x >> 22);
            tz = (uint8_t)(tp->spr.z >> 22);
            s->u6e = (uint8_t)tp->speed;
        } else if (cr->kind == 2) {
            tp = ped_get(cr->ped);
            const int32_t *pos = ref_get_kind1_pos_rect(cr->train);
            ty = (uint8_t)(pos[1] >> 22);
            tx = (uint8_t)(pos[0] >> 22);
            tz = (uint8_t)(pos[2] >> 22);
            s->u6e = (uint8_t)pos[4];
        }
        int d = imax(iabs(g_sent_by - s->dest[1] + ddy), iabs(g_sent_bx - s->dest[0] + ddx));
        if ((tc(g_sent_bx + ddx, g_sent_by + ddy, g_sent_bz) & 0xf) == 0) d = 0;
        int dist = s->dist;
        if ((dist > 2 || ((int16_t)d <= dist && c->speed > 3 && dist > 2)) && (dist > 6 || cr->kind != 1)) {
            s->u0c = tx;
            s->u0d = ty;
            s->u0e = tz;
            s->u4a = 1;
            c->brake = 0;
            if (s->u20 == 1) s->u20 = 0;
            s->sub = 0xcb;
            s->state = 0xcb;
            return 1;
        }
        int out = -2;   /* the driver who gets out (-2: none chosen here) */
        if (cr->kind == 0) {
            if (s->id == g_pursuits[s->pursuit].lead && c->speed < 4 && t->speed < 4) {
                s->u4b = 1;
                out = sentinel_car(s)->driver;
                s->u20 = 1;
            }
        } else if (cr->kind == 1) {
            if (((uint32_t)(c->spr.z ^ tp->spr.z) & 0xffc00000u) == 0) {
                s->u4b = 1;
                c->brake = 1;
                c->unkc0 = 1;
            }
            if (c->speed < 4) {
                s->u20 = 1;
                out = sentinel_car(s)->driver;
            }
        }
        if (out != -2) {
            s->u60 = (int16_t)out;
            s->state = 0x6e;
            s->sub = 0xd3;
            if (out < 0) s->state = 0xfe;
            else ped_get(out)->health = 100;
        }
        if (cr->started == 3 || cr->started == 5) return cop_go_home_now(s, ret);
        if (c->speed > 3) return 1;
        if ((int8_t)s->u6e > 2) return 1;
        if (s->u12 != 0) return 1;
        c->unkc0 = 1;
        s->u60 = sentinel_car(s)->driver;
        s->u20 = 1;
        s->state = 0x6e;
        s->sub = 0xd3;
        return 1;
    }
    case 0xd3: {   /* the cop is out: how to go for the criminal */
        Ped *p = cped(s);
        if (pursuit_crim(s->pursuit) < 0) break;
        Criminal *cr = crim_of(s);
        if (!cr) {
            s->state = 0xfe;
            return 0;
        }
        const Car *t = NULL;
        if (cr->kind == 0) {
            t = car_get(cr->car);
            if (anim_walking(p->anim)) {
                p->objective = 0x37;
                p->target_ped = cr->ped;
                p->u68 = 0;
                p->speed = 0;
                p->state = 3;
                p->u78 = 8;
                p->u7c = 8;
                ped_set_weapon(p->id, 1);
                if (p->u7c != 0x10) s->state = 0xf8;   /* (always: +0x7c was just set to 8) */
                p->walk_x = 0;
            }
        } else if (cr->kind == 1) {
            const Ped *tp = ped_get(cr->ped);
            if (g_pursuits[s->pursuit].shoot == 0) {
                p->anim = 3;
                p->objective = 0x21;
                p->target_x = 0;
                p->target_y = 0;
                p->state = 4;
                p->u78 = 8;
                p->u7c = 2;
                p->target_ped = tp->id;
                p->u48 = 0;
                s->u72 = 0;
                s->state = 0xe6;
            } else if (anim_walking(p->anim)) {
                cop_stand_and_shoot(p, cr->ped);
                s->state = 0xf8;
            }
        }
        if (s->state == 0xd3) {
            if (cr->started == 4) {
                cop_back_to_car(p, s->car_id);
                s->state = 0xd0;
                return 0;
            }
            /* (the original reads the criminal's car here even when he is on foot: port reads none) */
            if (s->dist > 6 && t && t->speed > 2) {
                p->anim = 3;
                cop_back_to_car(p, s->car_id);
                p->speed = 4;
                s->state = 0xbe;
                s->sub = 0xcb;
                return 0;
            }
        }
        break;
    }
    case 0xd4: {   /* the cop runs to the criminal's car door (or after him) */
        Ped *p = cped(s);
        if (pursuit_crim(s->pursuit) < 0) return 0;
        Criminal *cr = crim_of(s);
        if (!cr) {
            s->state = 0xfe;
            return 0;
        }
        if (cr->kind == 0) {
            Car *t = car_get(cr->car);
            door_point(t, 0, &p->walk_x, &p->walk_y);
            if (t->speed < 4 && t->script_held != 1) {
                if (t->burning == 0) {
                    if (p->state == 0xb) {
                        if (cr->ped == -1) {
                            t = car_get(cr->car);
                            cr->ped = t->driver;
                            if (t->driver == -1) goto d4_end;
                        }
                        if (t->vtype == CAR_VT_BIKE) {
                            p->u48 = 1;
                            s->state = 0xd4;
                            t->control = 0;
                            t->brake = 1;
                            t->input = 0;
                        } else {
                            int32_t x, y;
                            door_point(t, -4, &x, &y);
                            p->spr.angle = t->spr.angle;
                            if (((uint32_t)(p->spr.x ^ x) & 0xff800000u) != 0 || ((uint32_t)(p->spr.y ^ y) & 0xff800000u) != 0) {
                                coll_remove(p, p->spr.unk20);
                                coll_insert(COLL_PED, p->id, p, p->spr.unk20, x, y);
                            }
                            p->spr.x = x;
                            p->spr.y = y;
                        }
                        Ped *tp = ped_get(cr->ped);
                        if ((tp->anim == 0 || tp->anim == 0x80) && tp->state == 7 && t->u244 != 1) {
                            Snd_PlayPedVoice(p->spr.x, p->spr.y, p->spr.z, 0x15, p->id);
                            p->u48 = 0;
                            p->anim = 0x1a;
                            p->u40 = tp->id;
                            s->state = 0xd5;
                            t->control = 0;
                            t->speed = 0;
                            t->u1e = 0;
                            t->input = 0;
                            t->brake = 1;
                            t->u244 = 1;
                            g_car_forced_accel[t->id] = 0;   /* Car_SetForcedAccel 0x4082b0 */
                        }
                    }
                } else {
                    s->u74 = 1;
                    cr->started = 5;
                    s->state = 0xf8;
                }
            } else if (anim_walking(p->anim)) {
                p->walk_x = 0;
                p->walk_y = 0;
                cop_stand_and_shoot(p, cr->ped);
                p->firing = 1;
                g_pursuits[s->pursuit].u34 = 1;
                s->state = 0xf8;
            }
        } else if (cr->kind == 1) {
            const Ped *tp = ped_get(cr->ped);
            Pursuit *pu = &g_pursuits[s->pursuit];
            if (tp->firing == 1 && tp->weapon != 0) {
                p->u68 = 100;
                pu->shoot = 1;
            }
            p->u7c = 2;
            p->state = 4;
            if (pu->shoot == 0) {
                p->objective = 0x21;
                p->u78 = 8;
                ped_set_weapon(p->id, 1);
                p->firing = 0;
                p->target_ped = cr->ped;
            } else {
                p->objective = 0x1f;
                p->u78 = 8;
                if (tp->state == 9) {
                    p->u68 = 100;
                    p->objective = 0x26;
                    p->state = 3;
                    p->u7c = 8;
                }
                ped_set_weapon(p->id, 1);
                p->target_ped = cr->ped;
            }
            p->target_x = 0;
            p->target_y = 0;
            p->u40 = tp->id;
            s->u72 = 0;
            s->state = 0xe6;
            p->u48 = 0;
        }
    d4_end:
        if (cr->started == 4) {
            cop_back_to_car(p, s->car_id);
            p->speed = 4;
            s->state = 0xd0;
            return 0;
        }
        break;
    }
    case 0xd5: {   /* pulling him out of the car */
        if (pursuit_of(s)) pursuit_of(s)->shoot = 0;   /* (port: no write for group -1) */
        if (pursuit_crim(s->pursuit) < 0) break;
        Criminal *cr = crim_of(s);
        if (!cr) {
            s->state = 0xfe;
            return 0;
        }
        Ped *tp = ped_get(cr->ped);
        Car *t = car_get(cr->car);
        if (t->control != 3) t->control = -1;
        t->speed = 0;
        t->u1e = 0;
        t->brake = 1;
        t->input = 0;
        g_car_forced_accel[t->id] = 0;   /* Car_SetForcedAccel 0x4082b0 */
        if (tp->anim == 0x2b) {
            player_retarget_camera(0, tp->car, 2, tp->id);
            cr->started = 4;
            s->state = 0xda;
        }
        if ((int8_t)tp->u48 > 1) {
            tp->u48--;
            return 0;
        }
        break;
    }
    case 0xd6: {   /* the criminal on foot gives up? */
        if (pursuit_crim(s->pursuit) < 0) break;
        Criminal *cr = crim_of(s);
        if (!cr) {
            s->state = 0xfe;
            return 0;
        }
        Ped *p = cped(s);
        Ped *tp = ped_get(cr->ped);
        p->u48 = 1;
        if (g_pursuits[s->pursuit].shoot_on_sight == 1) {
            cr->started = 6;
            cop_stand_and_shoot(p, cr->ped);
            p->firing = 1;
        }
        if (tp->state == 5) {
            tp->firing = 0;
            return 0;
        }
        if (anim_walking(tp->anim) || tp->anim == 0x2b) {
            cr->started = 4;
            s->u74 = 4;
            s->state = 0xd7;
            return 0;
        }
        break;
    }
    case 0xd7: {   /* a few frames, then the cop walks him to the car */
        s->u74--;
        Ped *p = cped(s);
        if (pursuit_crim(s->pursuit) < 0) break;
        if (!crim_of(s)) {
            s->state = 0xfe;
            return 0;
        }
        p->walk_x = 0;
        p->walk_y = 0;
        p->speed = 0;
        if (s->u74 == 0) {
            int32_t x, y;
            door_point(c, 0x1e, &x, &y);
            p->walk_x = x;
            p->anim = 3;
            p->walk_y = y;
            ped_set_destination(p, p->walk_x, y, (c->spr.angle - 0x100) & 0x3ff, 0);
            p->speed = 4;
            s->state = 0xd8;
            return 0;
        }
        break;
    }
    case 0xd8:
        if (pursuit_crim(s->pursuit) < 0) break;
        if (!crim_of(s)) {
            s->state = 0xfe;
            return 0;
        }
        if (++s->u74 == 3) {
            s->state = 0xda;
            return 0;
        }
        break;
    case 0xd9:
        if (cped(s)->anim == 0) {
            s->state = 4;
            pursuit_recall_cops(s);
            return 0;
        }
        break;
    case 0xda: {   /* the arrest */
        int ci = pursuit_crim(s->pursuit);
        if (ci < 0) {
            s->state = 0xfe;
            return 0;
        }
        if (g_player_count < 2) {
            Criminal *cr = police_get_criminal(ci);
            const Ped *cp = cped(s);
            if (cp->health < 1 || cp->target_ped < 0) {
                Ped *pp = ped_get(player_get_ped(0));
                if (pp->state == 9) pp->state = 2;
                cr->started = 1;
            } else {
                const Ped *tp = ped_get(cp->target_ped);
                if (tp->state != 0x17 && tp->state != 5 && tp->health > 0 && tp->carried < 1) {
                    if (s->pursuit > -1) {
                        s->u74 = 0;
                        Criminal *c2 = crim_of(s);
                        if (pursuit_crim(s->pursuit) > -1 && c2 && ped_get(c2->ped)->health > 0) {
                            int ped = c2->ped;
                            police_update_chasers();
                            pursuit_recall_cops(s);
                            player_clear_wanted_level(ped);
                            player_clear_wanted_points(ped);
                        }
                    }
                    player_busted(0);
                    s->state = 0xfe;
                    return 0;
                }
            }
            s->state = 0xfe;
            return 0;
        }
        Criminal *cr = police_get_criminal(ci);
        if (!cr) {
            s->state = 0xfe;
            return 0;
        }
        Ped *tp = ped_get(cr->ped);   /* network games: the criminal dies */
        tp->health = 0;
        Snd_PlayUI(tp->spr.x, tp->spr.y, tp->spr.z, 0x21);
        s->state = 0xfe;
        return 0;
    }
    case 0xe6: {   /* on foot after a criminal on foot */
        Ped *p = cped(s);
        if (pursuit_crim(s->pursuit) < 0) break;
        Criminal *cr = crim_of(s);
        if (!cr) {
            s->state = 0xfe;
            return 0;
        }
        if (cr->kind == 0) {
            Snd_PlayPedVoice(p->spr.x, p->spr.y, p->spr.z, 0x13, p->id);
            p->objective = 0x27;
            p->firing = 0;
            ped_send_to_car_door1(p, cr->car);
            p->anim = 3;
            p->speed = 4;
            s->state = 0xd4;
            g_pursuits[s->pursuit].u34 = 0;
            p->u48 = 0;
        } else if (cr->kind == 1) {
            Ped *tp = ped_get(cr->ped);
            if (tp->firing == 1 && tp->weapon != 0 && p->objective != 0x1f) {
                p->objective = 0x1f;
                ped_set_weapon(p->id, 1);
                p->u68 = 100;
            }
            Snd_PlayPedVoice(p->spr.x, p->spr.y, p->spr.z, 0x12, p->id);
            if ((p->anim == 0 || p->anim > 0x10) && p->anim != 0x88) {
                if (tp->health == 0) {
                    cop_back_to_car(p, s->car_id);
                    p->firing = 0;
                    p->u48 = 0;
                    s->state = 0xd9;
                }
            } else {
                if (tp->state == 2 && p->state == 3) p->state = 4;
                if (tp->speed == 0) s->u72 = 0;
                else s->u72++;
            }
            if (tp->state == 9) s->state = 0xd6;
        }
        if (cr->started == 4 || cr->started == 3 || cr->started == 5) {
            p->anim = 3;
            p->speed = 1;
            cop_back_to_car(p, s->car_id);
            s->state = 0xd0;
            return 0;
        }
        break;
    }
    case 0xf0:
        if (cped(s)->anim == 0) {
            if (pursuit_crim(s->pursuit) < 0) {
                s->u70 = 0x14;
                s->state = 0xfc;   /* (no case for 0xfc: the next step is the fatal default) */
                return 0;
            }
            Criminal *cr = crim_of(s);
            if (cr) {
                cr->started = 3;
                s->u70 = 0x14;
                s->state = 0xfc;
                pursuit_recall_cops(s);
                return 0;
            }
            s->state = 0xfe;
            return 0;
        }
        break;
    case 0xf8: {   /* on foot at a criminal (stopped car or on foot) */
        Ped *p = cped(s);
        if (pursuit_crim(s->pursuit) < 0) break;
        Criminal *cr = crim_of(s);
        if (!cr) {
            s->state = 0xfe;
            return 0;
        }
        Pursuit *pu = &g_pursuits[s->pursuit];
        if (cr->kind == 0) {
            const Car *t = car_get(cr->car);
            if (t->damage < 0x65) {
                if (t->speed < 4 && t->script_held != 1) {
                    p->objective = 0x27;
                    p->firing = 0;
                    ped_send_to_car_door1(p, cr->car);
                    p->anim = 3;
                    p->speed = 4;
                    s->state = 0xd4;
                    pu->u34 = 0;
                    p->u48 = 0;
                } else if (anim_walking(p->anim) || p->anim == 0x62) {
                    p->objective = 0x20;
                    p->walk_x = 0;
                    p->walk_y = 0;
                    p->speed = 0;
                    Snd_PlayPedVoice(p->spr.x, p->spr.y, p->spr.z, 0x13, p->id);
                    p->state = 3;
                    p->u78 = 8;
                    p->u7c = 8;
                    ped_set_weapon(p->id, 1);
                    p->firing = 1;
                    p->target_ped = cr->ped;
                    pu->u34 = 1;
                }
                if (s->dist > 6 && t->speed > 3) {
                    p->anim = 3;
                    p->objective = 0x28;
                    p->firing = 0;
                    ped_send_to_car_door1(p, s->car_id);
                    s->state = 0xbe;
                    s->sub = 0xcb;
                }
            } else {
                cr->started = 5;
            }
        } else if (cr->kind == 1) {
            const Ped *tp = ped_get(cr->ped);
            if (tp->firing == 1 && tp->weapon != 0) {
                pu->shoot = 1;
                p->u68 = 100;
            }
            if (tp->health == 0) {
                pu->u34 = 0;
                cr->started = 3;
            }
            p->u48 = 0;
            p->state = 4;
            if (pu->shoot == 0) {
                p->firing = 0;
                p->objective = 0x21;
                p->u78 = 8;
                p->target_ped = tp->id;
                s->state = 0xd4;
                p->target_x = 0;
                p->target_y = 0;
            } else {
                p->objective = 0x1f;
                p->target_ped = tp->id;
                p->u78 = 8;
                s->state = 0xd4;
                ped_set_weapon(p->id, 1);
                p->target_x = 0;
                p->target_y = 0;
            }
        }
        if (cr->started == 3 || cr->started == 5) {
            p->anim = 3;
            p->firing = 0;
            p->speed = 1;
            s->state = 0xbe;
            s->sub = 0xf9;
            pursuit_recall_cops(s);
            return 0;
        }
        if (cr->started == 4) {
            cop_back_to_car(p, s->car_id);
            s->state = 0xd0;
            return 0;
        }
        if (pu->shoot == 1) {
            if (cr->kind == 0) {
                const Car *t = car_get(cr->car);
                if (t->speed > 3 && (anim_walking(p->anim) || p->anim > 0x61)) {
                    p->walk_x = 0;
                    p->walk_y = 0;
                    p->speed = 0;
                    p->state = 3;
                    p->u48 = 0;
                    p->objective = 0x20;
                    p->u78 = 8;
                    p->u7c = 8;
                    p->firing = 1;
                    ped_set_weapon(p->id, 1);
                    p->target_ped = cr->ped;
                    return 0;
                }
            } else if (cr->kind == 1) {
                const Ped *tp = ped_get(cr->ped);
                if (tp->speed != 0 && tp->anim < 0x11 && tp->anim != 0) {
                    p->state = 4;
                    p->u48 = 0;
                    p->objective = 0x1f;
                    p->target_x = 0;
                    p->target_y = 0;
                    p->target_ped = tp->id;
                    p->u78 = 8;
                    s->state = 0xd4;
                    ped_set_weapon(p->id, 1);
                    return 0;
                }
                p->u48 = 0;
                p->anim = 3;
                s->state = 0xd4;
                return 0;
            }
        }
        break;
    }
    case 0xf9: {   /* stop */
        Car *k = sentinel_car(s);
        k->brake = 1;
        k->unkc0 = 1;
        k->input = 0;
        k->speed = 0;
        return 0;
    }
    case 0xfe: {   /* going home: leave the group and the roadblock, wait for the cop on foot */
        if (s->pursuit > -1) pursuit_remove_cop(s);
        if (s->u5e > -1) roadblock_remove_car(s);
        s->u4a = 0;
        bool on = car_is_on_screen(sentinel_car(s));
        bool skip_dc2 = false;
        if (s->u60 >= 0) {
            Ped *cp = cped(s);
            if (ped_is_near_screen(cp)) {
                if (cp->target_ped > -1) {
                    Ped *tp = ped_get(cp->target_ped);
                    if (pursuit_crim(s->pursuit) < 0 && cp->health > 0 && cp->state != 0x17) {
                        cp->state = 1;
                        cp->objective = 0x26;
                        return 0;
                    }
                    if (tp->state == 9) {
                        if (pursuit_crim(s->pursuit) < 0) return 0;
                        if (tp->state != 0x17 && cp->health > 0 && tp->health > 0 && tp->state != 5 && tp->carried < 1) {
                            player_busted(0);
                            if (s->pursuit > -1) {
                                s->u74 = 0;
                                Criminal *c2 = crim_of(s);
                                if (pursuit_crim(s->pursuit) > -1 && c2 && ped_get(c2->ped)->health > 0) {
                                    int ped = c2->ped;
                                    police_update_chasers();
                                    pursuit_recall_cops(s);
                                    player_clear_wanted_level(ped);
                                    player_clear_wanted_points(ped);
                                }
                            }
                        }
                    }
                }
                int pst = cp->state;
                if (pst != 0x17 && cp->anim != 0 && cp->anim != 0xc && pst != 0x18 && cp->health != 0 &&
                    pst != 0x15 && cp->objective != 0x28 && pst != 7) {
                    Car *k = car_get(sentinel_car(s)->id);
                    if (k->damage < 100) {
                        k->u244 = 0;
                        cop_back_to_car(cp, s->car_id);
                        cp->firing = 0;
                    }
                }
                skip_dc2 = true;
            }
        } else if (on) {
            skip_dc2 = true;
        }
        if (!skip_dc2 && !on) {
            Car *k = car_get(sentinel_car(s)->id);
            if (k->burning > 0) return 0;
            car_repair(sentinel_car(s));
            cop_reset_to_patrol(s);
            return 0;
        }
        if (on && s->u60 < 0 && s->u04 == 1) {
            cop_reset_to_patrol(s);
            return 0;
        }
        break;
    }
    case 0xff: {   /* no way found: jump to the next node of the route */
        Car *k = sentinel_car(s);
        const uint8_t *node = &g_path_slots[s->route][k->counter119 * 3];
        s->dest[0] = node[0];
        s->dest[1] = node[1];
        s->dest[2] = node[2];
        k->counter119 = 0;
        s->state = 1;
        sentinel_warp_car(k, s->dest[0] << 22, s->dest[1] << 22, s->dest[2] << 22);
        return 1;
    }
    }
    return ret;
}
