/* Trains (0x46a9a0-0x46e81f) and the three train helpers of the car module (Train_SpawnCarriages
   0x407c30, Car_OnHit 0x407f00, Coll_ProjectileHit 0x408090). See docs/trains.md.

   A train (0x5c8 bytes, 6 of them at 0x7514a8) is made at level start from a train start the rail
   tracer found (rail.h): four carriages (kind 1; kind 2, a single unit, is never created: see
   train_create_from_map), each carried by two bogies that follow the railway block by block along a
   step table (the curve table 0x4b22e8), stopping at the stations of their track (doors open,
   passengers get off and on), waiting for the next station to be free, crashing into each other and
   into cars. The player boards at a station and rides (the train then runs station to station only
   when told to with the door keys). */
#pragma once
#include "../audio/audio.h"
#include "../render/sprite.h"
#include "layout.h"
#include "obj.h"
#include <stdbool.h>
#include <stdint.h>

enum {
    TRAIN_MAX = 6,              /* 0x7514a8 .. 0x753758 */
    TRAIN_CARS = 4,
    TRAIN_KIND_FOUR = 1,        /* four carriages */
    TRAIN_KIND_SINGLE = 2,      /* one unit (spawn block of road type: never created) */
    TRAIN_CAR_RUNNING = 0xe,    /* carriage +0x139 */
    TRAIN_CAR_WRECKED = 0xd,
    TRAIN_CURVES = 9, TRAIN_CURVE_STEPS = 0x5a,
};
/* train states (+2) */
enum {
    TRAIN_ST_STATION = 2,       /* the station sequence (sub-state +0x1d) */
    TRAIN_ST_RUN = 3,           /* accelerate to the top speed */
    TRAIN_ST_BRAKE = 4,         /* slow down, then the state in +9 */
    TRAIN_ST_SWITCH = 9,        /* wait for the switch in +0x10 */
    TRAIN_ST_LEAVE = 10,        /* wait for the next station to be free */
    TRAIN_ST_RIDDEN = 0xb,      /* stopped with the player on board (the door keys start it) */
    TRAIN_ST_LEFT = 0xc,        /* the player got off: back to the station sequence */
};

/* A bogie: a sprite (one end of the carriage, collision kind 8) and its place on the railway. */
typedef struct {
    Sprite spr;                 /* +0x00 x, y, z, zkey +0xc, angle +0x18 */
    uint8_t bx, by, bz;         /* +0x5c its block */
    uint8_t sub;                /* +0x5f step within the block (an index into the curve) */
    uint8_t in_grid;            /* +0x60 */
    uint8_t piece;              /* +0x61 the direction to the next block (rail.h directions) */
    uint8_t prev;               /* +0x62 the direction it came in by: positions are turned by it */
    uint8_t len;                /* +0x63 steps through this block: 0x40 straight, 0x31 corner, 0x46 curve end, 10 */
    uint8_t slope_dir;          /* +0x64 1 level / up, 2 down */
    uint8_t slope_h;            /* +0x65 height offset of an 8-block slope */
    uint8_t on_slope;           /* +0x66 */
    uint8_t curve;              /* +0x67 curve table row 0..8 */
    uint8_t in_bend;            /* +0x68 between the two ends of a curve (ext 4 / 5) */
    uint8_t blocks;             /* +0x69 counts blocks down after a down-slope piece (9) */
    uint8_t id;                 /* +0x6a (train * 4 + carriage) * 2 + bogie: its collision id */
    uint8_t u6b;
} TrainBogie;
GAME_OFS(TrainBogie, spr, 0x00);
GAME_OFS32(TrainBogie, bx, 0x5c); GAME_OFS32(TrainBogie, sub, 0x5f); GAME_OFS32(TrainBogie, piece, 0x61);
GAME_OFS32(TrainBogie, len, 0x63); GAME_OFS32(TrainBogie, curve, 0x67); GAME_OFS32(TrainBogie, id, 0x6a);
GAME_SIZE32(TrainBogie, 0x6c);

/* A carriage door: the side faces a platform (Train_FindPlatformDoors) and the door object. The
   position is never written (the original clears ped targets at 0, 0 with it). */
typedef struct {
    int32_t x, y, z;            /* +0x00 */
    uint8_t open;               /* +0x0c the side faces a platform */
    uint8_t u0d;
    int16_t obj;                /* +0x0e the door object (type 0xe) or -1 */
    Obj *o;                     /* +0x10 */
} TrainDoor;

typedef struct {
    TrainBogie bogie[2];        /* +0x000 */
    uint8_t bx, by, bz;         /* +0x0d8 the block it is in the grid by (only x, y compared) */
    uint8_t ud8;
    Sprite spr;                 /* +0x0dc the body (collision kind 10), angle +0xf4 */
    uint8_t id;                 /* +0x138 train * 4 + carriage */
    uint8_t state;              /* +0x139 0xe running, 0xd wrecked */
    uint8_t hits;               /* +0x13a hits left before a crash (10) */
    uint8_t orient;             /* +0x13b 0 for directions 3 / 12, 1 for 6 / 9 */
    TrainDoor door[2];          /* +0x13c the left (angle + 0x100) and right (angle - 0x100) side */
    uint8_t backwards;          /* +0x164 2: the body faces against the bogies' line (angle + 0x200) */
    uint8_t crashed;            /* +0x165 */
} TrainCar;
GAME_OFS32(TrainCar, bx, 0xd8); GAME_OFS32(TrainCar, spr, 0xdc); GAME_OFS32(TrainCar, id, 0x138);
GAME_OFS32(TrainCar, door, 0x13c); GAME_OFS32(TrainCar, backwards, 0x164); GAME_SIZE32(TrainCar, 0x168);

