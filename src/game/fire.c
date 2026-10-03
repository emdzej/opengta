/* Fires and fire engines (0x42e600-0x430400; see fire.h). */
#include "fire.h"
#include "car.h"
#include "carcoll.h"
#include "game.h"
#include "gmath.h"
#include "obj.h"
#include "path.h"
#include "ped.h"
#include "route.h"
#include "sentinel.h"
#include <math.h>
#include <stdlib.h>

FireState g_fire;

/* Fire_Init 0x42e600 */
void fire_init(void)
{
    for (int i = 0; i < FIRE_MAX; i++) {
        Fire *f = &g_fire.fires[i];
        f->obj = -1;
        f->x = f->y = f->z = 0;
        f->engine = -1;
        f->extra = -1;
        for (int k = 0; k < FIRE_OBJS; k++) f->objs[k] = -1;
    }
    g_fire.engines[0] = g_fire.engines[1] = g_fire.engines[2] = -1;
    g_fire.timers[0] = g_fire.timers[1] = g_fire.timers[2] = 0;
    g_fire.engine_count = 0;
    g_fire.count = 0;
    g_fire.engines_out = 0;
}

/* Fire_IsNearActive 0x42e680 (an engine id of 0 doesn't count, as in the original) */
bool fire_is_near_active(int obj)
{
    const Obj *o = obj_get(obj);
    for (int i = 0; i < FIRE_MAX; i++) {
        const Fire *f = &g_fire.fires[i];
        if (f->engine <= 0) continue;
        int d = abs((o->spr.x >> 22) - (f->x >> 6)) + abs((o->spr.y >> 22) - (f->y >> 6));
        if (d < 0x28 && d > -0x28) return true;
    }
    return false;
}

/* Fire_Extinguish 0x42e6f0: the fire the engine was sent to is dropped: a burning car is marked for
   removal, the merged fire object deleted. */
int fire_extinguish(int engine)
{
    int i = 0;
    while (i < FIRE_MAX && g_fire.fires[i].engine != (int16_t)engine) i++;
    if (i == FIRE_MAX) return -1;
    Fire *f = &g_fire.fires[i];
    const Obj *o = obj_get(f->obj);
    if (o->attach_kind == 1 && o->owner != -1) car_mark_for_removal(o->owner);
    if (f->extra >= 0) obj_delete_wrapper(f->extra);
    g_fire.count--;
    f->obj = -1;
    f->x = f->y = f->z = 0;
    f->engine = -1;
    f->extra = -1;
    return (int16_t)engine;
}

/* Fire_FindNearestUnattended 0x42e7b0 */
int fire_find_nearest_unattended(int32_t car_x, int32_t car_y)
{
    int16_t best = 10000;
    int found = -1;
    for (int i = 0; i < FIRE_MAX; i++) {
        const Fire *f = &g_fire.fires[i];
        if (f->obj == -1 || f->engine != -1 || f->x <= 0) continue;
        int dx = abs((int16_t)(car_x >> 22) - (f->x >> 6)), dy = abs((int16_t)(car_y >> 22) - (f->y >> 6));
        int16_t d = (int16_t)(dy * dy + dx * dx);
        if (d < best) found = i, best = d;
    }
    return found;
}

/* ======================================================================== the fire engines */

/* the cached block type 0x55fab0 [z][y][x], as the original indexes it */
static uint8_t cache_at(int x, int y, int z) { return car_cache_at((z * 0x100 + y) * 0x100 + x); }

/* the fire record whose engine is car `car` (FIRE_MAX: none), the loop every state repeats */
static int fire_of(int car)
{
    int i = 0;
    while (i < FIRE_MAX && g_fire.fires[i].engine != (int16_t)car) i++;
    return i;
}

/* the engine's hose object (+0x50). After the engine burnt out it is -1 and the original goes on
   reading object -1 (memory before the table) in the hose states; the port reads a blank object. */
static Obj *hose_of(const Sentinel *s)
{
    static Obj none;
    return s->u50 >= 0 ? obj_get(s->u50) : &none;
}

