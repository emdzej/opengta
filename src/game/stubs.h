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

/* ---- Game_Frame / Game_Update / Game_Render ---- */
#include "lights.h"                          /* lights_init / lights_update / lights_query, the junction overrides: ported (lights.c) */
#include "rail.h"                            /* rail_init and the Rail_* getters: ported (rail.c) */
#include "train.h"                           /* the train_* calls: ported (train.c) */
int player_train_crash_kick(int train);      /* Player_TrainCrashKick 0x463b70 */

/* ---- Game_HandleKey 0x430dc0 ---- */
void net_build_chat_prefix(int to);          /* Net_BuildChatPrefix 0x44c1b0 */

/* ---- events ---- */

/* ---- Mission_Load 0x445800 ---- */

/* ---- objects, explosions, fires, power-ups, block animations, map edits: ported (obj.c, expl.c, fire.c,
   powerup.c, blockanim.c, mapedit.c), included here for the callers of the old stubs ---- */
#include "blockanim.h"
#include "expl.h"
#include "fire.h"
#include "mapedit.h"
#include "powerup.h"
/* what they call that isn't ported */
void car_damp_thrust(Car *c);                /* Car_DampThrust 0x40c0c0 */
int fire_engine_dispatch(const int16_t info[4], int slot);   /* FireEngine_Dispatch 0x42ec70 (-1: no engine) */

/* ---- player module (0x4616b0-0x464900) as the mission runtime uses it (the accessors are exact) ---- */
/* Player_GetControlledPos 0x462ef0: the 16.16 x, y, z of what the player controls (car / ped sprite) */

/* ---- HUD and pager: ported (src/hud/hud.h, included here for the callers of the old stubs) ---- */
#include "../hud/hud.h"
/* ---- the police, wanted level, emergency services, gangs: ported (included for the callers of the old stubs) ---- */
#include "ambulance.h"
#include "gang.h"
#include "heli.h"
#include "police.h"
#include "sentinel.h"
#include "wanted.h"
/* ---- other subsystems the mission interpreter calls ---- */

/* ---- what the mission helpers (mission_obj.c, dummy.c) call ---- */
#include "traffic.h"                         /* traffic AI and generator: ported (traffic.c) */
#include "ai.h"                              /* sentinel_find_free / sentinel_get: ported (ai.c) */
#include "path.h"                            /* path_find, map_find_nearest_road, g_path_owner: ported (path.c) */
void front_get_multi_target(uint8_t *kind, uint8_t *value);   /* Front_GetMultiTarget 0x4269c0 */
extern int32_t g_police_no_patrols;          /* 0x503184 set by Police_InitForMission */

/* ---- what the MissionOp_* handlers (mission_ops.c) need besides the above ---- */
void camera_start_transition(int n);         /* Camera_StartTransition 0x43cac0 */
int ambulance_busy_sentinel(void);           /* DAT_004b3094: the sentinel an ambulance call holds (-1 none) */

/* ---- what the player / ped modules (player.c, ped*.c, input.c) call ---- */

/* ---- what the mission runtime objects (trigger.c) call ---- */
int player_get_view_id(int n);                         /* Player_GetViewId 0x462fa0 */
int player_get_multiplier(int n);                      /* Player_GetMultiplier 0x463090 */
void player_award_bonus(int n, int kind, int32_t x, int32_t y, int32_t z, int a, int cause);   /* Player_AwardBonus 0x462000 */
/* Map_GetLidBelow 0x4387b0 / Map_TestBlockAttr 0x44b310: map queries, here until the map module has them */
int map_get_lid_below(int32_t x, int32_t y, int32_t z);
int map_test_block_attr(int what, int bx, int by, int bz);

/* ---- what the car module (car.c, carcoll.c) calls ---- */
void car_fire_rocket(Car *c);                /* Car_FireRocket 0x489300 */
/* ---- what the projectiles (proj.c) call ---- */
/* ---- what the ped car code (ped_car.c) calls ---- */
/* ---- what the police / wanted level (wanted.c, heli.c) call, here until their modules have them ---- */
void area_get_sample(uint8_t x, uint8_t y, uint8_t *area, uint8_t *dir);   /* Area_GetSample 0x44b7b0 */
void player_inc_kills(int n, int kind);      /* Player_IncKills 0x462960 */
void player_set_view_fixed4(int32_t x, int32_t y, int32_t z, int n);   /* Player_SetViewFixed4 0x462d00 (kind 4) */
int fire_engine_update(Sentinel *s);         /* FireEngine_Update 0x42f460 */
void fire_engine_remove(Sentinel *s);        /* FireEngine_Remove 0x42e870 */
const int32_t *ref_get_kind1_pos_rect(int train);   /* Ref_GetKind1PosRect 0x45fb60: x, y, z of a ridden train */
