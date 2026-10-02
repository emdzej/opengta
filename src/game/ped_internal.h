/* Shared declarations of the ped module's files (ped.c, ped_anim.c, ped_move.c, ped_step.c,
   ped_car.c, ped_spawn.c). Not for other modules: they use ped.h. */
#pragma once
#include "car.h"
#include "carcoll.h"
#include "carphys.h"
#include "coll.h"
#include "game.h"
#include "gmath.h"
#include "obj.h"
#include "ped.h"
#include "player.h"
#include "stubs.h"
#include "../audio/audio.h"
#include <stdbool.h>
#include <stdint.h>

/* The car / object fields the ped code uses that car.h / obj.h don't name (yet), by their offsets in
   the original's records. Only fields before the embedded sprite (cars +0x250, objects +0x2c): their
   offsets are the same on every build. */
#define CAR_I16(c, off) (*(int16_t *)((uint8_t *)(c) + (off)))
#define CAR_I32(c, off) (*(int32_t *)((uint8_t *)(c) + (off)))
#define CAR_U8(c, off) (*(uint8_t *)((uint8_t *)(c) + (off)))
_Static_assert(offsetof(Car, spr) == 0x250, "car sprite");

/* sin / cos tables (0x511e28 / 0x512228) */
#define SIN(a) (g_sin[(a)])
#define COS(a) (g_sin[(a) + 256])

/* The cached block types (0x55fab0, [z][y][x] of block coordinates): bits 0..3 the directions, bits
   4..6 the ground type (0 air, 1 water, 2 road, 3 pavement, 4 field, 5 building), bit 7 slope. The
   original indexes the flat array x + (z * 256 + y) * 256 without checks (an x of 256 reads the next
   row); the port does the same and reads 0 outside the array. */
static inline uint8_t ped_type_cache(int bx, int by, int bz)
{
    long i = (long)bx + ((long)bz * 256 + by) * 256;
    if (i < 0 || i >= (long)sizeof g_game.map->type_cache) return 0;
    return (&g_game.map->type_cache[0][0][0])[i];
}
static inline int ped_ground_type(int bx, int by, int bz) { return (ped_type_cache(bx, by, bz) & 0x70) >> 4; }
static inline int32_t ped_ground_z(int32_t x, int32_t y, int32_t z) { return map_get_ground_z(g_game.map, x, y, z); }

/* ---- module state (bss of the original) ---- */
extern CollBox g_ped_box;                   /* 0x728450 the box the ped code builds its queries with */
typedef struct {                            /* 0x728498, 0x1c bytes: a group of followers */
    int16_t leader;                         /* +0x00 */
    int8_t count;                           /* +0x02 */
    uint8_t pad03;
    int16_t member[12];                     /* +0x04 */
} PedGroup;
enum { PED_GROUPS = 1 };
extern PedGroup g_ped_groups[PED_GROUPS];
extern int32_t g_ped_player_ped;            /* 0x7284c4 the player ped being updated */
extern uint8_t g_ped_728448;                /* 0x728448 ambient spawn cycle */
extern int16_t g_ped_frame4;                /* 0x72844a cycles 0..3 per frame */
extern int8_t g_ped_72844c;                 /* 0x72844c (Ped_TurnAtObstacle) */
extern int8_t g_ped_72844d;                 /* 0x72844d (Ped_Process: detour pattern 1..4) */
extern int32_t g_ped_7284b0;                /* 0x7284b0 (Ped_SpawnAmbient; 0x10 at level start) */
extern int8_t g_ped_7284c8;                 /* 0x7284c8 (Ped_AttackTarget) */
extern int32_t g_ped_spawn_player;          /* 0x7284cc player whose view the next ambient ped is for */
extern int8_t g_ped_group_active;           /* 0x7284d0 */
extern int8_t g_ped_7284d1;                 /* 0x7284d1 (Ped_Process) */
extern int8_t g_ped_spawn_stop;             /* 0x7284d2 Ped_SpawnAmbient found no place: stop for the frame */
extern int16_t g_ped_7284d4;                /* 0x7284d4 */
extern int16_t g_ped_7284d6;                /* 0x7284d6 */
extern int8_t g_ped_7284d8;                 /* 0x7284d8 (Ped_AttackTarget) */
extern int32_t g_ped_enter_ref[2];          /* 0x74f0e0 {2, ped} handed to Ped_TryEnterCar by AI peds */
extern int16_t g_ped_door_step;             /* 0x74f0f4 step of the get-in / get-out animation */
extern uint8_t g_ped_remap_cycle;           /* 0x74f0f6 */
extern int16_t g_ped_74f0f8;                /* 0x74f0f8 (Ped_TryEnterCar) */
extern int8_t g_ped_74f0fa, g_ped_74f0fb;   /* 0x74f0fa, 0x74f0fb (Ped_ComputeStep) */
extern int8_t g_ped_push_count;             /* 0x74f0fc (Ped_Process, state 0x14: pushing peds) */
extern int8_t g_ped_74f0fd, g_ped_74f0fe;   /* 0x74f0fd (Ped_SpawnAmbient), 0x74f0fe (Ped_PanicNear) */
extern int16_t g_ped_74f100;                /* 0x74f100 (Ped_SteerToTarget) */
extern int8_t g_ped_74f102, g_ped_74f103;   /* 0x74f102, 0x74f103 (Ped_CheckCrossing) */
extern int16_t g_ped_groups_used;           /* 0x74f120 */

