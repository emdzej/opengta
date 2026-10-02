/* Stubs for the subsystems not ported yet (see stubs.h). Each one says what the original does. */
#include "stubs.h"
#include "../render/sprite.h"
#include "coll.h"
#include "game.h"
#include "obj.h"
#include "ped.h"
#include "route.h"
#include <string.h>

int stub_calls[STUB_COUNT];
static int trigger_next, door_next, crane_next, carlist_next;   /* 0x7710f8, 0x7710f0, 0x771108, 0x773c20 */

void stubs_reset(void)
{
    memset(stub_calls, 0, sizeof stub_calls);
    trigger_next = door_next = crane_next = carlist_next = 0;   /* Mission_InitCityTables resets them */
}

/* ---- Game_Run ---- */
void tune_load_file(const char *name) { (void)name; }        /* optional ..\gtadata\config.ini */
bool net_reset_sync(void) { return true; }                    /* single player: always starts */
void net_unk_44b900(void) {}
/* net_end_game (Net_EndGame 0x412d00): stubbed by the frontend, src/front/front_net.c */
void net_sync_frame_inputs(uint32_t *controls) { (void)controls; }   /* single player: nothing to exchange */
uint32_t (*stub_controls)(void);
uint32_t input_read_controls(void) { return stub_controls ? stub_controls() : 0; }   /* keyboard / joystick / replay */
void player_apply_input(uint32_t control) { (void)control; }
void gfx_select_mode(void) {}                                 /* 640x480, the format of PIXFMT_32 */

/* ---- Game_Init / Game_Shutdown ---- */
void replay_begin(void) {}
void replay_end_save(void) {}
void replay_tick_frame(void) {}
bool replay_is_playing(void) { return false; }
void hud_init(void) {}
void hud_free_fonts(void) {}
void heli_init(void) {}
void lights_init(void) {}                                     /* junctions and their light sprites */
/* Sentinel_InitAll 0x41abd0: sentinels, ped requests, junction overrides (not ported); the location
   counts it also takes are ported (route_init_locations). */
void sentinel_init_all(void) { route_init_locations(); }
void path_reset(void) {}
void train_init_all(void) {}
void proj_reset(void) {}
void fire_init(void) {}
void expl_init(void) {}
void blockanim_reset(void) {}
void hunt_init(void) {}
void powerup_init_all(void) {}                                /* clears the 256 power-ups at 0x74f858 */
void area_localize_names(void) {}                             /* nav zone names through the FXT */

/* ---- the frame ---- */
void hud_tick_big_message(void) {}
void hud_clear_zone_text(int zone) { (void)zone; }
void hud_show_zone_text(const char *s, int zone) { (void)s, (void)zone; }
void hud_update(void) {}
void hud_draw(void) {}
void cars_update_all(void) {}
void heli_update(void) {}
void ped_update_all(void) {}
int train_update_all(void) { return 20; }
int lights_update(void) { return 20; }
void junction_update_override_timers(void) {}
void obj_update_all(void) {}
void emergency_update_all(void) {}
void expl_update_all(void) {}
void blockanim_tick(void) {}
void mission_update(void) {}
void player_update_all(void) {}

/* ---- Game_HandleKey ---- */
bool hud_handle_key(int key) { (void)key; return false; }
void hud_toggle_video_menu(void) {}
void hud_toggle_quit_prompt(void) {}
void hud_toggle_debug(void) {}
void hud_pause_on(void) {}
void hud_pause_off(void) {}
void hud_restore_subtitle(void) {}
void hud_refresh_zone(void) {}
void pager_resume(void) {}
void net_build_chat_prefix(int to) { (void)to; }
void player_add_ammo(int player, int weapon, int n) { (void)player, (void)weapon, (void)n; }

/* ---- events ---- */
void mission_on_brief_done(int arg) { (void)arg; }
void door_on_closed(int door) { (void)door; }
void door_on_opened(int door) { (void)door; }
void trigger_reset(int trigger) { (void)trigger; }

/* ---- Mission_Load ---- */
void mission_init_city_tables(void) { trigger_next = door_next = crane_next = carlist_next = 0; }
void dummy_init_groups(void) {}
void mission_reset_lists(void) {}
void mission_set_var505efa(int v) { (void)v; }
/* Traffic_PrimeCarPool 0x418f00: creates and deletes n cars of the traffic model row at block (1, 1, 1)
   with the sound suspended, pre-allocating their slots. */
