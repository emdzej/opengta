/* The car module 0x405790-0x40c0c0 (see car.h, docs/cars.md). */
#include "car.h"
#include "../exe.h"
#include "../hud/hud.h"
#include "carcoll.h"
#include "carinfo.h"
#include "event.h"
#include "game.h"
#include "gmath.h"
#include "mission_obj.h"
#include "mission_run.h"
#include "obj.h"
#include "ped.h"
#include "player.h"
#include "stubs.h"
#include "trigger.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

Car g_cars[CAR_MAX] = { 0 };
int16_t g_car_model_index[CAR_MODELS];
int g_cars_count;                            /* 0x501554 */
int g_cars_active;                           /* 0x4be170 */
uint16_t g_traffic_models[3][100];
int g_traffic_cycle;                         /* 0x504f40 */
int16_t g_car_forced_accel[CAR_MAX];         /* 0x4bde08 */
/* other state of the module */
static struct {
    uint8_t wreck_cycle;                     /* 0x501558 wreck sprite 0 / 1 */
    uint8_t remap_cycle;                     /* 0x501559 remap 0 or 7..12 */
    int16_t bomb_damage;                     /* 0x501550 damage that sets off a damage bomb (10) */
    int16_t u4be23c, u4be23e;
    int16_t tram_doors;                      /* 0x4be244 the tram's doors animate while set */
    int u504f38, u504f3c;
    int16_t near_view[4];                    /* 0x501548 dummies near each player's view this frame */
} cs;


const uint8_t *car_info_of_model(int model)
{
    if (model < 0 || model >= CAR_MODELS || g_car_model_index[model] < 0) return NULL;
    return car_info_record(g_car_model_index[model]);
}

/* Cars_Init 0x4070a0: the car info records (CarInfo_Setup, see carinfo.h), the model -> record table
   (records 0..99), every slot free (status -1, +0x139 = 0), the traffic tables shuffled */
void cars_init(void)
{
    car_info_setup(g_game.style);
    memset(g_car_model_index, 0xff, sizeof g_car_model_index);
    g_cars_active = 0;
    for (int i = 0; i < 100; i++)
        if (i < car_info_count()) {
            int m = car_info_record(i)[0x6b];
            if (m < CAR_MODELS) g_car_model_index[m] = (int16_t)i;   /* (the original writes past the table) */
        }
    cs.u4be23c = 8;
    cs.u4be23e = 0;
    cs.tram_doors = cs.u4be23e;
    g_cars_count = g_cars_active;
    for (int i = 0; i < CAR_MAX; i++) {
        g_cars[i].status = -1;
        cs.wreck_cycle = 0;
        g_cars[i].unk139 = 0;
    }
    cs.bomb_damage = 10;
    cs.remap_cycle = cs.wreck_cycle;
    traffic_init_model_tables();
}

/* Traffic_InitModelTables 0x418f80: three rows of 100 car models (0x4ac108, read from the exe), each
   shuffled by swapping every entry with a random one of its row (Math_Random % 100). */
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
    cs.u504f38 = 0;
    cs.u504f3c = 0;
}

int cars_in_use(void)
{
    int k = 0;
    for (int i = 0; i < CAR_MAX; i++) k += g_cars[i].status != -1;
    return k;
}

/* road direction bits of a heading: [0x80, 0x180) 8, [0x180, 0x280) 1, [0x280, 0x380) 4, else 2 */
static uint16_t heading_dirs(int16_t h)
{
    if (h < 0x380 && h > 0x7f) {
        if (h < 0x280) return h < 0x180 || h > 0x27f ? 8 : 1;
        return 4;
    }
    return 2;
}

/* the sprite / remap / driver checks of the spawners */
static bool player_drives_car(int car)       /* Player_IsCarPlayerDriven 0x462a10 */
{
    if (!g_players_ready) return false;
    for (int n = player_first(); n > -1; n = player_next(n))
        if (car == g_players[n].ped - 200) return true;
    return false;
}
static bool player_views_car(int car)        /* Player_IsCarViewTarget 0x462a60 */
{
    if (!g_players_ready) return false;
    for (int n = player_first(); n > -1; n = player_next(n))
        if (g_players[n].view_kind == 0 && g_players[n].view_id == car) return true;
    return false;
}
/* the slot the spawners take: the first free one (status -1, not reserved by +0x139) whose driver ped
   is unused (control -1, or dead, no anim and not down) and that no player drives or views */
static int car_alloc(void)
{
    int i = 0;
    for (; i < CAR_MAX; i++) {
        const Car *c = &g_cars[i];
        const Ped *p = ped_get(i + PED_DRIVER_FIRST);
        if (c->status == -1 && c->unk139 == 0 &&
            (p->control == -1 || (p->health == 0 && p->anim == 0 && p->state != 0xc)) &&
            !player_drives_car(i) && !player_views_car(i))
            break;
    }
    return i;
}

/* ---- Car_Init 0x4067c0 ---- */
void car_init(int32_t x, int32_t y, int32_t z, int angle, int model, int n)
{
    Car *c = &g_cars[(int16_t)n];
    int16_t a = (int16_t)angle;
    model = (int16_t)model;
    const uint8_t *in = car_info_record(g_car_model_index[model]);
    c->info = in;
    c->accel = carinfo_s16(in, 0x0e);
    c->braking = carinfo_s16(in, 0x10);
    c->max_speed = model == 4 ? 0x3c : carinfo_s16(in, 0x0a);
    c->horn_pattern = -1;
    c->horn_time = 0;
    c->min_speed = carinfo_s16(in, 0x0c);
    c->u7e = 100;
    c->base_frame = carinfo_s16(in, 6);
    c->door_dx = carinfo_s16(in, 0xae);
    c->enter_delay = 0;
    c->door_dy = carinfo_s16(in, 0xb0);
    c->vtype = in[0x6a];
    c->length = (int16_t)-carinfo_s16(in, 2);
    c->cam_w = carinfo_s16(in, 0);
    c->unk88 = 0;
    c->depth = carinfo_s16(in, 4);
    c->u84 = 0;
    c->half_l = (int16_t)(carinfo_s16(in, 2) >> 1);
    c->half_w = (int16_t)(c->cam_w >> 1);
    c->brake_peak = 0;
    sprite_init(&c->spr, x, y, z, a, carinfo_s16(in, 6));
    sprite_set_palette(&c->spr, sprite_car_palette(g_car_model_index[model]));
    c->model = (int16_t)model;
    c->z_offset = model == 0x2f ? 0x20000 : model == 0x25 ? 0x40000 : 0x30000;
    int32_t z0 = c->spr.z;
    c->id = (int16_t)n;
    c->hit_car = (int16_t)n;
    c->status = 0;
    c->u86 = 0, c->door1 = 0, c->door2 = 0, c->u114 = 0, c->falling = 0, c->bomb = 0, c->bomb_timer = 0;
    c->spr.zkey = z0;
    c->unkec = 0, c->ubc = 0, c->u1e = 0, c->speed = 0;
    c->front_heading = a;
    c->u12e = 0, c->brake = 0, c->ucc = 0, c->siren_state = 0;
    c->rear_heading = a;
    c->input = 0, c->horn = 0, c->u7c = 1, c->u12a = 1, c->turn_delta = 0, c->lane_mode = 0, c->burning = 0;
    c->driver = -1;
    c->ube = 0, c->turn_progress = 0, c->turret = 0, c->drive_mode = 0;
    c->u12c = model == 0x25;
    c->owner_status = 1;
    c->ua8 = -1;
    c->sinking = 0;
    c->uce = (int16_t)(((uint8_t)c->max_speed & 7) - 4);
    int hl = c->length >> 1;
    int32_t sx = math_sin(a) * hl, sy = math_cos(a) * hl;
    c->u11e = 0;
    c->front_x = x - sx;
    c->u120 = 0;
    c->front_y = y - sy;
    c->rear_y = sy + y;
    c->ue8 = z;
    c->rear_x = sx + x;
    c->rear_door = 0;
    c->cruise = 6;
    c->u8e = 0;
    c->control = 0;
    c->u124 = 100;
    c->udc = 0;
    c->damage = 0;
    c->unkc0 = 0;
    c->lane = 0x1f;
    c->ue0 = x;
    c->ue4 = y;
    c->ude = a;
    c->script_line = -1;
    c->u13a = 0;
    c->player = -1;
    c->u140 = 0;
    if (model == 0x25) {
        c->horn = (int16_t)obj_create_attached(n, 1, 0, 0, 0x21);
        c->siren_state = (int16_t)obj_create_attached(c->horn, 0, 0, 0x1e, 0x22);
        obj_list_rotate();
    }
    c->siren_type = model == 0x13 ? 1 : model == 1 ? 2 : model == 0x2b ? 3 : 0;
    c->road_dirs = heading_dirs(c->front_heading);
    int32_t gz = map_get_ground_z(g_game.map, c->spr.x, c->spr.y, c->spr.z);
    c->spr.z = gz;
    if (car_type_cache(c->spr.x, c->spr.y, gz) < 0x80) {
        c->spr.zkey = gz - c->z_offset;
    } else {
        c->spr.zkey = gz - 0x400000;
        if (gz - 0x400000 < 0) c->spr.zkey = 1;
    }
    c->turn_dirs = c->road_dirs;
    c->u98 = 0, c->ua6 = 0;
    c->uba = c->id;
    c->frames = 0, c->siren_tick = 0, c->u100 = 0, c->u102 = 0;
    c->sentinel = -1;
    if (cs.remap_cycle == 0) {
        sprite_set_remap(&c->spr, 0);
        c->remap = 0;
    } else {
        sprite_set_remap(&c->spr, cs.remap_cycle + 6);
        c->remap = (uint8_t)(cs.remap_cycle + 6);
    }
    if (++cs.remap_cycle > 6) cs.remap_cycle = 0;
    coll_insert(COLL_CAR, n, c, c->spr.unk20, x, y);
    coll_build_box(x, y, z, c->half_w, c->half_l, c->spr.angle, c->depth, &c->box_saved);
    c->next_x = x, c->next_y = y, c->next_z = z, c->next_heading = a;
    memset(c->u158, 0, sizeof c->u158);
    c->steer_cos = c->steer_sin = c->u180 = c->u184 = c->thrust_in = 0;
    c->steer = 1.57079637f;   /* 0x3fc90fdb */
    c->physics = 0, c->brake_in = 0, c->handbrake_in = 0, c->gear = 1;
    c->speed2 = 0, c->map_hit = 0, c->obj_hit = 0;
    c->impulse_x = c->impulse_y = c->impulse_px = c->impulse_py = 0;
    c->impulse_state = 0;
    c->u244 = 0;
    c->skid_r = 0, c->skid_l = 0;
    carphys_reset(c->id);
    car_update_ground(c);
    car_restore_box(c);
    c->physics = 0;
    c->active = 0;
    c->saved_x = c->spr.x;
    c->saved_y = c->spr.y;
    c->skid = 0;
}

