/* The ped table (0x7284e0, 620 x 0x100; Ped_Get 0x44f500) and the ped module (0x44ee50-0x4607af):
   level start (Ped_InitAll 0x44f370), the per-frame update (Ped_UpdateAll 0x45cd50), movement,
   animation, spawning and the enter / exit car chain. Slots 0..199 are ambient and mission peds,
   200..599 the drivers of cars 0..399 (ped = car + 200), 600..619 special peds. Fields whose meaning
   is not known are named uNN after their offset. See docs/peds.md.

   The module's functions are split over ped.c (table, sprite, update loop, accessors), ped_anim.c
   (Ped_Animate), ped_move.c (Ped_Process and the collision responses), ped_step.c (Ped_ComputeStep
   and the steering), ped_car.c (entering / leaving cars, respawn) and ped_spawn.c (creation, ambient
   peds, panic, weapons); their shared declarations are in ped_internal.h. */
#pragma once
#include "../render/camera.h"
#include "../render/sprite.h"
#include "layout.h"
#include "proj.h"
struct Car;
typedef struct Car Car;   /* proj_reset for Game_Init (game.c includes ped.h) */
#include <stdbool.h>
#include <stdint.h>

enum { PED_MAX = 620, PED_DRIVER_FIRST = 200, PED_SPECIAL_FIRST = 600 };

/* anim (+0x18) values with a fixed meaning (the rest are frames of the animations, see docs/peds.md) */
enum {
    PED_ANIM_FREE = 0,          /* slot unused */
    PED_ANIM_WALK_FIRST = 1,    /* 1..8 walk cycle, 9..0x10 run cycle */
    PED_ANIM_STAND = 0x88,
};

typedef struct Ped {
    int16_t id;                 /* +0x00 */
    int16_t turn;               /* +0x02 angle change per frame (player: the steering, ±0x10..0x30) */
    int16_t accel;              /* +0x04 player's accelerate / brake input (3 forward, < 0 back) */
    int16_t speed;              /* +0x06 step per frame (pixels); also the camera target speed */
    uint8_t move_speed;         /* +0x08 4 walk, 6 fast (Ped_SetMoveSpeed) */
    uint8_t pad09;
    int16_t anim_tick;          /* +0x0a Ped_Animate advances the animation every other call */
    int16_t u0c;
    int16_t u0e;                /* +0x0e frame counter (dead peds are removed after 1000) */
    int16_t control;            /* +0x10 control type: -1 none, 0 ambient, 8 + n player n */
    int16_t u12;                /* +0x12 0x80 / -0x80 alternating at init; turn of state 0x12 */
    int16_t u14;                /* +0x14 the car of a driver slot */
    int16_t graphic;            /* +0x16 graphic type (0 civilian, 1 cop, ...; 0xbd sprites each) */
    int16_t anim;               /* +0x18 animation frame state; 0 = slot free */
    int16_t idle_count;         /* +0x1a frames spent standing / in the current pose */
    uint8_t player_ctl;         /* +0x1c player-controlled */
    uint8_t pad1d;
    int16_t u1e;
    int32_t u20, u24, u28;
    int32_t target_x, target_y; /* +0x2c, +0x30 */
    uint8_t u34;                /* +0x34 (0xff at reset) */
    uint8_t pad35[3];
    int32_t walk_x, walk_y;     /* +0x38, +0x3c walk target (0: none) */
    int16_t u40;                /* +0x40 */
    int16_t mode;               /* +0x42 */
    int16_t u44;                /* +0x44 destination angle (Ped_SetDestination) */
    uint8_t firing;             /* +0x46 fire held */
    uint8_t u47, u48;
    int8_t health;              /* +0x49 100 alive, 0 dead */
    uint8_t u4a;                /* +0x4a Ped_SetFlag4A (armour) */
    uint8_t pad4b;
    int16_t car;                /* +0x4c car id (-1 on foot) */
    int16_t u4e;
    int16_t attach_kind, attach_id;   /* +0x50, +0x52 riding: 1 car, 2 object, 3 ped */
    int16_t u54, u56;           /* +0x54, +0x56 riding offset (pixels) */
    int16_t carried;            /* +0x58 carried object (-1) */
    int16_t u5a, u5c;
    uint8_t remap;              /* +0x5e */
    uint8_t pad5f;
    int32_t weapon;             /* +0x60 0 none, 1 pistol, 2 machine gun, 3 rocket launcher, 4 flamethrower */
    int16_t u64, u66;
    uint8_t u68;
    uint8_t pad69[3];
    int32_t objective;          /* +0x6c 0x19 wander, 0x25 player ... */
    int32_t state;              /* +0x70 1, 2 walk, 4 go to, 7 in car, 9, 10, 0xc / 0x17 dead ... */
    int32_t u74, u78, u7c, u80; /* +0x7c action (2 normal, 0x12 .. 0x14), +0x80 the saved one */
    int16_t u84;
    int16_t target_ped;         /* +0x86 */
    uint8_t u88;
    uint8_t group, group_slot;  /* +0x89, +0x8a */
    uint8_t u8b;                /* +0x8b mission ped */
    uint8_t train;              /* +0x8c train index (0xff none) */
    uint8_t pad8d[3];
    Sprite spr;                 /* +0x90 x +0x90, y +0x94, z +0x98, z key +0x9c, angle +0xa8 */
    uint8_t uec;                /* +0xec */
    uint8_t paded;
    int16_t uee;
    int16_t prev_angle;         /* +0xf0 the angle at the start of the frame */
    uint8_t padf2[2];
    int32_t uf4;
    int16_t uf8, ufa, ufc;
    uint8_t ufe;
    uint8_t padff;
} Ped;
GAME_OFS(Ped, turn, 0x02); GAME_OFS(Ped, accel, 0x04); GAME_OFS(Ped, speed, 0x06);
GAME_OFS(Ped, move_speed, 0x08); GAME_OFS(Ped, anim_tick, 0x0a); GAME_OFS(Ped, u0e, 0x0e);
GAME_OFS(Ped, control, 0x10); GAME_OFS(Ped, u14, 0x14); GAME_OFS(Ped, graphic, 0x16);
GAME_OFS(Ped, anim, 0x18); GAME_OFS(Ped, idle_count, 0x1a); GAME_OFS(Ped, player_ctl, 0x1c);
GAME_OFS(Ped, u1e, 0x1e); GAME_OFS(Ped, target_x, 0x2c); GAME_OFS(Ped, u34, 0x34);
GAME_OFS(Ped, walk_x, 0x38); GAME_OFS(Ped, u40, 0x40); GAME_OFS(Ped, mode, 0x42);
GAME_OFS(Ped, u44, 0x44); GAME_OFS(Ped, firing, 0x46); GAME_OFS(Ped, u48, 0x48);
GAME_OFS(Ped, health, 0x49); GAME_OFS(Ped, u4a, 0x4a); GAME_OFS(Ped, car, 0x4c);
GAME_OFS(Ped, attach_kind, 0x50); GAME_OFS(Ped, u54, 0x54); GAME_OFS(Ped, carried, 0x58);
GAME_OFS(Ped, u5a, 0x5a); GAME_OFS(Ped, remap, 0x5e); GAME_OFS(Ped, weapon, 0x60);
GAME_OFS(Ped, u64, 0x64); GAME_OFS(Ped, u68, 0x68); GAME_OFS(Ped, objective, 0x6c);
GAME_OFS(Ped, state, 0x70); GAME_OFS(Ped, u74, 0x74); GAME_OFS(Ped, u7c, 0x7c);
GAME_OFS(Ped, u80, 0x80); GAME_OFS(Ped, u84, 0x84); GAME_OFS(Ped, target_ped, 0x86);
GAME_OFS(Ped, group, 0x89); GAME_OFS(Ped, u8b, 0x8b); GAME_OFS(Ped, train, 0x8c);
GAME_OFS(Ped, spr, 0x90); GAME_OFS32(Ped, uec, 0xec); GAME_OFS32(Ped, uee, 0xee);
GAME_OFS32(Ped, prev_angle, 0xf0); GAME_OFS32(Ped, uf4, 0xf4); GAME_OFS32(Ped, uf8, 0xf8);
GAME_OFS32(Ped, ufc, 0xfc); GAME_OFS32(Ped, ufe, 0xfe); GAME_SIZE32(Ped, 0x100);

