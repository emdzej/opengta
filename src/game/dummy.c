/* Mission dummy cars 0x473440-0x473b8f (dummy.h): the convoy groups and the kind-9 controller states.
   The controller records (ai.h, Sentinel_Get 0x41ad60) are reached through sentinel_get and handled
   as raw bytes at the original's offsets. */
#include "dummy.h"
#include "car.h"
#include "game.h"
#include "player.h"
#include "stubs.h"
#include <string.h>

DummyGroup g_dummy_groups[DUMMY_GROUPS];

static int16_t rd16(const uint8_t *r, int o) { int16_t v; memcpy(&v, r + o, 2); return v; }
static void wr16(uint8_t *r, int o, int16_t v) { memcpy(r + o, &v, 2); }
static void wr32(uint8_t *r, int o, int32_t v) { memcpy(r + o, &v, 4); }
/* the controller's car (+0x40, a Car pointer in the original; the port keeps the car id in +0x1e
   and resolves it) */
static Car *rec_car(const uint8_t *r) { return car_get(rd16(r, 0x1e)); }
static void car_wr16(Car *c, int o, int16_t v) { memcpy((uint8_t *)c + o, &v, 2); }

void dummy_init_groups(void)                /* Dummy_InitGroups 0x473440: the eight counts cleared */
{
    for (int i = 0; i < DUMMY_GROUPS; i++) g_dummy_groups[i].count = 0;
}

/* the car fields a dummy controller takes over: control 10, owner status 1, +0x119 = 1, the
   controller id (+0xd8), +0xb6 / +0xc0 cleared */
static void take_car(Car *c, int ctrl)
{
    c->control = 10;
    c->owner_status = 1;
    ((uint8_t *)c)[0x119] = 1;
    c->sentinel = (int16_t)ctrl;
    car_wr16(c, 0xb6, 0);
    c->unkc0 = 0;
}

/* Dummy_StartDrive 0x473460: a free controller (kind 9, state 0xfa) drives the car to the block of
   pixel (x, y, z) at the head of the first empty group (none: fatal -0x9c); +0x116 = 2. */
int dummy_start_drive(int car, int x, int y, int z)
{
    int ctrl = (int16_t)sentinel_find_free();
    if (ctrl == -1) return ctrl;
    uint8_t *r = sentinel_get(ctrl);
    Car *c = car_get((int16_t)car);
    take_car(c, ctrl);
    car_wr16(c, 0x116, 2);
    wr16(r, 0x44, rd16(r, 0));
    r[0x64] = (uint8_t)(x >> 6);
    wr16(r, 0x1e, (int16_t)car);
    r[0x21] = 0;
    r[0x22] = 0;
    r[0x1b] = DUMMY_S_START;
    wr32(r, 0x38, 0);
    r[2] = 9;
    r[0x65] = (uint8_t)(y >> 6);
    r[0x66] = (uint8_t)(z >> 6);
    int g = 0;
    while (g < DUMMY_GROUPS && g_dummy_groups[g].count != 0) g++;
    if (g == DUMMY_GROUPS) game_fatal(-0x9c, 0xbe, 0);
    wr16(r, 0x0a, (int16_t)g);
    g_dummy_groups[g].ctrl[g_dummy_groups[g].count] = (int16_t)ctrl;   /* (no bound on the group size) */
    g_dummy_groups[g].count++;
    return ctrl;
}

/* Dummy_AddFollower 0x473570: the car gets a controller in the group of `leader`'s, with the leader's
   top speed (+0x28) and destination; the follower's +0x116 isn't set. */
int dummy_add_follower(int car, int leader)
{
    int ctrl = (int16_t)sentinel_find_free();
    if (ctrl == -1) return ctrl;
    uint8_t *r = sentinel_get(ctrl);
    Car *c = car_get((int16_t)car);
    take_car(c, ctrl);
    Car *lc = car_get((int16_t)leader);
    const uint8_t *lr = sentinel_get(lc->sentinel);
    c->max_speed = lc->max_speed;
    r[0x21] = 0;
    r[0x22] = 0;
    int16_t g = rd16(lr, 0x0a);
    wr16(r, 0x0a, g);
    r[0x1b] = DUMMY_S_START;
    wr32(r, 0x38, 0);
    wr16(r, 0x44, rd16(lr, 0));
    wr16(r, 0x1e, (int16_t)car);
    r[2] = 9;
    r[0x64] = lr[0x64];
    r[0x65] = lr[0x65];
    r[0x66] = lr[0x66];
    if (g < 0 || g >= DUMMY_GROUPS || g_dummy_groups[g].count >= DUMMY_GROUP_CARS) game_fatal(-0x9c, 0xbe, g);   /* port */
    g_dummy_groups[g].ctrl[g_dummy_groups[g].count] = (int16_t)ctrl;
    g_dummy_groups[g].count++;
    return ctrl;
}

/* the car fields a released car gets back: speed 0, +0xc0 = 1, input 0, +0xb6 = 1, control 0, owner
   status 1 */