/* the driver slot ped of a new car (Car_SpawnEx and the like) */
static void bind_driver(Car *c, int n, int driver)
{
    if (driver == 1) {
        c->driver = (int16_t)(n + PED_DRIVER_FIRST);
        Ped *p = ped_get(n + PED_DRIVER_FIRST);
        p->car = (int16_t)n;
        p->health = 100;
    } else {
        c->driver = -1;
    }
}

/* the spawn check of Car_SpawnEx / Car_SpawnOnRoad: another car in the way of `c`'s pending box at
   (x, y) refuses the spawn (a dummy one is removed if off screen) */
static bool spawn_blocked(Car *c, int32_t x, int32_t y)
{
    bool blocked = false;
    for (CollHit *h = coll_query_car_box(c, x, y, COLL_CAR, c->id); h; h = h->next) {
        Car *o = h->owner;
        if (c->id != o->id) {
            blocked = true;
            if (o->active == 0) car_remove_if_offscreen(o->id);
            break;
        }
    }
    coll_unlock();
    return blocked;
}

/* Car_SpawnEx 0x4078d0. The space check uses the box of slot g_cars_count (the next unused slot,
   not the new car: kept as in the original) before the car is created. */
int car_spawn_ex(int32_t x, int32_t y, int32_t z, int model, int driver, int angle, int remap)
{
    stub_calls[STUB_CAR_SPAWN]++;
    int n = car_alloc();
    if (g_car_model_index[(int16_t)model] == -1) n = CAR_MAX;
    if ((int16_t)n >= CAR_MAX) return -1;
    Car *probe = &g_cars[g_cars_count];
    if (probe->model != 0x2f && spawn_blocked(probe, x, y)) return -1;
    if ((int16_t)model == 99) model = 7;
    car_init(x, y, z, angle, model, n);
    Car *c = &g_cars[n];
    bind_driver(c, n, (int16_t)driver);
    c->owner_status = (int16_t)driver;
    c->siren_state = 0;
    if (n == g_cars_count) g_cars_count = n + 1;
    sprite_set_remap(&c->spr, remap);
    c->remap = (uint8_t)remap;
    return n & 0xffff;
}

/* Car_SpawnExOnGround 0x407ab0 (no space check) */
int car_spawn_ex_on_ground(int32_t x, int32_t y, int32_t z, int model, int driver, int angle, int remap)
{
    stub_calls[STUB_CAR_SPAWN]++;
    int n = car_alloc();
    if (g_car_model_index[(int16_t)model] == -1) n = CAR_MAX;
    if ((int16_t)n >= CAR_MAX) return -1;
    int32_t gz = map_get_ground_z(g_game.map, x, y, z);
    if ((int16_t)model == 99) model = 7;
    car_init(x, y, gz, angle, model, n);
    Car *c = &g_cars[n];
    bind_driver(c, n, (int16_t)driver);
    c->owner_status = (int16_t)driver;
    if (c->model != 0x25 && c->model != 0x2a) c->siren_state = 0;
    car_sync_physics(c);
    if (n == g_cars_count) g_cars_count++;
    sprite_set_remap(&c->spr, remap);
    c->remap = (uint8_t)remap;
    return n & 0xffff;
}

static int road_heading(uint8_t t)
{
    switch (t & 0xf) {
    case 1: return 0x200;
    case 2: return 0;
    case 4: return 0x300;
    case 8: return 0x100;
    default: return -1;
    }
}
static bool road_type(uint8_t t) { int k = t >> 4 & 7; return k == 2 || k == 6 || k == 7; }

/* Car_SpawnOnRoad 0x407310: on a road block, facing its direction (0xffff when refused) */
int car_spawn_on_road(int32_t x, int32_t y, int32_t z, int model, int driver)
{
    int n = car_alloc();
    if (g_car_model_index[(int16_t)model] == -1) n = CAR_MAX;
    if ((int16_t)n >= CAR_MAX) return 0xffff;
    uint8_t t = car_type_cache(x, y, z);
    int h = road_type(t) ? road_heading(t) : -1;
    if (h < 0) return 0xffff;
    car_init(x, y, map_get_ground_z(g_game.map, x, y, z - 0x3c0000), h, model, n);
    Car *c = &g_cars[n];
    if (c->model != 0x2f && spawn_blocked(c, x, y)) {
        car_delete(n);
        return 0xffff;
    }
    c->road_dirs = t & 0xf;
    c->unk139 = 1;
    c->u13a = 0;
    c->player = -1;
    c->siren_state = 0;
    if ((int16_t)driver == 1) {
        c->driver = (int16_t)(n + PED_DRIVER_FIRST);
        ped_get(n + PED_DRIVER_FIRST)->car = (int16_t)n;
        c->unk88 = 1;
    } else {
        c->driver = -1;
    }
    c->owner_status = (int16_t)driver;
    car_sync_physics(c);
    if (n == g_cars_count) g_cars_count++;
    return n & 0xffff;
}

/* Car_SpawnWithDriver 0x4075b0: heading 0, driver, normal ownership */
int car_spawn_with_driver(int32_t x, int32_t y, int32_t z, int model)
{
    int n = car_alloc();
    if (g_car_model_index[(int16_t)model] == -1) n = CAR_MAX;
    if ((int16_t)n >= CAR_MAX) return -1;
    car_init(x, y, z, 0, model, n);
    Car *c = &g_cars[n];
    c->road_dirs = 1;
    c->unk139 = 1;
    c->u13a = 0;
    c->player = -1;
    c->siren_state = 0;
    c->driver = (int16_t)(n + PED_DRIVER_FIRST);
    ped_get(n + PED_DRIVER_FIRST)->car = (int16_t)n;
    c->unk88 = 1;
    c->owner_status = 1;
    car_sync_physics(c);
    if (n == g_cars_count) g_cars_count++;
    return n & 0xffff;
}

/* thunk_Car_SpawnParked 0x4763d0 = Car_SpawnModel47AtBlock 0x4076f0: model 0x2f at the centre of
   block (bx, by) of layer bz - 1 if it is a road, facing its direction, with a driver, hunter control */
int car_spawn_parked(int bx, int by, int bz)
{
    stub_calls[STUB_CAR_SPAWN]++;
    int n = car_alloc();
    if ((int16_t)n >= CAR_MAX) return -1;
    uint8_t t = car_cache_at(bx + (bz * 0x100 - 0x100 + by) * 0x100);
    int h = road_type(t) ? road_heading(t) : -1;
    if (h < 0) return -1;
    int32_t x = bx * 0x400000 + 0x200000, y = by * 0x400000 + 0x200000;
    car_init(x, y, map_get_ground_z(g_game.map, x, y, bz * 0x400000 - 0x3d0000), h, 0x2f, n);
    Car *c = &g_cars[n];
    c->road_dirs = t & 0xff0f;
    c->driver = (int16_t)(n + PED_DRIVER_FIRST);
    Ped *p = ped_get(n + PED_DRIVER_FIRST);
    p->car = (int16_t)n;
    p->health = 100;
    c->owner_status = 1;
    c->control = CAR_CTL_HUNT;
    c->cruise = (int16_t)(c->max_speed - 1);
    c->siren_state = 0;
    car_sync_physics(c);
    if (n == g_cars_count) g_cars_count++;
    c->unk88 = 1;
    return n;
}

