/* The mission helpers 0x475700-0x479020 (see mission_obj.h): mission cars, block clearing, the PED_ON
   presets, ped AI changes, briefs, alarm sound slots, the car list and thin wrappers. Calls into the
   modules not ported yet go through stubs.h. */
#include "mission_obj.h"
#include "../audio/audio.h"
#include "../text.h"
#include "coll.h"
#include "game.h"
#include "gmath.h"
#include "mission_run.h"
#include "obj.h"
#include "player.h"
#include "stubs.h"
#include "trigger.h"
#include <string.h>

CarListEntry g_car_list[CARLIST_MAX];
int g_car_list_count;
int32_t g_alarm_slots[ALARM_SLOTS] = { -1, -1, -1, -1 };
int16_t g_mission_var505efa;
int32_t g_gang_lists_a[10], g_gang_lists_b[10];
int32_t g_gang_count_a, g_gang_count_b;

/* Car fields the struct doesn't name yet (all below the sprite at +0x250, so exact on every build). */
static int32_t car_rd32(const Car *c, int off) { int32_t v; memcpy(&v, (const uint8_t *)c + off, 4); return v; }
static void car_wr32(Car *c, int off, int32_t v) { memcpy((uint8_t *)c + off, &v, 4); }
static void car_wr16(Car *c, int off, int16_t v) { memcpy((uint8_t *)c + off, &v, 2); }
static int16_t car_rd16(const Car *c, int off) { int16_t v; memcpy(&v, (const uint8_t *)c + off, 2); return v; }

/* Car_Get of a handle. A failed creation (-1) or a bad line would index outside the table in the
   original; the port gives a scratch record of -1s instead (writes to it are lost). */
static Car *car_ref(int h)
{
    static Car none;
    if (h < 0 || h >= CAR_MAX) {
        memset(&none, 0xff, sizeof none);
        return &none;
    }
    return car_get(h);
}
static Ped *ped_ref(int h)
{
    static Ped none;
    if (h < 0 || h >= PED_MAX) {
        memset(&none, 0xff, sizeof none);
        return &none;
    }
    return ped_get(h);
}
static Obj *obj_ref(int h)
{
    static Obj none;
    if (h < 0 || h >= OBJ_MAX) {
        memset(&none, 0xff, sizeof none);
        return &none;
    }
    return obj_get(h);
}

static int16_t px(int32_t v) { return (int16_t)(v >> 16); }           /* the integer (pixel) part */

/* the models Car_RemoveIf* never remove: Car_IsEmergencyModel's list, tested inline */
bool car_is_emergency_model(int m)          /* Car_IsEmergencyModel 0x4769e0 */
{
    return m == 4 || m == 5 || m == 0x10 || m == 0xf || m == 0x20 || m == 0x2a;
}

/* the inline view test of the removal helpers: pixel (x, y) inside a player's view rectangle and
   more than 15 pixels inside the map */
static bool on_any_view(int x, int y)
{
    for (int n = player_first(); n > -1; n = player_next(n)) {
        const int32_t *r = player_get_view_rect(n);
        if (r[0] <= x && r[2] <= y && x <= r[1] && y <= r[3] && x > 0xf && y > 0xf && x < 0x3ff1 && y < 0x3ff1)
            return true;
    }
    return false;
}

/* the bounding ends a moved car gets back (+0x14 / +0x18 and +0xac / +0xb0: centre -/+ half the length
   along the heading) */
static void car_set_ends(Car *c, int32_t x, int32_t y)
{
    int a = c->spr.angle, h = c->length >> 1;
    c->front_x = x - math_sin(a) * h;
    c->front_y = y - math_cos(a) * h;
    c->rear_x = math_sin(a) * h + x;
    c->rear_y = math_cos(a) * h + y;
}

/* ---- 0x475700-0x475730: the explosion thunks ---- */

/* the edge offsets of a direction: 0 -x (heading 0x300), 1 +x (0x100), 2 -y (0x200), 3 +y (0) */
static void dir_offsets(int dir, int line, int *dx, int *dy, int *angle)
{
    *dx = *dy = 0, *angle = 0;
    switch (dir) {
    case 0: *dx = -1, *angle = 0x300; break;
    case 1: *dx = 1, *angle = 0x100; break;
    case 2: *dy = -1, *angle = 0x200; break;
    case 3: *dy = 1, *angle = 0; break;
    default: game_fatal(-0x11b, line, dir);
    }
}

/* thunk_MisObj_Create425520 0x475700 (0x425520): an explosion at the edge of block (bx, by, bz) in
   direction dir, two type 5 objects past it, the debris objects 0x2c, 0x2d, 0x2b with smoke (0x2e)
   on the last, the blast, and two fires (0x12) further out. Obj_RemoveAtBlock gets block coordinates
   where it expects pixels (as in the original: it clears near the map corner). */
void mis_obj_create425520(int bx, int by, int bz, int dir, int owner)
{
    int dx, dy, angle;
    dir_offsets(dir, 0x1ba, &dx, &dy, &angle);
    int32_t z = bz * 0x400000;
    int32_t y = (dy + 1 + by * 2) * 0x200000, x = (dx + 1 + bx * 2) * 0x200000;
    expl_create(x, y, z - 0x10000, owner);
    obj_create(dx * 0x200000 + x, dy * 0x200000 + y, z + 0xf0000, 5, 0x200);
    obj_create(dx * 0x200000 + x, dy * 0x200000 + y, z + 0xf0000, 5, 0x200);
    int32_t z2 = z + 0x1f0000, ox = dx * 0x40000 + x, oy = dy * 0x40000 + y;
    obj_create(ox, oy, z2, 0x2c, 0);
    obj_create(ox, oy, z2, 0x2d, 0);
    int last = obj_create(ox, oy, z2, 0x2b, 0);
    obj_create_animated(x + 0x40000, y + 0x40000, z2, 0x2e, last, 0);
    expl_damage_area(x, y, 0x400000, owner);
    if ((int16_t)last >= 0) obj_create_animated(dx * 0x100000 + x, dy * 0x80000 + y, 0x3f0000, 0x2e, last, 0);
    int32_t fz = z - 0x10001;
    int32_t fy = dy * 0xa0000 + y;
    obj_remove_at_block(ox >> 22, fy >> 22, fz >> 22, -1);
    obj_create_animated(ox, fy, fz, 0x12, -1, angle);
    y += dy * 0x60000;
    x += dx * 0x80000;
    obj_remove_at_block(x >> 22, y >> 22, fz >> 22, -1);
    obj_create_animated(x, y, fz, 0x12, -1, angle);
}

/* thunk_MisObj_Create4258d0 0x475710 (0x4258d0): only the explosion at the edge */
void mis_obj_create4258d0(int bx, int by, int bz, int dir, int owner)
{
    int dx, dy, angle;
    dir_offsets(dir, 0x1bc, &dx, &dy, &angle);
    expl_create((dx + 1 + bx * 2) * 0x200000, (dy + 1 + by * 2) * 0x200000, bz * 0x400000 - 0x10000, owner);
}

/* thunk_MisObj_Create425780 0x475720 (0x425780): the explosion and the two fires of 425520 */
void mis_obj_create425780(int bx, int by, int bz, int dir, int owner)
{
    int dx, dy, angle;
    dir_offsets(dir, 0x1bb, &dx, &dy, &angle);
    int32_t y = (dy + 1 + by * 2) * 0x200000, x = (dx + 1 + bx * 2) * 0x200000;
    expl_create(x, y, bz * 0x400000 - 0x10000, owner);
    int32_t fz = bz * 0x400000 - 0x10001, fy = dy * 0xa0000 + y, fx = dx * 0x40000 + x;
    obj_remove_at_block(fx >> 22, fy >> 22, fz >> 22, -1);
    obj_create_animated(fx, fy, fz, 0x12, -1, angle);
    y += dy * 0x60000;
    x = dx * 0x80000 + x;
    obj_remove_at_block(x >> 22, y >> 22, fz >> 22, -1);
    obj_create_animated(x, y, fz, 0x12, -1, angle);
}

/* Car_SnapHeading_00475730: the front heading (+0x90) snaps to the axis within twice the turn rate
   (+0x96); then, if the snapped heading's low byte is 0 (so for all four axes, not only 0) and the
   car is within one turn step of it, the turn stops. */
