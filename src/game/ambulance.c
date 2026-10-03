/* The ambulance crews 0x401340-0x401a7f (ambulance.h). A crew is a sentinel of kind 1 with an
   ambulance (model 5) and, at a call, a medic (+0x60). Its victims are ambulance requests (the ped
   ids at +0x24, the current one +0x21, their number +0x22). docs/police.md. */
#include "ambulance.h"
#include "../render/sprite.h"
#include "car.h"
#include "coll.h"
#include "gmath.h"
#include "ped.h"
#include "ped_internal.h"
#include "route.h"
#include "sentinel.h"
#include "stubs.h"
#include <stdlib.h>

int16_t g_ambu_crews[AMBU_CREWS_MAX];
int16_t g_ambu_ncrews;

/* the crew drops out of the list 0x50f290 (the last one takes its place) */
static void unlist(const Sentinel *s)
{
    int k = 0;
    while (g_ambu_crews[k] != s->id) k++;   /* (no bound: the crew is listed) */
    g_ambu_ncrews--;
    g_ambu_crews[k] = g_ambu_crews[g_ambu_ncrews];
}

/* Ambu_SendToHospital 0x401340: the destination becomes the first hospital and a path search
   starts from the nearest road block to the car (mode 3, then mode 2; neither: the car stops being
   driven, control 0). No road at all: state 0xff. */
void ambu_send_to_hospital(Sentinel *s)
{
    Car *c = sentinel_car(s);
    uint8_t q[8] = { 0 };
    s->dest[0] = g_hospital_block.x;
    s->dest[1] = g_hospital_block.y;
    s->dest[2] = g_hospital_block.z;
    q[2] = (uint8_t)(c->spr.x >> 22), q[3] = (uint8_t)(c->spr.y >> 22), q[4] = (uint8_t)(c->spr.z >> 22);
    if (!(uint8_t)map_find_nearest_road(q)) {
        s->state = 0xff;
        return;
    }
    s->u38 = 0;
    s->sub = s->state;
    g_path_result = (int16_t)path_find(q[2], q[3], q[4], g_hospital_block.x, g_hospital_block.y,
                                       g_hospital_block.z, 3, s->id);
    if (g_path_result == 0) {
        g_path_result = (int16_t)path_find(q[2], q[3], q[4], g_hospital_block.x, g_hospital_block.y,
                                           g_hospital_block.z, 2, s->id);
        if (g_path_result == 0) c->control = 0;
    }
    c->counter119 = 0;
}

/* Ambu_AssignVictim 0x401460: a request joins the first crew already out (one to nine victims, not
   one of its own peds) whose last victim lies within 10 blocks (Manhattan); else, without a crew yet
   and fewer than 10 crews, a new crew is sent (Ambu_Dispatch) and plans its route. The victim lies
   down for the medic (state 0xc). The new crew doesn't record itself in the request (+8 stays -1,
   only +6 = 1), so the request stays on the call queue. */
void ambu_assign_victim(SentRequest *r)
{
    for (int k = 0; k < g_ambu_ncrews; k++) {
        Sentinel *s = sentinel_ptr(g_ambu_crews[k]);
        if (!s || r->id == s->u60 || r->id == s->u62) continue;
        int8_t nv = (int8_t)s->u22;
        if (nv <= 0 || nv >= 10 || r->sentinel >= 0) continue;
        const SentRequest *last = &g_sent_requests[s->victims[nv - 1]];
        int d = abs((int)r->y - last->y) + abs((int)r->x - last->x);
        if ((int16_t)d >= 10) continue;
        r->sentinel = s->id;
        s->victims[nv] = r->id;
        ped_get(r->id)->state = 0xc;
        s->u22 = (uint8_t)(nv + 1);
        Car *c = sentinel_car(s);
        if (c->owner_status == 0) c->owner_status = 1;
        return;
    }
    if (r->sentinel != -1 || g_ambu_ncrews >= AMBU_CREWS_MAX) return;
    int f = (int16_t)sentinel_find_free();
    if (f == -1) return;
    Sentinel *s = sentinel_ptr(f);
    if (!s || (int16_t)ambu_dispatch(s->id, r) == -1) return;
    ped_get(r->id)->state = 0xc;
    s->victims[(int8_t)s->u21] = r->id;
    s->u22++;
    r->u6 = 1;
    sentinel_plan_route_to_target(s);
}