/* Car_ResetFromInfo 0x4063d0 */
void car_reset_from_info(int n)
{
    Car *c = &g_cars[(int16_t)n];
    const uint8_t *in = c->info;
    c->accel = carinfo_s16(in, 0x0e);
    int16_t model = c->model;
    c->braking = carinfo_s16(in, 0x10);
    c->max_speed = model == 4 ? 0x3c : carinfo_s16(in, 0x0a);
    int16_t id = c->id;
    c->min_speed = carinfo_s16(in, 0x0c);
    c->base_frame = carinfo_s16(in, 6);
    c->door_dx = carinfo_s16(in, 0xae);
    c->door_dy = carinfo_s16(in, 0xb0);
    c->vtype = in[0x6a];
    c->length = (int16_t)-carinfo_s16(in, 2);
    c->cam_w = carinfo_s16(in, 0);
    c->depth = carinfo_s16(in, 4);
    c->half_w = (int16_t)(c->cam_w >> 1);
    uint8_t ms = (uint8_t)c->max_speed;
    c->half_l = (int16_t)(carinfo_s16(in, 2) >> 1);
    c->spr.zkey = c->spr.z;
    int16_t h = c->spr.angle;
    c->z_offset = 0x20000;
    c->status = 0;
    c->u86 = 0, c->door1 = 0, c->door2 = 0, c->u114 = 0, c->falling = 0, c->bomb = 0, c->bomb_timer = 0, c->unkec = 0;
    c->front_heading = h;
    c->u12e = 0, c->ucc = 0, c->siren_state = 0;
    c->rear_heading = h;
    c->horn = 0, c->u7c = 1, c->u12a = 1, c->turn_delta = 0, c->lane_mode = 0, c->burning = 0;
    c->hit_car = id;
    c->ube = 0, c->turn_progress = 0, c->turret = 0, c->drive_mode = 0, c->u12c = 0, c->sinking = 0, c->rear_door = 0;
    c->keep_active = 0, c->prev_dirs = 0;
    c->uce = (int16_t)((ms & 7) - 4);
    c->cruise = 6, c->u8e = 0, c->control = 0, c->u124 = 100, c->owner_status = 1, c->udc = 0, c->damage = 0;
    c->unkc0 = 0, c->lane = 0x1f, c->ua8 = -1;
    if (model == 0x2f) c->z_offset = 0x10000;
    if (model == 0x25) {
        c->horn = (int16_t)obj_create_attached(id, 1, 0, 0, 0x21);
        c->siren_state = (int16_t)obj_create_attached(c->horn, 0, 0, 0x1e, 0x22);
        obj_list_rotate();
        c->z_offset = 0x40000;
        c->u12c = 1;
    }
    c->siren_type = model == 0x13 ? 1 : model == 1 ? 2 : model == 0x2b ? 3 : 0;
    int32_t z = c->spr.z;
    if (car_type_cache(c->spr.x, c->spr.y, z) < 0x80) {
        c->spr.zkey = z - c->z_offset;
    } else {
        c->spr.zkey = z - 0x400000;
        if (z - 0x400000 < 0) c->spr.zkey = 1;
    }
    c->turn_dirs = c->road_dirs;
    c->u98 = 0, c->ua6 = 0;
    c->uba = c->id;
    c->frames = 0, c->siren_tick = 0, c->u100 = 0, c->u102 = 0;
    c->sentinel = -1;
    c->horn_pattern = -1;
    c->horn_time = 0;
    memset(c->u158, 0, sizeof c->u158);
    c->steer_cos = c->steer_sin = c->u180 = c->u184 = c->thrust_in = 0;
    c->steer = 1.57079637f;
    c->physics = 0, c->brake_in = 0, c->handbrake_in = 0, c->gear = 1;
    c->speed2 = 0, c->map_hit = 0, c->obj_hit = 0;
    c->impulse_x = c->impulse_y = c->impulse_px = c->impulse_py = 0;
    c->impulse_state = 0;
    c->u244 = 0;
}

/* Car_Delete 0x40b660 */
void car_delete(int n)
{
    n = (int16_t)n;
    Car *c = &g_cars[n];
    if (c->model == 4 && c->sentinel > -1) {
        uint8_t *s = sentinel_get(c->sentinel);
        if (s && *(int32_t *)(s + 4) == 1) {
            /* an ambulance on a call stays (its crew walks away) */
            c->damage = 0;
            if (c->control != 3) {
                coll_remove(c, c->spr.unk20);
                if (c->driver != -1 && c->sinking != 9) {
                    Ped *p = ped_get(c->driver);
                    p->attach_kind = 0;
                    p->u7c = 2;
                    p->state = 2;
                    ped_set_health(c->driver, 0);
                    if (c->sinking == 8) {
                        p->state = 0x18;
                        c->driver = -1;
                    }
                }
            }
            if (c->sinking != 9) {
                if (c->sinking == 8) c->sinking = 9;
                s[0x1b] = 0xfe;
            }
            c->control = 3;
            return;
        }
    }
    mission_clear_counter(n);
    coll_remove(c, c->spr.unk20);
    int16_t sent = c->sentinel;
    c->owner_status = 0;
    c->status = -1;
    c->sinking = 0;
    c->unkec = 0;
    c->active = 0;
    if (sent > -1) {
        if (sent == g_path_owner) g_path_owner = -1;
        uint8_t *s = sentinel_get(sent);
        if (s) s[2] = 0;
    }
    if (c->driver != -1) {
        Ped *p = ped_get(c->driver);
        p->attach_kind = 0;
        p->u7c = 2;
        p->state = 2;
        if (c->model != 0x2f) {
            ped_set_health(c->driver, 0);
            if (c->sinking == 8) p->state = 0x18;
            c->driver = -1;
        }
    }
    if (c->model == 0x25) {
        if (c->horn != 0) obj_remove_moving(c->horn);
        if (c->siren_state != 0) obj_remove_moving(c->siren_state);
    }
    if (c->model == 0x2a && c->siren_state != 0) {
        obj_remove_moving(c->siren_state);
        fire_clear_objects(n);
    }
    c->siren_state = 0;
    c->script_held = 0;
}

static int iabs16(int v) { return v < 0 ? -v : v; }

/* ---- player controls ---- */

/* Player_Get*ByPed 0x4645c0-0x464750: control byte k (+0x191 + k) of the player whose ped this is
   (0 if none; the original reads player -1's record then) */
static int ctl_by_ped(int ped, int k)
{
    for (int n = player_first(); n > -1; n = player_next(n))
        if (g_players[n].ped == (int16_t)ped) return g_players[n].ctl[k];
    return 0;
}
enum { CTL_ACCEL = 0, CTL_BRAKE = 1, CTL_HANDBRAKE = 2, CTL_STEER = 3, CTL_GEAR = 4, CTL_SPECIAL = 5 };

/* Car_ApplyPlayerControls 0x40a2e0: with the engine on: the brake / handbrake / gear bytes, the drive
   force (thrust forward, -0.5 x thrust in reverse, -0.15 x for bikes; nothing in the air), the tank's
   turret (special + steer) and gun, and the steering (car info +0x98 turn ratio in degrees a frame) */
void car_apply_player_controls(Car *c)
{
    if (c->unk88 != 1 || c->u244 == 1) return;
    c->brake_in = (uint8_t)ctl_by_ped(c->driver, CTL_BRAKE);
    c->handbrake_in = (uint8_t)ctl_by_ped(c->driver, CTL_HANDBRAKE);
    int8_t gear = (int8_t)ctl_by_ped(c->driver, CTL_GEAR);
    c->gear = gear;
    c->thrust_in = 0;
    if (c->body.x == 0.0f) {
        c->body.x = (float)(int16_t)(c->spr.x >> 16);
        c->body.y = (float)(int16_t)(c->spr.y >> 16);
        c->body.angle = car_heading_to_angle(c->next_heading);
    }
    if ((int16_t)ctl_by_ped(c->driver, CTL_ACCEL) != 0) {
        if (c->gear == -1) {
            if (c->falling == 0) c->thrust_in = (float)((double)c->thrust * (c->vtype == 3 ? -0.15 : -0.5));
        } else if (c->gear == 0) {
            c->thrust_in = 0;
        } else if (c->gear == 1) {
            if (c->falling == 0) c->thrust_in = c->thrust;
        } else {
            game_fatal(-0x4a, 0x1cd, c->gear);
        }
    }
    bool tank = false;
    if (c->model == 0x25) {
        Obj *turret = obj_get(c->horn);
        Ped *drv = ped_get(c->driver);
        if (ctl_by_ped(c->driver, CTL_SPECIAL) != 0 && c->falling == 0) {
            int s = (int8_t)ctl_by_ped(c->driver, CTL_STEER);
            if (s < 0) c->turret = (int16_t)((c->turret + 0x20) & 0x3ff);
            else if ((int8_t)ctl_by_ped(c->driver, CTL_STEER) > 0) c->turret = (int16_t)((c->turret - 0x20) & 0x3ff);
        }
        turret->spr.angle = (int16_t)((c->spr.angle + c->turret) & 0x3ff);
        if (drv->firing != 0) {
            car_fire_rocket(c);
            drv->firing = 0;
        }
        if (0.0f < c->thrust_in && c->max_speed < (c->speed < 0 ? -c->speed : c->speed)) c->thrust_in = 0;
        tank = true;
    }
    if ((!tank || ctl_by_ped(c->driver, CTL_SPECIAL) == 0) && c->falling == 0) {
        double turn = (double)carinfo_s16(c->info, 0x98) * 0.017453292519944444;
        if ((int8_t)ctl_by_ped(c->driver, CTL_STEER) < 0) c->steer = (float)((double)c->steer - turn);
        else if ((int8_t)ctl_by_ped(c->driver, CTL_STEER) > 0) c->steer = (float)(turn + c->steer);
    }
}

/* ---- the pose and the box ---- */

void car_restore_box(Car *c) { c->box = c->box_saved; }   /* Car_RestoreBox 0x405a80 */
void car_save_box(Car *c) { c->box_saved = c->box; }      /* Car_SaveBox 0x405b30 */

/* Car_BoxMoveX 0x405be0: the pending box with only the x part of the move (heading kept) */
void car_box_move_x(Car *c)
{
    c->next_heading = c->spr.angle;
    c->next_x = c->spr.x;
    coll_build_box(c->spr.x, c->next_y, c->next_z, c->half_w, c->half_l, c->spr.angle, c->depth, &c->box_saved);
}
/* Car_BoxMoveY 0x405c40 */
void car_box_move_y(Car *c)
{
    c->next_heading = c->spr.angle;
    c->next_y = c->spr.y;
    coll_build_box(c->next_x, c->spr.y, c->next_z, c->half_w, c->half_l, c->spr.angle, c->depth, &c->box_saved);
}

static float fixed_px(int32_t v) { return (float)((double)v * (double)(1.0f / 65536.0f)); }

/* Car_SyncPhysics 0x405ca0: the next pose and the body's centre / angle back on the sprite, the
   wheel points from the heading, the velocity halved */