/* the jet objects objs[n - 1] .. objs[0] of a fire deleted, last first */
static void delete_jet(Fire *f, int n)
{
    for (int k = n - 1; k >= 0; k--) obj_delete(f->objs[k]);
}

/* FireEngine_Remove 0x42e870: an engine leaves engines[] (one it isn't listed in stays: see
   FireEngine_Spawn), its hose and car are deleted, the path search freed if it was the engine's, the
   record reset. */
void fire_engine_remove(Sentinel *s)
{
    int16_t count = g_fire.engine_count;
    for (int k = 0; k < FIRE_ENGINES; k++) {
        if (g_fire.engines[k] != s->id) continue;
        g_fire.engines[k] = -1;
        if (count > 0) g_fire.engine_count--;
        if (s->u50 >= 0) {
            Car *c = car_get(s->car_id);
            obj_delete(s->u50);
            c->siren_state = 0;
        }
        car_delete(s->car_id);
        if (s->id == g_path_owner) g_path_owner = -1;
        sentinel_reset(sentinel_ai(s));
        g_fire.engines_out--;
        return;
    }
}

/* FireEngine_Spawn 0x42e920: a fire engine (model 0x2a with its driver, Car_SpawnOnRoad) at fire
   station `base` (block centre, z + 0x3e0000), driven by sentinel sid: control 9, owner status 1,
   cruise 6, +0xce 10, node counter 1, the driver's control type 6; the record kind 6, state 1, its
   own path slot. It is listed at engines[engine_count] (quirk: after an engine left from a lower slot
   that overwrites a live one, which then never leaves the list and keeps no timer) with 2500 frames
   to stay out. The hose (type 0x32, attached to the car 1 left, 6 back, turning with it) goes to
   +0x50 and the car's +0x11c; no hose: the car is deleted again (the record stays set up and listed)
   and -1 returned. Returns the car (0xffff: the record is past the table). */
static int fire_engine_spawn(int sid, int base)
{
    const BlockXYZ *b = &g_fire_engine_bases[(int16_t)base];
    int car = car_spawn_on_road(b->x * 0x400000 + 0x200000, b->y * 0x400000 + 0x200000, b->z * 0x400000 + 0x3e0000, 0x2a, 1);
    if ((int16_t)car < 0) return car & 0xffff;
    Sentinel *s = sentinel_ptr(sid);
    if (!s) return 0xffff;
    Car *c = car_get(car);
    c->driver = (int16_t)(c->id + 200);
    ped_get(c->id + 200)->control = 6;
    c->owner_status = 1;
    c->counter119 = 1;
    c->sentinel = (int16_t)sid;
    c->cruise = 6;
    c->control = CAR_CTL_AI9;
    c->uce = 10;
    c->unk139 = 0;
    s->u21 = 0;
    s->u22 = 0;
    s->state = 1;
    s->route = s->id;
    s->car_id = (int16_t)car;
    s->u38 = 0;
    sentinel_set_car(s, c);
    s->kind = SENT_FIRE;
    int k = g_fire.engine_count++;
    g_fire.engines_out++;
    g_fire.engines[k] = s->id;
    g_fire.timers[k] = 0x9c4;
    int16_t hose = (int16_t)obj_create_attached(c->id, 5, -1, -6, 0x32);
    s->u50 = hose;
    c->siren_state = hose;
    if (s->u50 == -1) {
        car_delete(s->car_id);
        car = -1;
    }
    return car & 0xffff;
}

/* FireEngine_SetDestination 0x42ea90: the engine heads for block (x, y, z) >> 6 (pixels) with the
   siren on and the horn state 1; the route search (mode 5, from the car's block) starts unless
   another controller holds the search: then it waits in state 5 (stopped, braking). Either end off
   the road: state 0x27 (give up); the search not finished at once: state 2 (and +0x1c 2). */