void car_snap_heading(Car *c)
{
    int r = c->turn_delta < 0 ? -c->turn_delta : c->turn_delta;
    int h = c->front_heading;
    if (h >= 0x200 - 2 * r && h <= 0x200 + 2 * r) c->front_heading = 0x200;
    else if (h >= 0x100 - 2 * r && h <= 0x100 + 2 * r) c->front_heading = 0x100;
    else if (h >= 0x300 - 2 * r && h <= 0x300 + 2 * r) c->front_heading = 0x300;
    else if (h >= 0x400 - 2 * r || h <= 2 * r) c->front_heading = 0;
    if ((c->front_heading & 0xff) == 0) {
        int d = c->spr.angle - c->front_heading;
        if ((d < 0 ? -d : d) <= r) c->turn_delta = 0, c->turn_progress = 0;
    }
}

/* ---- mission cars ---- */

void mis_car_set_held(int car) { car_wr32(car_ref(car), 0x248, 1); }     /* MisCar_SetHeld 0x475800 */
void mis_car_clear_held(int car) { car_wr32(car_ref(car), 0x248, 0); }   /* MisCar_ClearHeld 0x475820 */

/* MisCar_Create 0x475840: a car on the ground of block (bx, by, bz) (its centre, z - 2 pixels); no free
   slot is fatal (-0x4e). */
int mis_car_create(int bx, int by, int bz, int model, int angle, int remap)
{
    int c = (int16_t)car_spawn_ex_on_ground(bx * 0x400000 + 0x200000, by * 0x400000 + 0x200000, bz * 0x400000 - 0x20000,
                                            (int16_t)model, 0, angle, remap);
    if (c == -1) game_fatal(-0x4e, 0x53, (int16_t)model);
    car_get(c)->unk139 = 0;
    return c;
}

/* MisCar_CreateAt 0x4758c0: the same at 16.16 (x, y) and block z */
int mis_car_create_at(int32_t x, int32_t y, int bz, int model, int angle, int remap)
{
    int c = (int16_t)car_spawn_ex_on_ground(x, y, bz * 0x400000 - 0x20000, (int16_t)model, 0, angle, remap);
    if (c == -1) game_fatal(-0x4e, 0x53, (int16_t)model);
    car_get(c)->unk139 = 0;
    return c;
}

/* MisCar_CreateType1 0x475930: through Car_SpawnEx with a driver (flag 1), z not snapped */
int mis_car_create_type1(int bx, int by, int bz, int model, int angle, int remap)
{
    int c = (int16_t)car_spawn_ex(bx * 0x400000 + 0x200000, by * 0x400000 + 0x200000, bz * 0x400000 - 0x20000,
                                  (int16_t)model, 1, angle, remap);
    if (c == -1) game_fatal(-0x4e, 0x53, (int16_t)model);
    car_get(c)->unk139 = 0;
    return c;
}

/* MisCar_CreateDriver 0x4759b0: the car's driver slot ped at pixel (x, y, z), sent to the car's door. */
int mis_car_create_driver(int x, int y, int z, int car)
{
    Car *c = car_ref(car);
    int id = (int16_t)(c->id + PED_DRIVER_FIRST);
    if (id < 0 || id >= PED_MAX) return id;   /* a failed car: the original writes outside the table */
    Ped *p = ped_get(id);
    p->graphic = 0;
    ped_spawn_in_slot(x << 16, y << 16, z << 16, 0, 0, 1, id, c->id);
    ped_send_to_car_door1(p, car);
    p->u8b = 1;
    p->graphic = 0;
    return id;
}

/* MisCar_CreatePlayerPed 0x475a20: player n's ped in the driver slot of `car` (car + 200), standing on
   block (bx, by, bz), bound to the player, with the player's colour; objective 0x25, control 8 + n. */
int mis_car_create_player_ped(int n, int bx, int by, int bz, int car, int angle, int remap)
{
    int ped = car_ref(car)->id + PED_DRIVER_FIRST;
    ped_spawn_in_slot(bx * 0x400000 + 0x200000, by * 0x400000 + 0x200000, bz * 0x400000 - 0x10000, 0, angle, 1,
                      ped, car_ref(car)->id);
    ped_set_player_controlled((int16_t)ped);
    player_set_controlled(n, PLAYER_ON_FOOT, (int16_t)ped);
    player_set_ped(n, ped);
    ped_set_appearance(ped, 0, remap);
    Ped *p = ped_ref(ped);
    p->objective = 0x25;
    if (n >= 0 && n <= 3) p->control = (int16_t)(8 + n);
    return ped & 0xffff;
}

void mis_ped_set_remap(int ped, int remap) { ped_set_appearance(ped, 0, remap); }   /* MisPed_SetRemap 0x475b00 */

/* MisCar_MakeKiller 0x475b20: the car's driver slot ped sits in it (state 7), the car turns a traffic
   dummy (control 0); bikes and convertibles get a visible driver (Ped_CreateCarDriver). */
void mis_car_make_killer(int car)
{
    if (car < 0 || car >= CAR_MAX) return;   /* (the creators stop on -1 before this) */
    Car *c = car_get(car);
    Ped *p = ped_get((int16_t)(c->id + PED_DRIVER_FIRST));
    car_set_dummy_control(c->id);
    c->owner_status = 1;
    c->driver = (int16_t)(c->id + PED_DRIVER_FIRST);
    c->unk88 = 1;
    p->health = 100;
    p->graphic = 0;
    p->control = 0;
    p->weapon = 0;
    p->objective = 0x19;
    p->state = 7;
    p->u78 = 8;
    p->u7c = 2;
    c->unkec = 1;
    if (c->vtype != 3 && !car_info_is_convertible(car)) return;
    c->status = 0;
    ped_create_car_driver(c);
}

void mis_car_set_flag128_99(int car) { car_ref(car)->owner_status = 99; }   /* MisCar_SetFlag128_99 0x475bd0 */
void mis_car_set_flag128_1(int car) { car_ref(car)->owner_status = 1; }     /* MisCar_SetFlag128_1 0x475bf0 */

void mis_car_park(int car)                  /* MisCar_Park 0x475c10 */
{
    Car *c = car_ref(car);
    c->control = 0;
    c->owner_status = 1;
    c->unk88 = 1;
}

/* MisCar_PutPlayerIn 0x475c40: player n drives `car`: the car turns player-controlled (control 1), its
   driver slot ped is the player's, placed at the driver's door (door_dx, door_dy + 6 rotated by the
   car's heading), in the grid if the car is a convertible. */
void mis_car_put_player_in(int car, int n)
{
    Car *c = car_ref(car);
    c->control = 1;
    c->unk88 = 1;
    c->owner_status = 1;
    c->driver = (int16_t)(c->id + PED_DRIVER_FIRST);
    ped_set_player_controlled(c->driver);
    n = (int8_t)n;
    player_set_controlled(n, PLAYER_ON_FOOT, c->driver);
    player_set_ped(n, c->driver);
    Ped *p = ped_ref(c->driver);
    if (n >= 0 && n <= 3) p->control = (int16_t)(8 + n);
    int a = c->spr.angle & 0x3ff, b = (c->spr.angle + 0x100) & 0x3ff;
    p->spr.y = math_cos(a) * c->door_dx + math_cos(b) * (c->door_dy + 6) + c->spr.y;
    p->u56 = 0;
    p->spr.x = math_sin(a) * c->door_dx + math_sin(b) * (c->door_dy + 6) + c->spr.x;
    p->u54 = 6;
    if (car_info_is_convertible(car)) {
        p->attach_kind = 1;
        p->attach_id = (int16_t)car;
        p->anim = 0x80;
        coll_insert(COLL_PED, p->id, p, p->spr.unk20, p->spr.x, p->spr.y);
    }
    player_enter_car(p->id, p->car);
    if (!c->physics) carphys_begin(c->id);
}

/* MisCar_GivePlayer 0x475dd0: player n's ped drives the car (physics control) */
void mis_car_give_player(int car, int n)
{
    Car *c = car_ref(car);
    c->control = 1;
    c->driver = player_get((int8_t)n)->ped;   /* Player_GetPed 0x461ec0 */
    c->unk88 = 1;
    c->owner_status = 1;
    if (!c->physics) carphys_begin(c->id);
}