void car_sync_physics(Car *c)
{
    c->next_x = c->spr.x;
    float fx = fixed_px(c->spr.x);
    c->next_heading = c->spr.angle;
    c->next_y = c->spr.y;
    c->body.cx = fx;
    c->body.cy = fixed_px(c->spr.y);
    if (c->falling == 0) {
        c->next_z = c->spr.z;
        phys_sync_from_centre(&c->body);
        c->body.angle = car_heading_to_angle(c->spr.angle);
        car_save_box(c);
    } else {
        phys_sync_from_centre(&c->body);
        c->body.angle = car_heading_to_angle(c->spr.angle);
    }
    int16_t h = c->spr.angle;
    float vx = (float)((double)c->body.vx * 0.5f);
    int hl = c->length >> 1;
    c->front_x = c->spr.x - math_sin(h) * hl;
    c->front_y = c->spr.y - math_cos(h) * hl;
    c->rear_x = math_sin(h) * hl + c->spr.x;
    c->body.vx = vx;
    c->body.vy = (float)((double)c->body.vy * 0.5f);
    c->rear_y = math_cos(h) * hl + c->spr.y;
}

/* Car_CommitMove 0x405df0 */
void car_commit_move(Car *c)
{
    c->spr.x = c->next_x;
    c->spr.y = c->next_y;
    c->spr.z = c->next_z;
    c->body.cx = fixed_px(c->next_x);
    c->body.cy = fixed_px(c->next_y);
    phys_sync_from_centre(&c->body);
    int16_t h = c->next_heading;
    if (c->spr.angle != h && g_cc.kind != -1) c->body.angle = car_heading_to_angle(h);
    c->spr.angle = h;
    coll_build_box(c->spr.x, c->spr.y, c->spr.z, c->half_w, c->half_l, h, c->depth, &c->box_saved);
    car_restore_box(c);
}

/* the test Car_BisectMove repeats, chosen by what the move hit (g_cc.kind) */
static int bisect_test(const CollBox *b, Car *c)
{
    int r = -1;
    switch (g_cc.kind) {
    case 2: r = coll_map_solid(b); break;
    case 3: r = coll_map_slopes(b, 0); break;
    case 4: r = coll_map_walls(b, 1); break;
    case 6: case 8: r = coll_first_hit(b, 6, c); break;
    case 9:
        r = coll_map_solid(b);
        if (r == -1) r = coll_map_walls(b, 1);
        break;
    case 10:
        r = coll_map_solid(b);
        if (r == -1) r = coll_map_slopes(b, 0);
        break;
    case 0xb:
        r = coll_map_slopes(b, 0);
        if (r == -1) r = coll_map_walls(b, 1);
        break;
    case 0xc:
        r = coll_map_solid(b);
        if (r == -1) r = coll_map_slopes(b, 0);
        if (r == -1) r = coll_map_walls(b, 1);
        break;
    default: break;   /* (r stays whatever the original's last value was: -1 at the start) */
    }
    return r;
}

/* Car_BisectMove 0x405ed0: up to 8 halvings (in whole pixels and heading units, the heading the short
   way round) between the current pose (free) and the next one (blocked); the last free pose found is
   the next pose. When nothing better than the start is found the car stays and its body is reset to
   the sprite. */
void car_bisect_move(Car *c)
{
    static CollBox probe;                    /* (on the original's stack) */
    int16_t z = (int16_t)(c->spr.z >> 16);
    uint16_t ox = (uint16_t)(c->spr.x >> 16), oy = (uint16_t)(c->spr.y >> 16), oh = (uint16_t)c->spr.angle;
    uint16_t fx = ox, fy = oy, fh = oh;                  /* free (local_60, param_1, local_64) */
    uint16_t bx = (uint16_t)(c->next_x >> 16), byy = (uint16_t)(c->next_y >> 16), bh = (uint16_t)c->next_heading;   /* blocked (local_54, local_5c, local_50) */
    uint16_t mx = 0, my = 0, mh = 0;                     /* the midpoint (local_68, local_6c, uVar15) */
    int16_t state = 0, steps = 0, res = 0;
    bool done = false;
    if (iabs16((int16_t)bx - (int16_t)fx) < 2 && iabs16((int16_t)byy - (int16_t)fy) < 2) {
        done = true;
    } else {
        do {
            if (state == 0) res = -1;
            else if (state == 1) byy = my, bx = mx, bh = mh;
            else if (state == 2) fy = my, fh = mh, fx = mx;
            else game_fatal(-0x4a, 0x1cb, state);
            int ddx = iabs16((int16_t)bx - (int16_t)fx);
            mx = fx;
            if (ddx > 1) mx = (uint16_t)(((int16_t)bx + (int16_t)fx) >> 1);
            int ddy = iabs16((int16_t)byy - (int16_t)fy);
            my = ddy < 2 ? fy : (uint16_t)(((int16_t)byy + (int16_t)fy) >> 1);
            int b16 = (int16_t)bh, f16 = (int16_t)fh;
            mh = fh;
            if (iabs16(b16 - f16) > 1) {
                if (f16 + 0x200 < b16 || b16 < f16 - 0x200) {
                    int v = (f16 + 0x400 + b16) >> 1;
                    mh = (uint16_t)(v & 0x3ff);   /* ((v & 0x800003ff) with the sign fix-up: v is positive) */
                } else {
                    mh = (uint16_t)((f16 + b16) >> 1);
                }
            }
            if (res == -1 && ddx < 2 && ddy < 2) {   /* the last probe was free and the gap closed */
                done = true;
                break;
            }
            coll_build_box((int16_t)mx << 16, (int16_t)my << 16, z << 16, c->half_w, c->half_l, (int16_t)mh, c->depth, &probe);
            res = (int16_t)bisect_test(&probe, c);
            state = res != -1 ? 1 : 2;
        } while (++steps < 8);
        /* (after 8 probes the original's "steps != 8" test never passes: the car stays) */
    }
    if (done) {
        if (fx != (uint16_t)(c->spr.x >> 16) || fy != (uint16_t)(c->spr.y >> 16) || fh != (uint16_t)c->spr.angle) {
            c->next_x = (int16_t)fx << 16;
            c->next_y = (int16_t)fy << 16;
            c->next_z = z << 16;
            c->next_heading = (int16_t)fh;
            coll_build_box(c->next_x, c->next_y, c->next_z, c->half_w, c->half_l, c->next_heading, c->depth, &c->box_saved);
            c->body.angle = car_heading_to_angle(c->next_heading);
            return;
        }
    }
    c->next_x = c->spr.x;
    c->next_y = c->spr.y;
    float cx = fixed_px(c->spr.x);
    c->next_z = c->spr.z;
    c->next_heading = c->spr.angle;
    c->body.cx = cx;
    c->body.cy = fixed_px(c->spr.y);
    phys_sync_from_centre(&c->body);
    c->body.angle = car_heading_to_angle(c->spr.angle);
    coll_build_box(c->next_x, c->next_y, c->next_z, c->half_w, c->half_l, c->next_heading, c->depth, &c->box_saved);
    c->body.angle = car_heading_to_angle(c->next_heading);
}

/* ---- per frame ---- */

/* Car_StopPhysics 0x40a5b0: back to kinematic: params reloaded, wheel points from the heading */
void car_stop_physics(Car *c)
{
    carphys_reset(c->id);
    int16_t h = c->spr.angle;
    c->physics = 0;
    int hl = c->length >> 1;
    c->front_x = c->spr.x - math_sin(h) * hl;
    c->front_y = c->spr.y - math_cos(h) * hl;
    c->rear_x = math_sin(h) * hl + c->spr.x;
    c->speed = 0;
    c->rear_y = math_cos(h) * hl + c->spr.y;
}

static float fabs_f(float v) { return v < 0 ? -v : v; }

/* Car_Update 0x40a640 */
void car_update(Car *c)
{
    if (c->physics == 0) {
        if (c->spr.x == c->next_x && c->spr.y == c->next_y && c->spr.z == c->next_z &&
            c->spr.angle == c->next_heading && c->falling == 0)
            car_dummy_move(c);
    } else {
        c->thrust_in = 0;
        if (c->control == CAR_CTL_PHYSICS && c->driver > -1 && c->u13a == 0) car_apply_player_controls(c);
        if (c->body.x == 0.0f) {
            c->body.x = (float)(int16_t)(c->spr.x >> 16);
            c->body.y = (float)(int16_t)(c->spr.y >> 16);
            c->body.angle = car_heading_to_angle(c->spr.angle);
        }
        if (c->impulse_state == 2) {
            if (c->impulse_x != 0.0f || c->impulse_y != 0.0f)
                phys_add_force_at_point(&c->body, 0, c->impulse_px, c->impulse_py, c->impulse_x, c->impulse_y);
            c->impulse_state = 0;
        } else if (c->impulse_state == 1) {
            c->impulse_state = 2;
        }
        carphys_step(c);
        bool skip = false;
        if (c->control == CAR_CTL_PHYSICS) {
            c->speed = 0;
        } else if (c->speed == 0) {
            const PhysBody *b = &c->body;
            const double k = 0.1;
            if (fabs_f(b->vx) < k && fabs_f(b->vy) < k && fabs_f(b->ax) < k && fabs_f(b->ay) < k && fabs_f(b->fx) < k &&
                fabs_f(b->fy) < k && fabs_f(b->aw) < k) {
                if (c->impulse_state != 0) skip = true;
                else car_stop_physics(c);
            }
        }
        if (!skip && c->impulse_state == 0) c->impulse_x = c->impulse_y = 0;
    }
    if (c->u244 == 1 && c->driver != -1 && ped_get(c->driver)->ufc == 1) {
        PhysBody *b = &c->body;
        b->vx = b->vy = b->w = 0;
        b->ax = b->ay = b->aw = b->fx = b->fy = b->torque = 0;
        c->speed = 0;
        c->thrust_in = 0;
        c->input = 0;
    }
    car_update_wreck(c);
    if (c->spr.x == c->next_x && c->spr.y == c->next_y && c->spr.z == c->next_z && c->spr.angle == c->next_heading &&
        c->falling == 0) {
        c->speed2 = 0;
        c->speed = 0;
    } else {
        coll_remove(c, c->spr.unk20);
        if (c->physics == 1) {
            int dx = iabs16(c->spr.x - c->next_x) >> 16, dy = iabs16(c->spr.y - c->next_y) >> 16;
            int16_t s = (int16_t)x87_ftol(sqrt((double)(dy * dy + dx * dx)));
            c->speed2 = s;
            c->speed = s;
        }
        coll_compute_bounds(&c->box_saved);
        car_collide_map(6, c);
        car_collide_objects(6, c);
        car_update_ground(c);
        c->spr.x = c->next_x;
        c->body.cx = fixed_px(c->next_x);
        c->spr.y = c->next_y;
        c->spr.z = c->next_z;
        c->body.cy = fixed_px(c->next_y);
        phys_sync_from_centre(&c->body);
        int16_t h = c->next_heading;
        if (c->spr.angle != h && g_cc.kind != -1) c->body.angle = car_heading_to_angle(h);
        c->spr.angle = h;
        coll_build_box(c->spr.x, c->spr.y, c->spr.z, c->half_w, c->half_l, h, c->depth, &c->box_saved);
        car_restore_box(c);
        if ((car_type_cache(c->spr.x, c->spr.y, c->spr.z) & 0x70) == 0x30 && (c->speed > 2 || c->horn_time > 0 || c->horn == 2)) {
            /* on a pavement: peds get out of the way (the front point twice, as in the original) */
            ped_panic_near(c->spr.x, c->spr.y, c->spr.z, c->driver);
            ped_panic_near(c->front_x, c->front_y, c->spr.z, c->driver);
            ped_panic_near(c->front_x, c->front_y, c->spr.z, c->driver);
        }
        coll_insert(COLL_CAR, c->id, c, c->spr.unk20, c->spr.x, c->spr.y);
        c->thrust_in = 0;
    }
    c->road_dirs = heading_dirs(c->front_heading);
    if ((c->info[0xa6] & 2) && c->speed != 0) sprite_toggle_delta(&c->spr, 0xb);
}

