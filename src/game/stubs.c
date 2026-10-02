/* Stubs for the subsystems not ported yet (see stubs.h). Each one says what the original does. */
#include "stubs.h"
#include "../render/sprite.h"
#include "coll.h"
#include "game.h"
#include "obj.h"
#include "ped.h"
#include "mission_obj.h"
#include "route.h"
#include <string.h>

int stub_calls[STUB_COUNT];

void stubs_reset(void)
{
    memset(stub_calls, 0, sizeof stub_calls);
}

/* ---- Game_Run ---- */
void tune_load_file(const char *name) { (void)name; }        /* optional ..\gtadata\config.ini */
bool net_reset_sync(void) { return true; }                    /* single player: always starts */
void net_unk_44b900(void) {}
/* net_end_game (Net_EndGame 0x412d00): stubbed by the frontend, src/front/front_net.c */
void net_sync_frame_inputs(uint32_t *controls) { (void)controls; }   /* single player: nothing to exchange */
/* 640x480, the format of PIXFMT_32. Gfx_SelectMode ends in HUD_LoadFonts with res 2 for modes taller
   than 400 lines (Gfx_SetVideoMode 0x414db0 calls HUD_SetViewSize first). */
void gfx_select_mode(void)
{
    int w = g_game.screen_w ? g_game.screen_w : 640, h = g_game.screen_h ? g_game.screen_h : 480;
    hud_set_view_size(w, h);
    hud_load_fonts(h > 400 ? 2 : 1);
}
/* one mode, 640x480x32 (the original lists what DirectDraw offers, names "w x h x bits" via ostream) */
static const GfxModeEntry gfx_modes[1] = { { "640x480x32", 0, { 0 }, NULL } };
static const uint8_t gfx_per_column[3] = { 1, 0, 0 };
void gfx_get_mode_lists(const void **modes, int *count, int *columns, const uint8_t **per_column)
{
    *modes = gfx_modes, *count = 1, *columns = 3, *per_column = gfx_per_column;
}
int gfx_get_mode_index(void) { return 0; }
void gfx_select_mode_index(int i) { (void)i; gfx_select_mode(); }

/* ---- Game_Init / Game_Shutdown ---- */
void heli_init(void) {}
void lights_init(void) {}                                     /* junctions and their light sprites */
/* Sentinel_InitAll 0x41abd0: sentinels, ped requests, junction overrides (not ported); the location
   counts it also takes are ported (route_init_locations). */
void sentinel_init_all(void) { route_init_locations(); }
void path_reset(void) {}
void train_init_all(void) {}
void fire_init(void) {}
void expl_init(void) {}
void blockanim_reset(void) {}
void hunt_init(void) {}
void powerup_init_all(void) {}                                /* clears the 256 power-ups at 0x74f858 */

/* ---- the frame ---- */
void heli_update(void) {}
int train_update_all(void) { return 20; }
int lights_update(void) { return 20; }
void junction_update_override_timers(void) {}
void obj_update_all(void) {}
void emergency_update_all(void) {}
void expl_update_all(void) {}
void blockanim_tick(void) {}

/* ---- Game_HandleKey ---- */
void net_build_chat_prefix(int to) { (void)to; }

/* ---- events ---- */

/* ---- Mission_Load ---- */
/* Traffic_PrimeCarPool 0x418f00: creates and deletes n cars of the traffic model row at block (1, 1, 1)
   with the sound suspended, pre-allocating their slots. */
void traffic_prime_car_pool(int n) { (void)n; stub_calls[STUB_TRAFFIC_PRIME]++; }

void gang_add_car(int car) { (void)car; }

void police_update_criminal_target(int a, int car, int b, int ped) { (void)a, (void)car, (void)b, (void)ped; }

void heli_set_exit_target(int32_t x, int32_t y) { (void)x, (void)y; }

/* ---- objects ---- */
void fire_register(int obj) { (void)obj; }

/* ---- player module (the accessors as in the original; the rest does nothing yet) ---- */
#include "player.h"
/* Player_AddScore 0x461f20: score += points * multiplier (with the HUD and frenzy cases): not ported. */

/* ---- other subsystems the mission interpreter calls ---- */
bool car_is_marked_for_removal(int car) { (void)car; return false; }
void ambulance_clear_request(int id) { (void)id; }
void heli_spawn(int32_t x, int32_t y, int32_t z, int size, int32_t tx, int32_t ty, int32_t tz)
{
    (void)x, (void)y, (void)z, (void)size, (void)tx, (void)ty, (void)tz;
}
void police_report_crime(int a, int id, int kind, int32_t x, int32_t y, int32_t z)
{
    (void)a, (void)id, (void)kind, (void)x, (void)y, (void)z;
}
void hunt_remove(int ped) { (void)ped; }
int hunt_add_block_target(int ped, int bx, int by, int bz) { (void)ped, (void)bx, (void)by, (void)bz; return 0; }
int hunt_add_block_target2(int ped, int bx, int by, int bz) { (void)ped, (void)bx, (void)by, (void)bz; return 0; }
void powerup_add(int type, int param, int32_t x, int32_t y, int32_t z) { (void)type, (void)param, (void)x, (void)y, (void)z; }
void powerup_remove_at(int32_t x, int32_t y) { (void)x, (void)y; }

