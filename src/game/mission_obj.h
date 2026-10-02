/* The mission helpers (0x475700-0x479020): the mission cars (MisCar_*), the car / ped / object
   queries and removals the interpreter and the loader use (block clearing, off-screen removal), the
   PED_ON presets (Mission_PedCreate_*) and the AI changes (Mission_PedSetObj_*), the briefs, the four
   alarm sound slots, the car list (GTA_DEMAND) and a few thin wrappers. docs/missions.md. */
#pragma once
#include "car.h"
#include "ped.h"
#include <stdbool.h>
#include <stdint.h>

/* ---- the car list (0x771110: 10 x 16 bytes, count 0x773c20; GTA_DEMAND lines) ---- */
enum { CARLIST_MAX = 10 };
typedef struct {
    int16_t a;                  /* +0x0 the line's p1 */
    int16_t model;              /* +0x2 -1 any */
    int16_t remap;              /* +0x4 -1 (0xffff) any */
    int16_t target;             /* +0x6 cars to deliver */
    int16_t u8;                 /* +0x8 */
    int16_t count;              /* +0xa delivered so far (cleared by CarList_Add) */
    int16_t uc, ue;
} CarListEntry;
_Static_assert(sizeof(CarListEntry) == 16, "car list entry");
extern CarListEntry g_car_list[CARLIST_MAX];   /* 0x771110 */
extern int g_car_list_count;                /* 0x773c20 (reset by Mission_InitCityTables) */

/* ---- alarm sound slots (0x773c24: 4 object ids, -1 free; Snd_SetEmitterB slots) ---- */
enum { ALARM_SLOTS = 4 };
extern int32_t g_alarm_slots[ALARM_SLOTS];  /* 0x773c24 */

/* ---- other state written here ---- */
extern int16_t g_mission_var505efa;         /* 0x505efa (header value 3) */
extern int32_t g_gang_lists_a[10];          /* 0x513270 (Mission_ResetLists_thunk -> 0x431500) */
extern int32_t g_gang_lists_b[10];          /* 0x513248 */
extern int32_t g_gang_count_a, g_gang_count_b;   /* 0x51329c, 0x513298 */

/* ---- mission objects and explosions (0x475700-0x475720: thunks to the explosion module) ---- */
/* MisObj_Create425520 (0x425520 via 0x475700): explosion at the block edge of direction dir (0 -x,
   1 +x, 2 -y, 3 +y) with debris, fire and smoke objects; owner = the player scored. */
void mis_obj_create425520(int bx, int by, int bz, int dir, int owner);
void mis_obj_create4258d0(int bx, int by, int bz, int dir, int owner);   /* 0x475710: the explosion only */
void mis_obj_create425780(int bx, int by, int bz, int dir, int owner);   /* 0x475720: explosion and two fires */
void car_snap_heading(Car *c);              /* Car_SnapHeading_00475730 */