/* the player view rects (Player_GetViewRect 0x462c30) with a margin */
static bool in_view(int n, int px, int py, int m)
{
    const ViewRect *r = &g_players[n].rect;
    return r->left - m <= px && r->top - m <= py && px <= r->right + m && py <= r->bottom + m;
}

/* Cars_UpdateAll 0x40adc0 */
void cars_update_all(void)
{
    memset(cs.near_view, 0, sizeof cs.near_view);
    g_cars_active = 0;
    for (int i = 0; i < g_cars_count; i = (int16_t)(i + 1)) {
        Car *c = &g_cars[i];
        if (c->status == -1) continue;
        int px = (int16_t)(c->spr.x >> 16), py = (int16_t)(c->spr.y >> 16);
        c->active = 0;
        bool visible = false;
        for (int n = player_first(); n > -1; n = player_next(n))
            if (in_view(n, px, py, g_players[n].rect.half)) { visible = true; break; }
        int16_t ctl = c->control;
        bool ai = ctl != 0;
        if (ctl == 3) {
            const uint8_t *s = sentinel_get(c->sentinel);
            if (s && s[0x1b] == 1) ai = false;
        }
        if (visible || c->unkec != 0 || c->drive_mode != 0 || ai || c->keep_active != 0) c->active = 1;
        if (c->speed == 0) c->hit_car = (int16_t)i;
        if (ctl != CAR_CTL_PHYSICS && c->horn_time > 0 && c->info[0xa9] < 0x3c) {
            if (c->horn_pattern == -1) c->horn_pattern = (int8_t)((int16_t)math_random() % 10);
            if (c->horn_time < 0x32) c->horn_time--;
            else c->horn_time = 0x31;
        }
        if (c->enter_delay > 0 && --c->enter_delay == 0) c->unk88 = 1;
        if (c->active == 1) {
            g_cars_active++;
            if (c->owner_status == 2) c->speed++;
            else if (c->owner_status == 3) c->owner_status = 1, c->lane_mode = 0, c->lane = 0x20;
            if (c->owner_status != 1 && c->speed > 0) c->input = 0, c->speed--;
            switch (c->bomb) {
            case 2: {
                int16_t t = c->bomb_timer--;
                if (t == 0) {
                    expl_car_explode(i);
                    c->bomb = 0;
                } else if (c->bomb_timer == 0x7c) {
                    mission_show_bomb_timer(0x7d);
                }
                break;
            }
            case 4:
                if (cs.bomb_damage < c->damage) { expl_car_explode(i); c->bomb = 0; }
                break;
            case 5:
                if ((c->max_speed * 3) >> 2 < c->speed) {
                    hud_show_zone_text(exe_str(0x4ab268), 1);   /* "click" */
                    c->bomb = 6;
                }
                break;
            case 6:
                if (c->speed < c->max_speed >> 1) { expl_car_explode(i); c->bomb = 0; }
                break;
            default: break;
            }
            if (cs.tram_doors != 0 && c->model == 9 && c->unkec != 0) {
                if (c->rear_door != 0) sprite_remove_delta(&c->spr, (uint8_t)(c->rear_door + 10));
                int16_t s = ++c->rear_door;
                if (s == 5) c->rear_door = 4;
                sprite_add_delta(&c->spr, (uint8_t)(c->rear_door + 10));
                if (s == 5) cs.tram_doors = 0;
            }
            if (c->status == 8 && c->vtype == 3) {
                sprite_set_frame(&c->spr, c->base_frame + 9);
                c->turret = 0;
            } else if (c->status == 1 && c->vtype == 3 && c->turret == 0) {
                sprite_set_frame(&c->spr, c->base_frame);
            }
            switch (c->control) {
            case 0:
                car_dummy_follow_road(c);
                for (int n = player_first(); n > -1; n = player_next(n))
                    if (in_view(n, (int16_t)(c->spr.x >> 16), (int16_t)(c->spr.y >> 16), 0x140)) cs.near_view[n]++;
                break;
            case 2: case 9: sentinel_drive_car(c); break;
            case 3: if (c->owner_status != 0) sentinel_drive_car(c); break;
            case 10: sentinel_drive_car(c); break;
            case 0x32: hunt_update_car(c); break;
            case -1: case 1: break;
            default: game_fatal(-0x4a, 0x17e, c->control);
            }
            if (c->siren_state == 1 || c->siren_state == 2) {
                if (++c->siren_tick == 8) {
                    c->siren_tick = 0;
                    sprite_remove_delta(&c->spr, (uint8_t)(c->siren_state + 0xe));
                    if (++c->siren_state == 3) c->siren_state = 1;
                    sprite_add_delta(&c->spr, (uint8_t)(c->siren_state + 0xe));
                }
            }
            if (c->active != 0 || c->control == CAR_CTL_PHYSICS) {
                c->frames++;
                if (c->active == 1) {
                    car_dummy_drive(c);
                    car_update(c);
                }
                if (style_requested() != 2 && map_test_block_attr(1, c->spr.x >> 22, c->spr.y >> 22, c->spr.z >> 22) &&
                    c->damage < 100 && c->owner_status != 99) {
                    /* on a railway (not in the third city) */
                    c->damage = (int16_t)(c->damage + 5);
                    if (c->damage > 100) c->damage = 100;
                    if (c->control == CAR_CTL_PHYSICS && c->damage > 0x19)
                        c->thrust = (float)((double)(0x7d - c->damage) * 0.01f * carinfo_float(c->info, 0x80));
                }
                if (c->sinking > 0) {
                    /* in the water: the driver's player is on foot again, splashes, then the car goes */
                    if (c->model != 0x2f)
                        for (int n = player_first(); n > -1; n = player_next(n)) {
                            int16_t d = c->driver;
                            if (player_get_ped(n) == d) {
                                Ped *p = ped_get(d);
                                p->spr.x = c->spr.x, p->spr.y = c->spr.y, p->spr.z = c->spr.z;
                                player_set_controlled(n, 2, c->driver);
                                player_set_view_target(n, 2, c->driver);
                                Snd_PlayVoice(6);
                            }
                        }
                    if (c->sinking != 9) {
                        obj_create(c->spr.x, c->spr.y, c->spr.z + 1, 0x36, c->spr.angle);
                        for (int k = 0; k < 4; k++)
                            if ((car_type_cache(c->box.x[k], c->box.y[k], c->spr.z) & 0x70) == 0x10)
                                obj_create(c->box.x[k], c->box.y[k], c->spr.z + 1, 0x36, c->spr.angle);
                        if (c->sinking != 9) c->sinking = 8;
                    }
                    c->control = 0;
                    ambulance_clear_request(i + 0x26c);
                    if (c->owner_status != 99) {
                        c->damage = 0x65;
                        if (c->control == CAR_CTL_PHYSICS) c->thrust = (float)((double)carinfo_float(c->info, 0x80) * 0.24f);
                    }
                    if (c->model == 0x2f && c->driver > -1) ped_set_health(c->driver, 100);
                    if (c->burning != 0) {
                        obj_delete_by_owner(c->id);
                        c->burning = 0;
                    }
                    car_delete(i);
                }
            }
        }
        if (c->owner_status == 4) c->owner_status = 1;
    }
    if (g_game.opt.debug_text) {
        char buf[64];
        hud_clear_zone_text(99);
        snprintf(buf, sizeof buf, exe_str(0x4ab25c), g_cars_active);   /* "cars: %d" */
        hud_show_zone_text(buf, 99);
    }
    for (int n = player_first(); n > -1; n = player_next(n)) traffic_spawn_around_view(n, cs.near_view[n]);
}