static void release_car(Car *c, int16_t ctrl_id)
{
    c->speed = 0;
    c->unkc0 = 1;
    c->input = 0;
    car_wr16(c, 0xb6, 1);
    c->control = 0;
    c->owner_status = 1;
    c->sentinel = ctrl_id;
}

/* the start of a path search from the car's block (or the nearest road, Map_FindNearestRoad) to the
   destination (+0x64..+0x66), mode 5 */
/* (case 5 passes the original's uninitialised query bytes; the port starts from the car's block) */
static int16_t request_path(uint8_t *r, const Car *c, bool always_road)
{
    uint8_t q[8] = { 0 };
    int bx = c->spr.x >> 22, by = c->spr.y >> 22, bz = c->spr.z >> 22;
    q[2] = (uint8_t)bx, q[3] = (uint8_t)by, q[4] = (uint8_t)bz;
    bool road = always_road || (g_game.map && (g_game.map->type_cache[(bz < MAP_Z ? bz : MAP_Z - 1)][by & 0xff][bx & 0xff] & 0xf) == 0);
    if (road && map_find_nearest_road(q)) bx = q[2], by = q[3], bz = q[4];
    return (int16_t)path_find(bx, by, bz, r[0x64], r[0x65], r[0x66], 5, r[0]);
}

/* Dummy_Update 0x473660: one step of a dummy controller, by state (+0x1b):
   - 0xfa start: when the path search is idle (0x4b3094 = -1), a search from the car's block (or the
     nearest road if the block's cached type has no road bits) to the destination; result 1: arrived
     (state 1, car +0x88 = 1), 3: wait for the search (sub state and state 2), others: car control 0;
   - 2 waiting: once the search is idle, its result 1 arrives (state 1, car stopped, +0x88 = 1) and 0
     fails (state 0x27); while busy a moving car (speed > 0) is braked;
   - 5 re-path: like the start (from the nearest road) when idle, else stays;
   - 1 arrived (after Sentinel_OverrideLights): unless +0xc is set, every car of the group is released
     (the controller freed; the group emptied). The group loop writes the leader's car each time and
     the followers' controller ids -1 (as in the original);
   - 100..103 attack player n: every car of the group hunts the player's controlled object (mode 1
     for cars with +0x116 = 1, 4 for 2). */
int dummy_update(uint8_t *r)
{
    Car *c = rec_car(r);
    switch (r[0x1b]) {
    case DUMMY_S_ARRIVED: {
        sentinel_override_lights(r, c);
        if (r[0xc]) return 1;
        int g = rd16(r, 0x0a);
        DummyGroup *grp = &g_dummy_groups[g];
        if (grp->count == 0) {
            release_car(c, 0);
            r[2] = 0;
            grp->count = 0;
            return 0;
        }
        for (int i = 0; i < grp->count; i++) {
            uint8_t *m = sentinel_get(grp->ctrl[i]);
            release_car(rec_car(r), -1);
            m[2] = 0;
        }
        grp->count = 0;
        return 0;
    }
    case DUMMY_S_PATH_WAIT:
        if (g_path_owner == -1) {
            if (g_path_result == 1) {
                r[0x1b] = DUMMY_S_ARRIVED;
                c->unkc0 = 0;
                car_wr16(c, 0xb6, 0);
                c->unk88 = 1;
                return 1;
            }
            if (g_path_result == 0) r[0x1b] = DUMMY_S_FAILED;
            return 1;
        }
        if (c->speed > 0) {
            c->input = 0;
            c->unkc0 = 1;
            car_wr16(c, 0xb6, 1);
            return 0;
        }
        return 0;
    case DUMMY_S_REPATH:
        if (g_path_owner != -1) {
            r[0x1b] = DUMMY_S_REPATH;
            return 0;
        }
        g_path_result = request_path(r, c, true);
        r[0x1c] = 2;
        r[0x1b] = DUMMY_S_PATH_WAIT;
        return 0;
    case 100: case 101: case 102: case 103: {
        int target = (int16_t)player_get_controlled_id(r[0x1b] - 100);
        DummyGroup *grp = &g_dummy_groups[rd16(r, 0x0a)];
        for (int i = 0; i < grp->count; i++) {
            Car *m = car_get(grp->ctrl[i]);   /* the group holds controller ids, read as car ids (original) */
            sentinel_get(m->sentinel)[2] = 0;
            int16_t mode;
            memcpy(&mode, (uint8_t *)m + 0x116, 2);
            if (mode == 1) hunt_add_car_target(m->id, target, 1);
            else if (mode == 2) hunt_add_car_target(m->id, target, 4);
        }
        return 0;
    }
    case DUMMY_S_START:
        if (g_path_owner == -1) {
            int16_t res = request_path(r, c, false);
            switch (res) {
            case 1:
                r[0x1b] = DUMMY_S_ARRIVED;
                c->unk88 = 1;
                break;
            case 3:
                r[0x1c] = 2;
                r[0x1b] = DUMMY_S_PATH_WAIT;
                break;
            default:
                if ((unsigned)res < 4) c->control = 0;
            }
        }
        return 0;
    }
    return 0;
}
