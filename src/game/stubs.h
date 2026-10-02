/* Every call from the game core into a subsystem that is not ported yet, in one place (stubs.c), with
   the original's name and address. A port replaces its stubs by defining the same functions in its
   own file and deleting them here. A stub that returns something returns what the original's contract
   needs (a running index for the creators that hand out table slots, 20 for the update functions
   whose result Game_Update checks). Each stub counts its calls (stub_calls) so tests can see what a
   level start asked for. */
#pragma once
#include "car.h"
#include "carcoll.h"
#include "obj.h"
#include "ped.h"
#include <stdbool.h>
#include <stdint.h>

/* call counters, indexed by STUB_* */
enum {
    STUB_CAR_SPAWN, STUB_PED_CREATE, STUB_TRIGGER, STUB_DOOR, STUB_CRANE, STUB_CARLIST,
    STUB_TRAFFIC_PRIME, STUB_CLEAR_BLOCK, STUB_COUNT
};
extern int stub_calls[STUB_COUNT];
void stubs_reset(void);                      /* the per-level counters (and the creators' indices) */

/* ---- Game_Run 0x4148a0 ---- */
void tune_load_file(const char *name);       /* Tune_LoadFile 0x412d20: ..\gtadata\config.ini car tuning */
bool net_reset_sync(void);                   /* Net_ResetSync 0x44bd30 (true: start) */
void net_unk_44b900(void);                   /* Net_unk_0044b900 0x44b900 */
void net_end_game(void);                     /* Net_EndGame 0x412d00 (stub in src/front/front_net.c) */
void net_sync_frame_inputs(uint32_t *controls);   /* Net_SyncFrameInputs 0x44b930 */
void gfx_select_mode(void);                  /* Gfx_GetModeIndex 0x414d30 / Gfx_SelectMode 0x414cc0, Style_ConvertPalettes */
/* the mode list of the in-game video menu (0x504cd0: 0x1c-byte records {name[0x14], column, .., mode}) */
typedef struct { char name[0x14]; uint8_t column; uint8_t pad[3]; const void *mode; } GfxModeEntry;
/* Gfx_GetModeLists 0x414c20: the records, their count, the column count (3) and the modes per column */
void gfx_get_mode_lists(const void **modes, int *count, int *columns, const uint8_t **per_column);
int gfx_get_mode_index(void);                /* Gfx_GetModeIndex 0x414d30 (0x503224) */
void gfx_select_mode_index(int i);           /* Gfx_SelectMode 0x414cc0 by list index (with HUD_LoadFonts) */

/* ---- Game_Init 0x430a20 / Game_Shutdown 0x430b10 ---- */
void heli_init(void);                        /* Heli_Init 0x40dcc0 */
void lights_init(void);                      /* Lights_Init 0x47dcf0 */
void sentinel_init_all(void);                /* Sentinel_InitAll 0x41abd0 (the location counts are ported: route.c) */
void path_reset(void);                       /* Path_Reset 0x46e9e0 */
void train_init_all(void);                   /* Train_InitAll 0x46a9a0 */
void fire_init(void);                        /* Fire_Init 0x42e600 */
void expl_init(void);                        /* Expl_Init 0x425c50 */
void blockanim_reset(void);                  /* BlockAnim_Reset 0x402240 */
void hunt_init(void);                        /* Hunt_Init 0x4317f0 */
void powerup_init_all(void);                 /* PowerUp_InitAll 0x46a0a0 */

/* ---- Game_Frame / Game_Update / Game_Render ---- */
void heli_update(void);                      /* Heli_Update 0x40dfb0 */
int train_update_all(void);                  /* Train_UpdateAll 0x46a9d0 (20 = ok) */
int lights_update(void);                     /* Lights_Update 0x47e420 (20 = ok) */
void junction_update_override_timers(void);  /* Junction_UpdateOverrideTimers 0x41e140 */
void obj_update_all(void);                   /* Obj_UpdateAll 0x44d790 */
void emergency_update_all(void);             /* Emergency_UpdateAll 0x419880 */
void expl_update_all(void);                  /* Expl_UpdateAll 0x425b60 */
void blockanim_tick(void);                   /* BlockAnim_Tick 0x402580 */