static void fire_engine_set_destination(Sentinel *s, int16_t x, int16_t y, int16_t z)
{
    Car *c = sentinel_car(s);
    int32_t cz = c->spr.z, cx = c->spr.x, cy = c->spr.y;
    g_sent_route_car = c->id + 1;
    s->dest[0] = (uint8_t)(x >> 6);
    s->dest[1] = (uint8_t)(y >> 6);
    s->dest[2] = (uint8_t)(z >> 6);
    s->u95 = 0;
    car_siren_on(c);
    c->horn = 1;
    if (g_path_owner >= 0) {
        s->state = 5;
        s->sub = 5;
        c->unkc0 = 1;
        c->brake = 1;
        c->speed = 0;
        return;
    }
    s->u38 = 0;
    c->counter119 = 1;
    int bz = (int16_t)(cz >> 22), by = (int16_t)(cy >> 22), bx = (int16_t)(cx >> 16) >> 6;
    if ((cache_at(bx, by, bz) & 0xf) != 0 && (cache_at(x >> 6, y >> 6, z >> 6) & 0xf) != 0) {
        int r = (int16_t)path_find(cx >> 22, cy >> 22, cz >> 22, x >> 6, y >> 6, z >> 6, 5, (uint8_t)s->id);
        if (r != 0) {
            if (r == 3) s->sub = 2, s->state = 2;
            return;
        }
    }
    s->state = 0x27;
}

/* FireEngine_ArriveCheck 0x42ebe0: the engine takes fire i: more than 2 blocks away (Manhattan) it
   drives there (state 1), else it stops for it (state 10). */
static void fire_engine_arrive_check(Sentinel *s, int i)
{
    Fire *f = &g_fire.fires[(int16_t)i];
    const Car *c = sentinel_car(s);
    int dy = (int16_t)(c->spr.y >> 22) - (f->y >> 6), dx = (int16_t)(c->spr.x >> 22) - (f->x >> 6);
    if ((int16_t)(abs(dy) + abs(dx)) > 2) {
        f->engine = c->id;
        s->state = 1;
        fire_engine_set_destination(s, f->x, f->y, f->z);
        return;
    }
    s->state = 10;
    f->engine = s->car_id;
    s->u95 = 0;
}

int fire_engine_dispatch(const int16_t info[4], int slot)
{
    if (g_fire.engines_out >= 3) {
        int k = 0;
        while (k < FIRE_ENGINES && g_fire.engines[k] != -1) k++;
        if (k == FIRE_ENGINES) {
            /* quirk: the records looked at are sentinels 0..2, and one counts only when it is the
               engine of the same engines[] slot and heading home (+0x95) */
            for (int i = 0; i < FIRE_ENGINES; i++) {
                const Sentinel *r = sentinel_ptr(i);
                if (!r || r->u95 != 1 || g_fire.engines[i] != r->id) continue;
                Sentinel *e = sentinel_ptr(g_fire.engines[i]);
                if (!e) return -1;
                fire_engine_arrive_check(e, slot);
                Car *c = sentinel_car(e);
                c->brake = 1;
                c->unkc0 = 1;
                c->speed = 0;
                return c->id;
            }
            return -1;
        }
    }
    int sid = (int16_t)sentinel_find_free();
    if (sid < 0) return -1;
    Sentinel *s = sentinel_ptr(sid);
    int best = -1, bestd = 0xff4b;
    for (int i = 0; i < 4; i++) {
        int d = abs(g_fire_engine_bases[i].y - (info[2] >> 6)) + abs(g_fire_engine_bases[i].x - (info[1] >> 6));
        if (d < bestd) best = i, bestd = d;
    }
    if ((int16_t)fire_engine_spawn(sid, best) < 0) {
        /* the nearest station gave none: every station in turn */
        int i = 0;
        for (; i < 4; i++)
            if ((uint16_t)fire_engine_spawn(sid, i) < 0x8000) break;
        if (i == 4) return -1;
    }
    fire_engine_set_destination(s, info[1], info[2], info[3]);
    return sentinel_car(s)->id;
}

/* FireEngine_ReturnToBase 0x42ee40: the engine heads home: the nearest fire station, siren and horn
   off, +0x95 = 1 (Dispatch may send it to another fire on the way), and unless another controller
   holds the search a route (mode 5) whose result goes to 0x7537b2. Then state 0x28 (driving home;
   +0x1c too), 0x23 while the search runs, 0x27 when it failed. Quirk: an end off the road sets 0xff,
   immediately overwritten, and the state follows the previous search result. */