/* Car_SetWheelAngles 0x4090b0 (dummies: the wheel headings follow the steering) */
void car_set_wheel_angles(Car *c, int steer)
{
    int16_t s = (int16_t)steer;
    if (c->speed < 1) {
        c->front_heading &= 0x3ff;
        c->rear_heading &= 0x3ff;
        c->turn_progress = (int16_t)(c->spr.angle & 0x7f);
        return;
    }
    int16_t vt = c->vtype;
    uint16_t h;
    if (vt == 0 || vt == 1 || vt == 8 || vt == 9) {
        if (c->turn_progress == 0 || (uint8_t)c->front_heading != 0 || c->control == 3)
            c->front_heading = (int16_t)((c->spr.angle + s * 4) & 0x3ff);
        if (c->turn_progress == 0) c->prev_dirs = (int16_t)c->road_dirs;
        h = (uint16_t)c->spr.angle;
        c->turn_progress = (int16_t)(h & 0xff);
    } else {
        if (c->turn_progress == 0 || (int8_t)c->front_heading != 0) {
            h = (uint16_t)c->spr.angle;
            c->front_heading = (int16_t)((h + s * 3) & 0x3ff);
        } else {
            h = (uint16_t)c->spr.angle;
            if ((uint8_t)h != 0) c->front_heading = (int16_t)((c->front_heading + s) & 0x3ff);
        }
        if (c->turn_progress == 0) c->prev_dirs = (int16_t)c->road_dirs;
        c->turn_progress = (int16_t)(h & 0xff);
        c->rear_heading = (int16_t)((h - s * 2) & 0x3ff);
        if (c->control != 3 || c->brake != 1 || c->speed < 6) return;
    }
    c->rear_heading = (int16_t)((h - s * 3) & 0x3ff);
}

/* Car_SnapHeading 0x409700 */
void car_snap_road_heading(Car *c)
{
    uint16_t h = (uint16_t)c->front_heading;
    c->road_dirs = heading_dirs((int16_t)h);
    int16_t ctl = c->control;
    if (ctl == CAR_CTL_PHYSICS && c->turn_delta == 0) {
        if ((h & 0xff) < 0x30) c->front_heading = (int16_t)(h & 0xff00);
        else if ((h & 0xff) > 0xd0) c->front_heading = (int16_t)((uint16_t)(h + 0x30) & 0xff00);
        c->front_heading = (int16_t)(c->front_heading & 0x3ff);
        c->next_heading = c->front_heading;
    }
    uint16_t td = (uint16_t)c->turn_delta;
    if (td == 0) return;
    if (c->turn_dirs != (int16_t)c->road_dirs && ctl != CAR_CTL_PHYSICS && ctl != CAR_CTL_HUNT) {
        int k = iabs16((int16_t)td) * 2;
        int fh = c->front_heading;
        if (fh >= 0x200 - k && fh <= k + 0x200) c->front_heading = 0x200;
        else if (fh >= 0x100 - k && fh <= k + 0x100) c->front_heading = 0x100;
        else if (fh >= 0x300 - k && fh <= k + 0x300) c->front_heading = 0x300;
        else if (0x400 - k <= fh || c->front_heading <= k) c->front_heading = 0;
    }
    int16_t rd = (int16_t)c->road_dirs;
    if (c->turn_dirs == rd && c->prev_dirs == rd) return;
    int k = iabs16((int16_t)c->turn_delta);
    int16_t fh = c->front_heading, a = c->spr.angle;
    int lo, hi;
    switch (fh) {
    case 0x200: lo = 0x200 - k, hi = k + 0x200; break;
    case 0x100: lo = 0x100 - k, hi = k + 0x100; break;
    case 0x300: lo = 0x300 - k, hi = k + 0x300; break;
    case 0:
        if (!((a >= 0 && a < k) || (a > 0x400 - k && a <= 0x400))) return;
        goto done;
    default: return;
    }
    if (a <= lo || hi <= a) return;
done:
    c->prev_dirs = rd;
    c->turn_delta = 0;
    c->turn_progress = 0;
}

/* Car_SnapToRoadDir 0x407e30 */
void car_snap_to_road_dir(Car *c)
{
    uint16_t d = car_type_cache(c->spr.x, c->spr.y, c->spr.z) & 0xf;
    if (c->control == 3 && c->road_dirs != d) return;
    int h = road_heading((uint8_t)d);
    if (h < 0) return;
    c->road_dirs = (uint16_t)h;   /* (the heading, not the bits: as in the original) */
    c->next_heading = (int16_t)h;
}

/* the wheel points of a car at rest (Car_DummyMove), headings 0 / 0x100 / 0x200 / 0x300 exactly */
static void rest_wheels(Car *c)
{
    int16_t h = c->spr.angle;
    int32_t hl = c->half_l * 0x10000;
    if ((h & 0x7f) == 0) {
        switch (h) {
        case 0x200: c->front_x = c->spr.x, c->front_y = c->spr.y - hl, c->rear_y = hl + c->spr.y, c->rear_x = c->spr.x; break;
        case 0x100: c->front_x = hl + c->spr.x, c->front_y = c->spr.y, c->rear_x = c->spr.x - hl, c->rear_y = c->spr.y; break;
        case 0x300: c->front_x = c->spr.x - hl, c->front_y = c->spr.y, c->rear_x = hl + c->spr.x, c->rear_y = c->spr.y; break;
        case 0: case 0x400: c->front_x = c->spr.x, c->front_y = hl + c->spr.y, c->rear_x = c->spr.x, c->rear_y = c->spr.y - hl; break;
        default: break;   /* (0x80 multiples: unchanged) */
        }
    } else {
        c->front_x = math_sin(h) * c->half_l + c->spr.x;
        c->rear_x = c->spr.x * 2 - c->front_x;
        c->front_y = math_cos(h) * c->half_l + c->spr.y;
        c->rear_y = c->spr.y * 2 - c->front_y;
    }
}

/* Car_DummyMove 0x409a30: kinematic cars move their wheel points along the wheel headings by the
   speed; the new centre / heading come from the two points */
void car_dummy_move(Car *c)
{
    int16_t steer = c->turn_delta;
    if (c->damage > 0x62 || c->falling != 0) c->turn_delta = 0;
    if (c->speed == 0) {
        c->next_x = c->spr.x;
        c->next_y = c->spr.y;
        float fx = fixed_px(c->spr.x);
        c->next_z = c->spr.z;
        c->next_heading = c->spr.angle;
        c->body.cx = fx;
        c->body.cy = fixed_px(c->spr.y);
        phys_sync_from_centre(&c->body);
        c->body.angle = car_heading_to_angle(c->spr.angle);
        car_save_box(c);
        c->body.vx = 0;
        c->body.vy = 0;
        rest_wheels(c);
    } else {
        rest_wheels(c);
        if (c->turn_delta == 0) car_dummy_keep_lane(c);
        int16_t sp = c->speed;
        if (steer != 0) car_set_wheel_angles(c, steer);
        if (c->unkc0 != 0 || c->brake == 1) {
            if (c->unkc0 == 0 && c->brake == 1) {
                if (c->speed < 0 && c->vtype != 0xd) c->speed++;
            }
            if (c->brake != 1) {
                c->skid = 0;
            } else {
                c->lane_mode = 0;
                if (c->turn_delta == 0 && c->speed > 0) {
                    int16_t s = c->speed;
                    c->speed = (int16_t)(s - 1);
                    if ((int16_t)(s - 1) > 0x14) c->speed = (int16_t)(s - 2);
                    if (c->speed == 0) c->brake = 0;
                }
                if (c->control == 3) {
                    if (c->turn_delta == 0 || c->speed <= c->cruise || (int16_t)c->road_dirs == c->prev_dirs) {
                        c->skid = 0;
                        if (c->speed > 0) c->speed--;
                    } else {
                        c->skid = 21.0f;
                    }
                }
            }
        } else {
            c->skid = 0;
        }
        c->front_heading &= 0x3ff;
        c->rear_heading &= 0x3ff;
        int32_t fx = math_sin(c->front_heading) * sp + c->front_x, fy = math_cos(c->front_heading) * sp + c->front_y;
        int32_t rx = math_sin(c->rear_heading) * sp + c->rear_x, ry = math_cos(c->rear_heading) * sp + c->rear_y;
        uint8_t t = (uint8_t)map_get_type_at(g_game.map, c->spr.x, c->spr.y, c->spr.z);
        if ((t & 0x70) == 0x30 && c->speed > 0) {
            ped_panic_near(fx, fy, c->spr.z, c->driver);
            ped_panic_near(c->spr.x, c->spr.y, c->spr.z, c->driver);
            ped_panic_near(rx, ry, c->spr.z, c->driver);
        }
        if (c->vtype == 3 && c->status == 7) {
            c->input = 0;
            sprite_set_frame(&c->spr, c->base_frame + 7);
            if (c->speed > 0) c->speed--;
            else if (c->speed < 0) c->speed++;
        }
        if (c->spr.z < 0) c->spr.z = c->z_offset;
        else if (c->spr.z > 0x13f0000) c->spr.z = 0x13f0000;
        c->next_y = fy - ((fy - ry) >> 1);
        c->next_x = fx - ((fx - rx) >> 1);
        c->next_z = c->spr.z;
        int h = math_atan2(fy - ry, fx - rx) & 0x3ff;
        c->next_heading = (int16_t)h;
        coll_build_box(c->next_x, c->next_y, c->next_z, c->half_w, c->half_l, h, c->depth, &c->box_saved);
        if (c->spr.x != c->next_x || c->spr.y != c->next_y || c->spr.z != c->next_z || c->spr.angle != c->next_heading ||
            c->falling != 0 || c->turn_delta == 0) {
            c->front_x = fx, c->front_y = fy, c->rear_x = rx, c->rear_y = ry;
            c->rear_heading = c->next_heading;
            car_emit_skidmarks(c);
        }
        c->ua6 = (int16_t)(c->ua6 + c->speed);
        if (c->u124 < 0x40) c->u124 = (int16_t)(c->speed + c->u124);
        c->front_heading = (int16_t)(c->front_heading & 0x3ff);   /* (the high byte & 3) */
        car_snap_road_heading(c);
        if (c->brake == 0) c->rear_heading = c->next_heading;
        c->body.vx = (float)((double)(float)(int16_t)(c->next_x >> 16) - (float)(int16_t)(c->spr.x >> 16));
        c->body.vy = (float)((double)(float)(int16_t)(c->next_y >> 16) - (float)(int16_t)(c->spr.y >> 16));
    }
    if (car_is_near_view(c) && c->turn_delta == 0 && c->lane_mode == 0 && c->damage < 100 && (uint8_t)c->spr.angle != 0 &&
        c->driver != -1)
        car_snap_to_road_dir(c);
}

