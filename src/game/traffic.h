/* Traffic: the dummy (control 0) drivers of the car module and the traffic AI 0x415a50-0x418f80
   (look-ahead, throttle, lanes, junctions, give-way lids and red lights), the generator that moves
   off-screen pool cars to a road just outside each player's view (Traffic_SpawnAroundView 0x4183d0)
   and the pool itself (Traffic_PrimeCarPool 0x418f00, Traffic_InitModelTables 0x418f80). Also the
   car-module helpers they share with the AI drivers (0x406e90-0x4072a0, 0x408330, 0x408440). See
   docs/traffic.md. */
#pragma once
#include "car.h"
#include <stdbool.h>
#include <stdint.h>

extern int16_t g_traffic_recycle;           /* 0x504f38 where Traffic_FindRecyclable continues */
extern uint8_t g_traffic_edge[4];           /* 0x504f3c the edge cycle (0..4) of each player */

/* ---- the car module's probes (0x407160-0x4072a0): 1 free, 0 blocked ---- */
/* Car_IsSpaceFree 0x407160: no moving car (speed != 0) but this one in the block query at (x, y, z) */
bool car_is_space_free(int32_t x, int32_t y, int32_t z, const Car *c);
/* Car_IsBlockedByCar 0x4071c0: false if another car is there (an inactive one is removed if nobody
   sees it); model 0x2f never blocks */
bool car_is_blocked_by_car(int32_t x, int32_t y, int32_t z, const Car *c);
/* Car_IsSpaceClearOfAll 0x407230: nothing there but this car and its driver */
bool car_is_space_clear_of_all(int32_t x, int32_t y, int32_t z, const Car *c);
/* Car_CheckAhead 0x4072a0: the car's pending box moved to (x, y) touches no other car (an inactive
   one is removed if nobody sees it) */
bool car_check_ahead(int32_t x, int32_t y, Car *c);
/* Car_IsPathClear 0x415a50: no live ped but the driver, no heavy (weight 3) object, nothing of kinds 8
   / 10 in the block query at (x, y, z) */
bool car_is_path_clear(int32_t x, int32_t y, int32_t z, const Car *c);

/* ---- horn and lane changes ---- */
void car_set_horn(Car *c, int on);          /* Car_SetHorn 0x406e90: horn / siren, the car ahead moves over */
void car_set_horn_by_id(int car, int on);   /* Car_SetHornById 0x406fc0 (+0x12e; not for the tank) */
void car_start_lane_change(Car *c);         /* Car_StartLaneChange 0x408330 */

/* ---- the dummy driver (Cars_UpdateAll / Car_Update / Car_DummyMove call these) ---- */
void car_dummy_follow_road(Car *c);         /* Car_DummyFollowRoad 0x408440: turns, lanes, slopes */
void car_dummy_drive(Car *c);               /* Car_DummyDrive 0x416610: look-ahead, lights, give way */
void car_dummy_keep_lane(Car *c);           /* Car_DummyKeepLane 0x415fe0 */
void car_lane_check_left(Car *c, int dirs, int step);    /* Car_LaneCheckLeft 0x4162f0 */
void car_lane_check_right(Car *c, int dirs, int step);   /* Car_LaneCheckRight 0x416480 */
void car_dummy_throttle(Car *c);            /* Car_DummyThrottle 0x4170e0 */
void car_update_wheelspin(Car *c);          /* Car_UpdateWheelspin 0x415b10 */
void car_dummy_turn_left(Car *c);           /* Car_DummyTurnLeft 0x4173d0 (give way, one side) */
void car_dummy_turn_right(Car *c);          /* Car_DummyTurnRight 0x417640 (give way, both sides) */
void car_dummy_check_lights(Car *c);        /* Car_DummyCheckLights 0x417980: wait for green */

/* ---- the generator ---- */
int traffic_find_recyclable(void);          /* Traffic_FindRecyclable 0x417b70 (-1 none) */
/* Traffic_RespawnCar 0x417d90: car n to the centre of the road block of (x, y), ground z, heading
   along road direction dir (1 / 2 / 4 / 8); false if a view or an object is in the way */
bool traffic_respawn_car(int n, int32_t x, int32_t y, int32_t z, int dir);
int traffic_angle_to_dir(int angle);        /* Traffic_AngleToDir 0x418390 */
void traffic_spawn_around_view(int player, int near);   /* Traffic_SpawnAroundView 0x4183d0 */
/* the edge scanners 0x418970 / 0x418b40 / 0x418c80 / 0x418dc0 over the view rect (left, right,
   top, bottom pixels): a car on the first suitable lane of direction dir (not when dir == skip) */
bool traffic_try_spawn_top(const int32_t *rect, int car, int dir, int skip);
bool traffic_try_spawn_bottom(const int32_t *rect, int car, int dir, int skip);
bool traffic_try_spawn_left(const int32_t *rect, int car, int dir, int skip);
bool traffic_try_spawn_right(const int32_t *rect, int car, int dir, int skip);
bool traffic_is_road_3x3(int bx, int by, int bz);   /* Traffic_IsRoad3x3 0x418ab0 */
void traffic_prime_car_pool(int n);         /* Traffic_PrimeCarPool 0x418f00 */
void traffic_init_model_tables(void);       /* Traffic_InitModelTables 0x418f80 */