/* ---- ped.c ---- */
bool ped_test_collision(const Ped *p);      /* Ped_TestCollision 0x44ee50 */
bool ped_get_group_leader_pos(int32_t *x, int32_t *y);   /* Ped_GetGroupLeaderPos 0x44ef10 */
void ped_walk_to_car_side_a(Ped *p);        /* Ped_WalkToCarSideA 0x44efa0 */
void ped_walk_to_car_side_b(Ped *p);        /* Ped_WalkToCarSideB 0x44f040 */
void ped_get_cop_look(int16_t *graphic, uint8_t *remap);   /* Ped_GetCopLook 0x44f210 */
void ped_set_destination(Ped *p, int32_t x, int32_t y, int angle, int mode);   /* Ped_SetDestination 0x45f780 */
void ped_set_dest_objective36(Ped *p, int32_t x, int32_t y, int angle);         /* 0x45f810 */
const CameraTarget *ped_get_pos_rect(int id);   /* Ped_GetPosRect 0x45fb00 */

/* ---- ped_anim.c ---- */
void ped_animate(Ped *p);                   /* Ped_Animate 0x44fa70 */

/* ---- ped_move.c ---- */
void ped_process(Ped *p);                   /* Ped_Process 0x45a3b0 */
void ped_update_riding(Ped *p);             /* Ped_UpdateRiding 0x45cbb0 */
bool map_find_walkable_z(int32_t x, int32_t y, int32_t z);   /* Map_FindWalkableZ 0x459430 */
/* Ped_CheckWallHit 0x459770: the box corners against buildings / air; *ground the type found */
bool ped_check_wall_hit(const CollBox *b, Ped *p, int *hit_type, int *ground, int32_t x, int32_t y);

/* ---- ped_step.c ---- */
void ped_compute_step(int32_t *x, int32_t *y, Ped *p);   /* Ped_ComputeStep 0x456fb0 */
void ped_steer_to_target(Ped *p);           /* Ped_SteerToTarget 0x455040 */
void ped_turn_at_obstacle(Ped *p, int blocked);   /* Ped_TurnAtObstacle 0x4561c0 */
bool ped_check_ahead(const Car *c, const Ped *p);   /* Ped_CheckAhead 0x456640 */
bool ped_is_dir_clear(int32_t x, int32_t y, int32_t z, int angle);       /* Ped_IsDirClear 0x455dc0 */
bool ped_is_dir_clear_far(int32_t x, int32_t y, int32_t z, int angle);   /* Ped_IsDirClearFar 0x455fc0 */

/* ---- ped_car.c ---- */
void ped_sit_in_car(Ped *p);                /* Ped_SitInCar 0x454dd0 */
void ped_finish_enter_car(Ped *p, int car); /* Ped_FinishEnterCar 0x45ddc0 */
void ped_try_enter_car(int32_t ref[2]);     /* Ped_TryEnterCar 0x45def0 */
void ped_player_exit_car(int32_t ref[2], int ped);   /* Ped_PlayerExitCar 0x45d3d0 */
void ped_exit_car_at_park_point(int32_t ref[2]);     /* Ped_ExitCarAtParkPoint 0x45fc60 */

/* ---- ped_spawn.c ---- */
void ped_leave_group(Ped *p);               /* Ped_LeaveGroup 0x44f8d0 */
void ped_fire_weapon(Ped *p);               /* Ped_FireWeapon 0x4532f0 */
bool ped_is_on_screen(const Ped *p);        /* Ped_IsOnScreen 0x453480 */
bool ped_is_near_screen(const Ped *p);      /* Ped_IsNearScreen 0x4536d0 */
void ped_spawn_ambient(int kind);           /* Ped_SpawnAmbient 0x4537b0 */
