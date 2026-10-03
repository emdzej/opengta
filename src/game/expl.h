/* Explosions (0x425170-0x426320): 25 animated sprite slots (0x50f7e8, 0x64 bytes each); one explosion
   takes four consecutive free slots (the first free group of 0..21), one per quarter of a 2 x 2 block
   picture (frames 1-12, 13-24, 25-36, 37-48 of the explosion sprite group 0x774ef4, a step every 2
   game frames). The slots are in the collision grid as kind 0xc (the renderer draws them, the
   queries see them). The blast (Expl_DamageArea) sets peds on fire or kills them, wrecks cars and
   kicks objects; gas tanks (object type 0x45) close to it go off 5 frames later through the 25
   delayed explosions (0x50f610). The block-face explosions 0x425520 / 0x425780 / 0x4258d0 are ported
   in mission_obj.c (as the targets of the mission thunks 0x475700-0x475720). */
#pragma once
#include "../render/sprite.h"
#include <stdbool.h>
#include <stdint.h>

enum { EXPL_SLOTS = 25, EXPL_GROUPS = 0x16, EXPL_DELAYED = 25 };

typedef struct {
    Sprite spr;                 /* +0x00 */
    int16_t frame;              /* +0x5c 0 free, else the frame within the group's 48 */
    int16_t tick;               /* +0x5e */
    int16_t u60;                /* +0x60 (-1 at Expl_Init) */
    uint8_t pad62[2];
} ExplSlot;
_Static_assert(sizeof(void *) != 4 || sizeof(ExplSlot) == 0x64, "explosion slot");

typedef struct {
    int32_t x, y, z;            /* +0x0 */
    int16_t timer;              /* +0xc frames left (0 idle) */
    int16_t owner;              /* +0xe player */
} ExplDelayed;

extern ExplSlot g_expl[EXPL_SLOTS];            /* 0x50f7e8 */
extern ExplDelayed g_expl_delayed[EXPL_DELAYED];   /* 0x50f610 */

void expl_init(void);                          /* Expl_Init 0x425c50 */
/* Expl_Create 0x425170 (and its copy thunk_Expl_Create 0x425b50): an explosion centred on (x, y) at
   height z (16.16), caused by player `owner` (-1 none). */
void expl_create(int32_t x, int32_t y, int32_t z, int owner);
void expl_car_explode(int car);                /* Expl_CarExplode 0x425480: centre and the 4 corners */
/* Expl_AtFaceIfSolid 0x425960: an explosion on side `face` (0 -x, 1 +x, 2 -y, 3 +y) of block (bx, by,
   bz) if that face is solid, and two fires beside it where there is none; true if it exploded. */
bool expl_at_face_if_solid(int bx, int by, int bz, int face, int owner);
void expl_update_all(void);                    /* Expl_UpdateAll 0x425b60 */
/* Expl_DamageArea 0x425ca0: the blast at (x, y) in the layer of z on everything in the 3 x 3 cells. */
void expl_damage_area(int32_t x, int32_t y, int32_t z, int owner);
int expl_active(void);                         /* slots in use (for checks) */
