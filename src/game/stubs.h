/* Every call from the game core into a subsystem that is not ported yet, in one place (stubs.c), with
   the original's name and address. A port replaces its stubs by defining the same functions in its
   own file and deleting them here. A stub that returns something returns what the original's contract
   needs (a running index for the creators that hand out table slots, 20 for the update functions
   whose result Game_Update checks). Each stub counts its calls (stub_calls) so tests can see what a
   level start asked for. */
#pragma once
#include "car.h"
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
uint32_t input_read_controls(void);          /* Input_ReadControls 0x432e00 (see stub_controls) */
extern uint32_t (*stub_controls)(void);      /* test hook: the control word of the frame */
void player_apply_input(uint32_t control);   /* Player_ApplyInput 0x463ec0 */
void gfx_select_mode(void);                  /* Gfx_GetModeIndex 0x414d30 / Gfx_SelectMode 0x414cc0, Style_ConvertPalettes */

/* ---- Game_Init 0x430a20 / Game_Shutdown 0x430b10 ---- */
void replay_begin(void);                     /* Replay_Begin 0x432c90 */
void replay_end_save(void);                  /* Replay_EndSave 0x432d80 */
void replay_tick_frame(void);                /* Replay_TickFrame 0x433790 */
bool replay_is_playing(void);                /* Replay_IsPlaying 0x4337a0 */
void hud_init(void);                         /* HUD_Init 0x483010 */
void hud_free_fonts(void);                   /* HUD_FreeFonts 0x4832c0 */
void heli_init(void);                        /* Heli_Init 0x40dcc0 */
void lights_init(void);                      /* Lights_Init 0x47dcf0 */
void sentinel_init_all(void);                /* Sentinel_InitAll 0x41abd0 (the location counts are ported: route.c) */
void path_reset(void);                       /* Path_Reset 0x46e9e0 */
void train_init_all(void);                   /* Train_InitAll 0x46a9a0 */
void proj_reset(void);                       /* Proj_Reset 0x4879b0 */
void fire_init(void);                        /* Fire_Init 0x42e600 */
void expl_init(void);                        /* Expl_Init 0x425c50 */
void blockanim_reset(void);                  /* BlockAnim_Reset 0x402240 */
void hunt_init(void);                        /* Hunt_Init 0x4317f0 */
void powerup_init_all(void);                 /* PowerUp_InitAll 0x46a0a0 */
void area_localize_names(void);              /* Area_LocalizeNames 0x44b4a0 */

/* ---- Game_Frame / Game_Update / Game_Render ---- */
void hud_tick_big_message(void);             /* HUD_TickBigMessage 0x486640 */
void hud_clear_zone_text(int zone);          /* HUD_ClearZoneText 0x481c50 */
void hud_show_zone_text(const char *s, int zone);   /* HUD_ShowZoneText 0x481a40 */
void hud_update(void);                       /* HUD_Update 0x485d70 */
void hud_draw(void);                         /* HUD_Draw 0x483390 */
void cars_update_all(void);                  /* Cars_UpdateAll 0x40adc0 */
void heli_update(void);                      /* Heli_Update 0x40dfb0 */
void ped_update_all(void);                   /* Ped_UpdateAllThunk 0x45d3c0 (Ped_UpdateAll 0x45cd50) */
int train_update_all(void);                  /* Train_UpdateAll 0x46a9d0 (20 = ok) */
int lights_update(void);                     /* Lights_Update 0x47e420 (20 = ok) */
void junction_update_override_timers(void);  /* Junction_UpdateOverrideTimers 0x41e140 */
void obj_update_all(void);                   /* Obj_UpdateAll 0x44d790 */
void emergency_update_all(void);             /* Emergency_UpdateAll 0x419880 */
void expl_update_all(void);                  /* Expl_UpdateAll 0x425b60 */
void blockanim_tick(void);                   /* BlockAnim_Tick 0x402580 */
void mission_update(void);                   /* Mission_Update 0x446fe0 */
void player_update_all(void);                /* Player_UpdateAll 0x464880 */

/* ---- Game_HandleKey 0x430dc0 ---- */
bool hud_handle_key(int key);                /* HUD_HandleKey 0x482d00 (true: consumed) */
void hud_toggle_video_menu(void);            /* HUD_ToggleVideoMenu 0x482330 */
void hud_toggle_quit_prompt(void);           /* HUD_ToggleQuitPrompt 0x4822a0 */
void hud_toggle_debug(void);                 /* HUD_ToggleDebug 0x483000 */
void hud_pause_on(void);                     /* HUD_PauseOn 0x481510 */
void hud_pause_off(void);                    /* HUD_PauseOff 0x481530 */
void hud_restore_subtitle(void);             /* HUD_RestoreSubtitle 0x482070 */
void hud_refresh_zone(void);                 /* HUD_RefreshZone 0x482290 */
void pager_resume(void);                     /* Pager_Resume 0x482fb0 */
void net_build_chat_prefix(int to);          /* Net_BuildChatPrefix 0x44c1b0 */
void player_add_ammo(int player, int weapon, int n);   /* Player_AddAmmo 0x461b50 */