/* ---- Game_HandleKey 0x430dc0 ---- */
void net_build_chat_prefix(int to);          /* Net_BuildChatPrefix 0x44c1b0 */

/* ---- events ---- */

/* ---- Mission_Load 0x445800 ---- */
void traffic_prime_car_pool(int n);          /* Traffic_PrimeCarPool 0x418f00 */
void gang_add_car(int car);                  /* Gang_AddCar 0x431530 */
void police_update_criminal_target(int a, int car, int b, int ped);   /* Police_UpdateCriminalTarget 0x4132c0 */
void heli_set_exit_target(int32_t x, int32_t y);   /* Heli_SetExitTarget 0x40dd10 */

/* ---- objects ---- */
void fire_register(int obj);                 /* Fire_Register 0x42ef80 */

/* ---- player module (0x4616b0-0x464900) as the mission runtime uses it (the accessors are exact) ---- */
/* Player_GetControlledPos 0x462ef0: the 16.16 x, y, z of what the player controls (car / ped sprite) */

/* ---- HUD and pager: ported (src/hud/hud.h, included here for the callers of the old stubs) ---- */
#include "../hud/hud.h"
/* ---- other subsystems the mission interpreter calls ---- */
bool car_is_marked_for_removal(int car);     /* Car_IsMarkedForRemoval 0x41a470 */
void ambulance_clear_request(int id);        /* Ambulance_ClearRequest 0x41a3d0 */
void heli_spawn(int32_t x, int32_t y, int32_t z, int size, int32_t tx, int32_t ty, int32_t tz);   /* Heli_Spawn 0x40dd30 */
void police_report_crime(int a, int id, int kind, int32_t x, int32_t y, int32_t z);   /* Police_ReportCrime 0x4136c0 */
void hunt_remove(int ped);                   /* Hunt_Remove 0x431840 */
int hunt_add_block_target(int ped, int bx, int by, int bz);    /* Hunt_AddBlockTarget 0x4319b0 (-1: full) */
int hunt_add_block_target2(int ped, int bx, int by, int bz);   /* Hunt_AddBlockTarget2 0x431ab0 (-1: full) */
void powerup_add(int type, int param, int32_t x, int32_t y, int32_t z);   /* PowerUp_Add 0x46a0d0 */
void powerup_remove_at(int32_t x, int32_t y);   /* PowerUp_RemoveAt 0x46a4a0 */