/* Ambu_Dispatch 0x4015d0: sentinel s becomes a crew (kind 1) at the hospital nearest the request:
   an ambulance (model 5, with a driver) on the road there, driven by s (control 2, owner status 1,
   top speed 15, +0x104 = 6), the driver dressed as a cop (Ped_GetCopLook); state 1, no victims yet;
   listed in 0x50f290. Returns the car, -1 if none. */
int ambu_dispatch(int sid, const SentRequest *r)
{
    Sentinel *s = sentinel_ptr(sid);
    if (!s) return -1;
    s->kind = SENT_AMBULANCE;
    sentinel_pick_nearest_base_to(s, (const uint8_t *)r);
    int car = (int16_t)car_spawn_on_road(s->dest[0] * 0x400000 + 0x200000, s->dest[1] * 0x400000 + 0x200000,
                                         s->dest[2] * 0x400000 + 0x3e0000, 5, 1);
    if (car != -1) {
        Car *c = car_get(car);
        c->owner_status = 1;
        c->control = CAR_CTL_AI2;
        c->counter119 = 1;
        c->sentinel = (int16_t)sid;
        c->max_speed = 0xf;
        c->cruise = 6;
        c->unk139 = 0;
        if (c->driver != -1) {
            Ped *p = ped_get(c->driver);
            ped_get_cop_look(&p->graphic, &p->remap);
            sprite_set_remap(&p->spr, p->remap);
        }
        s->u21 = 0;
        s->u22 = 0;
        s->state = 1;
        s->route = s->id;
        s->car_id = (int16_t)car;
        s->u38 = 0;
        sentinel_set_car(s, c);
        g_ambu_crews[g_ambu_ncrews++] = s->id;
    }
    return car;
}

/* Ambu_Remove 0x401700: out of the crew list, the medic removed, the path search freed if it was the
   crew's, the ambulance deleted, the record reset */
void ambu_remove(Sentinel *s)
{
    unlist(s);
    if (s->u60 != -1) ped_remove(ped_get(s->u60)->id);
    if (g_path_owner == s->id) g_path_owner = -1;
    car_delete(s->car_id);
    sentinel_reset(sentinel_ai(s));
}

/* a point `d` pixels along the car's heading (d < 0: behind it) */
static int32_t along_x(const Car *c, int d) { return math_sin(c->spr.angle) * d + c->spr.x; }
static int32_t along_y(const Car *c, int d) { return math_cos(c->spr.angle) * d + c->spr.y; }

/* the crew gives up when nobody of it is near a screen any more (medic, its other ped, the victim) */
static bool crew_unseen(const Sentinel *s)
{
    return s->u60 > -1 && s->u62 > -1 && s->victims[(int8_t)s->u21] > -1 &&
           !ped_is_near_screen(ped_get(s->u60)) && !ped_is_near_screen(ped_get(s->u62)) &&
           !ped_is_near_screen(ped_get(s->victims[(int8_t)s->u21]));
}

/* Ambu_Update 0x401790: one step of a crew; returns 1 when the car should drive on (Sentinel_DriveCar
   then steers it), 0 when it stays. States (+0x1b):
     200   wait for the path search, then plan the route to the victim: state 1 (return to 1);
     1     driving; at the victim's block (+0xc = 0, or within 5 blocks and stopped) with no route
           (+0x4e) left: the junction it held is released, the car stops: 0x50;
     0x50  the rear door opens (the car stopped, owner status 0): 0x5a; nobody near a screen: 0x6c;
     0x5a  the medic gets out at the rear wheels (Ped_CreateSpecial): 0x5b;
     0x5b  he walks behind the car: 0x5c, then to the victim: 100;
     100   he follows the victim's position until he touches it (Ped_TestCollision): 0x66, or
           arrives: 0x65;
     0x65  75 frames later the victim is revived (health 100, walking: state 2, objective 0x19): 0x66;
     0x66 / 0x67  the medic walks back behind the car, 0x6a / 0x6b into it;
     0x6c  the medic gone, the victim's body removed (still dead: state 0x17), the rear door closes
           (0x6d) and the crew continues at state 2;
     2     the next victim: its block is the destination (0x50 within 6 blocks, else 200); none
           left: back to the hospital (3);
     3     (path search idle) Ambu_SendToHospital: 4;
     4     at the hospital (+0xc = 0) the crew is removed (as Ambu_Remove);
     0xfe  the crew is told to go (Sentinel_DriveCar, Police_EndPursuit...): once the medic is off
           screen he and the record go (the car stays); a car without a driver sends the medic back
           to it (0x66);
     0xff  nothing.
   Before that: a car taken out of control 2 (stolen) with both peds set makes them stop walking and
   the crew leave (0xfe); during the call (0x51..0x65) a victim that is alive again, not lying for
   the medic (state 0xc) or dead for more than 950 frames ends it (0x66 near a screen, else 0x6c).
   After it: a dead medic ends the call (the medic and, while reviving, the victim become ped
   control 1), and the crew drives back (3). */