/* Car_IsAtBlock 0x475e20: the car's camera target position (Car_GetCamTarget: z less the ground
   offset) in block (bx, by, bz) */
bool car_is_at_block(int car, int bx, int by, int bz)
{
    const Car *c = car_ref(car);
    return c->spr.x >> 22 == bx && c->spr.y >> 22 == by && (c->spr.z - c->z_offset) >> 22 == bz;
}

/* Ped_IsNearBlock 0x475e60: within r blocks in x and y, on layer bz */
bool ped_is_near_block(int ped, int bx, int by, int bz, int r)
{
    const Ped *p = ped_ref((int16_t)ped);   /* Ped_GetPosRect: the sprite position */
    int x = p->spr.x >> 22, y = p->spr.y >> 22;
    return bx - r <= x && by - r <= y && x <= bx + r && y <= by + r && p->spr.z >> 22 == bz;
}

bool car_is_wrecked(int car) { return car_ref(car)->damage > 99; }   /* Car_IsWrecked 0x475ed0 */
int car_get_damage(int car) { return car_ref(car)->damage; }        /* Car_GetDamage 0x475ef0 */

/* MisCar_SpawnBatch 0x475f10: n traffic cars (Traffic_PrimeCarPool), each bound to its driver slot.
   The loop runs from the old car count up to the new count plus the old count. */
void mis_car_spawn_batch(int n)
{
    int first = g_cars_count;
    traffic_prime_car_pool(n);
    int end = (int16_t)g_cars_count + (int16_t)first;
    for (int i = (int16_t)first; i < end && i < CAR_MAX; i++) {   /* (the original runs past the table) */
        car_get(i)->driver = (int16_t)(i + PED_DRIVER_FIRST);
        ped_get(i + PED_DRIVER_FIRST)->car = (int16_t)i;
    }
}

/* Police_InitForMission 0x475f60: with the emergency services and the police on, the criminal and
   pursuit records; the patrol cars only in single player without the no-patrols switch (0x503184
   tells the police which case it is). The multiplayer win condition is read and ignored. */
void police_init_for_mission(void)
{
    if (!g_game.opt.emergency || !g_game.opt.police) return;
    uint8_t kind, value;
    front_get_multi_target(&kind, &value);
    if (!g_game.opt.no_patrols && g_player_count < 2) {
        g_police_no_patrols = 0;
        police_spawn_patrol_cars();
    } else {
        g_police_no_patrols = 1;
    }
    police_init_criminals();
    police_init_pursuits();
}

int car_get_driver(int car) { return car_ref(car)->driver; }   /* Car_GetDriver 0x475fd0 */

int car_get_driver_info(int car)            /* Car_GetDriverInfo 0x475ff0 */
{
    if (car_ref(car)->driver == -1) return 0xff;
    return ped_driver_leave_car(car);
}

/* Car_HasLiveDriver 0x476020: `ped` drives the car, sits in it (state 7 / 6), is alive and not in a
   transition animation (anim 0 or above 0x7e) */
bool car_has_live_driver(int ped, int car)
{
    if (car < 0) return false;
    if (car_ref(car)->driver != ped) return false;
    const Ped *p = ped_ref(ped);
    return (p->state == 7 || p->state == 6) && p->health > 0 && (p->anim == 0 || p->anim > 0x7e);
}

/* Car_MoveAxis 0x476080: the crane moves the car's pending position on one axis by `step` pixels;
   true when that coordinate's pixel part equals target (tested after the step). */
bool car_move_axis(int car, int target, int step, int axis)
{
    bool done = false;
    Car *c = car_ref(car);
    coll_remove(c, c->spr.unk20);
    if (axis == 0) {
        c->next_x += (int16_t)step * 0x10000;
        done = px(c->next_x) == target;
    } else if (axis == 1) {
        c->next_y += (int16_t)step * 0x10000;
        done = px(c->next_y) == target;
    } else if (axis == 2) {
        c->next_z += (int16_t)step * 0x10000;
        done = px(c->next_z) == target;
    }
    car_commit_move(c);
    coll_insert(COLL_CAR, c->id, c, c->spr.unk20, c->spr.x, c->spr.y);
    car_set_ends(c, c->spr.x, c->spr.y);
    return done;
}

/* Car_MoveTowards 0x4761c0: one step of `step` pixels toward pixel (x, y, z) on each axis (x, y on the
   pending position, compared with the current one; z on the sprite itself, and down only: when the
   car is below z the pending z just takes the current one). No commit. True when all three match. */
bool car_move_towards(int car, int x, int y, int z, int step)
{
    int n = 0;
    Car *c = car_ref(car);
    int32_t d = (int16_t)step * 0x10000;
    coll_remove(c, c->spr.unk20);
    if (px(c->spr.x) < x) c->next_x += d;
    else if (x < px(c->spr.x)) c->next_x -= d;
    else n++;
    if (px(c->spr.y) < y) c->next_y += d;
    else if (y < px(c->spr.y)) c->next_y -= d;
    else n++;
    if (px(c->spr.z) < z) c->next_z = c->spr.z;
    else if (z < px(c->spr.z)) c->spr.z -= d, c->next_z = c->spr.z;
    else n++;
    coll_insert(COLL_CAR, c->id, c, c->spr.unk20, c->spr.x, c->spr.y);
    car_set_ends(c, c->spr.x, c->spr.y);
    return n == 3;
}

/* Ped_CheckInCar 0x476340: mode -1 always 1; 0: 1 if the ped's car (below 501: the original's bound,
   past the 400 cars) has it as driver; 1: 1 if its car is 1000; 2: 2 if it isn't animating or sits in
   a car. Else 0. */
int ped_check_in_car(int ped, int mode)
{
    if (mode == -1) return 1;
    const Ped *p = ped_ref(ped);
    if (mode == 0) {
        if (p->car >= 0 && p->car < 0x1f5 && car_ref(p->car)->driver == ped) return 1;   /* port: >= 400 reads -1s */
    } else if (mode == 1) {
        if (p->car == 1000) return 1;
    } else if (mode == 2) {
        if (p->anim == 0 || p->state == 7 || p->state == 6) return 2;
    }
    return 0;
}

void car_unk_004318e0_wrap(int car, int target, int mode) { hunt_add_car_target(car, target, mode); }   /* 0x4763e0 */

/* Car_GetModelValue 0x476400: car info u16 at +0x6e + 2 field, times 1000 */
int car_get_model_value(int car, int field)
{
    const uint8_t *info = car_info_of_model(car_ref(car)->model);
    if (!info) return 0;
    int o = 0x6e + (int16_t)field * 2;
    return (info[o] | info[o + 1] << 8) * 1000;
}

void mis_car_destroy(int car)               /* MisCar_Destroy 0x476440: wrecked; model 0x2f also stops */
{
    Car *c = car_ref(car);
    c->damage = 100;
    if (c->model == 0x2f) c->speed = 0;
}

static void drive_mode(int car, int mode)
{
    Car *c = car_ref(car);
    car_wr16(c, 0x116, (int16_t)mode);
    car_wr16(c, 0xce, -5);
    c->max_speed = 0x19;
}
void mis_car_set_drive_mode1(int car) { drive_mode(car, 1); }   /* MisCar_SetDriveMode1 0x476470 */
void mis_car_set_drive_mode2(int car) { drive_mode(car, 2); }   /* MisCar_SetDriveMode2 0x4764d0 */

/* MisCar_SetFlag9c 0x4764a0: the bomb state (+0x9c), timer 0x7d; a bomb repairs the car */
void mis_car_set_flag9c(int car, int v)
{
    Car *c = car_ref(car);
    c->bomb = v;
    c->bomb_timer = 0x7d;
    if (v != 0) c->damage = 0;
}

/* Car_GetCardinalDir 0x476500: heading -> road direction bit (2 up, 4 at 0x280.., 1 at 0x180.., else 8) */
int car_get_cardinal_dir(int car)
{
    int a = car_ref(car)->spr.angle;
    if (a > 0x37f || a < 0x80) return 2;
    if (a > 0x27f) return 4;
    if (a > 0x17f && a < 0x280) return 1;
    return 8;
}