/* ---- what the mission helpers (mission_obj.c, dummy.c) call ---- */
void expl_create(int32_t x, int32_t y, int32_t z, int owner) { (void)x, (void)y, (void)z, (void)owner; }
void expl_damage_area(int32_t x, int32_t y, int32_t r, int owner) { (void)x, (void)y, (void)r, (void)owner; }
int obj_create_animated(int32_t x, int32_t y, int32_t z, int type, int owner, int angle)
{
    (void)x, (void)y, (void)z, (void)type, (void)owner, (void)angle;
    return -1;
}
void obj_kick(int32_t x, int32_t y, int obj, int kind, int angle) { (void)x, (void)y, (void)obj, (void)kind, (void)angle; }
void obj_set_state(int obj, int state) { obj_get(obj)->state = (int16_t)state; }
void ambulance_cancel_for_ped(int ped) { (void)ped; }
void hunt_add_car_target(int car, int target, int mode) { (void)car, (void)target, (void)mode; }
int train_get_count(void) { return 0; }
uint8_t *train_get(int i) { static uint8_t rec[0x5c8]; (void)i; return rec; }
void train_crash(int train, int mode) { (void)train, (void)mode; }
int sentinel_find_free(void) { return -1; }
uint8_t *sentinel_get(int i) { static uint8_t rec[0x98]; (void)i; return rec; }
void sentinel_override_lights(uint8_t *rec, Car *c) { (void)rec, (void)c; }
int map_find_nearest_road(uint8_t out[8]) { (void)out; return 0; }
int path_find(int x, int y, int z, int dx, int dy, int dz, int mode, int ctrl)
{
    (void)x, (void)y, (void)z, (void)dx, (void)dy, (void)dz, (void)mode, (void)ctrl;
    return 0;
}
int16_t g_path_owner = -1;
int16_t g_path_result;
void front_get_multi_target(uint8_t *kind, uint8_t *value) { *kind = 0, *value = 0; }
void map_set_block_face(int x, int y, int z, int face, int tile) { (void)x, (void)y, (void)z, (void)face, (void)tile; }
void map_set_block_type(int x, int y, int z, uint32_t info) { (void)x, (void)y, (void)z, (void)info; }
int police_find_criminal_by_ped(int ped) { (void)ped; return -1; }
void police_clear_criminal(int i) { (void)i; }
void police_spawn_patrol_cars(void) {}
void police_init_criminals(void) {}
void police_init_pursuits(void) {}
int32_t g_police_no_patrols;

/* ---- for the MissionOp_* handlers (mission_ops.c) ---- */
void camera_start_transition(int n) { (void)n; }
bool powerup_exists_at(int32_t x, int32_t y) { (void)x, (void)y; return false; }
bool train_any_wrecked(void) { return false; }
int ambulance_busy_sentinel(void) { return -1; }
bool obj_is_on_screen(const Obj *o) { (void)o; return false; }

/* ---- what the player / ped modules call ---- */
int train_command(int cmd, int train) { (void)cmd, (void)train; return 0; }
int train_is_boarded(int train) { (void)train; return 0; }
void car_set_horn_by_id(int car, int on) { (void)car, (void)on; }
void police_show_criminal_record(void) {}

