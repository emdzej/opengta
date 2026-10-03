/* The police (0x464e20-0x46a09f): the criminal record screen, the police cars and their controllers
   (sentinels of kind 2: patrols, pursuit groups, roadblocks, the cops on foot) and the state machine
   Cop_Update 0x466f10, whose arrest is Player_Busted. The wanted level and the pursuit records are
   wanted.h; the sentinel records sentinel.h. See docs/police.md.

   Roadblocks live in the junction override records (lights.h JunctionOvr, 0x505f00): a junction's
   traffic-light object angle (+0x0c) names the CMP roadblock vertex set (route.h g_roadblock_sets);
   +0x0e is 1 while a roadblock stands there, +0x10 its timer (400 frames, kept up while one of its
   cars is on screen), +0x12 its cars (shorts) and +0x5a their count. g_junction_list (0x50586c, at most
   0x14 used) lists the junctions holding a roadblock. A roadblock cop's controller has +0x5e = the
   junction. */
#pragma once
#include "car.h"
#include "route.h"
#include "sentinel.h"
#include <stdint.h>

enum { POLICE_CARS_MAX = 100, POLICE_OBJ_QUEUE_MAX = 0x70, ROADBLOCK_CARS = 0x24 };

extern int16_t g_police_cars[POLICE_CARS_MAX];        /* 0x50f2a8 the patrol car controllers */
extern int16_t g_police_ncars;                        /* 0x5058a2 */
extern int16_t g_police_obj_queue[POLICE_OBJ_QUEUE_MAX];   /* 0x504f68 objects to delete off screen */
extern int16_t g_police_nobj;                         /* 0x505ef8 (fatal past 99) */

void police_flush_object_delete_queue(void);   /* Police_FlushObjectDeleteQueue 0x464e20 */
void police_show_criminal_record(void);        /* Police_ShowCriminalRecord 0x464ec0 */
void cop_reset_to_patrol(Sentinel *s);         /* Cop_ResetToPatrol 0x4650e0 */
int cop_check_ped_state(int s);                /* Cop_CheckPedState 0x4651c0 (0: the cop was dropped) */
void police_spawn_patrol_cars(void);           /* Police_SpawnPatrolCars 0x465300 */
/* Police_SpawnCarAtTarget 0x4654c0: Police_SpawnCar at the respawn block 0x74f850; the car (-1) */
int police_spawn_car_at_target(int s);
/* Police_SpawnCar 0x4654f0: a police car (model 4) with its driver at block (bx, by, bz) for controller
   s; the car id, -1 if none (or more than 99 patrol cars) */
int police_spawn_car(int s, int bx, int by, int bz);
/* Police_FindNearestCar 0x465b20: the patrol car (states 1 / 4 / 6) nearest (x, y), -1 */
int police_find_nearest_car(int32_t x, int32_t y);
void pursuit_remove_cop(Sentinel *s);          /* Pursuit_RemoveCop 0x465760 */
void cop_dismiss(Car *c, int ped);             /* Cop_Dismiss 0x465870 */
void roadblock_remove_car(Sentinel *s);        /* Roadblock_RemoveCar 0x4658f0 */
void cop_release(Sentinel *s);                 /* Cop_Release 0x465a40 */
int pursuit_find_farthest_cop(int pursuit);    /* Pursuit_FindFarthestCop 0x465bc0 (-1) */
int pursuit_find_nearest_cop(int pursuit);     /* Pursuit_FindNearestCop 0x465cf0 (-1) */
int cop_has_clear_line(Sentinel *s, int n);    /* Cop_HasClearLine 0x465e30 (1 clear, 0 a building, -1) */
void pursuit_update_lead(Sentinel *s);         /* Pursuit_UpdateLead 0x465f90 */
void cop_path_to_route_start(Sentinel *s);     /* Cop_PathToRouteStart 0x466070 */
void cop_path_to_target(Sentinel *s);          /* Cop_PathToTarget 0x466180 */
void roadblock_try_place_ahead(Sentinel *s);   /* Roadblock_TryPlaceAhead 0x466230 */
/* Roadblock_Spawn 0x466450: cars, cops and barriers across the road from vertex v, for junction j */
void roadblock_spawn(const BlockXYZ *v, int j, int pursuit);
void pursuit_recall_cops(Sentinel *s);         /* Pursuit_RecallCops 0x466a50 */
void cop_recall_one(Sentinel *s);              /* Cop_RecallOne 0x466bb0 */
int police_find_nearest_target(Sentinel *s);   /* Police_FindNearestTarget 0x466c70 (criminal, -1) */
void cop_join_nearest_pursuit(Sentinel *s);    /* Cop_JoinNearestPursuit 0x466d90 */
void cop_set_speed_by_wanted(Sentinel *s);     /* Cop_SetSpeedByWanted 0x466e80 */
int cop_update(Sentinel *s);                   /* Cop_Update 0x466f10 (1: Sentinel_DriveCar drives on) */