void car_clear_field0ec(int car) { car_ref(car)->unkec = 0; }   /* Car_ClearField0EC 0x476550 */

/* Car_PlaceInGrid 0x476570: an off-screen car moves to the centre of the block idx % 3 to the right
   and (idx / 3) * stride down from its own, with a new pending box; false if on screen. */
bool car_place_in_grid(int car, int idx, int stride)
{
    Car *c = car_ref(car);
    if (car_is_on_screen(c)) return false;
    idx = (int16_t)idx, stride = (int16_t)stride;
    int32_t x = ((c->spr.x >> 22) + idx % 3) * 0x400000 + 0x200000;
    int32_t y = (stride * (idx / 3) + (c->spr.y >> 22)) * 0x400000 + 0x200000;
    coll_remove(c, c->spr.unk20);
    c->next_x = x;
    c->next_y = y;
    coll_build_box(x, y, c->next_z, c->half_w, c->half_l, (int16_t)c->next_heading, car_rd16(c, 0x30), &c->box_saved);
    car_set_ends(c, x, y);
    coll_insert(COLL_CAR, c->id, c, c->spr.unk20, x, y);
    car_commit_move(c);
    return true;
}

/* Mission_CheckPlayerCarOk 0x4766a0: false only for a player in a car whose ped sits in it (state 7 /
   6) and the car is model 0x2f; a dead ped whose own car is wrecked (and not 0x2f) is "ok" first. */
bool mission_check_player_car_ok(int n)
{
    if (player_get_controlled_kind(n) != PLAYER_IN_CAR) return true;
    const Car *c = car_ref(player_get_controlled_id(n));
    const Ped *p = ped_ref(player_get(n)->ped);
    if (p->health == 0 && p->car >= 0) {
        const Car *pc = car_ref(p->car);
        if (pc->damage > 99 && pc->model != 0x2f) return true;
    }
    if ((p->state == 7 || p->state == 6) && c->model == 0x2f) return false;
    return true;
}

/* the common end of the two Car_RemoveIfOffscreen: a bike's or convertible's visible driver goes too */
static void remove_car(Car *c)
{
    if ((c->vtype == 3 || car_info_is_convertible(c->id)) && c->driver >= 0) {
        ped_remove(c->driver);
        c->driver = -1;
    }
    car_delete(c->id);
}

/* Car_RemoveIfOffscreen 0x476720: deletes a reserved (+0x139 = 1), unscripted, undriven, unburning
   ordinary car no player sees */
bool car_remove_if_offscreen(int car)
{
    Car *c = car_ref(car);
    if (car_is_burning(c) || c->unk139 != 1 || c->script_line >= 0 || c->control >= 1 || car_is_emergency_model(c->model) ||
        car_is_marked_for_removal(c->id))
        return false;
    if (on_any_view(px(car_get(c->id)->spr.x), px(car_get(c->id)->spr.y))) return false;
    remove_car(c);
    return true;
}

/* Car_RemoveIfOffscreenEx 0x476880: the same without the burning and script tests, but not for a car
   with +0x244 = 1; a scripted car's object forgets it first. */
bool car_remove_if_offscreen_ex(int car)
{
    Car *c = car_ref(car);
    if (c->unk139 != 1 || c->control >= 1 || car_is_emergency_model(c->model) || car_is_marked_for_removal(c->id) ||
        car_rd32(c, 0x244) == 1)
        return false;
    if (on_any_view(px(car_get(c->id)->spr.x), px(car_get(c->id)->spr.y))) return false;
    if (c->script_line >= 0) {
        mission_forget_object_handle(c->script_line);
        c->script_line = -1;
    }
    remove_car(c);
    return true;
}

/* Car_RemoveAtBlock 0x476a10: the cars near pixel (x, y) (Coll_QueryCars, without `exclude`), only
   those whose pixel block is (x, y)'s with exact = 1, go through Car_RemoveIfOffscreenEx. Mode 1 does
   it only when no player's +0x1a4 is `exclude`, mode 0 always, other modes never. Returns whether
   every one went (true when none). */
bool car_remove_at_block(int x, int y, int exclude, int exact, int mode)
{
    bool all = true;
    for (CollHit *h = coll_query_cars(x << 16, y << 16, exclude); h; h = h->next) {
        const Car *c = h->owner;
        if (c->id == exclude) continue;
        if (mode == 1) {
            if (player_any_field1a4_is(exclude)) continue;
        } else if (mode != 0) continue;
        if ((int8_t)exact == 1 && !((px(c->spr.x) & ~0x3f) == (x & ~0x3f) && (px(c->spr.y) & ~0x3f) == (y & ~0x3f))) continue;
        bool r = car_remove_if_offscreen_ex(c->id);
        if (all) all = r;
    }
    coll_unlock();
    return all;
}

/* Car_RemoveInSquare 0x476af0: meant to clear the (2r+1)^2 blocks around (x, y), but every call is
   for (x, y) itself: (2r + 1) * 2r times (none for r = 0). z is unused. */
bool car_remove_in_square(int x, int y, int z, int r, int exclude, int mode)
{
    (void)z;
    bool all = true;
    int x0 = x - r * 0x40, x1 = r * 0x40 + x;
    if (x1 < x0) return true;
    int y0 = y - r * 0x40, y1 = r * 0x40 + y;
    for (int i = ((unsigned)(x1 - x0) >> 6) + 1; i; i--)
        if (y0 < y1)
            for (int j = ((unsigned)(y1 - y0 - 1) >> 6) + 1; j; j--)
                if (!car_remove_at_block(x, y, exclude, 0, mode) && all) all = false;
    return all;
}

/* the filter of Car_ClearForCar / Car_RemoveOffscreenAtBlock: undriven, not an emergency model, not
   marked for removal */
static bool ordinary(const Car *c)
{
    return c->control < 1 && !car_is_emergency_model(c->model) && !car_is_marked_for_removal(c->id);
}

/* Car_RemoveOffscreenAtBlock 0x476b90: deletes the unscripted ordinary cars touching block (x, y)
   (pixels) at layer z - 1 that no player sees. Returns 0. */
bool car_remove_offscreen_at_block(int x, int y, int z, int exclude)
{
    for (CollHit *h = coll_query_block(x << 16, y << 16, z * 0x10000 - 0x10000, COLL_CAR, exclude); h; h = h->next) {
        const Car *c = h->owner;
        if (c->script_line >= 0 || !ordinary(c)) continue;
        if (on_any_view(px(car_get(c->id)->spr.x), px(car_get(c->id)->spr.y))) continue;
        car_delete(c->id);
    }
    coll_unlock();
    return false;
}

static void delete_ordinary_at(int32_t x, int32_t y, int32_t z, int exclude)
{
    for (CollHit *h = coll_query_block(x, y, z, COLL_CAR, exclude); h; h = h->next) {
        const Car *c = h->owner;
        if (ordinary(c) && car_rd32(c, 0x244) != 1) car_delete(c->id);
    }
    coll_unlock();
}

/* Car_ClearForCar 0x476cd0: makes room for `car` (or a new car of `model` heading `angle`) at pixel
   (x, y, z): cars overlapping the car's pending box go off screen-checked, ordinary cars on the block
   are deleted, and for a model longer than 0x40 pixels also those on the blocks before and behind
   along its axis (0 / 0x200: y, 0x100 / 0x300: x; other headings test the block itself twice).
   Always returns 0 in the low byte (what Mission_ClearBlock passes on). */
int car_clear_for_car(int x, int y, int z, int car, int model, int angle)
{
    if (car != -1) {
        Car *c = car_ref(car);
        for (CollHit *h = coll_query_car_box(c, c->spr.x, c->spr.y, COLL_CAR, car); h; h = h->next)
            if (((const Car *)h->owner)->id != car) car_remove_if_offscreen_ex(((const Car *)h->owner)->id);
        coll_unlock();
    }
    int32_t zz = z * 0x10000 - 0x10000;
    delete_ordinary_at(x << 16, y << 16, zz, car);
    if (model < 0) return 0;
    const uint8_t *info = car_info_of_model(model);
    if (!info || (int16_t)(info[2] | info[3] << 8) < 0x41) return 0;
    angle = (int16_t)angle;
    int dx = 0, dy = 0;
    if (angle == 0x200 || angle == 0) dy = -0x40;
    else if (angle == 0x100 || angle == 0x300) dx = -0x40;
    delete_ordinary_at((dx + x) * 0x10000, (dy + y) * 0x10000, zz, car);
    delete_ordinary_at((x - dx) * 0x10000, (y - dy) * 0x10000, zz, car);
    return 0;
}

