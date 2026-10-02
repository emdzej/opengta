/* The mission runtime objects: triggers, doors, cranes, timed bombs, the race ranking, car triggers,
   player slots and positional sound sources (0x473b90-0x4756ff), and their per-frame update with the
   kill scoring, respawn points and garages (0x479020-0x47bb10). Created by Mission_Load's object lines
   (mission_load.c), driven by Mission_UpdateTriggers from Mission_Update. docs/missions.md. */
#pragma once
#include "car.h"
#include "ped.h"
#include <stdbool.h>
#include <stdint.h>

enum {
    TRIGGER_MAX = 210,          /* 0x771628 .. 0x773068 */
    DOOR_MAX = 64,              /* 0x773220 .. 0x773c20 */
    CRANE_MAX = 4,              /* 0x773130 .. 0x773220 */
    CRANE_STACK = 6,
    CARTRIG_MAX = 25,           /* 0x773068 */
    TIMED_BOMB_MAX = 4,         /* 0x7715d8 */
    SOUND_SOURCE_MAX = 5,       /* 0x77161c */
    TRIGGER_TARGETS = 20,       /* 0x7713c0 race checkpoint order */
};

/* trigger states (+0x00) */
enum {
    TRIG_FIRE = 0,              /* fires on its next update (whether or not the player is there) */
    TRIG_ARMED = 1,             /* goes to TRIG_FIRE when its condition holds (Trigger_Create, Trigger_Disarm) */
    TRIG_DELAYED = 2,           /* waiting for the event 4 (Trigger_Reset) its delay scheduled */
    TRIG_RUNNING = 3,           /* fired, the process it started runs (Trigger_SetState3) */
    TRIG_DEAD = 4,              /* never updated again (Trigger_Kill, RESET for temporary ones) */
};

/* A trigger (0x20 bytes at 0x771628). Kinds: docs/missions.md (Mission_Load's object types map to
   them: TRIGGER 4, SPRAY 3, BOMBSHOP 8, CARTRIGGER 9, ONETRIGGER 10, MOVING_TRIG 0xb ...). */
typedef struct {
    int32_t state;              /* +0x00 TRIG_* */
    int32_t kind;               /* +0x04 */
    int32_t a;                  /* +0x08 usually the label to start (or a door, a colour, a car, a trigger) */
    int32_t d;                  /* +0x0c a car / ped / linked trigger (Trigger_SetParam14); -1 none */
    uint8_t x, y, z;            /* +0x10 block */
    uint8_t pad13;
    int32_t b;                  /* +0x14 radius in blocks (or a second label / car line) */
    int32_t c;                  /* +0x18 the arming delay in frames, or a frame counter */
    int8_t proc;                /* +0x1c process to kill when it fires (Trigger_SetFlag1c; -1 none) */
    uint8_t persistent;         /* +0x1d kept by RESET */
    uint8_t pad1e[2];
} Trigger;
_Static_assert(sizeof(Trigger) == 0x20, "trigger record");

/* door states (+0x04): the animation completion events swap the names (docs/missions.md) */
enum { DOOR_OPEN = 0, DOOR_CLOSED = 1, DOOR_OPENING = 2, DOOR_CLOSING = 3 };

/* A door (0x28 bytes at 0x773220). */
typedef struct {
    uint8_t anim;               /* +0x00 block animation slot (BlockAnim_Create) */
    uint8_t pad01[3];
    int32_t state;              /* +0x04 DOOR_* */
    int32_t locked;             /* +0x08 1: Door_Update skips it */
    int32_t range;              /* +0x0c blocks around the door the player must be within */
    int32_t orient;             /* +0x10 0..3: which faces of which blocks animate */
    uint8_t x, y, z;            /* +0x14 block */
    uint8_t pad17;
    int32_t tile;               /* +0x18 first tile of the animation */
    int32_t frames;             /* +0x1c frame count / speed passed to the animation */
    int16_t cond;               /* +0x20 opening condition: 0 any car, 1 emergency car, 2 a car (+ remap), 3 a model, 5 a bomb car */
    int16_t car;                /* +0x22 car id / model / script line (-1 any) */
    int16_t remap;              /* +0x24 (-1 any) */
    uint8_t face_slot;          /* +0x26 tile remap slot of the animated face */
    uint8_t persistent;         /* +0x27 */
} Door;
_Static_assert(sizeof(Door) == 0x28, "door record");

/* A crane (0x3c bytes at 0x773130). */
typedef struct {
    int32_t state;              /* +0x00 0 idle, 1-6 grab .. drop, 7 return, 8 full (clears off screen), 9 none, 10 screwed */
    int32_t obj;                /* +0x04 the crane object (type 0x1e) */
    int32_t x, y, z;            /* +0x08 pixels */
    int32_t dir;                /* +0x14 +1 / -1: the side it drops to */
    int32_t car;                /* +0x18 car being moved (-1) */
    int32_t count;              /* +0x1c cars stacked */
    int32_t stack[CRANE_STACK]; /* +0x20 */
    int32_t slot;               /* +0x38 (Mission_SetSlot773168) */
} Crane;
_Static_assert(sizeof(Crane) == 0x3c, "crane record");

