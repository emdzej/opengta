/* The scripted helicopter (0x40dc80-0x40e19f): the pickup aircraft of a mission's ending (Heli_Spawn
   from the CHOPPER command, the fly-away point from CHOPPER_ENDPOINT): it turns to its landing block,
   flies there, descends, waits 40 frames, takes the player (the camera follows it), climbs, flies to
   the exit point and ends the level. One record (0x501bc8) with its sprite (car info model 88) and a
   shadow object (type 0x5c). See docs/police.md. */
#pragma once
#include "../render/sprite.h"
#include "layout.h"
#include <stdint.h>

typedef struct {
    int16_t id;                 /* +0x00 0 active, -1 none */
    int16_t pad02;
    int32_t tx, ty, tz;         /* +0x04 landing point (16.16) */
    int16_t angle0;             /* +0x10 heading at the spawn */
    int16_t u12;                /* +0x12 (-1) */
    int16_t u14;                /* +0x14 */
    int16_t pad16;
    int32_t state;              /* +0x18 1 turn, 2 fly, 3 descend, 4 wait, 5 climb, 6 fly away, 7 end, 8 done */
    uint8_t u1c;                /* +0x1c (0xff) */
    uint8_t pad1d;
    int16_t u1e;
    int32_t speed;              /* +0x20 */
    int32_t shadow;             /* +0x24 the shadow object */
    int32_t timer;              /* +0x28 */
    int32_t ex, ey;             /* +0x2c exit point (16.16) */
    Sprite spr;                 /* +0x34 x +0x34, y +0x38, z +0x3c, +0x40, angle +0x4c */
} Heli;
GAME_OFS(Heli, tx, 0x04); GAME_OFS(Heli, angle0, 0x10); GAME_OFS(Heli, state, 0x18);
GAME_OFS(Heli, speed, 0x20); GAME_OFS(Heli, shadow, 0x24); GAME_OFS(Heli, ex, 0x2c); GAME_OFS32(Heli, spr, 0x34);

/* Heli_GetPos 0x40dc80: the record 0x501bb0 {x, y, z, ..., speed} */
typedef struct { int32_t x, y, z, u0c, speed; } HeliPos;

extern Heli g_heli;                         /* 0x501bc8 */

const HeliPos *heli_get_pos(void);          /* Heli_GetPos 0x40dc80 */
void heli_init(void);                       /* Heli_Init 0x40dcc0 */
void heli_set_exit_target(int32_t x, int32_t y);   /* Heli_SetExitTarget 0x40dd10 */
/* Heli_Spawn 0x40dd30: at (x, y, z) facing `angle`, to land at (tx, ty, tz) */
void heli_spawn(int32_t x, int32_t y, int32_t z, int angle, int32_t tx, int32_t ty, int32_t tz);
int heli_turn_towards(Heli *h, int32_t x, int32_t y);   /* Heli_TurnTowards 0x40de90 (1 aligned, -1 turning) */
void heli_update_shadow(Heli *h);           /* Heli_UpdateShadow 0x40df30 */
void heli_update(void);                     /* Heli_Update 0x40dfb0 */