/* Ped_RemoveDummiesAtBlock 0x476f40: removes the wandering peds (objective 0x19) whose pixel block is
   (x, y, z)'s */
void ped_remove_dummies_at_block(int x, int y, int z, int exclude)
{
    for (CollHit *h = coll_query_block(x << 16, y << 16, z * 0x10000 - 1, COLL_PED, exclude); h; h = h->next) {
        const Ped *p = h->owner;
        if (p->objective == 0x19 && (px(p->spr.x) & ~0x3f) == (x & ~0x3f) && (px(p->spr.y) & ~0x3f) == (y & ~0x3f) &&
            (px(p->spr.z) & ~0x3f) == (z & ~0x3f))
            ped_remove(p->id);
    }
    coll_unlock();
}

/* the object part of Obj_RemoveAtBlock / Mission_ClearBlock / Mission_ClearRow: objects whose block
   is pixel (x, y, z)'s */
static void remove_objects_at(int x, int y, int z, int exclude)
{
    for (CollHit *h = coll_query_block(x << 16, y << 16, z * 0x10000 - 1, COLL_OBJECT, exclude); h; h = h->next) {
        const Obj *o = h->owner;
        if (o->spr.x >> 22 == x >> 6 && (px(o->spr.y) & ~0x3f) == (y & ~0x3f) && (px(o->spr.z) & ~0x3f) == (z & ~0x3f))
            obj_delete(o->id);
    }
    coll_unlock();
}

void obj_remove_at_block(int x, int y, int z, int exclude) { remove_objects_at(x, y, z, exclude); }   /* 0x476fe0 */

/* World_AnyThingAt 0x477070: a fire in the block of (x, y) */
bool world_any_thing_at(int32_t x, int32_t y)
{
    bool r = coll_get_fires_in_block(x, y) != NULL;
    coll_unlock();
    return r;
}

/* Mission_ClearBlock 0x4770a0: deletes the objects in the block of the pixel position (x, y, z), then
   the dummy peds and the cars there. Mission_Load passes block coordinates for most types, so for those
   it clears near the map's corner (pixel (x, y), block 0 or 1): nothing at level start. */
int mission_clear_block(int x, int y, int z, int a, int b, int c)
{
    remove_objects_at(x, y, z, a);
    ped_remove_dummies_at_block(x, y, z, a);
    return car_clear_for_car(x, y, z, a, b, c);
}

/* Mission_ClearRow 0x477160: objects and dummy peds of the block, then the off-screen cars of n + 1
   blocks from it stepping (dx, dy) blocks (n < 0: none). The test exclude == 0x7e changes nothing. */
bool mission_clear_row(int x, int y, int z, int exclude, int n, int dx, int dy)
{
    remove_objects_at(x, y, z, exclude);
    ped_remove_dummies_at_block(x, y, z, exclude);
    if (n >= 0)
        for (int i = n + 1; i; i--) {
            car_remove_offscreen_at_block(x, y, z, exclude);
            y += dy * 0x40;
            x += dx * 0x40;
        }
    return true;
}

/* Mission_ExplodePed 0x477260: an explosion at a ped on foot; a ped in a car wrecks the car instead */
void mission_explode_ped(int ped)
{
    const Ped *p = ped_ref(ped);
    if (p->state != 7 && p->state != 6) {
        expl_create(p->spr.x, p->spr.y, p->spr.z, -1);
        return;
    }
    Car *c = car_ref(p->car);
    c->damage = 100;
    if (c->model == 0x2f) c->speed = 0;
}

bool ped_is_dead(int ped) { return ped_ref(ped)->health == 0; }   /* Ped_IsDead 0x4772c0 */

int ped_take_killer(int ped)                /* Ped_TakeKiller 0x4772e0 */
{
    Ped *p = ped_ref(ped);
    int k = -2;
    if (p->health == 0) {
        if (p->u5a > -2) k = p->u5a;
        p->u5a = -2;
    }
    return k;
}

/* Ped_KillIfPossible 0x477320: removes the ped unless dead (0xc / 0x17) or sitting in a car it can't be
   seen in (not a bike or convertible); a ped in state 3 is left in state 2. */
bool ped_kill_if_possible(int ped)
{
    Ped *p = ped_ref(ped);
    if (p->state == 0xc || p->state == 0x17) return false;
    if ((p->state == 7 || p->state == 6) && car_ref(p->car)->vtype != 3 && !car_info_is_convertible(p->car)) return false;
    ped_remove(ped);
    ambulance_cancel_for_ped(ped);
    if (p->state == 3) p->state = 2;
    return true;
}

/* Car_RemoveIfUnused 0x4773a0: deletes a used slot that isn't burning and not +0x244 = 1. The
   ambulance request it clears first is car + 0x26c (as in the original). */
bool car_remove_if_unused(int car)
{
    Car *c = car_ref(car);
    ambulance_clear_request(car + 0x26c);
    if (c->status >= 0 && c->burning < 1 && car_rd32(c, 0x244) != 1) {
        car_delete(car);
        return true;
    }
    return false;
}

/* ---- PED_ON presets (0x4773f0-0x477b50) ---- */

/* the shared start: Ped_Create at pixel (x, y, z - 2) with (a, b) = (0, 0x88), (0, 0x62) or (1, 1) */
static Ped *preset(int *id, int x, int y, int z, int angle, int a, int b)
{
    *id = ped_create(x << 16, y << 16, (z - 2) * 0x10000, a, angle, b, 0);
    return ped_ref(*id);
}
static void no_walk(Ped *p) { p->target_x = p->target_y = 0, p->walk_x = p->walk_y = 0; }

int mission_ped_create_0(int x, int y, int z, int angle)   /* 0x4773f0: wanders (state 3, action 8) */
{
    int id;
    Ped *p = preset(&id, x, y, z, angle, 0, 0x88);
    p->graphic = 0, p->state = 3, p->u7c = 8, p->u84 = 100, p->u8b = 1;
    return id & 0xffff;
}

int mission_ped_create_1(int x, int y, int z, int angle, int target)   /* 0x477460: armed, objective 0x18 */
{
    int id;
    Ped *p = preset(&id, x, y, z, angle, 0, 0x62);
    p->u8b = 1, p->weapon = 1;
    no_walk(p);
    p->graphic = 0, p->state = 3, p->objective = 0x18, p->u7c = 8, p->target_ped = (int16_t)target;
    return id & 0xffff;
}

int mission_ped_create_8(int x, int y, int z, int angle, int target)   /* 0x4774e0: armed, state 4, objective 0x1c */
{
    int id;
    Ped *p = preset(&id, x, y, z, angle, 0, 0x62);
    p->u8b = 1, p->weapon = 1;
    no_walk(p);
    p->graphic = 0, p->state = 4, p->objective = 0x1c, p->u7c = 8, p->target_ped = (int16_t)target;
    return id & 0xffff;
}

int mission_ped_create_4(int x, int y, int z, int angle)   /* 0x477560: Ped_Create (1, 1), state 3, speed 1 */
{
    int id;
    Ped *p = preset(&id, x, y, z, angle, 1, 1);
    no_walk(p);
    p->graphic = 0, p->u8b = 1, p->state = 3, p->speed = 1;
    return id & 0xffff;
}

int mission_ped_create_6(int x, int y, int z, int angle)   /* 0x4775d0: (1, 1), state 2, objective 0x24, action 5 */
{
    int id;
    Ped *p = preset(&id, x, y, z, angle, 1, 1);
    p->graphic = 0, p->state = 2, p->objective = 0x24, p->u7c = 5, p->u8b = 1;
    return id & 0xffff;
}