void traffic_prime_car_pool(int n) { (void)n; stub_calls[STUB_TRAFFIC_PRIME]++; }
void police_init_for_mission(void) {}
void ped_remove_dummies_at_block(int x, int y, int z, int a) { (void)x, (void)y, (void)z, (void)a; }
int car_clear_for_car(int x, int y, int z, int a, int b, int c)
{
    (void)x, (void)y, (void)z, (void)a, (void)b, (void)c;
    stub_calls[STUB_CLEAR_BLOCK]++;
    return 0;
}

/* The car slot Car_SpawnEx 0x4078d0 takes: the first free one (status -1, not reserved by +0x139)
   whose driver slot ped is unused (control -1, or dead and not in state 0xc). The original also skips
   cars a player drives or views (no player owns a car before the level starts). */
static int car_alloc(void)
{
    for (int i = 0; i < CAR_MAX; i++) {
        const Car *c = &g_cars[i];
        const Ped *p = ped_get(i + PED_DRIVER_FIRST);
        if (c->status == -1 && c->unk139 == 0 &&
            (p->control == -1 || (p->health == 0 && p->anim == 0 && p->state != 0xc)))
            return i;
    }
    return CAR_MAX;
}

/* The sprite group of a car info vtype (CarInfo_Setup 0x40c100 rebases the sprite numbers by it). */
static int vtype_group(int vt)
{
    switch (vt) {
    case 0: case 2: return SPRITE_GROUP_BUS;
    case 3: return SPRITE_GROUP_BIKE;
    case 8: return SPRITE_GROUP_TRAIN;
    case 9: return SPRITE_GROUP_TRAM;
    case 13: return SPRITE_GROUP_BOAT;
    case 14: return SPRITE_GROUP_TANK;
    default: return SPRITE_GROUP_CAR;
    }
}

/* Placeholder for Car_Init 0x4067c0: id, model, info, size, the sprite (frame of the car info, car
   palette of the record), the grid. Nothing of the physics or the collision box. */
static void car_init_stub(int i, int32_t x, int32_t y, int32_t z, int angle, int model)
{
    Car *c = &g_cars[i];
    const uint8_t *info = car_info_of_model(model);
    memset(c, 0, sizeof *c);
    c->id = (int16_t)i;
    c->status = 0;
    c->model = (int16_t)model;
    c->info = info;
    c->spr.unk20[0] = c->spr.unk20[1] = -1;
    int frame = 0;
    if (info) {
        c->vtype = info[0x6a];
        c->half_w = (int16_t)((info[0] | info[1] << 8) / 2);
        c->half_l = (int16_t)((info[2] | info[3] << 8) / 2);
        c->length = (int16_t)(info[2] | info[3] << 8);
        c->cam_w = (int16_t)(info[0] | info[1] << 8);
        frame = (int16_t)(info[6] | info[7] << 8) + sprite_group_base(vtype_group(c->vtype));
    }
    sprite_init(&c->spr, x, y, z, angle & 0x3ff, frame);
    if (info) sprite_set_palette(&c->spr, sprite_car_palette(g_car_model_index[model]));
    coll_insert(COLL_CAR, i, c, c->spr.unk20, x, y);
}

static int spawn(int32_t x, int32_t y, int32_t z, int model, int driver, int angle, int remap, bool ground)
{
    stub_calls[STUB_CAR_SPAWN]++;
    int i = car_alloc();
    model = (int16_t)model;
    if (model < 0 || model >= CAR_MODELS || g_car_model_index[model] == -1) i = CAR_MAX;
    if (i >= CAR_MAX) return -1;
    if (model == 99) model = 7;
    if (ground) z = map_get_ground_z(g_game.map, x, y, z);
    car_init_stub(i, x, y, z, angle, model);
    Car *c = &g_cars[i];
    if (driver == 1) {
        c->driver = (int16_t)(i + PED_DRIVER_FIRST);
        Ped *p = ped_get(i + PED_DRIVER_FIRST);
        p->car = (int16_t)i;
        p->health = 100;
    } else {
        c->driver = -1;
    }
    c->owner_status = (int16_t)driver;
    c->siren_state = 0;
    if (i == g_cars_count) g_cars_count++;
    sprite_set_remap(&c->spr, remap);
    c->remap = (uint8_t)remap;
    return i;
}
int car_spawn_ex(int32_t x, int32_t y, int32_t z, int model, int driver, int angle, int remap)
{
    return spawn(x, y, z, model, driver, angle, remap, false);
}
int car_spawn_ex_on_ground(int32_t x, int32_t y, int32_t z, int model, int driver, int angle, int remap)
{
    return spawn(x, y, z, model, driver, angle, remap, true);
}
/* thunk_Car_SpawnParked 0x4763d0: model 0x2f on a road block facing along its direction bits. */
int car_spawn_parked(int bx, int by, int bz)
{
    (void)bx, (void)by, (void)bz;
    stub_calls[STUB_CAR_SPAWN]++;
    return -1;
}
int mis_car_create_at(int32_t x, int32_t y, int bz, int model, int angle, int remap)
{
    return spawn(x, y, bz * 0x400000 - 0x20000, model, 0, angle, remap, true);
}
void mis_car_set_flag9c(int car, int v) { (void)car, (void)v; }
void mis_car_set_drive_mode1(int car) { (void)car; }
void mis_car_set_drive_mode2(int car) { (void)car; }
void ped_create_car_driver(Car *c) { (void)c; }
void gang_add_car(int car) { (void)car; }
void ped_send_to_car_door1(Ped *p, int car) { (void)p, (void)car; }