typedef struct {
    uint8_t kind;               /* +0x00 1 four carriages, 2 single unit */
    uint8_t dir;                /* +0x01 6 forward, 7 reversed (Train_Reverse) */
    uint8_t state;              /* +0x02 TRAIN_ST_* */
    uint8_t boarded;            /* +0x03 1 no, 2 the player rides */
    uint8_t speed;              /* +0x04 bogies move speed / 10 steps a frame */
    uint8_t u05;
    int16_t rider;              /* +0x06 the ped that boarded (Ped_unk_0045fbe0) */
    uint8_t u08;                /* +0x08 cleared when braking ends */
    uint8_t next_state;         /* +0x09 after braking */
    uint8_t cross_x, cross_y;   /* +0x0a the last level crossing block */
    uint8_t station_x, station_y;   /* +0x0c the last station block */
    uint8_t switch_x, switch_y; /* +0x0e the last switch block */
    uint8_t wait_switch;        /* +0x10 */
    uint8_t front_car, rear_car;     /* +0x11, +0x12 */
    uint8_t front_bogie, rear_bogie; /* +0x13, +0x14 */
    uint8_t station, next_station;   /* +0x15, +0x16 */
    uint8_t u17;
    int16_t passengers;         /* +0x18 to get off; then the doors' countdown (100) */
    int16_t timer;              /* +0x1a */
    uint8_t ped_tick;           /* +0x1c */
    uint8_t sub;                /* +0x1d station sequence 0..5 */
    uint8_t toggle;             /* +0x1e */
    uint8_t u1f, u20;           /* +0x1f, +0x20 (never set: collisions with its own bogies are ignored) */
    uint8_t doors_open;         /* +0x21 */
    uint8_t max_speed;          /* +0x22 0x3c, 0x50 ridden */
    uint8_t door_frame;         /* +0x23 8 closed, 1..7 opening (7 open) */
    TrainCar car[TRAIN_CARS];   /* +0x24 */
    uint8_t cross_count;        /* +0x5c4 (single unit: level crossing passes) */
} Train;
GAME_OFS(Train, rider, 0x06); GAME_OFS(Train, wait_switch, 0x10); GAME_OFS(Train, passengers, 0x18);
GAME_OFS(Train, sub, 0x1d); GAME_OFS(Train, door_frame, 0x23);
GAME_OFS32(Train, car, 0x24); GAME_OFS32(Train, cross_count, 0x5c4); GAME_SIZE32(Train, 0x5c8);

extern Train g_trains[TRAIN_MAX];           /* 0x7514a8 */
extern uint8_t g_train_count;               /* 0x75377b */

/* Train_FindDoorNear's result (0x753758) */
typedef struct { int16_t angle, speed; } TrainNear;
/* Train_GetBoardInfo's record (0x75375c): where the ridden train is, for the camera (Ref_GetKind1PosRect
   0x45fb60 copies it into the ped module's position record, speed / 10) */
typedef struct { int32_t x, y, z; int16_t angle, speed, w, h; } TrainBoardInfo;
/* Train_GetDoorOffsets' record (0x753770): where a ped steps off on each side */
typedef struct { int16_t right_dx, left_dx, right_dy, left_dy; uint8_t right, left; } TrainDoorOffsets;

void train_init_all(void);                   /* Train_InitAll 0x46a9a0 */
int train_update_all(void);                  /* Train_UpdateAll 0x46a9d0 (0x14; 0x15 not started) */
/* Train_Command 0x46a9f0: 1 / 2 board / leave, 3 / 4 the door keys, 5 the speed, 7 fill the board info,
   8 the rider (low byte), 9 the rider = the ped that just boarded; others fatal */
int train_command(int cmd, int train);
int train_create_from_map(void);             /* Train_CreateFromMap 0x46b240 */
void train_update(void);                     /* Train_Update 0x46bd60 */
const TrainNear *train_find_door_near(int32_t x, int32_t y, int32_t z, int r, int car);   /* 0x46ab00 */
int train_check_platform_sides(int train);   /* Train_CheckPlatformSides 0x46ae40 (-1: no platform side) */
const TrainDoorOffsets *train_get_door_offsets(int id);   /* Train_GetDoorOffsets 0x46b090 (NULL: doors shut) */
/* Train_GetCarriage 0x46b200: carriage id's body sprite as dwords {x, y, z, zkey, .., .., angle} */
const int32_t *train_get_carriage(int id);
void train_crash(int train, int car);        /* Train_Crash 0x46d650 */
bool train_is_boarded(int train);            /* Train_IsBoarded 0x46dca0 */
void train_update_door_sprites(int train);   /* Train_UpdateDoorSprites 0x46dcc0 */
void train_load_passengers(int train, int ped);   /* Train_LoadPassengers 0x46e450 */
const TrainBoardInfo *train_get_board_info(void); /* the record Train_Command 7 fills (0x75375c) */
uint8_t *train_get(int i);                   /* Train_Get 0x46e7a0 */
int train_get_count(void);                   /* Train_GetCount 0x46e7c0 */
bool train_any_wrecked(void);                /* Train_AnyWrecked 0x46e7d0 */
int train_curve_entry(int curve, int step, uint8_t *a, uint8_t *b);   /* the curve table, for checks (angle) */

void train_fill_snd(int i, SndTrain *out);     /* what Snd_GatherLoops reads of train i (+4, +0x100, +0x538) */