int mission_ped_create_5(int x, int y, int z, int angle, int target)   /* 0x477630: state 4, action 2 */
{
    int id;
    Ped *p = preset(&id, x, y, z, angle, 0, 0x88);
    no_walk(p);
    p->graphic = 0, p->state = 4, p->u7c = 2, p->target_ped = (int16_t)target, p->u8b = 1;
    return id & 0xffff;
}

int mission_ped_create_7(int x, int y, int z, int angle, int target)   /* 0x4776b0: state 1, action 5 */
{
    int id;
    Ped *p = preset(&id, x, y, z, angle, 0, 0x88);
    no_walk(p);
    p->graphic = 0, p->state = 1, p->u7c = 5, p->target_ped = (int16_t)target, p->u8b = 1;
    return id & 0xffff;
}

int mission_ped_create_9(int x, int y, int z, int angle)   /* 0x477730: state 3, objective 0x22 */
{
    int id;
    Ped *p = preset(&id, x, y, z, angle, 0, 0x62);
    p->state = 3, p->objective = 0x22, p->u7c = 8, p->graphic = 0, p->u8b = 1;
    return id & 0xffff;
}

int mission_ped_create_10(int x, int y, int z, int angle)  /* 0x477790: state 2, objective 0x23, speed 3 */
{
    int id;
    Ped *p = preset(&id, x, y, z, angle, 0, 0x62);
    p->state = 2, p->objective = 0x23, p->graphic = 0, p->u7c = 5, p->u8b = 1, p->u84 = 100, p->speed = 3;
    return id & 0xffff;
}

int mission_ped_create_11(int x, int y, int z, int angle, int target)  /* 0x477800: armed, state 4, objective 0x31 */
{
    int id;
    Ped *p = preset(&id, x, y, z, angle, 0, 0x62);
    p->u8b = 1, p->weapon = 1, p->state = 4, p->objective = 0x31, p->u7c = 8, p->graphic = 0;
    no_walk(p);
    p->target_ped = (int16_t)target;
    return id & 0xffff;
}

int mission_ped_create_12(int x, int y, int z, int angle, int target)  /* 0x477880: armed, state 3, objective 0x30 */
{
    int id;
    Ped *p = preset(&id, x, y, z, angle, 0, 0x62);
    p->u8b = 1, p->weapon = 1, p->state = 3, p->objective = 0x30, p->u7c = 8, p->graphic = 0;
    no_walk(p);
    p->target_ped = (int16_t)target;
    return id & 0xffff;
}

/* the objective and state of the typed codes 0x15-0x1a, 0x29-0x2e (Mission_PedCreate_Typed and
   Mission_PedSetObj_Typed); false for other codes */
static bool typed_objective(int kind, int32_t *objective, int32_t *state)
{
    static const int8_t lo[6][2] = { { 0x18, 3 }, { 0x1a, 3 }, { 0x1b, 3 }, { 0x1c, 4 }, { 0x1d, 4 }, { 0x1e, 4 } };
    static const int8_t hi[6][2] = { { 0x29, 3 }, { 0x2c, 3 }, { 0x2e, 3 }, { 0x2b, 4 }, { 0x2d, 4 }, { 0x2f, 4 } };
    const int8_t *e = kind >= 0x15 && kind <= 0x1a ? lo[kind - 0x15] : kind >= 0x29 && kind <= 0x2e ? hi[kind - 0x29] : NULL;
    if (!e) return false;
    *objective = e[0], *state = e[1];
    return true;
}

/* Mission_PedCreate_Typed 0x477900: armed, action 8, the code's objective and state (other codes
   leave them as Ped_Create set them) */
int mission_ped_create_typed(int x, int y, int z, int angle, int target, int kind)
{
    int id;
    Ped *p = preset(&id, x, y, z, angle, 0, 0x62);
    p->u8b = 1, p->weapon = 1, p->target_ped = (int16_t)target, p->u7c = 8;
    no_walk(p);
    p->graphic = 0;
    typed_objective(kind, &p->objective, &p->state);
    return id & 0xffff;
}

int mission_ped_create_13(int x, int y, int z, int angle, int target)  /* 0x477ad0: state 4, objective 0x39, action 2 */
{
    int id;
    Ped *p = preset(&id, x, y, z, angle, 0, 0x88);
    no_walk(p);
    p->graphic = 0, p->state = 4, p->objective = 0x39, p->u78 = 8, p->u7c = 2, p->target_ped = (int16_t)target, p->u8b = 1;
    return id & 0xffff;
}

int mission_ped_create_guard(int x, int y, int z, int angle)   /* 0x477b50: state 3, objective 0x38, action 2, no target */
{
    int id;
    Ped *p = preset(&id, x, y, z, angle, 0, 0x88);
    p->state = 3, p->objective = 0x38, p->graphic = 0, p->u7c = 2, p->u78 = 8, p->target_ped = -1, p->u8b = 1;
    return id & 0xffff;
}

/* The creator Mission_SpawnPed 0x43d460 calls per PED sub-type: 0 (and 0xe, which the loader maps to
   0), 1, 4..0xc, 0xd and the typed codes; other sub-types create nothing (-1). */
int mission_ped_create(int kind, int x, int y, int z, int angle, int arg)
{
    switch (kind) {
    case 0: return (int16_t)mission_ped_create_0(x, y, z, angle);
    case 1: return (int16_t)mission_ped_create_1(x, y, z, angle, arg);
    case 4: return (int16_t)mission_ped_create_4(x, y, z, angle);
    case 5: return (int16_t)mission_ped_create_5(x, y, z, angle, arg);
    case 6: return (int16_t)mission_ped_create_6(x, y, z, angle);
    case 7: return (int16_t)mission_ped_create_7(x, y, z, angle, arg);
    case 8: return (int16_t)mission_ped_create_8(x, y, z, angle, arg);
    case 9: return (int16_t)mission_ped_create_9(x, y, z, angle);
    case 10: return (int16_t)mission_ped_create_10(x, y, z, angle);
    case 0xb: return (int16_t)mission_ped_create_11(x, y, z, angle, arg);
    case 0xc: return (int16_t)mission_ped_create_12(x, y, z, angle, arg);
    case 0xd: return (int16_t)mission_ped_create_13(x, y, z, angle, arg);
    default:
        if ((kind >= 0x15 && kind <= 0x1a) || (kind >= 0x29 && kind <= 0x2e))
            return (int16_t)mission_ped_create_typed(x, y, z, angle, arg, kind);
        return -1;
    }
}

/* ---- AI changes (0x477bd0-0x478570) ---- */

/* the shared start: the ped must be alive and not in state 0x15, 0x18, 0x17, 0xc; a ped driving under
   action 0x11 leaves its car (car +0x244 / +0x248 and the links cleared) */
static Ped *changeable(int ped)
{
    Ped *p = ped_ref(ped);
    if (p->state == 0x15 || p->state == 0x18 || p->state == 0x17 || p->state == 0xc || p->health == 0) return NULL;
    return p;
}
static void leave_car(Ped *p)
{
    if (p->u7c == 0x11 && p->car >= 0) {
        Car *c = car_ref(p->car);
        car_wr32(c, 0x244, 0);
        car_wr32(c, 0x248, 0);
        c->driver = -1;
        p->car = -1;
    }
}

bool mission_ped_set_obj_attack(int ped, int target)   /* 0x477bd0: state 4, action 2 */
{
    Ped *p = changeable(ped);
    if (!p) return false;
    leave_car(p);
    p->state = 4, p->target_ped = (int16_t)target, p->u7c = 2, p->u8b = 1;
    no_walk(p);
    return true;
}

bool mission_ped_set_obj_follow(int ped, int target)   /* 0x477c80: state 1, action 5 */
{
    Ped *p = changeable(ped);
    if (!p) return false;
    leave_car(p);
    p->state = 1, p->u7c = 5, p->target_ped = (int16_t)target, p->u8b = 1;
    no_walk(p);
    return true;
}

bool mission_ped_send_to(int ped, int car)  /* Mission_PedSendTo 0x477d20: to the door of `car` */
{
    Ped *p = changeable(ped);
    if (!p) return false;
    leave_car(p);
    no_walk(p);
    ped_send_to_car_door1(p, car);
    p->u8b = 1;
    return true;
}