static void fire_engine_return_to_base(Sentinel *s)
{
    Car *c = sentinel_car(s);
    if (!c) return;
    int32_t cz = c->spr.z, cx = c->spr.x, cy = c->spr.y;
    g_sent_route_car = c->id + 1;
    /* the request of the current victim gets the record (an engine has none: victims[0] is -1, so
       the original writes 0x50caae, unused memory before the request table; the port drops it) */
    int v = s->victims[(int8_t)s->u21];
    if (v >= 0 && v < SENT_REQUESTS) g_sent_requests[v].sentinel = s->id;
    sentinel_pick_nearest_base(s);
    car_siren_off(c);
    c->horn = 0;
    s->u95 = 1;
    if (g_path_owner >= 0) return;
    s->u38 = 0;
    c->counter119 = 1;
    if ((cache_at((int16_t)(cx >> 22), (int16_t)(cy >> 22), (int16_t)(cz >> 22)) & 0xf) != 0 &&
        (cache_at(s->dest[0], s->dest[1], s->dest[2]) & 0xf) != 0)
        g_path_result = (int16_t)path_find(cx >> 22, cy >> 22, cz >> 22, s->dest[0], s->dest[1], s->dest[2], 5, (uint8_t)s->id);
    else
        s->state = 0xff;
    s->sub = 0x28;
    s->state = 0x28;
    if (g_path_result != 0) {
        if (g_path_result == 3) s->state = 0x23, s->sub = 0x23;
        return;
    }
    s->state = 0x27;
}

/* FireEngine_AimHoseAtObject 0x42f240: the hose turns 3 a frame toward the fire object; 1 when it
   points at it (within 3), -1 while turning */
static int fire_engine_aim_hose_at_object(Sentinel *s, const Fire *f)
{
    const Obj *t = obj_get(f->obj);
    Obj *h = hose_of(s);
    int a = math_atan2(t->spr.y - h->spr.y, t->spr.x - h->spr.x);
    uint16_t d = (uint16_t)((a - h->spr.angle) & 0x3ff);
    if (d == 0) return 1;
    h->spr.angle = (int16_t)(h->spr.angle + (d < 0x201 ? 3 : -3));
    if (d < 4) d = 0;
    h->spr.angle &= 0x3ff;
    return d == 0 ? 1 : -1;
}

/* FireEngine_AimHoseAtCar 0x42f2d0: the hose turns 3 a frame toward the car's centre (back along the
   car: it is mounted behind it); 1 within 3 */
static int fire_engine_aim_hose_at_car(Sentinel *s)
{
    const Car *c = sentinel_car(s);
    Obj *h = hose_of(s);
    int a = math_atan2(c->spr.y - h->spr.y, c->spr.x - h->spr.x);
    uint16_t d = (uint16_t)((a - h->spr.angle) & 0x3ff);
    h->spr.angle = (int16_t)(h->spr.angle + (d < 0x201 ? 3 : -3));
    h->spr.angle &= 0x3ff;
    if (d > 3 && d != 0) return -1;
    return 1;
}

/* the engine gives its fire up (burnt out, or out too long): spraying (a jet up) it first switches
   the jet off (0x97); otherwise, before the hose work, the fire object goes and the fire is
   dropped; then dismissed (0xff) */
static void fire_engine_give_up(Sentinel *s, const Car *c)
{
    int i = fire_of(c->id);
    if (i != FIRE_MAX && g_fire.fires[i].objs[0] > -1) {
        s->state = 0x97;
        return;
    }
    if (s->state < 0x6e) {
        int j = fire_of(s->car_id);
        if (j != FIRE_MAX) obj_delete_wrapper(g_fire.fires[j].obj);
        fire_extinguish(c->id);
    }
    s->state = 0xff;
}