extern Ped g_peds[PED_MAX];                 /* 0x7284e0 */
extern int g_peds_active;                   /* 0x74f104: spawned peds (Ped_SpawnInSlot counts) */

static inline Ped *ped_get(int id) { return &g_peds[id]; }      /* Ped_Get 0x44f500 */
void ped_reset(int id);                     /* Ped_Reset 0x44f230 */
void ped_init_all(void);                    /* Ped_InitAll 0x44f370 */
void ped_update_all(void);                  /* Ped_UpdateAllThunk 0x45d3c0 -> Ped_UpdateAll 0x45cd50 */
void ped_update_sprite(int id);             /* Ped_UpdateSprite 0x44f100 */
/* Ped_SpawnInSlot 0x44f680: ped `slot` at (x, y, z) unless the slot is taken (anim != 0). */
void ped_spawn_in_slot(int32_t x, int32_t y, int32_t z, int speed, int angle, int anim, int slot, int car);
void ped_set_player_controlled(int id);     /* Ped_SetPlayerControlled 0x45fa50 */
void ped_clear_player_controlled(int id);   /* Ped_ClearPlayerControlled 0x45fa70 */
static inline void ped_set_control_type(int id, int t) { g_peds[id].control = (int16_t)t; }   /* 0x44f0e0 */
static inline void ped_set_weapon_raw(int id, int w) { g_peds[id].weapon = w; }               /* 0x4532d0 */
static inline void ped_set_flag4a(Ped *p, int on) { p->u4a = on == 1; }                       /* 0x44ef80 */
static inline void ped_set_move_speed(Ped *p, int fast) { p->move_speed = fast == 1 ? 6 : 4; }  /* 0x44ef60 */
static inline void ped_set_health(int id, int h) { g_peds[id].health = (int8_t)h; }           /* 0x45fbf0 */
static inline bool ped_is_state9(int id) { return g_peds[id].state == 9; }                    /* 0x460770 */
static inline bool ped_is_state10(int id) { return g_peds[id].state == 10; }                  /* 0x460790 */
void ped_set_weapon(int id, int w);         /* Ped_SetWeapon 0x44eed0 (1..4, fatal otherwise; 0 ignored) */
void ped_make_police(Ped *p);               /* Ped_MakePolice 0x44eea0 */
void ped_set_appearance(int id, int graphic, int colour);   /* Ped_SetAppearance 0x45fc10 */
void ped_remove(int id);                    /* Ped_Remove 0x45faa0 */
/* Ped_StartFiring 0x453210 / Ped_StopFiring 0x453260: on what a player controls (kind 0 car: its
   driver, 2 on foot: the ped). `ref` is the {kind, id} pair of a player record (+0xbc). */