/* A delayed explosion (0x10 bytes at 0x7715d8). */
typedef struct {
    int16_t id;                 /* +0x00 car or ped */
    int16_t kind;               /* +0x02 0 car, 1 ped */
    int16_t who;                /* +0x04 */
    int16_t pad06;
    int32_t state;              /* +0x08 0 set, 1 counting, 2 done */
    int8_t frames;              /* +0x0c */
    uint8_t pad0d[3];
} TimedBomb;
_Static_assert(sizeof(TimedBomb) == 0x10, "timed bomb record");

typedef struct { int32_t trigger, car; } CarTrig;   /* 0x773068 */

/* everything Mission_InitCityTables resets */
typedef struct {
    Trigger triggers[TRIGGER_MAX];      /* 0x771628 */
    int ntriggers;                      /* 0x7710f8 */
    Door doors[DOOR_MAX];               /* 0x773220 */
    int ndoors;                         /* 0x7710f0 */
    int doors_unlocked;                 /* 0x771618 doors Door_Update looks at */
    Crane cranes[CRANE_MAX];            /* 0x773130 */
    int ncranes;                        /* 0x771108 */
    CarTrig cartrigs[CARTRIG_MAX];      /* 0x773068 */
    TimedBomb bombs[TIMED_BOMB_MAX];    /* 0x7715d8 */
    uint8_t sound_used[SOUND_SOURCE_MAX];   /* 0x77161c */
    int32_t player_slot[4];             /* 0x7713b0 */
    uint8_t city_enable[0x200];         /* 0x7711b0 per-city table (Mission_GetByte7711b0) */
    uint8_t face_used[0x200];           /* 0x773c38 tile remap slots in use (Door_IsFaceSlotUsed) */
    uint8_t city_flag[0x200];           /* 0x7713d8 (Mission_IsFlagSet) */
    /* the race ranking */
    int8_t targets[TRIGGER_TARGETS];    /* 0x7713c0 checkpoint k -> trigger */
    int32_t progress[4];                /* 0x7710e0 checkpoints passed per player */
    int32_t finish_frame[4];            /* 0x7710d0 frame the player crossed the line (-1) */
    int8_t finish_order[4];             /* 0x7713d4 */
    uint8_t ntargets;                   /* 0x771104 */
    uint8_t ranks_sorted;               /* 0x7710f5 */
    uint8_t phones_off;                 /* 0x7710f4 a phone call is on (MissionOp_MPhone sets it): MPHONES triggers don't fire; reset by Mission_InitCityTables */
    int16_t ranks[4];                   /* 0x7710fc */
} MissionRuntime;

extern MissionRuntime g_mrt;

/* ---- 0x473b90-0x474080 ---- */
bool ped_is_on_any_screen(const Ped *p);    /* Ped_IsOnAnyScreen 0x473b90 */
bool car_is_on_any_screen(int car);         /* Car_IsOnAnyScreen 0x473c50 */
int mission_map_door_type(int t);           /* Mission_MapDoorType 0x473cd0 */
int mission_clamp_coord(int v);             /* Mission_ClampCoord 0x473d10 */
void mission_set_player_slot(int n, int v); /* Mission_SetPlayerSlot 0x473d30 */
void mission_clear_player_slot(int n);      /* Mission_ClearPlayerSlot 0x473d50 */
int mission_get_player_slot(int n);         /* Mission_GetPlayerSlot 0x473d70 */
bool mission_has_player_slot(int n);        /* Mission_HasPlayerSlot 0x473d80 */
void mission_reset_player_slots(void);      /* Mission_ResetPlayerSlots 0x473da0 */
void mission_reset_sound_sources(void);     /* Mission_ResetSoundSources 0x473dc0 */
int mission_add_sound_source(int32_t x, int32_t y, int32_t z);   /* Mission_AddSoundSource 0x473dd0 (-1 none free) */
void mission_remove_sound_source(int slot); /* Mission_RemoveSoundSource 0x473e30 */
void mission_remove_all_sound_sources(void);   /* Mission_RemoveAllSoundSources 0x473e50 */
void mission_reset_timed_bombs(void);       /* Mission_ResetTimedBombs 0x473e70 */
/* Mission_AddTimedBomb 0x473ea0: slot `n` (0..3) explodes `id` of `kind` after `frames` once counting */
bool mission_add_timed_bomb(int n, int kind, int id, int who, int frames);
void mission_update_timed_bombs(void);      /* Mission_UpdateTimedBombs 0x473ef0 */
void mission_reset_progress(void);          /* Mission_ResetProgress 0x473fb0 */
/* Mission_SetTargetOrder 0x474020: the next checkpoint is `trigger` (the original takes the count
   0x771104 as its first argument; every caller passes it) */
void mission_set_target_order(int trigger);
bool mission_all_players_below(int n);      /* Mission_AllPlayersBelow 0x474040 */
bool mission_is_flag_set(int n);            /* Mission_IsFlagSet 0x474080 */
bool door_is_face_slot_used(int n);         /* Door_IsFaceSlotUsed 0x4740a0 */
int mission_get_byte7711b0(int n);          /* Mission_GetByte7711b0 0x4740c0 */