bool mission_ped_set_obj_wander(int ped)    /* 0x477dc0: state 3, action 8, objective 0x19, speed 0 */
{
    Ped *p = changeable(ped);
    if (!p) return false;
    leave_car(p);
    no_walk(p);
    p->speed = 0, p->state = 3, p->u7c = 8, p->u84 = 100, p->u8b = 1, p->objective = 0x19, p->firing = 0, p->u78 = 8;
    return true;
}

bool mission_ped_set_obj_wander2(int ped)   /* 0x477e80: the same with action 2, speed 1 */
{
    Ped *p = changeable(ped);
    if (!p) return false;
    leave_car(p);
    no_walk(p);
    p->state = 3, p->u7c = 2, p->u84 = 100, p->u8b = 1, p->objective = 0x19, p->firing = 0, p->u78 = 8, p->speed = 1;
    return true;
}

bool mission_ped_set_obj_24(int ped)        /* 0x477f40: state 2, objective 0x24, action 5 */
{
    Ped *p = changeable(ped);
    if (!p) return false;
    leave_car(p);
    no_walk(p);
    p->state = 2, p->objective = 0x24, p->u7c = 5, p->u84 = 100, p->u8b = 1, p->firing = 0, p->u78 = 8;
    return true;
}

bool mission_ped_set_obj_18(int ped, int target)   /* 0x478000: armed, state 3, objective 0x18 */
{
    Ped *p = changeable(ped);
    if (!p) return false;
    leave_car(p);
    no_walk(p);
    p->state = 3, p->weapon = 1, p->u7c = 8, p->u84 = 100, p->objective = 0x18, p->target_ped = (int16_t)target, p->u8b = 1;
    return true;
}

bool mission_ped_set_obj_1c(int ped, int target)   /* 0x4780c0: armed, state 4, objective 0x1c */
{
    Ped *p = changeable(ped);
    if (!p) return false;
    leave_car(p);
    no_walk(p);
    p->state = 4, p->u7c = 8, p->weapon = 1, p->u84 = 100, p->objective = 0x1c, p->target_ped = (int16_t)target, p->u8b = 1;
    return true;
}

bool mission_ped_set_obj_23(int ped)        /* 0x478180: state 2, objective 0x23, action 5, speed 3 */
{
    Ped *p = changeable(ped);
    if (!p) return false;
    leave_car(p);
    no_walk(p);
    p->state = 2, p->u7c = 5, p->objective = 0x23, p->u8b = 1, p->speed = 3;
    return true;
}

bool mission_ped_set_obj_31(int ped, int target)   /* 0x478230: armed, state 4, objective 0x31 */
{
    Ped *p = changeable(ped);
    if (!p) return false;
    leave_car(p);
    p->target_ped = (int16_t)target;
    no_walk(p);
    p->state = 4, p->u7c = 8, p->u84 = 100, p->objective = 0x31, p->weapon = 1, p->u8b = 1;
    return true;
}

void mission_ped_set_obj_30(int ped, int target)   /* 0x4782f0: armed, state 3, objective 0x30, no checks */
{
    Ped *p = ped_ref(ped);
    leave_car(p);
    no_walk(p);
    p->state = 3, p->u7c = 8, p->u84 = 100, p->objective = 0x30, p->target_ped = (int16_t)target, p->weapon = 1, p->u8b = 1;
}

/* Mission_PedSetObj_Typed 0x478380: armed, actions 8 / 8, objective 0x1c unless the code gives one
   (other codes keep the state) */
bool mission_ped_set_obj_typed(int ped, int target, int kind)
{
    Ped *p = changeable(ped);
    if (!p) return false;
    leave_car(p);
    p->u7c = 8, p->u78 = 8, p->target_ped = (int16_t)target;
    no_walk(p);
    p->u84 = 100, p->objective = 0x1c, p->u8b = 1, p->weapon = 1;
    typed_objective(kind, &p->objective, &p->state);
    return true;
}

bool mission_ped_set_obj_39(int ped, int target)   /* 0x478570: state 4, objective 0x39, action 2 */
{
    Ped *p = changeable(ped);
    if (!p) return false;
    leave_car(p);
    p->objective = 0x39, p->target_ped = (int16_t)target, p->u78 = 8, p->state = 4, p->u7c = 2, p->u8b = 1;
    no_walk(p);
    return true;
}

/* Ped_IsNearPoint16 0x478630 / Ped_IsNearPoint 0x478690: pixel (x, y) strictly within r of the ped's
   pixel position on both axes */
bool ped_is_near_point(int ped, int x, int y, int r)
{
    r = (int16_t)r;
    if (r == 0) r = 0x10;
    const Ped *p = ped_ref(ped);
    int ppx = px(p->spr.x), ppy = px(p->spr.y);
    x = (int16_t)x, y = (int16_t)y;
    return x < ppx + r && ppx - r < x && y < ppy + r && ppy - r < y;
}
bool ped_is_near_point16(int ped, int x, int y) { return ped_is_near_point(ped, x, y, 0x10); }

/* Ped_IsInBlockRect 0x478700: the ped's block (its car's when in one, or when the ped isn't animating)
   within r blocks of (bx, by); bz unused */
bool ped_is_in_block_rect(int ped, int bx, int by, int bz, int r)
{
    (void)bz;
    const Ped *p = ped_ref(ped);
    int32_t x, y;
    if (p->anim == 0 || p->state == 7 || p->state == 6) {
        const Car *c = car_ref(p->car);
        x = c->spr.x, y = c->spr.y;
    } else {
        x = p->spr.x, y = p->spr.y;
    }
    bx = (int16_t)bx, by = (int16_t)by;
    int cx = px(x) >> 6, cy = (int16_t)(y >> 22);
    return bx - r <= cx && cx <= bx + r && by - r <= cy && cy <= by + r;
}

/* ---- objects ---- */

int mission_obj_create(int x, int y, int z, int type, int angle)   /* Mission_ObjCreate 0x478790 */
{
    return obj_create(x << 16, y << 16, (z << 16) - 1, (int16_t)type, angle);
}

bool obj_remove_if_active(int obj)          /* Obj_RemoveIfActive 0x4787d0 */
{
    if (obj_ref(obj)->state == 0) return false;
    obj_delete(obj);
    return true;
}

void mission_set_var505efa(int v) { g_mission_var505efa = (int16_t)v; }       /* 0x478800 */
void mission_set_var5031cc(int v) { g_game.opt.emergency = (int16_t)v; }      /* 0x478810 */

int obj_create_at_ped(int ped, int type, int angle)   /* Obj_CreateAtPed 0x478820 */
{
    const Ped *p = ped_ref(ped);
    return obj_create(p->spr.x, p->spr.y, p->spr.z - 0x400000, type, angle);
}

/* Obj_ThrowToCar 0x478860: the object is kicked (kind 0xe) from the car's position toward it */
void obj_throw_to_car(int obj, int car)
{
    const Car *c = car_ref(car);
    const Obj *o = obj_ref(obj);
    obj_kick(c->spr.x, c->spr.y, obj, 0xe, math_atan2(c->spr.y - o->spr.y, c->spr.x - o->spr.x));
}

/* Obj_ThrowToPoint 0x4788c0: the same toward (x, y); the kick position is the object's x with the
   target's y (as in the original) */
void obj_throw_to_point(int obj, int32_t x, int32_t y)
{
    const Obj *o = obj_ref(obj);
    obj_kick(o->spr.x, y, obj, 0xe, math_atan2(y - o->spr.y, x - o->spr.x));
}

void mission_obj_set_state(int obj, int state) { obj_set_state(obj, state); }   /* 0x478900 */

/* ---- briefs ---- */

/* frames -> seconds as the timed briefs do: frames / 30, rounded to 5 in tens (x / 10 * 5, + 5 when the
   last digit is 5 or more), doubled */
static int brief_time(int frames)
{
    int s = frames / 0x1e, t = s / 10 * 5;
    if (s % 10 >= 5) t += 5;
    return t * 2;
}

static void brief(int kind, int text, int time, int player)
{
    text = (int16_t)text;
    if (text < 1000) game_fatal(-0xf6, kind == 5 ? 0x1cf : 0x154, text);
    hud_brief(kind, text, time, player);
}