/* Car_EmitSkidmarks 0x409250: when skidding (> 20) on the ground, tyre marks (object 10) under the
   rear wheels (cars) or the back (bikes), and skid marks (0xb, 0xc after a puddle of oil / 0x11 on
   flagged lids) on roads / pavements / fields */
static int skid_type(int32_t x, int32_t y, int32_t z, uint8_t *ctr)
{
    int t;
    if (*ctr == 0) {
        const MapBlock *b = map_get_block(g_game.map, x >> 22, y >> 22, (z >> 22) + 1);
        t = b && mission_is_flag_set(b->lid) ? 0x11 : 0xb;
    } else {
        t = 0xc;
        (*ctr)--;
    }
    if (coll_is_traffic_object_near(x, y, z)) *ctr = (uint8_t)(crt_rand() % 10 + 0xc);
    return t;
}
static void skid_mark(int32_t x, int32_t y, uint8_t cell, int32_t gz, int32_t z, int type, int angle)
{
    if ((((uint32_t)gz ^ (uint32_t)z) & 0xffc00000u) != 0 || (int8_t)cell < 0) return;
    int k = cell & 0x70;
    if (k == 0x20 || k == 0x30 || k == 0x40) obj_create(x, y, z, type, angle);
}
void car_emit_skidmarks(Car *c)
{
    if (c->skid <= 20.0f || c->falling != 0 || c->model == 0x25) {
        c->skid_l = 0;
        c->skid_r = 0;
        return;
    }
    int hl = c->length >> 1;
    int ang = math_atan2((c->rear_y - math_cos(c->spr.angle) * hl) - c->spr.y, (c->rear_x - math_sin(c->spr.angle) * hl) - c->spr.x);
    const CollBox *b = &c->box_saved;
    if (c->vtype == 3) {
        int32_t sx = b->x[3] + b->x[2], sy = b->y[3] + b->y[2];
        int32_t x = sx >> 1, y = sy >> 1;
        int32_t z = c->next_z + 1;
        if (z > 0x1400000) z = 0x1400000;
        obj_create(x, y, c->next_z, 10, ang);
        int32_t gz = b->gz[2];
        uint8_t cell = car_cache_at(((gz - 1) >> 22) * 0x10000 + (sy >> 23) * 0x100 + (sx >> 23));
        if (cell == 0) return;
        int t;
        if (c->skid_l == 0) {
            const MapBlock *blk = map_get_block(g_game.map, sx >> 23, sy >> 23, (z >> 22) + 1);
            t = blk && mission_is_flag_set(blk->lid) ? 0x11 : 0xb;
        } else {
            t = 0xc;
            c->skid_l--;
        }
        if (coll_is_traffic_object_near(x, y, z)) c->skid_l = (uint8_t)(crt_rand() % 10 + 0xc);
        skid_mark(x, y, cell, gz, z, t, ang);
        return;
    }
    if (c->vtype != 4) return;
    for (int side = 0; side < 2; side++) {
        int a = (c->spr.angle + (side == 0 ? 0x100 : -0x100)) & 0x3ff;
        int w = c->cam_w >> 2;
        int32_t x = math_sin(a) * w + c->rear_x, y = math_cos(a) * w + c->rear_y;
        int32_t z = c->next_z + 1;
        if (z > 0x1400000) z = 0x1400000;
        obj_create(x, y, c->next_z, 10, side == 0 ? ang : c->spr.angle);
        int32_t gz = side == 0 ? b->gz[2] : b->gz[3];
        uint8_t cell = car_cache_at(((gz - 1) >> 22) * 0x10000 + (y >> 22) * 0x100 + (x >> 22));
        if (side == 1 && cell == 0) return;
        int t = skid_type(x, y, z, side == 0 ? &c->skid_r : &c->skid_l);
        skid_mark(x, y, cell, gz, z, t, ang);
    }
}

/* Car_UpdateWreck 0x408af0: at damage 100 (once the delay +0x114 has run out and not sinking) the
   car explodes: the driver dies (thrown out of a convertible), the attacker scores, the burnt-out
   sprite of the vtype; damage becomes 0x65 */
void car_update_wreck(Car *c)
{
    if (c->damage != 100) return;
    if (c->u114 != 0 || c->sinking != 0) {
        c->u114--;
        return;
    }
    car_mark_for_removal(c->id);
    if (c->model != 0x25) sprite_clear_deltas(&c->spr);
    c->turret = 0;
    expl_create(c->spr.x, c->spr.y, c->spr.z, c->player);
    if (c->driver > -1 && c->model != 0x2f) {
        Ped *p = ped_get(c->driver);
        if (c->player > -1) {
            p->u5a = (int16_t)player_get_ped(c->player);
            score_ped_killed(p, 2);
        }
    }
    int16_t drv = c->driver;
    if (c->siren_type != 0 && c->keep_active == 0) {
        c->turn_delta = 0;
        c->input = 0;
        sprite_save_frame(&c->spr);
    }
    if (c->driver != -1) {
        if (c->id < 0) game_fatal(-0xf1, 0x14c, c->id);
        if (g_cars[c->id].info[0xa6] & 1) {
            ped_eject_driver(c);
            ped_set_health(drv, 0);
        }
    }
    if (c->driver > -1 && c->model != 0x2f) {
        ped_set_health(c->driver, 0);
        Ped *p = ped_get(c->driver);
        p->anim = 0x2d;
        p->attach_id = 0;
        p->attach_kind = 0;
        p->spr.z = c->spr.z - 1;
    }
    c->input = 0;
    c->u1e = 0;
    c->damage = 0x65;
    c->u13a = 0;
    c->turn_delta = 0;
    if (c->control != 3) {
        c->control = 0;
        c->owner_status = 0;
    }
    g_car_forced_accel[c->id] = 0;
    if (c->control != 3) c->driver = -1;
    c->speed >>= 2;
    if (c->vtype != 0xe && c->burning == 0) {
        obj_create_animated(c->spr.x, c->spr.y, c->spr.z, 0x12, c->id, 1);
        c->burning++;
        if (c->player > -1 && c->model != 0x2f) {
            int kind;
            switch (c->model) {
            case 4: case 0x20: kind = 0x28; break;
            case 5: kind = 0x29; break;
            case 0x2a: kind = 0x2a; break;
            default: kind = 0x14; break;
            }
            player_award_bonus(c->player, kind, c->spr.x, c->spr.y, c->spr.z, 1, 0);
            police_report_crime(1, player_get_ped(c->player), c->driver < 0 ? 7 : 8, c->spr.x, c->spr.y, c->spr.z);
        }
    }
    if (c->vtype != 3 && c->vtype != 4) {
        expl_create(c->front_x, c->front_y, c->spr.z, c->player);
        expl_create(c->rear_x, c->rear_y, c->spr.z, c->player);
    }
    if (c->model == 0x2c || c->bomb == 4) {
        int q = c->length >> 2;
        expl_create(c->spr.x - math_sin(c->spr.angle) * q, c->spr.y - math_cos(c->spr.angle) * q, c->spr.z, c->player);
        expl_create(math_sin(c->spr.angle) * q + c->spr.x, math_cos(c->spr.angle) * q + c->spr.y, c->spr.z, c->player);
    }
    c->turn_delta = 0;
    c->input = (int16_t)-carinfo_s16(c->info, 0x10);
    int wcar = sprite_group_base(SPRITE_GROUP_WCAR);
    switch (c->vtype) {
    case 0: case 8: case 9: sprite_set_frame(&c->spr, wcar + 3); break;
    case 1: case 4:
        if (c->model == 0x2f) {
            sprite_set_frame(&c->spr, wcar + 2);
        } else {
            sprite_set_frame(&c->spr, cs.wreck_cycle + wcar);
            if (++cs.wreck_cycle > 1) cs.wreck_cycle = 0;
        }
        break;
    case 3:
        if (c->status != 7) {
            sprite_set_frame(&c->spr, wcar + 5);
            c->status = 7;
            ped_eject_driver(c);
        }
        break;
    case 0xd: sprite_set_frame(&c->spr, wcar + 6); break;
    case 0xe:
        sprite_set_frame(&c->spr, wcar + 4);
        sprite_set_remap(&c->spr, 0);
        obj_on_car_wrecked(c);
        return;
    default: game_fatal(-0x4a, 0x177, c->vtype);
    }
    sprite_set_remap(&c->spr, 0);
}

/* ---- state ---- */

/* Car_SetPhysicsControl 0x4082d0 */
void car_set_physics_control(int n)
{
    g_cars[n].control = 1;
    g_cars[n].unkc0 = 1;
}

static void damage_thrust(Car *c, int d)
{
    if (c->control == CAR_CTL_PHYSICS && d > 0x19)
        c->thrust = (float)((double)(0x7d - d) * 0.01f * carinfo_float(c->info, 0x80));
}
/* Car_AddDamage 0x40a200 (capped at 100; nothing for mission-locked cars); past 25 the engine loses
   1% of its thrust per point */
void car_add_damage(Car *c, int n)
{
    if (c->damage < 100 && c->owner_status != 99) {
        int16_t d = (int16_t)(c->damage + (int16_t)n);
        c->damage = d;
        if (d > 100) c->damage = 100;
        damage_thrust(c, c->damage);
    }
}
/* Car_SetDamage 0x40a280 */
void car_set_damage(Car *c, int n)
{
    if (c->owner_status == 99) return;
    c->damage = (int16_t)n;
    damage_thrust(c, (int16_t)n);
}