/* the jet (state 0x6e): from the hose to the fire object, whose position the record takes (pixels);
   the distance d (integer square root of the squared pixel distances, as shorts) gives n = (d - 52)
   / 32 + 1 segments and the rest of the division (over 16: 0) shortens the last one. Over 320 pixels
   or 9 segments: no jet (0xaa). Along the line a wall (cached type bits 0x70 = 0x50 on the fixed
   layer 2, quirk) also sets 0xaa, but a jet that is then made overrides it. The jet: a type 0x30
   object 0x24 ahead of the hose, n - 1 more each 0x1e ahead of the previous, and a type 0x31 end
   (0x14 - n) * 2 - rest ahead of the last; then state 0x82 (spraying, objs[9] = -1) and the fire
   object goes (Obj_DeleteWrapper: its record keeps the position the spraying state compares).
   The end object not made: the jet is deleted again (0xa0, the fire object stays). */
static int fire_engine_make_jet(Sentinel *s, const Car *c, int i)
{
    Fire *f = &g_fire.fires[i];
    const Obj *fo = obj_get(f->obj);
    f->x = (int16_t)(fo->spr.x >> 16);
    f->y = (int16_t)(fo->spr.y >> 16);
    f->z = (int16_t)(fo->spr.z >> 16);
    const Obj *h = hose_of(s);
    int dx = (int16_t)abs((int16_t)(h->spr.x >> 16) - (int16_t)(fo->spr.x >> 16));
    int dy = (int16_t)abs((int16_t)(h->spr.y >> 16) - (int16_t)(fo->spr.y >> 16));
    int16_t d = (int16_t)(int32_t)sqrt((double)(dx * dx + dy * dy));   /* fild, fsqrt, __ftol */
    if (d > 0x140) {
        s->state = 0xaa;
        return 0;
    }
    int e = (int16_t)(d - 0x34);
    int n = e / 32 + 1, rest = e % 32;
    if ((int16_t)rest > 0x10) rest = 0;
    if ((int16_t)n == -2 || (int16_t)n >= 9) {
        s->state = 0xaa;
        return 0;
    }
    if ((int16_t)n == -1) {
        s->state = 0x78;
        return 0;
    }
    int a = math_atan2(h->spr.y - fo->spr.y, h->spr.x - fo->spr.x);
    for (int k = 0; (int16_t)k < (int16_t)n; k++) {
        int32_t px = math_sin(a) * (k << 6) + h->spr.x, py = math_cos(a) * (k << 6) + h->spr.y;
        if ((car_cache_at(0x20000 + (py >> 22) * 0x100 + (px >> 22)) & 0x70) == 0x50) {
            s->state = 0xaa;
            break;
        }
    }
    f->objs[0] = (int16_t)obj_create_attached(s->u50, 0, 0, 0x24, 0x30);
    if (f->objs[0] == -1) {
        f->objs[0] = -1;
    } else {
        int k = 1;
        for (; (int16_t)k < (int16_t)n; k++) {
            f->objs[k] = (int16_t)obj_create_attached(f->objs[k - 1], 0, 0, 0x1e, 0x30);
            if (f->objs[k] == -1) break;
        }
        /* (the previous one is always there: the -1 case can't happen) */
        if (f->objs[k - 1] == -1) {
            delete_jet(f, k);
            s->state = 0xa0;
            return 0;
        }
        f->objs[k] = (int16_t)obj_create_attached(f->objs[k - 1], 0, 0, (0x14 - n) * 2 - rest, 0x31);
        if (f->objs[k] == -1) {
            delete_jet(f, k);
            s->state = 0xa0;
            return 0;
        }
        f->objs[9] = -1;
        s->state = 0x82;
    }
    int j = fire_of(c->id);
    if (j != FIRE_MAX) obj_delete_wrapper(g_fire.fires[j].obj);
    return 0;
}