/* Mission_PedCreate_*: Ped_Create 0x453e90 at pixel coordinates (z - 2 pixels) and the sub-type's
   state. The placeholder takes the first free ambient slot (0..199) and spawns it standing. */
int mission_ped_create(int kind, int x, int y, int z, int angle, int arg)
{
    (void)kind, (void)arg;
    stub_calls[STUB_PED_CREATE]++;
    for (int i = 0; i < PED_DRIVER_FIRST; i++)
        if (ped_get(i)->anim == 0) {
            ped_spawn_in_slot(x << 16, y << 16, (z - 2) * 0x10000, 0, angle, 1, i, -1);
            return i;
        }
    return -1;
}
bool car_info_is_convertible(int car)
{
    const uint8_t *info = car >= 0 && car < CAR_MAX ? g_cars[car].info : NULL;
    return info && (info[0xa6] & 1);   /* car info +0xa6 bit 0 (CarInfo_IsConvertible 0x40bdb0) */
}
void player_enter_car(int ped, int car) { (void)ped, (void)car; }
void carphys_begin(int car) { (void)car; }
void ped_update_sprite(int ped) { (void)ped; }
void police_update_criminal_target(int a, int car, int b, int ped) { (void)a, (void)car, (void)b, (void)ped; }
int mission_map_door_type(int t) { return t; }

/* The mission runtime creators return the index of the record they fill (a running count). */
int door_create(int x, int y, int z, int a, int b, int c, int d, int e, int persistent)
{
    (void)x, (void)y, (void)z, (void)a, (void)b, (void)c, (void)d, (void)e, (void)persistent;
    stub_calls[STUB_DOOR]++;
    return door_next++;
}
void door_set_open_any(int door, int v) { (void)door, (void)v; }
void door_lock(int door) { (void)door; }
void door_set_open_mode3(int door, int a, int b) { (void)door, (void)a, (void)b; }
void door_set_open_mode5(int door, int a, int b) { (void)door, (void)a, (void)b; }
void door_set_open_by_car(int door, int a, int b, int c) { (void)door, (void)a, (void)b, (void)c; }
/* Crane_Create 0x475620: its object (type 0x1e at pixel coordinates) is created for real. */
int crane_create(int x, int y, int z, int dir)
{
    (void)dir;
    stub_calls[STUB_CRANE]++;
    obj_create(x << 16, y << 16, z * 0x10000 - 1, 0x1e, 0);
    return crane_next++;
}
int trigger_create(int x, int y, int z, int type, int a, int b, int c, int persistent)
{
    (void)x, (void)y, (void)z, (void)type, (void)a, (void)b, (void)c, (void)persistent;
    stub_calls[STUB_TRIGGER]++;
    return trigger_next++;
}
void trigger_set_param14(int trigger, int v) { (void)trigger, (void)v; }
void mission_set_target_order(int trigger) { (void)trigger; }
void car_trig_add(void) {}
int car_list_add(int a, int model, int remap, int count)
{
    (void)a, (void)model, (void)remap, (void)count;
    stub_calls[STUB_CARLIST]++;
    return carlist_next++;
}
void heli_set_exit_target(int32_t x, int32_t y) { (void)x, (void)y; }

/* ---- objects ---- */
void fire_register(int obj) { (void)obj; }