int ambu_update(Sentinel *s)
{
    Car *c = sentinel_car(s);
    Ped *victim = NULL;
    int ret = 0;
    if (c->control != CAR_CTL_AI2 && s->u60 > -1 && s->u62 > -1) {
        Ped *a = ped_get(s->u60), *b = ped_get(s->u62);
        a->walk_x = 0, a->u48 = 1;
        b->walk_x = 0, b->u48 = 1;
        s->state = 0xfe;
    }
    if (s->state > 0x50 && s->state < 0x66) {
        const Ped *v = ped_get(s->victims[(int8_t)s->u21]);
        if ((v->health != 0 || v->state != 0xc || v->u0e > 0x3b6) && s->u60 > -1)
            s->state = ped_is_near_screen(ped_get(s->u60)) ? 0x66 : 0x6c;
    }
    switch (s->state) {
    case 1:
        if ((s->u0c == 0 || (g_sent_dest_dist < 5 && c->speed == 0)) && s->u4e == 0) {
            if (s->u58 != 0) {
                s->u58 = 4;
                sentinel_override_lights(s, c);
            }
            c->speed = 0;
            c->unkc0 = 0;
            c->input = 0;
            c->brake = 0;
            c->owner_status = 0;
            s->state = 0x50;
            break;
        }
        ret = 1;
        break;
    case 2: {
        int8_t cur = (int8_t)(s->u21 + 1);
        s->u21 = (uint8_t)cur;
        s->u12 = 0;
        s->group = 0;
        s->u4e = 0;
        if (cur < (int8_t)s->u22) {
            const SentRequest *r = &g_sent_requests[s->victims[cur]];
            s->u10 = 0;
            s->u14 = 0, s->u16 = 0;
            int dy = abs((int)(uint8_t)(c->spr.y >> 22) - r->y), dx = abs((int)(uint8_t)(c->spr.x >> 22) - r->x);
            int d = dx > dy ? dx : dy;
            s->dest[0] = r->x, s->dest[1] = r->y, s->dest[2] = r->z;
            s->state = (int16_t)d < 6 ? 0x50 : 200;
        } else {
            c->owner_status = 1;
            c->unkc0 = 0;
            s->u14 = 0, s->u16 = 0;
            s->dest[0] = g_hospital_block.x, s->dest[1] = g_hospital_block.y, s->dest[2] = g_hospital_block.z;
            s->sub = 3;
            s->state = 3;
            s->u22 = 10, s->u21 = 10;
        }
        break;
    }
    case 3:
        if (g_path_owner < 0) {
            s->u14 = 0, s->u16 = 0;
            s->u10 = 0;
            c->counter119 = 0;
            s->state = 4;
            s->u22 = 10, s->u21 = 10;
            ambu_send_to_hospital(s);
        }
        break;
    case 4:
        sentinel_override_lights(s, c);
        if (s->u0c == 0) {
            ambu_remove(s);
            break;
        }
        ret = 1;
        break;
    case 0x50:
        if (crew_unseen(s)) {
            s->state = 0x6c;
            break;
        }
        c->owner_status = 0;
        c->speed = 0;
        c->brake = 1;
        c->unkc0 = 1;
        c->input = 0;
        if (car_open_rear_door_step(s->car_id)) s->state = 0x5a;
        break;
    case 0x5a: {
        int p = (int16_t)ped_create_special(c->rear_x, c->rear_y, c->spr.z + 0x10000, 1,
                                            (c->spr.angle - 0x200) & 0x3ff, 0, 0, 0, 0);
        if (p > -1) {
            s->u60 = (int16_t)p;
            ped_get(p)->car = c->id;
            s->state = 0x5b;
            s->u20 = 1;
        }
        break;
    }
    case 0x5b: {
        Ped *m = ped_get(s->u60);
        m->car = c->id;
        ped_set_dest_objective36(m, along_x(c, c->length), along_y(c, c->length), (c->spr.angle - 0x200) & 0x3ff);
        s->state = 0x5c;
        break;
    }
    case 0x5c: {
        Ped *m = ped_get(s->u60);
        if (m->walk_x == 0) {
            victim = ped_get(s->victims[(int8_t)s->u21]);
            ped_set_dest_objective36(m, victim->spr.x, victim->spr.y, victim->spr.angle);
            s->state = 100;
            m->car = -1;
        }
        break;
    }
    case 100: {
        Ped *m = ped_get(s->u60);
        victim = ped_get(s->victims[(int8_t)s->u21]);
        if (m->walk_x == 0) {
            s->state = 0x65;
            m->idle_count = 0;
        } else {
            m->walk_x = victim->spr.x, m->walk_y = victim->spr.y;
            m->target_x = victim->spr.x, m->target_y = victim->spr.y;
            if (ped_test_collision(victim)) s->state = 0x66;
        }
        break;
    }
    case 0x65: {
        Ped *m = ped_get(s->u60);
        victim = ped_get(s->victims[(int8_t)s->u21]);
        if (++m->idle_count == 0x4b) {
            victim->health = 100;
            victim->anim = 1;
            victim->state = 2;
            victim->u78 = 8;
            victim->u7c = 2;
            victim->objective = 0x19;
            s->state = 0x66;
        } else {
            m->speed = 0;
        }
        if (crew_unseen(s)) s->state = 0x6c;
        break;
    }
    case 0x66:
        ped_set_dest_objective36(ped_get(s->u60), along_x(c, c->half_l), along_y(c, c->half_l), c->spr.angle);
        s->state = 0x67;
        break;
    case 0x67: {
        Ped *m = ped_get(s->u60);
        if (m->walk_x == 0) {
            s->state = 0x6a;
        } else {
            m->walk_x = along_x(c, c->length);
            m->walk_y = along_y(c, c->length);
            m->target_x = m->walk_x, m->target_y = m->walk_y;
        }
        if (crew_unseen(s)) s->state = 0x6c;
        break;
    }
    case 0x6a: {
        Ped *m = ped_get(s->u60);
        m->car = c->id;
        ped_set_dest_objective36(m, c->spr.x, c->spr.y, c->spr.angle);
        s->state = 0x6b;
        break;
    }
    case 0x6b: {
        Ped *m = ped_get(s->u60);
        if (m->walk_x != 0 && ped_is_visible_recent(m)) {
            m->walk_x = c->spr.x, m->walk_y = c->spr.y;
            m->target_x = c->spr.x, m->target_y = c->spr.y;
            break;
        }
        s->state = 0x6c;
        break;
    }
    case 0x6c: {
        Ped *v = ped_get(s->victims[(int8_t)s->u21]);
        int st = v->state;
        if (st == 0x17 || st == 0xc) {
            v->anim = 0;
            if (st == 0xc) {
                v->state = 0x17;
                coll_remove(v, v->spr.unk20);
            }
        }
        if (s->u60 > -1) {
            ped_remove(s->u60);
            s->u60 = -1;
        }
        s->u20 = 0;
        s->sub = 2;
        s->state = 0x6d;
        break;
    }
    case 0x6d:
        if (car_close_rear_door_step(s->car_id)) s->state = s->sub;
        break;
    case 200:
        if (g_path_owner < 0) {
            s->state = 1;
            s->sub = 1;
            sentinel_plan_route_to_target(s);
        }
        break;
    case 0xfe:
        if (s->u60 > -1 && !ped_is_near_screen(ped_get(s->u60))) {
            if (s->u60 > -1) {
                ped_remove(s->u60);
                s->u60 = -1;
            }
            unlist(s);
            if (g_path_owner == s->id) g_path_owner = -1;
            sentinel_reset(sentinel_ai(s));
            break;
        }
        if (c->driver == -1) s->state = 0x66;
        break;
    case 0xff:
        ret = 0;
        break;
    }
    if (s->u60 > -1) {
        Ped *m = ped_get(s->u60);
        if (m->health == 0) {
            s->u60 = -1;
            m->control = 1;
            if (s->state == 100 || s->state == 0x65) victim->control = 1;
            s->u22 = 10, s->u21 = 10;
            s->state = 3;
        }
    }
    return ret;
}