/* ---- what the mission runtime objects (trigger.c) call ---- */
static int blockanim_next;   /* 0x4bbc68 */
int blockanim_create(int x, int y, int z, int face) { (void)x, (void)y, (void)z, (void)face; return blockanim_next++ & 0x3f; }
void blockanim_set_tile(int a, int mode, int tile) { (void)a, (void)mode, (void)tile; }
int blockanim_get_tile_slot(int a) { return a; }
void blockanim_start_forward(int a, int n, int frames, int mode, int tile) { (void)a, (void)n, (void)frames, (void)mode, (void)tile; }
void blockanim_start_reverse(int a, int n, int frames, int mode, int tile) { (void)a, (void)n, (void)frames, (void)mode, (void)tile; }
void blockanim_set_event(int a, int type, int arg) { (void)a, (void)type, (void)arg; }
void map_set_block_kind(int x, int y, int z, int kind) { (void)x, (void)y, (void)z, (void)kind; }
void map_or_block_flags(int x, int y, int z, int flags) { (void)x, (void)y, (void)z, (void)flags; }
void gang_update(void) {}
int player_get_view_id(int n) { return (int16_t)g_players[n].view_id; }   /* 0x462fa0 */
int player_get_multiplier(int n) { return (int16_t)g_players[(int16_t)n].mult; }   /* 0x463090 */
void player_award_bonus(int n, int kind, int32_t x, int32_t y, int32_t z, int a, int cause)
{
    (void)n, (void)kind, (void)x, (void)y, (void)z, (void)a, (void)cause;
}
void powerup_collect(int player, int32_t x, int32_t y, int how) { (void)player, (void)x, (void)y, (void)how; }
int lights_query(int what, int bx, int by) { (void)what, (void)bx, (void)by; return 0; }   /* (no lights: peds may cross) */
/* Police_CopsForWanted 0x4131d0: wanted level 1-2 -> 1, 3-4 -> 2, else 0 (no side effects) */
int police_cops_for_wanted(int player)
{
    int l = g_players[player < 0 ? 0 : player].wanted_level;
    return l == 1 || l == 2 ? 1 : l == 3 || l == 4 ? 2 : 0;
}
void obj_delete_wrapper(int obj) { obj_delete(obj); }   /* a thunk to Obj_Delete */
/* Map_GetLidBelow 0x4387b0: the lid tile of block (x, y, (z >> 22) + 1) (16.16), 0 above the column */
int map_get_lid_below(int32_t x, int32_t y, int32_t z)
{
    const Map *m = g_game.map;
    const int16_t *col = map_column(m, x >> 22 & 0xff, y >> 22 & 0xff);
    int l = (z >> 22) + 1;
    if (col[0] > l) return 0;
    return m->blocks[col[l - col[0] + 1]].lid;
}
/* Map_TestBlockAttr 0x44b310: a block's attributes (the last block's type map is cached, 0x6b3ea8):
   1 railway, 2 crossing (lights bits = 1), 3 road / 6 / 7 types, 4 type map without the type bits,
   5 the type map's high bits, 6 the lights bits, 7 lights 4 / 5 (the value), 8 lights 2, 9 pavement. Outside the
   map 0. */
int map_test_block_attr(int what, int bx, int by, int bz)
{
    static int16_t cx = -1, cy = -1, cz = -1;   /* 0x4b1de4.. */
    static uint32_t t;                          /* 0x6b3ea8 */
    bx = (int16_t)bx, by = (int16_t)by, bz = (int16_t)bz;
    if (bx < 0 || bx > 0xff || by < 0 || by > 0xff || bz < 0 || bz > 5) return 0;
    if (bx != cx || by != cy || bz != cz) {
        t = map_get_type_map(g_game.map, bx, by, bz);
        cx = (int16_t)bx, cy = (int16_t)by, cz = (int16_t)bz;
    }
    int k;
    switch ((int16_t)what) {
    case 1: return (t & 0x800000) != 0;
    case 2: return (t >> 16 & 7) == 1;
    case 3: k = t >> 4 & 7; return k == 2 || k == 6 || k == 7;
    case 4: return (int)(t & 0xffffff0f);
    case 5: return (int)(t >> 8 & 0xffff3f);
    case 6: return (int)(t >> 16 & 7);
    case 7: k = t >> 16 & 7; return k == 5 || k == 4 ? k : 0;
    case 8: return (t >> 16 & 7) == 2;
    case 9: return (t >> 4 & 7) == 3;
    }
    return 0;
}
int obj_create_attached(int owner, int kind, int dx, int dy, int type) { (void)owner, (void)kind, (void)dx, (void)dy, (void)type; return -1; }
void ambulance_request_for_ped(int ped) { (void)ped; }

/* ---- what the car module (car.c, carcoll.c) calls ---- */
void car_dummy_follow_road(Car *c) { (void)c; }
void car_dummy_drive(Car *c) { (void)c; }
void car_dummy_keep_lane(Car *c) { (void)c; }
void sentinel_drive_car(Car *c) { (void)c; }
void hunt_update_car(Car *c) { (void)c; }
void traffic_spawn_around_view(int player, int near) { (void)player, (void)near; }
void expl_car_explode(int car) { (void)car; }
void car_mark_for_removal(int car) { (void)car; }
void obj_list_rotate(void) {}
void obj_remove_moving(int obj) { (void)obj; }
void obj_delete_by_owner(int owner) { (void)owner; }
void fire_clear_objects(int car) { (void)car; }
void obj_on_car_wrecked(Car *c) { (void)c; }
void powerup_reveal(int32_t x, int32_t y) { (void)x, (void)y; }
void car_fire_rocket(Car *c) { (void)c; }
/* ---- what the projectiles (proj.c) call ---- */
bool expl_at_face_if_solid(int bx, int by, int bz, int face, int player) { (void)bx, (void)by, (void)bz, (void)face, (void)player; return false; }

int train_check_platform_sides(int train) { (void)train; return -1; }   /* (no trains) */
int train_get_door_offsets(int train) { (void)train; return 0; }
void train_update_door_sprites(int train) { (void)train; }
void train_load_passengers(int train, int ped) { (void)train, (void)ped; }
void cop_dismiss(Car *c, int ped) { (void)c, (void)ped; }
int fire_has_objects(int car) { (void)car; return 0; }