/* ---- what the mission helpers (mission_obj.c, dummy.c) call ---- */
void expl_create(int32_t x, int32_t y, int32_t z, int owner);          /* Expl_Create 0x425170 */
void expl_damage_area(int32_t x, int32_t y, int32_t r, int owner);     /* Expl_DamageArea 0x425ca0 */
int obj_create_animated(int32_t x, int32_t y, int32_t z, int type, int owner, int angle);   /* Obj_CreateAnimated 0x44d3d0 */
void obj_kick(int32_t x, int32_t y, int obj, int kind, int angle);     /* Obj_Kick 0x44d5d0 */
void obj_set_state(int obj, int state);      /* Obj_SetState 0x44c5f0 (the stub sets the state only) */
void ambulance_cancel_for_ped(int ped);      /* Ambulance_CancelForPed 0x41a250 */
void hunt_add_car_target(int car, int target, int mode);   /* Hunt_AddCarTarget 0x4318e0 */
int train_get_count(void);                   /* Train_GetCount 0x46e7c0 */
uint8_t *train_get(int i);                   /* Train_Get 0x46e7a0 (0x5c8-byte record) */
void train_crash(int train, int mode);       /* Train_Crash 0x46d650 */
int sentinel_find_free(void);                /* Sentinel_FindFree 0x41ad30 (-1 none) */
uint8_t *sentinel_get(int i);                /* Sentinel_Get 0x41ad60 (0x98-byte records at 0x507ea0) */
void sentinel_override_lights(uint8_t *rec, Car *c);   /* Sentinel_OverrideLights 0x41e1c0 */
int map_find_nearest_road(uint8_t out[8]);   /* Map_FindNearestRoad 0x41a490 (in: block bytes at +2..+4) */
int path_find(int x, int y, int z, int dx, int dy, int dz, int mode, int ctrl);   /* Path_Find 0x4716f0 */
extern int16_t g_path_owner;                 /* 0x4b3094 the controller the path search serves (-1 idle) */
extern int16_t g_path_result;                /* 0x7537b2 */
void front_get_multi_target(uint8_t *kind, uint8_t *value);   /* Front_GetMultiTarget 0x4269c0 */
void map_set_block_face(int x, int y, int z, int face, int tile);   /* Map_SetBlockFace 0x438020 */
void map_set_block_type(int x, int y, int z, uint32_t info); /* Map_SetBlockType 0x437b50 */
int police_find_criminal_by_ped(int ped);    /* Police_FindCriminalByPed 0x414250 (-1 none) */
void police_clear_criminal(int i);           /* Police_ClearCriminal 0x414190 */
void police_spawn_patrol_cars(void);         /* Police_SpawnPatrolCars 0x465300 */
void police_init_criminals(void);            /* Police_InitCriminals 0x413250 */
void police_init_pursuits(void);             /* Police_InitPursuits 0x40d640 */
extern int32_t g_police_no_patrols;          /* 0x503184 set by Police_InitForMission */

/* ---- what the MissionOp_* handlers (mission_ops.c) need besides the above ---- */
void camera_start_transition(int n);         /* Camera_StartTransition 0x43cac0 */
bool powerup_exists_at(int32_t x, int32_t y);   /* PowerUp_ExistsAt 0x46a530 */
bool train_any_wrecked(void);                /* Train_AnyWrecked 0x46e7d0 */
int ambulance_busy_sentinel(void);           /* DAT_004b3094: the sentinel an ambulance call holds (-1 none) */
bool obj_is_on_screen(const Obj *o);         /* Obj_IsOnScreen 0x44c2f0 */

/* ---- what the player / ped modules (player.c, ped*.c, input.c) call ---- */
int train_command(int cmd, int train);       /* Train_Command 0x46a9f0 (1 board, 2 leave, 3 go, 4 stop; 5 / 8 queries) */
int train_is_boarded(int train);             /* Train_IsBoarded 0x46dca0 */
void car_set_horn_by_id(int car, int on);    /* Car_SetHornById 0x406fc0 */
void police_show_criminal_record(void);      /* Police_ShowCriminalRecord 0x464ec0 */