/* ---- mission cars (0x475800-0x476550) ---- */
void mis_car_set_held(int car);             /* MisCar_SetHeld 0x475800 */
void mis_car_clear_held(int car);           /* MisCar_ClearHeld 0x475820 */
int mis_car_create(int bx, int by, int bz, int model, int angle, int remap);         /* MisCar_Create 0x475840 */
int mis_car_create_at(int32_t x, int32_t y, int bz, int model, int angle, int remap);  /* MisCar_CreateAt 0x4758c0 */
int mis_car_create_type1(int bx, int by, int bz, int model, int angle, int remap);   /* MisCar_CreateType1 0x475930 */
int mis_car_create_driver(int x, int y, int z, int car);                              /* MisCar_CreateDriver 0x4759b0 */
int mis_car_create_player_ped(int n, int bx, int by, int bz, int car, int angle, int remap);   /* 0x475a20 */
void mis_ped_set_remap(int ped, int remap); /* MisPed_SetRemap 0x475b00 */
void mis_car_make_killer(int car);          /* MisCar_MakeKiller 0x475b20 */
void mis_car_set_flag128_99(int car);       /* MisCar_SetFlag128_99 0x475bd0 */
void mis_car_set_flag128_1(int car);        /* MisCar_SetFlag128_1 0x475bf0 */
void mis_car_park(int car);                 /* MisCar_Park 0x475c10 */
void mis_car_put_player_in(int car, int n); /* MisCar_PutPlayerIn 0x475c40 */
void mis_car_give_player(int car, int n);   /* MisCar_GivePlayer 0x475dd0 */
bool car_is_at_block(int car, int bx, int by, int bz);          /* Car_IsAtBlock 0x475e20 */
bool ped_is_near_block(int ped, int bx, int by, int bz, int r); /* Ped_IsNearBlock 0x475e60 */
bool car_is_wrecked(int car);               /* Car_IsWrecked 0x475ed0 */
int car_get_damage(int car);                /* Car_GetDamage 0x475ef0 */
void mis_car_spawn_batch(int n);            /* MisCar_SpawnBatch 0x475f10 */
void police_init_for_mission(void);         /* Police_InitForMission 0x475f60 */
int car_get_driver(int car);                /* Car_GetDriver 0x475fd0 */
int car_get_driver_info(int car);           /* Car_GetDriverInfo 0x475ff0 */
bool car_has_live_driver(int ped, int car); /* Car_HasLiveDriver 0x476020 */
bool car_move_axis(int car, int target, int step, int axis);    /* Car_MoveAxis 0x476080 */
bool car_move_towards(int car, int x, int y, int z, int step);  /* Car_MoveTowards 0x4761c0 */
int ped_check_in_car(int ped, int mode);    /* Ped_CheckInCar 0x476340 */
/* car_spawn_parked (thunk_Car_SpawnParked 0x4763d0) is the car module's: stubs.h */
void car_unk_004318e0_wrap(int car, int target, int mode);   /* 0x4763e0 -> Hunt_AddCarTarget 0x4318e0 */
int car_get_model_value(int car, int field);/* Car_GetModelValue 0x476400 */
void mis_car_destroy(int car);              /* MisCar_Destroy 0x476440 */
void mis_car_set_drive_mode1(int car);      /* MisCar_SetDriveMode1 0x476470 */
void mis_car_set_flag9c(int car, int v);    /* MisCar_SetFlag9c 0x4764a0 */
void mis_car_set_drive_mode2(int car);      /* MisCar_SetDriveMode2 0x4764d0 */
int car_get_cardinal_dir(int car);          /* Car_GetCardinalDir 0x476500 */
void car_clear_field0ec(int car);           /* Car_ClearField0EC 0x476550 */
bool car_place_in_grid(int car, int idx, int stride);   /* Car_PlaceInGrid 0x476570 */
bool mission_check_player_car_ok(int n);    /* Mission_CheckPlayerCarOk 0x4766a0 */
bool car_remove_if_offscreen(int car);      /* Car_RemoveIfOffscreen 0x476720 */
bool car_remove_if_offscreen_ex(int car);   /* Car_RemoveIfOffscreenEx 0x476880 */
bool car_is_emergency_model(int model);     /* Car_IsEmergencyModel 0x4769e0 */
bool car_remove_at_block(int x, int y, int exclude, int exact, int mode);   /* Car_RemoveAtBlock 0x476a10 */
bool car_remove_in_square(int x, int y, int z, int r, int exclude, int mode);   /* Car_RemoveInSquare 0x476af0 */
bool car_remove_offscreen_at_block(int x, int y, int z, int exclude);   /* Car_RemoveOffscreenAtBlock 0x476b90 */
/* Car_ClearForCar 0x476cd0 (x, y, z pixels): its result is what Mission_ClearBlock returns */
int car_clear_for_car(int x, int y, int z, int car, int model, int angle);
void ped_remove_dummies_at_block(int x, int y, int z, int exclude);    /* Ped_RemoveDummiesAtBlock 0x476f40 */
void obj_remove_at_block(int x, int y, int z, int exclude);            /* Obj_RemoveAtBlock 0x476fe0 */
bool world_any_thing_at(int32_t x, int32_t y);                         /* World_AnyThingAt 0x477070 */
/* Mission_ClearBlock 0x4770a0: pixel (x, y, z); a = exclude id, b = model, c = angle (for Car_ClearForCar) */
int mission_clear_block(int x, int y, int z, int a, int b, int c);
bool mission_clear_row(int x, int y, int z, int exclude, int n, int dx, int dy);   /* Mission_ClearRow 0x477160 */
void mission_explode_ped(int ped);          /* Mission_ExplodePed 0x477260 */
bool ped_is_dead(int ped);                  /* Ped_IsDead 0x4772c0 */
int ped_take_killer(int ped);               /* Ped_TakeKiller 0x4772e0 (-2 none) */
bool ped_kill_if_possible(int ped);         /* Ped_KillIfPossible 0x477320 */
bool car_remove_if_unused(int car);         /* Car_RemoveIfUnused 0x4773a0 */

