/* Fires and fire engines (the fire module 0x42e600-0x430400): fires are animated objects of types
   0x12 / 0x13 / 0x2e (obj.c; Obj_UpdateAll spreads and moves them). Fire_Register records up to 4
   fires a fire engine is sent to (0x511988, 0x24 bytes each), when the fire is next to a road and no
   recorded fire is near. The fire engines (FireEngine_* 0x42e870-0x430400) are sentinels of kind 6
   (sentinel.h) driving car model 0x2a with a hose object (type 0x32): FireEngine_Dispatch spawns one at
   the fire station nearest the fire, Sentinel_DriveCar drives it along a mode 5 route and calls
   FireEngine_Update each frame, which stops it near the fire, turns the hose, sprays a jet of water
   objects (types 0x30 / 0x31), removes the fire and sends the engine on to the next unattended fire
   or back to its station. See docs/objects.md "Fire engines". */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { FIRE_MAX = 4, FIRE_ENGINES = 3, FIRE_OBJS = 10 };

typedef struct {
    int16_t obj;                /* +0x00 the fire object (-1 free) */
    int16_t x, y, z;            /* +0x02 the road block next to it, in pixels (block * 64) */
    int16_t engine;             /* +0x08 the car id of the fire engine sent (-1 none) */
    int16_t objs[FIRE_OBJS];    /* +0x0a the water jet's objects (-1 none; Fire_ClearObjects scans 9);
                                   objs[9] (+0x1c) counts the frames of spraying (from -1 to 0x104) */
    int16_t extra;              /* +0x1e a fire merged into this one (-1) */
    uint8_t pad20[4];
} Fire;
_Static_assert(sizeof(Fire) == 0x24, "fire record");

typedef struct {
    Fire fires[FIRE_MAX];       /* 0x511988 */
    int16_t count;              /* 0x511984 recorded fires */
    int16_t engines[FIRE_ENGINES];   /* 0x511a1c fire-engine sentinels (-1) */
    int16_t engine_count;       /* 0x511a18 (also the next engines[] slot FireEngine_Spawn writes) */
    int16_t engines_out;        /* 0x511a1a fire engines on the road */
    int32_t timers[FIRE_ENGINES];   /* 0x511978 frames an engine may stay out (2500 at the spawn) */
} FireState;
extern FireState g_fire;

void fire_init(void);                        /* Fire_Init 0x42e600 */
/* Fire_IsNearActive 0x42e680: a recorded fire with an engine id above 0 within 40 blocks (x + y) */
bool fire_is_near_active(int obj);
int fire_extinguish(int engine);             /* Fire_Extinguish 0x42e6f0 (the engine, -1 unknown) */
/* Fire_FindNearestUnattended 0x42e7b0: the fire nearest (squared block distance, as shorts) to the
   sentinel's car that has no engine (-1 none) */
int fire_find_nearest_unattended(int32_t car_x, int32_t car_y);
void fire_register(int obj);                 /* Fire_Register 0x42ef80 */
int fire_has_objects(int engine);            /* Fire_HasObjects 0x42f350 */
void fire_clear_objects(int engine);         /* Fire_ClearObjects 0x42f3a0 */

/* ---- the fire engines ---- */
struct Sentinel;
/* FireEngine_Dispatch 0x42ec70: an engine for the fire info = {object, x, y, z} (pixels) recorded in
   slot `slot`: a new one from the nearest of the 4 fire stations (FireEngine_Spawn 0x42e920), or with
   3 out and no free engine slot one already heading home; its car id, -1 if none */
int fire_engine_dispatch(const int16_t info[4], int slot);
int fire_engine_update(struct Sentinel *s);  /* FireEngine_Update 0x42f460 (from Sentinel_DriveCar; 1: drive on) */
void fire_engine_remove(struct Sentinel *s); /* FireEngine_Remove 0x42e870 */