/* FireEngine_Update 0x42f460: one frame of a fire engine (Sentinel_DriveCar, kind 6); 1: the driving
   goes on this frame, 0: the engine stands. First the give-up checks (burnt out, damage 100; the
   2500-frame timer of its engines[] slot run out), then the state (+0x1b):
     1     driving to the fire: stops (state 10) without a route node, or within 4 blocks (Chebyshev)
           unless moving and the block ahead is closer, a different layer counting 500 more;
     2     the route search running (owner): found: 1, failed: 0x27; another controller's: brakes;
     5     waiting for the search: starts it (mode 5) -> 2; ends off the road: 0x27;
     8     like 1, braking (no state sets it);
     10    braking to a stop -> 0x14;            0x14  the hose stops turning with the car -> 0x64;
     0x64  the hose turns to the fire -> 0x6e;   0x6e  the water jet (fire_engine_make_jet);
     0x78  a jet of the end object only;         0x82  spraying: 261 frames while the fire object
           stays where it was, then 0x97;        0x97  the jet deleted -> 0xa0;
     0xa0  the hose turns back, turns with the car again, the fire is dropped -> 0x1e;
     0x1e  the next unattended fire (FireEngine_ArriveCheck) or home (FireEngine_ReturnToBase);
     0x23  the search home running: found: 0x28, failed: 0x27;
     0x27  gives the fire up (the fire object and the record go) -> 0xff (dismissed);
     0x28  driving home: at the end of the route (no node, not turning) 0xff; an unattended fire
           on the way is taken (ArriveCheck);
     0xaa  -> 0x97.
   Burnt out (damage 100) the hose is deleted at the end of every frame and the fire dropped. */