/* ---- PED_ON presets (pixel x, y, z; Ped_Create at z - 2 pixels) ---- */
int mission_ped_create_0(int x, int y, int z, int angle);              /* 0x4773f0 */
int mission_ped_create_1(int x, int y, int z, int angle, int target);  /* 0x477460 */
int mission_ped_create_8(int x, int y, int z, int angle, int target);  /* 0x4774e0 */
int mission_ped_create_4(int x, int y, int z, int angle);              /* 0x477560 */
int mission_ped_create_6(int x, int y, int z, int angle);              /* 0x4775d0 */
int mission_ped_create_5(int x, int y, int z, int angle, int target);  /* 0x477630 */
int mission_ped_create_7(int x, int y, int z, int angle, int target);  /* 0x4776b0 */
int mission_ped_create_9(int x, int y, int z, int angle);              /* 0x477730 */
int mission_ped_create_10(int x, int y, int z, int angle);             /* 0x477790 */
int mission_ped_create_11(int x, int y, int z, int angle, int target); /* 0x477800 */
int mission_ped_create_12(int x, int y, int z, int angle, int target); /* 0x477880 */
int mission_ped_create_typed(int x, int y, int z, int angle, int target, int kind);   /* 0x477900 */
int mission_ped_create_13(int x, int y, int z, int angle, int target); /* 0x477ad0 */
int mission_ped_create_guard(int x, int y, int z, int angle);          /* Mission_PedCreate_Guard 0x477b50 */
/* the creator Mission_SpawnPed 0x43d460 calls for a PED sub-type (0..0x2e); arg = the target handle */
int mission_ped_create(int kind, int x, int y, int z, int angle, int arg);

/* ---- AI changes of a live ped (false: dead, or in state 0xc / 0x15 / 0x17 / 0x18) ---- */
bool mission_ped_set_obj_attack(int ped, int target);   /* 0x477bd0 */
bool mission_ped_set_obj_follow(int ped, int target);   /* 0x477c80 */
bool mission_ped_send_to(int ped, int car);             /* Mission_PedSendTo 0x477d20 */
bool mission_ped_set_obj_wander(int ped);               /* 0x477dc0 */
bool mission_ped_set_obj_wander2(int ped);              /* 0x477e80 */
bool mission_ped_set_obj_24(int ped);                   /* 0x477f40 */
bool mission_ped_set_obj_18(int ped, int target);       /* 0x478000 */
bool mission_ped_set_obj_1c(int ped, int target);       /* 0x4780c0 */
bool mission_ped_set_obj_23(int ped);                   /* 0x478180 */
bool mission_ped_set_obj_31(int ped, int target);       /* 0x478230 */
void mission_ped_set_obj_30(int ped, int target);       /* 0x4782f0 (no checks) */
bool mission_ped_set_obj_typed(int ped, int target, int kind);   /* 0x478380 */
bool mission_ped_set_obj_39(int ped, int target);       /* 0x478570 */
bool ped_is_near_point16(int ped, int x, int y);        /* Ped_IsNearPoint16 0x478630 (pixels) */
bool ped_is_near_point(int ped, int x, int y, int r);   /* Ped_IsNearPoint 0x478690 (r 0 = 16) */
bool ped_is_in_block_rect(int ped, int bx, int by, int bz, int r);   /* Ped_IsInBlockRect 0x478700 */

