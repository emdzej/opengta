/* Fires (0x42e600-0x42f460, the fire side; see fire.h). */
#include "fire.h"
#include "car.h"
#include "game.h"
#include "obj.h"
#include "stubs.h"
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
    g_fire.u511978[0] = g_fire.u511978[1] = g_fire.u511978[2] = 0;
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