/* ---- what the mission runtime objects (trigger.c) call ---- */
int blockanim_create(int x, int y, int z, int face);   /* BlockAnim_Create 0x402250 (slot 0..63) */
void blockanim_set_tile(int a, int mode, int tile);    /* BlockAnim_SetTile 0x402500 */
int blockanim_get_tile_slot(int a);                    /* BlockAnim_GetTileSlot 0x402610 */
void blockanim_start_forward(int a, int n, int frames, int mode, int tile);   /* BlockAnim_StartForward 0x402390 */
void blockanim_start_reverse(int a, int n, int frames, int mode, int tile);   /* BlockAnim_StartReverse 0x402440 */
void blockanim_set_event(int a, int type, int arg);    /* BlockAnim_SetEvent 0x402550 (event on completion) */
void map_set_block_kind(int x, int y, int z, int kind);   /* Map_SetBlockKind 0x437cb0 */
void map_or_block_flags(int x, int y, int z, int flags);  /* Map_OrBlockFlags 0x437e70 */
void gang_update(void);                                /* Gang_Update 0x4315b0 */
int player_get_view_id(int n);                         /* Player_GetViewId 0x462fa0 */
int player_get_multiplier(int n);                      /* Player_GetMultiplier 0x463090 */
void player_award_bonus(int n, int kind, int32_t x, int32_t y, int32_t z, int a, int cause);   /* Player_AwardBonus 0x462000 */
void powerup_collect(int player, int32_t x, int32_t y, int how);   /* PowerUp_Collect 0x46a560 */
int lights_query(int what, int bx, int by);  /* Lights_Query 0x47df00 (0x34: the light's state for peds) */
int police_cops_for_wanted(int player);      /* Police_CopsForWanted 0x4131d0 */
void obj_delete_wrapper(int obj);            /* Obj_DeleteWrapper 0x44ed50 */
/* Map_GetLidBelow 0x4387b0 / Map_TestBlockAttr 0x44b310: map queries, here until the map module has them */
int map_get_lid_below(int32_t x, int32_t y, int32_t z);
int map_test_block_attr(int what, int bx, int by, int bz);
/* Obj_CreateAttached 0x44cad0: an object attached to entity `owner` (kind) at a local offset; -1 if none */
int obj_create_attached(int owner, int kind, int dx, int dy, int type);
void ambulance_request_for_ped(int ped);     /* Ambulance_RequestForPed 0x41a1a0 */

/* ---- what the car module (car.c, carcoll.c) calls ---- */
void car_dummy_follow_road(Car *c);          /* Car_DummyFollowRoad 0x408440 (traffic AI) */
void car_dummy_drive(Car *c);                /* Car_DummyDrive 0x416610 (traffic AI) */
void car_dummy_keep_lane(Car *c);            /* Car_DummyKeepLane 0x415fe0 (traffic AI) */
void sentinel_drive_car(Car *c);             /* Sentinel_DriveCar 0x41aed0 (and its thunk 0x42e860) */
void hunt_update_car(Car *c);                /* Hunt_UpdateCar 0x431f70 */
void traffic_spawn_around_view(int player, int near);   /* Traffic_SpawnAroundView 0x4183d0 */
void expl_car_explode(int car);              /* Expl_CarExplode 0x425480 */
void car_mark_for_removal(int car);          /* Car_MarkForRemoval 0x41a360 */
void obj_list_rotate(void);                  /* Obj_ListRotate 0x44cab0 */
void obj_remove_moving(int obj);             /* Obj_RemoveMoving 0x44eb30 */
void obj_delete_by_owner(int owner);         /* Obj_DeleteByOwner 0x44d360 */
void fire_clear_objects(int car);            /* Fire_ClearObjects 0x42f3a0 */
void obj_on_car_wrecked(Car *c);             /* Obj_OnCarWrecked 0x44ecc0 */
void powerup_reveal(int32_t x, int32_t y);   /* PowerUp_Reveal 0x46a190 */
void car_fire_rocket(Car *c);                /* Car_FireRocket 0x489300 */
/* ---- what the projectiles (proj.c) call ---- */
bool expl_at_face_if_solid(int bx, int by, int bz, int face, int player);   /* Expl_AtFaceIfSolid 0x425960 */
/* ---- what the ped car code (ped_car.c) calls ---- */
int train_check_platform_sides(int train);   /* Train_CheckPlatformSides 0x46ae40 (-1: no platform side) */
int train_get_door_offsets(int train);       /* Train_GetDoorOffsets 0x46b090 */
void train_update_door_sprites(int train);   /* Train_UpdateDoorSprites 0x46dcc0 */
void train_load_passengers(int train, int ped);   /* Train_LoadPassengers 0x46e450 */
void cop_dismiss(Car *c, int ped);           /* Cop_Dismiss 0x465870 */
int fire_has_objects(int car);               /* Fire_HasObjects 0x42f350 */