void mission_brief_timed1(int frames, int text, int player) { brief(1, text, brief_time(frames), player); }   /* 0x478920 */
void mission_brief_timed0(int frames, int text, int player) { brief(0, text, brief_time(frames), player); }   /* 0x478990 */
void mission_brief_countdown(int frames, int text, int player) { brief(2, text, frames, player); }            /* 0x478a00 */
void mission_brief3(int text, int player) { brief(3, text, 0, player); }   /* 0x478a40 */
void mission_brief4(int text, int player) { brief(4, text, 0, player); }   /* 0x478a80 */
void mission_brief5(int text, int player) { brief(5, text, 0, player); }   /* 0x478ac0 */

/* Mission_ShowBombTimer 0x478b00: the pager countdown of the FXT key "bomb_set" with frames / 25
   seconds (the port's text_get takes no arguments: the key's text goes as is) */
void mission_show_bomb_timer(int frames)
{
    pager_add_countdown(text_get("bomb_set"), (int16_t)frames / 25, -1);   /* id -1 (disassembly 0x478b0a) */
}

void obj_toggle_state23(int obj)            /* Obj_ToggleState23 0x478b30 */
{
    int s = obj_ref(obj)->state;
    if (s == 2) obj_set_state(obj, 3);
    else if (s == 3) obj_set_state(obj, 2);
}

/* Obj_StepAxis 0x478b70: x moves by step and is done when its pixel part is then target; y is done
   (without moving) when it is already at target, else moves; z moves and is done once at or past
   target. */
bool obj_step_axis(int obj, int target, int step, int axis)
{
    bool done = false;
    Obj *o = obj_ref(obj);
    int32_t d = (int16_t)step * 0x10000;
    coll_remove(o, o->spr.unk20);
    axis = (int16_t)axis;
    if (axis == 0) {
        o->spr.x += d;
        done = px(o->spr.x) == target;
    } else if (axis == 1) {
        if (px(o->spr.y) != target) o->spr.y += d;
        else done = true;
    } else if (axis == 2) {
        o->spr.z += d;
        done = px(o->spr.z) >= target;
    }
    coll_insert(COLL_OBJECT, o->id, o, o->spr.unk20, o->spr.x, o->spr.y);
    return done;
}

/* Wanted_ClearForPed 0x478c20: a ped with a criminal record loses its wanted level and the record */
bool wanted_clear_for_ped(int ped)
{
    int i = (int16_t)police_find_criminal_by_ped(ped);
    if (i == -1) return false;
    player_clear_wanted_level(ped);
    player_clear_wanted_points(ped);
    police_clear_criminal(i);
    return true;
}

void mission_call438020(int x, int y, int z, int face, int tile) { map_set_block_face(x, y, z, face & 0xff, tile); }   /* 0x478c60 */
void map_set_block_thunk(int x, int y, int z, uint32_t info) { map_set_block_type(x, y, z, info); }   /* 0x478c80 */

/* ---- alarm sound slots ---- */

/* Mission_AlarmSoundAdd 0x478c90: the object's alarm takes a free slot, else the last slot whose
   object is away from it (Chebyshev distance > 0) and near no screen */
void mission_alarm_sound_add(int obj)
{
    const Obj *o = obj_ref(obj);
    for (int i = 0; i < ALARM_SLOTS; i++)
        if (g_alarm_slots[i] == -1) {
            Snd_SetEmitterB(i, o->spr.x, o->spr.y, o->spr.z);
            g_alarm_slots[i] = obj;
            return;
        }
    int pick = -1;
    for (int i = 0; i < ALARM_SLOTS; i++) {
        if (g_alarm_slots[i] < 0) continue;
        const Obj *s = obj_ref(g_alarm_slots[i]);
        int dy = o->spr.y - s->spr.y, dx = o->spr.x - s->spr.x;
        dy = dy < 0 ? -dy : dy, dx = dx < 0 ? -dx : dx;
        if ((dx > dy ? dx : dy) > 0 && !pos_is_near_screen(s->spr.x, s->spr.y)) pick = i;
    }
    if (pick >= 0) {
        Snd_SetEmitterB(pick, o->spr.x, o->spr.y, o->spr.z);
        g_alarm_slots[pick] = obj;
    }
}

void mission_alarm_sound_remove(int obj)    /* Mission_AlarmSoundRemove 0x478d80 */
{
    for (int i = 0; i < ALARM_SLOTS; i++)
        if (g_alarm_slots[i] == obj) {
            Snd_ClearEmitterB(i);
            g_alarm_slots[i] = -1;
        }
}

void mission_alarm_sound_stop_all(void)     /* Mission_AlarmSoundStopAll 0x478dc0: objects back to state 1 */
{
    for (int i = 0; i < ALARM_SLOTS; i++)
        if (g_alarm_slots[i] >= 0) {
            Snd_ClearEmitterB(i);
            obj_set_state((int16_t)g_alarm_slots[i], 1);
            g_alarm_slots[i] = -1;
        }
}

void mission_alarm_sound_reset(void)        /* Mission_AlarmSoundReset 0x478e10 */
{
    for (int i = 0; i < ALARM_SLOTS; i++) g_alarm_slots[i] = -1;
}

/* ---- the car list ---- */

int car_list_add(int a, int model, int remap, int count)   /* CarList_Add 0x478e30 (no bound check) */
{
    if (g_car_list_count >= CARLIST_MAX) game_fatal(-0xab, 0x130, g_car_list_count);   /* port: the original writes on */
    CarListEntry *e = &g_car_list[g_car_list_count];
    e->a = (int16_t)a, e->model = (int16_t)model, e->remap = (int16_t)remap, e->target = (int16_t)count;
    e->count = 0;
    return g_car_list_count++;
}

CarListEntry *car_list_get(int i) { return &g_car_list[i]; }   /* CarList_Get 0x478e80 */

/* Mission_SetSlot773168 0x478e90: the crane's +0x38 if it is still -1 (1), else -1 */
int mission_set_slot773168(int crane, int v)
{
    Crane *c = crane_get(crane);
    if (c->slot != -1) return -1;
    c->slot = v;
    return 1;
}

/* CarList_Matches 0x478ec0: model and remap (the sprite's) match the entry or the entry's are -1 */
int car_list_matches(int car, int i)
{
    const Car *c = car_ref(car);
    const CarListEntry *e = &g_car_list[i];
    if (c->model != e->model && e->model != -1) return -1;
    if ((uint16_t)c->spr.remap != (uint16_t)e->remap && (uint16_t)e->remap != 0xffff) return -1;
    return 1;
}

/* CarList_CountSameModel 0x478f10: cars of ids[] (up to six, -1 ends) of the car's model */
int car_list_count_same_model(int car, const int32_t ids[6])
{
    int n = 0, m = car_ref(car)->model;
    for (int i = 0; i < 6 && ids[i] != -1; i++)
        if (car_ref((int16_t)ids[i])->model == m) n++;
    return n;
}

bool car_list_is_complete(int i)            /* CarList_IsComplete 0x478f60 */
{
    const CarListEntry *e = &g_car_list[(int8_t)i];
    return e->count > 0 && e->count == e->target;
}

/* Mission_ResetLists_thunk 0x478f90 (0x431500, the gang module): both lists of ten -1, counts 0 */
void mission_reset_lists(void)
{
    for (int i = 0; i < 10; i++) g_gang_lists_a[i] = g_gang_lists_b[i] = -1;
    g_gang_count_a = g_gang_count_b = 0;
}

/* Player_FindTrainSlot 0x478fa0: the train whose +6 is the player's ped, when the player rides one */
int player_find_train_slot(int n)
{
    if (player_get_controlled_kind(n) != PLAYER_ON_TRAIN) return 0xff;
    int16_t ped = player_get(n)->ped;
    int count = (int16_t)train_get_count();
    for (int i = 0; i < count; i++) {
        const uint8_t *t = train_get(i);
        if ((int16_t)(t[6] | t[7] << 8) == ped) return i & 0xff;
    }
    return 0xff;
}

void mission_call46d650(int train) { train_crash(train, 2); train_crash(train, 1); }   /* 0x479000 */