void ped_start_firing(const int32_t *ref);
void ped_stop_firing(const int32_t *ref);
/* Ped_EnterExitKey 0x45f5e0: the player's enter / exit key (`ref` as above, ped = the player's ped). */
void ped_enter_exit_key(int32_t *ref, int ped);
void ped_enter_exit_key_alt(int32_t *ref);   /* Ped_EnterExitKeyAlt 0x45f650 */
void ped_respawn_beside_car(int fast, int ped);   /* Ped_RespawnBesideCar 0x45f980 */
/* Ped_SetTurnInput 0x45f670: the player's turn input `*turn` (±4) to ped `ped`, scaled and clamped. */
void ped_set_turn_input(int16_t *turn, int ped);
/* Player_RespawnAtStation 0x4601a0: kind 0 police station (busted), 1 hospital (wasted). */
void player_respawn_at_station(Ped *p, int kind);
/* ---- creation and visibility (ped_spawn.c) ---- */
/* Ped_Create 0x453e90: a free ped of 0..199 at (x, y, z) 16.16 with speed, angle, anim; kind 0x16
   makes it a group follower. Its id, -1 if none is free. */
int ped_create(int32_t x, int32_t y, int32_t z, int speed, int angle, int anim, int kind);
void ped_create_in_slot(int32_t x, int32_t y, int32_t z, int angle, int slot);   /* Ped_CreateInSlot 0x454090 */
int ped_create_special(int32_t x, int32_t y, int32_t z, int speed, int angle, int attach_kind, int attach_id,
                       int off56, int off54);   /* Ped_CreateSpecial 0x454180 */
int ped_create_with_anim(int32_t x, int32_t y, int32_t z, int speed, int angle, int anim);   /* 0x4542e0 */
bool ped_create_anim41(int32_t x, int32_t y, int32_t z, int angle);   /* Ped_CreateAnim41 0x454430 */
int ped_place_and_send(int id, int32_t x, int32_t y, int32_t z, int angle, int32_t dx, int32_t dy);   /* 0x454460 */
void ped_panic_near(int32_t x, int32_t y, int32_t z, int ped);   /* Ped_PanicNear 0x452f30 (ped: the cause, -1) */
void ped_clear_targets_in_block(int32_t x, int32_t y);   /* Ped_ClearTargetsInBlock 0x453280 */
bool ped_is_visible_recent(const Ped *p);   /* Ped_IsVisibleRecent 0x453540 */
bool ped_is_on_screen(const Ped *p);        /* Ped_IsOnScreen 0x453480 */
bool pos_is_near_screen(int32_t x, int32_t y);   /* Pos_IsNearScreen 0x453630 */
/* ---- cars (ped_car.c) ---- */
void ped_create_car_driver(Car *c);         /* Ped_CreateCarDriver 0x44f7a0 */
void ped_eject_driver(Car *c);              /* Ped_EjectDriver 0x44f510 */
void ped_send_to_car_door1(Ped *p, int car);   /* Ped_SendToCarDoor1 0x45f4a0 */
void ped_send_to_car_door2(Ped *p, int car);   /* Ped_SendToCarDoor2 0x45f850 */
int ped_driver_leave_car(int car);          /* Ped_DriverLeaveCar 0x45ed10 */
int map_find_pavement_along_road(int *bx, int *by, int *bz, int16_t *angle, int n);   /* 0x45ffb0 */
void player_respawn_multi(Ped *p);          /* Player_RespawnMulti 0x460500 */
/* Map_SlopeDelta 0x454c60: the height change across a slope block of `type` (type map) from (x1, y1) to (x2, y2) */
int map_slope_delta(uint32_t type, int32_t x1, int32_t y1, int32_t x2, int32_t y2);
int peds_in_use(void);                      /* slots with anim != 0 (for checks) */
const CameraTarget *ped_get_pos_rect(int id);   /* Ped_GetPosRect 0x45fb00 */
/* Ref_GetKind1PosRect 0x45fb60: the position record of a ridden train (x, y, z 16.16, size, speed / 10,
   angle), the static record Ped_GetPosRect 0x45fb00 fills too (0x74f10c) */
const CameraTarget *ref_get_kind1_pos_rect(int train);