int fire_engine_update(Sentinel *s)
{
    Car *c = sentinel_car(s);
    int dx = 0, dy = 0;   /* [0x14], [0x18]: the block ahead along the road direction */
    switch (c->road_dirs) {
    case 1: dy = -1; break;
    case 2: dy = 1; break;
    case 4: dx = -1; break;
    case 8: dx = 1; break;
    }
    int bx = c->spr.x >> 22, by = c->spr.y >> 22, bz = c->spr.z >> 22;
    int r = 0;
    if (c->damage >= 100) {
        fire_engine_give_up(s, c);
    } else {
        for (int k = 0; k < FIRE_ENGINES; k++) {
            if (g_fire.engines[k] != s->id) continue;
            if (--g_fire.timers[k] == 0) fire_engine_give_up(s, c);
            break;
        }
    }

    int i;
    switch (s->state) {
    case 1: case 8: {
        int ax = abs(dx - s->dest[0] + bx), ay = abs(dy - s->dest[1] + by);
        int ahead = ax > ay ? ax : ay;
        ax = abs(bx - s->dest[0]), ay = abs(by - s->dest[1]);
        int cur = ax > ay ? ax : ay;
        if (s->state == 1) {
            if (abs(bz - s->dest[2]) != 0) ahead = cur + 500;
            if (s->u0c != 0 && (cur >= 5 || (c->speed != 0 && (int16_t)ahead < cur) || (int16_t)ahead >= 500)) {
                r = 1;
                break;
            }
        } else if (s->u0c != 0 && (cur >= 5 || (c->speed != 0 && (int16_t)ahead < cur))) {
            c->brake = 1;
            c->unkc0 = 1;
            r = 1;
            break;
        }
        c->brake = 1;
        c->unkc0 = 1;
        s->state = 10;
        break;
    }
    case 2:
        if (g_path_owner == -1) {
            if (g_path_result == 1) {
                s->state = 1;
                c->unkc0 = 0;
                c->brake = 0;
            } else if (g_path_result == 0) {
                s->state = 0x27;
            }
            r = 1;
        } else if (c->speed > 0) {
            c->input = 0;
            c->unkc0 = 1;
            c->brake = 1;
        }
        break;
    case 5:
        if (g_path_owner != -1) {
            s->state = 5;
            break;
        }
        if ((cache_at(bx, by, bz) & 0xf) == 0 || (cache_at(s->dest[0], s->dest[1], s->dest[2]) & 0xf) == 0) {
            s->state = 0x27;
            break;
        }
        g_path_result = (int16_t)path_find(bx, by, bz, s->dest[0], s->dest[1], s->dest[2], 5, (uint8_t)s->id);
        s->state = 2;
        s->sub = 2;
        break;
    case 10:
        c->unkc0 = 1;
        c->brake = 1;
        c->input = 0;
        if (c->speed == 0) s->state = 0x14;
        else if (c->speed > 0) c->speed--;
        break;
    case 0x14:
        c->unkc0 = 1;
        c->brake = 1;
        s->state = 0x64;
        hose_of(s)->attach_kind = 1;
        break;
    case 0x1e:
        i = (int16_t)fire_find_nearest_unattended(c->spr.x, c->spr.y);
        if (i == -1) fire_engine_return_to_base(s);
        else fire_engine_arrive_check(s, i);
        break;
    case 0x23:
        if (g_path_owner == -1) {
            if (g_path_result == 1) {
                c->unkc0 = 0;
                c->brake = 0;
                s->state = 0x28;
                s->sub = 0x28;
            } else if (g_path_result == 0) {
                s->state = 0x27;
            }
        } else if (c->speed > 0) {
            c->input = 0;
            c->unkc0 = 1;
            c->brake = 1;
            r = 1;
        }
        break;
    case 0x27:
        i = fire_of(s->car_id);
        if (i != FIRE_MAX && i > -1) {
            int j = fire_of(s->car_id);
            if (j != FIRE_MAX) obj_delete_wrapper(g_fire.fires[j].obj);
            fire_extinguish(c->id);
        }
        s->state = 0xff;
        break;
    case 0x28:
        if (s->u0c == 0 && c->turn_delta == 0) {
            s->state = 0xff;
            break;
        }
        for (i = 0; i < FIRE_MAX; i++)
            if (g_fire.fires[i].engine == -1 && g_fire.fires[i].obj != -1) break;
        if (i < FIRE_MAX) fire_engine_arrive_check(s, i);
        r = 1;
        break;
    case 0x64:
        i = fire_of(c->id);
        if (i == FIRE_MAX || c->id < 0) {
            s->state = 0x27;
            break;
        }
        if (fire_engine_aim_hose_at_object(s, &g_fire.fires[i]) == 1) s->state = 0x6e;
        break;
    case 0x6e:
        i = fire_of(c->id);
        if (i == FIRE_MAX || c->id < 0) {
            s->state = 0x27;
            break;
        }
        r = fire_engine_make_jet(s, c, i);
        break;
    case 0x78:
        i = fire_of(c->id);
        if (i == FIRE_MAX || c->id < 0) {
            s->state = 0x27;
            break;
        }
        g_fire.fires[i].objs[0] = (int16_t)obj_create_attached(s->u50, 0, 0, 0x24, 0x31);
        if (g_fire.fires[i].objs[0] != -1) {
            g_fire.fires[i].objs[9] = -1;
            s->state = 0x82;
        }
        i = fire_of(c->id);
        if (i != FIRE_MAX) obj_delete_wrapper(g_fire.fires[i].obj);
        break;
    case 0x82: {
        i = fire_of(c->id);
        if (i == FIRE_MAX || c->id < 0) {
            s->state = 0x27;
            break;
        }
        Fire *f = &g_fire.fires[i];
        const Obj *fo = obj_get(f->obj);
        if ((int16_t)(fo->spr.x >> 16) != f->x || (int16_t)(fo->spr.y >> 16) != f->y || (int16_t)(fo->spr.z >> 16) != f->z)
            s->state = 0x97;
        else if (f->objs[9] < 0x104)
            f->objs[9]++;
        else
            s->state = 0x97;
        break;
    }
    case 0x97: {
        i = fire_of(c->id);
        if (i == FIRE_MAX || c->id < 0) {
            s->state = 0x27;
            break;
        }
        Fire *f = &g_fire.fires[i];
        int n = 0;
        while (n < 9 && f->objs[n] != -1) n++;
        delete_jet(f, n);
        for (int k = 0; k < FIRE_OBJS; k++) f->objs[k] = -1;
        s->state = 0xa0;
        break;
    }
    case 0xa0:
        i = fire_of(c->id);
        if (i == FIRE_MAX || c->id < 0) break;
        if (fire_engine_aim_hose_at_car(s) == 1) {
            s->state = 0x1e;
            hose_of(s)->attach_kind = 5;
            fire_extinguish(c->id);
        }
        break;
    case 0xaa:
        s->state = 0x97;
        break;
    }

    if (c->damage >= 100) {
        if (s->u50 >= 0) obj_delete(s->u50);   /* (the original deletes object -1 on later frames) */
        s->u50 = -1;
        c->siren_state = 0;
        fire_extinguish(c->id);
    }
    return r;
}