/* ---- objects ---- */
int mission_obj_create(int x, int y, int z, int type, int angle);   /* Mission_ObjCreate 0x478790 (pixels) */
bool obj_remove_if_active(int obj);         /* Obj_RemoveIfActive 0x4787d0 */
void mission_set_var505efa(int v);          /* Mission_SetVar505efa 0x478800 */
void mission_set_var5031cc(int v);          /* Mission_SetVar5031cc 0x478810 (g_game.opt.emergency) */
int obj_create_at_ped(int ped, int type, int angle);    /* Obj_CreateAtPed 0x478820 (one block up) */
void obj_throw_to_car(int obj, int car);    /* Obj_ThrowToCar 0x478860 */
void obj_throw_to_point(int obj, int32_t x, int32_t y);   /* Obj_ThrowToPoint 0x4788c0 (16.16) */
void mission_obj_set_state(int obj, int state);   /* Mission_ObjSetState 0x478900 */

/* ---- briefs (HUD_Brief 0x4821c0 kinds 0..5; text numbers below 1000 are fatal) ---- */
void mission_brief_timed1(int frames, int text, int player);   /* Mission_BriefTimed1 0x478920 */
void mission_brief_timed0(int frames, int text, int player);   /* Mission_BriefTimed0 0x478990 */
void mission_brief_countdown(int frames, int text, int player);/* Mission_BriefCountdown 0x478a00 */
void mission_brief3(int text, int player);  /* Mission_Brief3 0x478a40 */
void mission_brief4(int text, int player);  /* Mission_Brief4 0x478a80 */
void mission_brief5(int text, int player);  /* Mission_Brief5 0x478ac0 */
void mission_show_bomb_timer(int frames);   /* Mission_ShowBombTimer 0x478b00 */

void obj_toggle_state23(int obj);           /* Obj_ToggleState23 0x478b30 */
bool obj_step_axis(int obj, int target, int step, int axis);   /* Obj_StepAxis 0x478b70 */
bool wanted_clear_for_ped(int ped);         /* Wanted_ClearForPed 0x478c20 */
void mission_call438020(int x, int y, int z, int face, int tile);   /* 0x478c60 -> Map_SetBlockFace 0x438020 */
void map_set_block_thunk(int x, int y, int z, uint32_t info);       /* 0x478c80 -> Map_SetBlockType 0x437b50 */
void mission_alarm_sound_add(int obj);      /* Mission_AlarmSoundAdd 0x478c90 */
void mission_alarm_sound_remove(int obj);   /* Mission_AlarmSoundRemove 0x478d80 */
void mission_alarm_sound_stop_all(void);    /* Mission_AlarmSoundStopAll 0x478dc0 */
void mission_alarm_sound_reset(void);       /* Mission_AlarmSoundReset 0x478e10 */
int car_list_add(int a, int model, int remap, int count);   /* CarList_Add 0x478e30 */
CarListEntry *car_list_get(int i);          /* CarList_Get 0x478e80 */
int mission_set_slot773168(int crane, int v);   /* Mission_SetSlot773168 0x478e90 (crane record +0x38) */
int car_list_matches(int car, int i);       /* CarList_Matches 0x478ec0 (1 / -1) */
int car_list_count_same_model(int car, const int32_t ids[6]);   /* CarList_CountSameModel 0x478f10 */
bool car_list_is_complete(int i);           /* CarList_IsComplete 0x478f60 */
void mission_reset_lists(void);             /* Mission_ResetLists_thunk 0x478f90 (0x431500) */
int player_find_train_slot(int n);          /* Player_FindTrainSlot 0x478fa0 (0xff none) */
void mission_call46d650(int train);         /* 0x479000: Train_Crash(train, 2) then (train, 1) */