/* Car_Repair 0x40abd0 */
void car_repair(Car *c)
{
    sprite_set_frame(&c->spr, carinfo_s16(c->info, 6));
    if (c->owner_status != 99) c->damage = 0;
    uint8_t r = c->remap;
    sprite_set_remap(&c->spr, r);
    c->remap = r;
    carphys_reset(c->id);   /* (the original passes no argument: whatever was on the stack) */
}

/* Car_SirenOff 0x40ac30 / Car_SirenOn 0x40ac90 (not tanks: their +0x11a / +0x11c hold objects) */
void car_siren_off(Car *c)
{
    if (c->model == 0x25) return;
    if (c->siren_state == 1 || c->siren_state == 2) {
        sprite_remove_delta(&c->spr, 0xf);
        sprite_remove_delta(&c->spr, 0x10);
        c->siren_state = 0;
    }
    if (c->horn != 0) c->horn = 0;
}
void car_siren_on(Car *c)
{
    if (c->model == 0x25) return;
    if (c->siren_state == 0) {
        c->siren_state = 1;
        c->siren_tick = 0;
    }
    if (c->horn == 0) c->horn = 1;
}

/* Car_ResetSiren99 0x405960: a parked car's alarm goes off when it is hit */
void car_reset_siren99(Car *c)
{
    if (c->siren_state == 99) {
        c->horn = 1;
        c->siren_state = 0;
    }
}

/* Car_HasDoor 0x405990: 0 always; n > 0 if the car info's remap n - 1 (the hls triples at +0x16) isn't
   all zero */
bool car_has_door(const Car *c, int n)
{
    n = (int16_t)n;
    if (n == 0) return true;
    const uint8_t *in = c->info;
    return carinfo_s16(in, 0x10 + n * 6) != 0 || carinfo_s16(in, 0x12 + n * 6) != 0 || carinfo_s16(in, 0x14 + n * 6) != 0;
}

void car_set_remap(Car *c, int remap)        /* Car_SetRemap 0x4059d0 */
{
    sprite_set_remap(&c->spr, remap);
    c->remap = (uint8_t)remap;
}

/* Car_AssignCycleRemap 0x405a00 */
void car_assign_cycle_remap(int n)
{
    Car *c = &g_cars[(int16_t)n];
    car_set_remap(c, cs.remap_cycle == 0 ? 0 : cs.remap_cycle + 6);
    if (++cs.remap_cycle > 6) cs.remap_cycle = 0;
}

/* Car_PlayCrashSound 0x406390: sample 6 slow, 7, 8 fast */
void car_play_crash_sound(const Car *c)
{
    Snd_PlayAt(c->spr.x, c->spr.y, c->spr.z, c->speed > 0x10 ? 8 : c->speed > 6 ? 7 : 6);
}

/* Car_SyncDriverSprite 0x405860: a driver's ped sprite sits on the car (not in convertibles or on
   bikes, where it is drawn) */
void car_sync_driver_sprite(int ped)
{
    if (ped == -1) return;
    Ped *p = ped_get(ped);
    if (p->car == -1) return;
    Car *c = &g_cars[p->car];
    if (c->id < 0) game_fatal(-0xf1, 0x14c, c->id);
    if (!(g_cars[c->id].info[0xa6] & 1) && c->vtype != 3) {
        p->spr.x = c->spr.x, p->spr.y = c->spr.y, p->spr.z = c->spr.z;
    }
}

/* Car_OnDriverEnter 0x407000 (fire pressed in a car): the driver's player; a bomb armed on entry
   starts its timer, a type 3 one ends the game */
void car_on_driver_enter(int n)
{
    Car *c = &g_cars[(int16_t)n];
    c->player = (int16_t)player_find_by_ped(c->driver);
    if (c->bomb == 1) c->bomb = 2;
    else if (c->bomb == 3) event_schedule(1, 0, -n);
}

/* Car_SetDriverById 0x40bc20: a new driver; the engine starts 4 frames later */
void car_set_driver_by_id(int n, int ped)
{
    Car *c = &g_cars[(int16_t)n];
    c->driver = (int16_t)ped;
    if (c->enter_delay == 0 && c->unk88 == 0 && (int16_t)ped != -1) c->enter_delay = 4;
}

/* Car_SetOwnerStatus 0x40bbc0 */
void car_set_owner_status(int n, int v)
{
    Car *c = &g_cars[(int16_t)n];
    c->owner_status = (int16_t)v;
    c->udc = 0;
    c->input = 0;
}

/* CarInfo_IsConvertible 0x40bdb0 */
bool car_info_is_convertible(int n)
{
    if ((int16_t)n < 0) game_fatal(-0xf1, 0x14c, n);
    return g_cars[(int16_t)n].info[0xa6] & 1;
}

/* Car_IsOnScreen 0x40acd0 */
bool car_is_on_screen(const Car *c)
{
    int px = (int16_t)(c->spr.x >> 16), py = (int16_t)(c->spr.y >> 16);
    for (int n = player_first(); n > -1; n = player_next(n))
        if (in_view(n, px, py, g_players[n].rect.half)) return true;
    return false;
}

/* Pos_IsNearAnyView 0x40ad40 */
bool pos_is_near_any_view(int32_t x, int32_t y)
{
    for (int n = player_first(); n > -1; n = player_next(n))
        if (in_view(n, (int16_t)(x >> 16), (int16_t)(y >> 16), 0x280)) return true;
    return false;
}

/* Car_IsNearView 0x409960: near (0x280) some view but inside (0x80) none */
bool car_is_near_view(const Car *c)
{
    int px = (int16_t)(c->spr.x >> 16), py = (int16_t)(c->spr.y >> 16);
    bool near = false;
    for (int n = player_first(); n > -1; n = player_next(n)) {
        if (!in_view(n, px, py, 0x280)) continue;
        if (in_view(n, px, py, 0x80)) return false;
        near = true;
    }
    return near;
}

/* ---- doors ---- */

/* the door steps animate a delta per frame: door 1 deltas 6..9, door 2 and the rear door 11..14 */
static bool open_step(Car *c, int16_t *d, int base, bool rear)
{
    if (*d != 0) sprite_remove_delta(&c->spr, (uint8_t)(*d + base));
    int16_t was = *d;
    if (!rear || was != 4) (*d)++;
    int16_t now = *d;
    if (!rear && now == 5) *d = 4;
    sprite_add_delta(&c->spr, (uint8_t)(*d + base));
    return rear ? was == 4 : now == 5;
}
static bool close_step(Car *c, int16_t *d, int base)
{
    if (*d != 0) {
        sprite_remove_delta(&c->spr, (uint8_t)(*d + base));
        (*d)--;
    }
    if (*d == 0) return true;
    sprite_add_delta(&c->spr, (uint8_t)(*d + base));
    return false;
}
bool car_open_door1_step(int n) { Car *c = &g_cars[(int16_t)n]; return open_step(c, &c->door1, 5, false); }      /* 0x40b8c0 */
bool car_close_door1_step(int n) { Car *c = &g_cars[(int16_t)n]; return close_step(c, &c->door1, 5); }           /* 0x40b940 */
bool car_open_door2_step(int n) { Car *c = &g_cars[(int16_t)n]; return open_step(c, &c->door2, 10, false); }     /* 0x40b9c0 */
bool car_close_door2_step(int n) { Car *c = &g_cars[(int16_t)n]; return close_step(c, &c->door2, 10); }          /* 0x40ba40 */
bool car_open_rear_door_step(int n) { Car *c = &g_cars[(int16_t)n]; return open_step(c, &c->rear_door, 10, true); }   /* 0x40bac0 */
bool car_close_rear_door_step(int n) { Car *c = &g_cars[(int16_t)n]; return close_step(c, &c->rear_door, 10); }       /* 0x40bb40 */

/* the driver's door: car info door 0 (dx across, dy along the car, pixels) rotated by the heading */
void car_get_door_position(const Car *c, int32_t *x, int32_t *y)
{
    int16_t h = c->spr.angle;
    int u = (h + 0x100) & 0x3ff;
    *x = c->spr.x + math_sin(h) * c->door_dy + math_sin(u) * c->door_dx;
    *y = c->spr.y + math_cos(h) * c->door_dy + math_cos(u) * c->door_dx;
}

/* ---- camera, sound ---- */

/* Car_GetCamTarget 0x408220 (one static record, as in the original) */
const CarCamTarget *car_get_cam_target(int n)
{
    static CarCamTarget t;
    const Car *c = &g_cars[(int16_t)n];
    t.x = c->spr.x;
    t.y = c->spr.y;
    t.z = c->spr.z - c->z_offset;
    t.angle = c->spr.angle;
    t.speed = (int16_t)(c->speed * 3);
    t.h = (int16_t)-c->length;
    t.w = c->cam_w;
    return &t;
}

/* the SndCar view of car n for Snd_GatherLoops / Music_UpdateRadio (audio.h) */
void car_fill_snd(int n, SndCar *s)
{
    const Car *c = &g_cars[n];
    memset(s, 0, sizeof *s);
    s->id = c->id;
    s->control = c->control;
    s->status = c->status;
    s->speed = c->speed;
    s->engine_off = c->enter_delay;
    s->engine_on = c->unk88;
    s->damage = c->damage;
    s->burning = c->burning;
    s->fast_pitch = c->falling;
    s->siren = c->horn;
    s->horn_time = c->horn_time;
    s->horn_pattern = c->horn_pattern;
    s->skid_flag = c->handbrake_in;
    s->b147 = c->gear;
    s->brake_peak = c->brake_peak;
    s->skid = c->skid;
    s->x = c->spr.x, s->y = c->spr.y, s->z = c->spr.z;
    if (c->info && c->status != -1) {
        s->convertible = c->info[0xa6];
        s->engine = c->info[0xa7];
        s->radio = c->info[0xa8];
        s->horn = c->info[0xa9];
        s->sound_fn = c->info[0xaa];
        s->fast_change = c->info[0xab];
    }
}
