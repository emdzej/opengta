/* The ped table (0x7284e0, 620 x 0x100; Ped_Get 0x44f500) and its level-start initialisation
   (Ped_InitAll 0x44f370, Ped_Reset 0x44f230). Slots 0..199 are ambient and mission peds, 200..599
   the drivers of cars 0..399 (ped = car + 200), 600..619 special peds. Fields whose meaning is
   unknown but which Ped_Reset sets are named uNN after their offset. */
#pragma once
#include "../render/sprite.h"
#include "layout.h"
#include <stdint.h>

enum { PED_MAX = 620, PED_DRIVER_FIRST = 200, PED_SPECIAL_FIRST = 600 };

typedef struct Ped {
    int16_t id;                 /* +0x00 */
    int16_t u02, u04;           /* +0x02, +0x04 */
    int16_t speed;              /* +0x06 (the camera target speed) */
    uint8_t move_speed;         /* +0x08 4 walk, 6 fast (Ped_SetMoveSpeed) */
    uint8_t pad09;
    int16_t u0a, u0c, u0e;      /* +0x0a..+0x0e */
    int16_t control;            /* +0x10 control type: -1 none, 0 ambient, 8 + n player n */
    int16_t u12;                /* +0x12 0x80 / -0x80 alternating at init */
    int16_t u14;                /* +0x14 the car of a driver slot */
    int16_t graphic;            /* +0x16 graphic type (0 civilian, 1 cop, ...) */
    int16_t anim;               /* +0x18 anim state; 0 = slot free */
    int16_t u1a;
    uint8_t player_ctl;         /* +0x1c player-controlled */
    uint8_t pad1d;
    int16_t u1e;
    int32_t u20, u24, u28;
    int32_t target_x, target_y; /* +0x2c, +0x30 */
    uint8_t u34;                /* +0x34 (0xff at reset) */
    uint8_t pad35[3];
    int32_t walk_x, walk_y;     /* +0x38, +0x3c walk target */
    int16_t u40;                /* +0x40 */
    int16_t mode;               /* +0x42 */
    int16_t u44;
    uint8_t firing;             /* +0x46 */
    uint8_t u47, u48;
    int8_t health;              /* +0x49 100 alive */
    uint8_t u4a;                /* +0x4a Ped_SetFlag4A (armour) */
    uint8_t pad4b;
    int16_t car;                /* +0x4c car id (-1 on foot) */
    int16_t u4e;
    int16_t attach_kind, attach_id;   /* +0x50, +0x52 1 car, 2 object, 3 ped */
    int16_t u54, u56;
    int16_t carried;            /* +0x58 carried object (-1) */
    int16_t u5a, u5c;
    uint8_t remap;              /* +0x5e */
    uint8_t pad5f;
    int32_t weapon;             /* +0x60 0 none, 1..4 */
    int16_t u64, u66;
    uint8_t u68;
    uint8_t pad69[3];
    int32_t objective;          /* +0x6c 0x19 wander, 0x25 player ... */
    int32_t state;              /* +0x70 2 walk, 7 in car, 0xc dead ... */
    int32_t u74, u78, u7c, u80;
    int16_t u84;
    int16_t target_ped;         /* +0x86 */
    uint8_t u88;
    uint8_t group, group_slot;  /* +0x89, +0x8a */
    uint8_t u8b;
    uint8_t train;              /* +0x8c train index (0xff none) */
    uint8_t pad8d[3];
    Sprite spr;                 /* +0x90 x +0x90, y +0x94, z +0x98, z key +0x9c, angle +0xa8 */
    uint8_t uec;                /* +0xec */
    uint8_t paded;
    int16_t uee, uf0;
    uint8_t padf2[2];
    int32_t uf4;
    int16_t uf8, ufa, ufc;
    uint8_t ufe;
    uint8_t padff;
} Ped;
GAME_OFS(Ped, speed, 0x06); GAME_OFS(Ped, move_speed, 0x08); GAME_OFS(Ped, control, 0x10);
GAME_OFS(Ped, u14, 0x14); GAME_OFS(Ped, graphic, 0x16); GAME_OFS(Ped, anim, 0x18);
GAME_OFS(Ped, player_ctl, 0x1c); GAME_OFS(Ped, u1e, 0x1e); GAME_OFS(Ped, target_x, 0x2c);
GAME_OFS(Ped, u34, 0x34); GAME_OFS(Ped, walk_x, 0x38); GAME_OFS(Ped, mode, 0x42);
GAME_OFS(Ped, firing, 0x46); GAME_OFS(Ped, health, 0x49); GAME_OFS(Ped, u4a, 0x4a);
GAME_OFS(Ped, car, 0x4c); GAME_OFS(Ped, attach_kind, 0x50); GAME_OFS(Ped, carried, 0x58);
GAME_OFS(Ped, remap, 0x5e); GAME_OFS(Ped, weapon, 0x60); GAME_OFS(Ped, u68, 0x68);
GAME_OFS(Ped, objective, 0x6c); GAME_OFS(Ped, state, 0x70); GAME_OFS(Ped, u80, 0x80);
GAME_OFS(Ped, target_ped, 0x86); GAME_OFS(Ped, group, 0x89); GAME_OFS(Ped, train, 0x8c);
GAME_OFS(Ped, spr, 0x90); GAME_OFS32(Ped, uec, 0xec); GAME_OFS32(Ped, uee, 0xee);
GAME_OFS32(Ped, uf4, 0xf4); GAME_OFS32(Ped, ufe, 0xfe); GAME_SIZE32(Ped, 0x100);

extern Ped g_peds[PED_MAX];                 /* 0x7284e0 */
extern int g_peds_active;                   /* 0x74f104: spawned peds (Ped_SpawnInSlot counts) */

static inline Ped *ped_get(int id) { return &g_peds[id]; }      /* Ped_Get 0x44f500 */
void ped_reset(int id);                     /* Ped_Reset 0x44f230 */
void ped_init_all(void);                    /* Ped_InitAll 0x44f370 */
/* Ped_SpawnInSlot 0x44f680: ped `slot` at (x, y, z) unless the slot is taken (anim != 0). */
void ped_spawn_in_slot(int32_t x, int32_t y, int32_t z, int speed, int angle, int anim, int slot, int car);
void ped_set_player_controlled(int id);     /* Ped_SetPlayerControlled 0x45fa50 */
static inline void ped_set_control_type(int id, int t) { g_peds[id].control = (int16_t)t; }   /* 0x44f0e0 */
static inline void ped_set_weapon_raw(int id, int w) { g_peds[id].weapon = w; }               /* 0x4532d0 */
static inline void ped_set_flag4a(Ped *p, int on) { p->u4a = on == 1; }                       /* 0x44ef80 */
static inline void ped_set_move_speed(Ped *p, int fast) { p->move_speed = fast == 1 ? 6 : 4; }  /* 0x44ef60 */
void ped_set_appearance(int id, int graphic, int colour);   /* Ped_SetAppearance 0x45fc10 */
int peds_in_use(void);                      /* slots with anim != 0 (for checks) */