/* ---- triggers ---- */
static inline Trigger *trigger_get(int n) { return &g_mrt.triggers[n]; }   /* Trigger_Get 0x4740e0 */
/* Trigger_Create 0x4744a0: block (x, y, z), kind, a, b (radius), c; some kinds move c to d (and c = 0) */
int trigger_create(int x, int y, int z, int kind, int a, int b, int c, int persistent);
void trigger_set_flag1c(int trigger, int proc);   /* Trigger_SetFlag1c 0x474580 */
void trigger_disable_all_temp(void);        /* Trigger_DisableAllTemp 0x4745a0 */
void trigger_disarm(int trigger);           /* Trigger_Disarm 0x474d10: state 1 */
void trigger_set_state3(int trigger);       /* Trigger_SetState3 0x474d30 */
void trigger_kill(int trigger);             /* Trigger_Kill 0x474d70 */
void trigger_set_param14(int trigger, int v);   /* Trigger_SetParam14 0x474db0: d (+0xc) */
void trigger_set_param08(int trigger, int v);   /* Trigger_SetParam08 0x474dd0: a (+0x8) */
void trigger_update(int trigger);           /* Trigger_Update 0x47a3a0 */
void trigger_reset(int trigger);            /* Trigger_Reset 0x47baf0 (event type 4): state 0 */

/* ---- doors ---- */
static inline Door *door_get(int n) { return &g_mrt.doors[n]; }
/* Door_Create 0x4740f0: (x, y, z) block, orient 0..3, frames, tile, side tile (0: none), cond, flag */
int door_create(int x, int y, int z, int orient, int frames, int tile, int side_tile, int cond, int persistent);
void door_reset_all_temp(void);             /* Door_ResetAllTemp 0x4745c0 */
void door_open(int door);                   /* Door_Open 0x4745f0 */
void door_close(int door);                  /* Door_Close 0x474670 */
void door_set_open_any(int door, int range);    /* Door_SetOpenAny 0x4746d0 */
void door_set_open_by_car(int door, int range, int car, int remap);   /* Door_SetOpenByCar 0x474710 */
void door_set_open_mode5(int door, int range, int car);   /* Door_SetOpenMode5 0x474770 */
void door_set_open_mode3(int door, int range, int model); /* Door_SetOpenMode3 0x4747b0 */
bool door_lock(int door);                   /* Door_Lock 0x474800 */
bool door_unlock(int door);                 /* Door_Unlock 0x474840 */
void door_on_closed(int door);              /* Door_OnClosed 0x474880 (event 2: the opening finished) */
void door_on_opened(int door);              /* Door_OnOpened 0x4748c0 (event 3: the closing finished) */
bool door_player_in_front(int door);        /* Door_PlayerInFront 0x474900 */
void door_update(int door);                 /* Door_Update 0x479f00 */

/* ---- ranking / score target ---- */
bool mission_score_target_reached(void);    /* Mission_ScoreTargetReached 0x4749c0 */
int mission_get_player_val(int n);          /* Mission_GetPlayerVal 0x474a20 */
void mission_sort_player_ranks(void);       /* Mission_SortPlayerRanks 0x474a50 */
int mission_get_player_rank(int n);         /* Mission_GetPlayerRank 0x474bd0 */

/* ---- car triggers ---- */
void car_trig_reset_all(void);              /* CarTrig_ResetAll 0x474c10 */
bool car_trig_add(int trigger, int car);    /* CarTrig_Add 0x474c30 */
void car_trig_check_enter(int car, int player_car);   /* CarTrig_CheckEnter 0x474c60 */

/* ---- cranes ---- */
bool crane_check_space(int crane, int car); /* Crane_CheckSpace 0x474df0 */
int crane_request_car(int crane, int car);  /* Crane_RequestCar 0x475010: 0 ok, 1 full, 2 busy, 3 burning, 4 a mission car */
void crane_update(int crane);               /* Crane_Update 0x475090 */
int crane_create(int x, int y, int z, int dir);   /* Crane_Create 0x475620 (pixels) */
static inline Crane *crane_get(int n) { return &g_mrt.cranes[n]; }   /* Crane_Get 0x4756a0 */
bool crane_is_car_held(int car);            /* Crane_IsCarHeld 0x4756c0 */

/* ---- 0x479020-0x47bb10 ---- */
void score_ped_killed(Ped *victim, int cause);   /* Score_PedKilled 0x479020 */
int player_choose_respawn_point(const Ped *p, int start);   /* Player_ChooseRespawnPoint 0x479610 */
void player_respawn(int n);                 /* Player_Respawn 0x479930 */
void player_set_stat_by_city(int n, int kind);   /* Player_SetStatByCity 0x4799d0 */
void mission_init_city_tables(void);        /* Mission_InitCityTables 0x479ab0 */
void mission_update_triggers(void);         /* Mission_UpdateTriggers 0x479e00 */
int garage_respray_cost(int car, int player);   /* Garage_ResprayCost 0x47ba30 */