/* ---- events ---- */
void mission_on_brief_done(int arg);         /* Mission_OnBriefDone 0x445580 */
void door_on_closed(int door);               /* Door_OnClosed 0x474880 */
void door_on_opened(int door);               /* Door_OnOpened 0x4748c0 */
void trigger_reset(int trigger);             /* Trigger_Reset 0x47baf0 */

/* ---- Mission_Load 0x445800 ---- */
void mission_init_city_tables(void);         /* Mission_InitCityTables 0x479ab0 */
void dummy_init_groups(void);                /* Dummy_InitGroups 0x473440 */
void mission_reset_lists(void);              /* Mission_ResetLists_thunk 0x478f90 (gang lists 0x513270) */
void mission_set_var505efa(int v);           /* Mission_SetVar505efa 0x478800 */
void traffic_prime_car_pool(int n);          /* Traffic_PrimeCarPool 0x418f00 */
void police_init_for_mission(void);          /* Police_InitForMission 0x475f60 */
void ped_remove_dummies_at_block(int x, int y, int z, int a);   /* Ped_RemoveDummiesAtBlock 0x476f40 */
/* Car_ClearForCar 0x476cd0: removes the cars in the way (its result is what Mission_ClearBlock returns) */
int car_clear_for_car(int x, int y, int z, int a, int b, int c);
/* the car spawners (Car_Init 0x4067c0 not ported: the stub takes the slot the original would and sets
   the position, heading, model, remap and the slot bookkeeping) */
int car_spawn_ex(int32_t x, int32_t y, int32_t z, int model, int driver, int angle, int remap);   /* 0x4078d0 */
int car_spawn_ex_on_ground(int32_t x, int32_t y, int32_t z, int model, int driver, int angle, int remap);   /* 0x407ab0 */
int car_spawn_parked(int bx, int by, int bz);   /* thunk_Car_SpawnParked 0x4763d0 */
int mis_car_create_at(int32_t x, int32_t y, int bz, int model, int angle, int remap);   /* MisCar_CreateAt 0x4758c0 */
void mis_car_set_flag9c(int car, int v);     /* MisCar_SetFlag9c 0x4764a0 */
void mis_car_set_drive_mode1(int car);       /* MisCar_SetDriveMode1 0x476470 */
void mis_car_set_drive_mode2(int car);       /* MisCar_SetDriveMode2 0x4764d0 */
void ped_create_car_driver(Car *c);          /* Ped_CreateCarDriver 0x44f7a0 */
void gang_add_car(int car);                  /* Gang_AddCar 0x431530 */
void ped_send_to_car_door1(Ped *p, int car);  /* Ped_SendToCarDoor1 0x45f4a0 */
/* Mission_PedCreate_0 0x4773f0 .. Mission_PedCreate_13 0x477ad0, Mission_PedCreate_Typed 0x477900 (kind = the PED sub-type; arg = the extra handle or -1) */
int mission_ped_create(int kind, int x, int y, int z, int angle, int arg);
bool car_info_is_convertible(int car);       /* CarInfo_IsConvertible 0x40bdb0 */
void player_enter_car(int ped, int car);     /* Player_EnterCar 0x463a10 */
void carphys_begin(int car);                 /* CarPhys_Begin 0x460a20 */
void ped_update_sprite(int ped);             /* Ped_UpdateSprite 0x44f100 */
void police_update_criminal_target(int a, int car, int b, int ped);   /* Police_UpdateCriminalTarget 0x4132c0 */
int mission_map_door_type(int t);            /* Mission_MapDoorType 0x473cd0 */
int door_create(int x, int y, int z, int a, int b, int c, int d, int e, int persistent);   /* Door_Create 0x4740f0 */
void door_set_open_any(int door, int v);     /* Door_SetOpenAny 0x4746d0 */
void door_lock(int door);                    /* Door_Lock 0x474800 */
void door_set_open_mode3(int door, int a, int b);   /* Door_SetOpenMode3 0x4747b0 */
void door_set_open_mode5(int door, int a, int b);   /* Door_SetOpenMode5 0x474770 */
void door_set_open_by_car(int door, int a, int b, int c);   /* Door_SetOpenByCar 0x474710 */
int crane_create(int x, int y, int z, int dir);   /* Crane_Create 0x475620 (its Obj_Create is done) */
int trigger_create(int x, int y, int z, int type, int a, int b, int c, int persistent);   /* Trigger_Create 0x4744a0 */
void trigger_set_param14(int trigger, int v);   /* Trigger_SetParam14 0x474db0 */
void mission_set_target_order(int trigger);  /* Mission_SetTargetOrder 0x474020 (first arg: 0x771104) */
void car_trig_add(void);                     /* CarTrig_Add 0x474c30 */
int car_list_add(int a, int model, int remap, int count);   /* CarList_Add 0x478e30 */
void heli_set_exit_target(int32_t x, int32_t y);   /* Heli_SetExitTarget 0x40dd10 */

/* ---- objects ---- */
void fire_register(int obj);                 /* Fire_Register 0x42ef80 */
