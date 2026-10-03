/* The AI controllers ("sentinels" in the original's error text): 129 records of 0x98 bytes at
   0x507ea0 (Sentinel_Get 0x41ad60 accepts 0..0x80; Sentinel_ClearTable 0x41adb0 resets 0..0x7f;
   Sentinel_FindFree 0x41ad30 hands out 0..49). A car whose control mode is 2 / 3 / 9 / 10 is driven
   by the record its +0xd8 names (Sentinel_DriveCar 0x41aed0, the emergency services module); kind 9
   records are mission dummies (dummy.c). The record is kept byte-exact: the original's +0x40 is a Car
   pointer, the port keeps the 32-bit slot and resolves the car from +0x1e (ai_car). Fields whose
   meaning is unknown are uNN; see docs/traffic.md. */
#pragma once
#include "car.h"
#include <stdint.h>

enum { AI_MAX = 129, AI_TABLE = 128, AI_FREE_SEARCH = 50, AI_SIZE = 0x98 };
/* kinds (+0x02) */
enum { AI_KIND_FREE = 0, AI_KIND_AMBULANCE = 1, AI_KIND_POLICE = 2, AI_KIND_FIRE = 6, AI_KIND_DUMMY = 9 };

typedef struct AiCtl {
    int16_t id;                 /* +0x00 own index */
    uint8_t kind;               /* +0x02 AI_KIND_* (0 free) */
    uint8_t u03;
    int32_t u04;                /* +0x04 */
    int16_t path_slot;          /* +0x08 path slot (0x7537d0) the route is in (-1 none) */
    int16_t group;              /* +0x0a dummy convoy group / police pursuit */
    uint8_t arrived;            /* +0x0c */
    uint8_t u0d, u0e, u0f, u10, u11, u12, u13;
    int16_t u14, u16, u18;
    uint8_t u1a;
    uint8_t state;              /* +0x1b */
    uint8_t sub_state;          /* +0x1c */
    uint8_t u1d;
    int16_t car;                /* +0x1e car id (-1) */
    uint8_t u20, u21, u22, u23;
    int32_t u24[5];             /* +0x24..+0x34 (-1) */
    int32_t path_progress;      /* +0x38 */
    uint8_t u3c, u3d, u3e, u3f;
    uint32_t car_ptr;           /* +0x40 Car * in the original (the port resolves +0x1e) */
    int16_t u44;                /* +0x44 */
    uint8_t u46, u47;
    int16_t path_index;         /* +0x48 current node of the route (-1) */
    uint8_t u4a, u4b;
    int16_t u4c;
    uint8_t u4e, u4f;
    int16_t u50, u52, u54, u56; /* +0x50.. (-1) */
    uint8_t lights_state;       /* +0x58 traffic-light override state 0..4 (Sentinel_OverrideLights) */
    uint8_t lights_mode;        /* +0x59 the junction's saved mode (-1 none) */
    uint8_t lights_saved;       /* +0x5a */
    uint8_t lights_x;           /* +0x5b junction block x */
    uint8_t lights_y;           /* +0x5c junction block y */
    uint8_t u5d;
    int16_t junction;           /* +0x5e override junction / roadblock (-1) */
    int16_t foot_ped;           /* +0x60 (-1) */
    int16_t u62;                /* +0x62 (-1) */
    uint8_t dest_x, dest_y, dest_z;   /* +0x64 target block */
    uint8_t u67;
    int16_t u68, u6a;           /* +0x68, +0x6a (-1) */
    int16_t pursuit;            /* +0x6c pursuit group (-1) */
    uint8_t u6e, u6f, u70, u71;
    int16_t u72, u74;
    uint8_t u76;
    uint8_t u77[30];            /* +0x77 10 x 3 bytes */
    uint8_t u95, u96, u97;
} AiCtl;
GAME_OFS(AiCtl, kind, 0x02); GAME_OFS(AiCtl, path_slot, 0x08); GAME_OFS(AiCtl, group, 0x0a);
GAME_OFS(AiCtl, arrived, 0x0c); GAME_OFS(AiCtl, u14, 0x14); GAME_OFS(AiCtl, u1a, 0x1a); GAME_OFS(AiCtl, state, 0x1b);
GAME_OFS(AiCtl, car, 0x1e); GAME_OFS(AiCtl, u24, 0x24); GAME_OFS(AiCtl, path_progress, 0x38);
GAME_OFS(AiCtl, car_ptr, 0x40); GAME_OFS(AiCtl, u44, 0x44); GAME_OFS(AiCtl, path_index, 0x48);
GAME_OFS(AiCtl, u4c, 0x4c); GAME_OFS(AiCtl, u50, 0x50); GAME_OFS(AiCtl, lights_state, 0x58);
GAME_OFS(AiCtl, junction, 0x5e); GAME_OFS(AiCtl, dest_x, 0x64); GAME_OFS(AiCtl, u68, 0x68);
GAME_OFS(AiCtl, pursuit, 0x6c); GAME_OFS(AiCtl, u72, 0x72); GAME_OFS(AiCtl, u76, 0x76);
GAME_OFS(AiCtl, u77, 0x77); GAME_OFS(AiCtl, u95, 0x95);
_Static_assert(sizeof(AiCtl) == AI_SIZE, "AI controller record");

extern AiCtl g_ai[AI_MAX];                  /* 0x507ea0 */

/* Sentinel_Get 0x41ad60: record i (0..0x80), raw bytes at the original's offsets; negative is fatal
   -0x91 ("Illegal request for a sentinel"), past 0x80 NULL */
uint8_t *sentinel_get(int i);
static inline AiCtl *ai_get(int i) { return (AiCtl *)(void *)sentinel_get(i); }
int sentinel_find_free(void);               /* Sentinel_FindFree 0x41ad30: first of 0..49 with kind 0 (-1) */
void sentinel_clear_table(void);            /* Sentinel_ClearTable 0x41adb0: records 0..127 to defaults */
void sentinel_reset(AiCtl *r);              /* Sentinel_Reset 0x41aae0: one record to defaults (id kept) */
/* the controller's car: +0x1e (the original dereferences its Car pointer +0x40) */
static inline Car *ai_car(const AiCtl *r) { return r->car >= 0 && r->car < CAR_MAX ? &g_cars[r->car] : NULL; }