/* Fire_Register 0x42ef80: a fire object (type 0x12 only) not recorded yet, with a free record, fewer
   than 4 fires and engines, no recorded fire near (Fire_IsNearActive) and not on the cars 0xb / 0xd:
   within 2 blocks (x + y, against every record, the free ones at block 0 too) of a recorded fire it
   is that fire's `extra`; otherwise the nearest road (Map_FindNearestRoad) on its layer, at most 4
   blocks away, gets an engine (FireEngine_Dispatch; none: the record is dropped again). */
void fire_register(int obj)
{
    int16_t id = (int16_t)obj;
    for (int i = 0; i < FIRE_MAX; i++)
        if (g_fire.fires[i].obj == id || g_fire.fires[i].extra == id) return;
    int slot = 0;
    while (g_fire.fires[slot].obj != -1)
        if (++slot > 3) return;
    if (g_fire.count >= 4 || g_fire.engine_count >= 4) return;
    Obj *o = obj_get(obj);
    if (o->type != 0x12 || fire_is_near_active(id)) return;
    if (o->owner >= 0 && o->attach_kind == 1) {
        int m = car_get(o->owner)->model;
        if (m == 0xb || m == 0xd) return;
    }
    int16_t oz = (int16_t)(o->spr.z >> 16);
    int by = (int16_t)(o->spr.y >> 16) >> 6, bx = (int16_t)(o->spr.x >> 16) >> 6;
    for (int i = 0; i < FIRE_MAX; i++) {
        int d = (int16_t)(abs(bx - (g_fire.fires[i].x >> 6)) + abs(by - (g_fire.fires[i].y >> 6)));
        if (d < 3) {
            o->u12 = 1;   /* Obj_SetFlagE2 0x44ed30 */
            g_fire.fires[i].extra = id;
            return;
        }
    }
    uint8_t road[8] = { 0 };
    road[2] = (uint8_t)(o->spr.x >> 22);
    road[3] = (uint8_t)(o->spr.y >> 22);
    road[4] = (uint8_t)(o->spr.z >> 22);
    if (!map_find_nearest_road(road) || oz >> 6 != road[4]) return;
    int d = abs(by - road[3]), e = abs(bx - road[2]);
    if (e <= d) e = d;
    if (e >= 5) return;
    o->u12 = 1;
    Fire *f = &g_fire.fires[slot];
    g_fire.count++;
    f->obj = id;
    f->x = (int16_t)(road[2] << 6);
    f->y = (int16_t)(road[3] << 6);
    f->z = (int16_t)(road[4] << 6);
    int16_t info[4] = { id, f->x, f->y, f->z };   /* {object, x, y, z} for the dispatcher */
    int16_t engine = (int16_t)fire_engine_dispatch(info, slot);
    if (engine == -1) {
        g_fire.count--;
        f->obj = f->x = f->y = f->z = -1;
    }
    f->engine = engine;
}

/* Fire_HasObjects 0x42f350: the engine's fire has a first object */
int fire_has_objects(int engine)
{
    for (int i = 0; i < FIRE_MAX; i++)
        if (g_fire.fires[i].engine == (int16_t)engine) return g_fire.fires[i].objs[0] >= 0;
    return 0;
}

/* Fire_ClearObjects 0x42f3a0: the engine's fire's objects (up to the first -1 of the first 9) are
   deleted, last first, and the 10 entries cleared. */
void fire_clear_objects(int engine)
{
    int i = 0;
    while (i < FIRE_MAX && g_fire.fires[i].engine != (int16_t)engine) i++;
    if (i == FIRE_MAX || g_fire.fires[i].objs[0] < 0) return;
    Fire *f = &g_fire.fires[i];
    int n = 0;
    while (n < 9 && f->objs[n] != -1) n++;
    for (int k = n - 1; k >= 0; k--) obj_delete(f->objs[k]);
    for (int k = 0; k < FIRE_OBJS; k++) f->objs[k] = -1;
}
