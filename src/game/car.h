/* The car module (0x405790-0x40c0c0): the car table (0x4be248, 400 x 0x2b0; Car_Get 0x408200),
   creation (Car_Init 0x4067c0, the spawners), the per-frame loop (Cars_UpdateAll 0x40adc0,
   Car_Update 0x40a640), player controls, wrecks, doors, sirens, damage and the accessors. Car n's
   driver is ped 200 + n. The rigid body is carphys.h, collision carcoll.h, the style's car info
   records carinfo.h. See docs/cars.md. Fields whose meaning is unknown are named uNN. */
#pragma once
#include "../audio/audio.h"
#include "../render/sprite.h"
#include "carphys.h"
#include "coll.h"
#include "layout.h"
#include <stdbool.h>
#include <stdint.h>

enum { CAR_MAX = 400, CAR_MODELS = 100 };

/* control modes (+0x04) */
enum { CAR_CTL_DUMMY = 0, CAR_CTL_PHYSICS = 1, CAR_CTL_AI2 = 2, CAR_CTL_AI3 = 3, CAR_CTL_AI9 = 9,
       CAR_CTL_AI10 = 10, CAR_CTL_HUNT = 0x32 };
/* car info vtypes (+0x34) */
enum { CAR_VT_BUS = 0, CAR_VT_FRONT = 1, CAR_VT_2 = 2, CAR_VT_BIKE = 3, CAR_VT_CAR = 4, CAR_VT_TRAIN = 8,
       CAR_VT_TRAM = 9, CAR_VT_BOAT = 13, CAR_VT_TANK = 14 };

typedef struct Car {
    int16_t id;                 /* +0x00 own index */
    int16_t driver;             /* +0x02 driver ped id (-1 none) */
    int16_t control;            /* +0x04 CAR_CTL_*: 0 dummy, 1 physics / player, 2/3/9/10 AI, 0x32 hunter */
    int16_t active;             /* +0x06 visible to a player (or bomb / fire / AI) this frame */
    int16_t status;             /* +0x08 -1 free slot; 7 fallen bike; 1 / 8 bike frames */
    int16_t input;              /* +0x0a dummy accel input; -car info +0x10 for wrecks */
    int32_t saved_x, saved_y;   /* +0x0c Car_SavePos 0x40c0a0 */
    int32_t front_x, front_y;   /* +0x14 front wheel point */
    int16_t speed;              /* +0x1c pixels per frame (physics: the distance moved) */
    int16_t u1e;
    int16_t accel;              /* +0x20 car info +0x0e */
    int16_t model;              /* +0x22 */
    int16_t door_dx, door_dy;   /* +0x24 the first door record of the car info (+0xae / +0xb0) */
    int16_t max_speed;          /* +0x28 car info +0x0a (60 for model 4) */
    int16_t min_speed;          /* +0x2a car info +0x0c */
    int16_t cam_w;              /* +0x2c width (car info +0) */
    int16_t u2e;
    int16_t depth;              /* +0x30 car info +4 (the box id) */
    int16_t braking;            /* +0x32 car info +0x10 */
    int16_t vtype;              /* +0x34 car info +0x6a */
    int16_t length;             /* +0x36 minus the car info height (negative) */
    CollBox box;                /* +0x38 current hitbox */
    int16_t u7c, u7e;           /* +0x7c (1), +0x7e (100) */
    int16_t enter_delay;        /* +0x80 frames until +0x88 is set after a driver got in */
    int16_t u82, u84, u86;
    int16_t unk88;              /* +0x88 engine on (a driver sits in it): the player controls work */
    int16_t half_w, half_l;     /* +0x8a, +0x8c pixels */
    int16_t u8e;
    int16_t front_heading;      /* +0x90 */
    int16_t turn_progress;      /* +0x92 */
    int16_t turn_dirs;          /* +0x94 road dirs the turn started on */
    int16_t turn_delta;         /* +0x96 */
    int16_t u98, u9a;
    int32_t bomb;               /* +0x9c 1 armed on entry -> 2 timer, 3 event on entry, 4 by damage, 5 speed, 6 */
    int16_t bomb_timer;         /* +0xa0 */
    uint16_t road_dirs;         /* +0xa2 road direction bits under the car (from the heading for physics cars) */
    int16_t ua4, ua6, ua8, uaa; /* +0xa6 distance counter, +0xa8 (-1) */
    int32_t rear_x, rear_y;     /* +0xac rear wheel point */
    int16_t rear_heading;       /* +0xb4 */
    int16_t brake;              /* +0xb6 Car_BeginBrake 0x40be90 */
    int16_t hit_car;            /* +0xb8 car last touched (-1) */
    int16_t uba, ubc, ube;      /* +0xbe impact speed */
    int16_t unkc0;              /* +0xc0 1 with Car_SetPhysicsControl */
    int16_t lane_mode;          /* +0xc2 */
    int16_t uc4;
    int16_t sinking;            /* +0xc6 water: 1 splash, 8 sinking, 9 (car deleted) */
    int16_t door1;              /* +0xc8 door 1 animation step (deltas 6..9) */
    int16_t keep_active;        /* +0xca */
    int16_t ucc, uce;           /* +0xce (max speed & 7) - 4 */
    int16_t frames;             /* +0xd0 frames updated */
    int16_t door2;              /* +0xd2 door 2 step (deltas 11..14) */
    int16_t prev_dirs;          /* +0xd4 */
    int16_t rear_door;          /* +0xd6 rear door step (deltas 11..14) */
    int16_t sentinel;           /* +0xd8 AI driver record (0x507ea0) */
    int16_t siren_tick;         /* +0xda */
    int16_t udc, ude;
    int32_t ue0, ue4, ue8;      /* +0xe0 x, y, z at creation */
    int16_t unkec;              /* +0xec mission cars with a killer driver (MisCar_MakeKiller) */
    int16_t siren_type;         /* +0xee 1 / 2 / 3 for models 0x13 / 1 / 0x2b */
    int16_t turret;             /* +0xf0 tank turret angle / bike lean */
    int16_t uf2, uf4, uf6;
    int16_t base_frame;         /* +0xf8 car info sprite number */
    int16_t hit_dir;            /* +0xfa direction of the last hit relative to the heading */
    int16_t damage;             /* +0xfc 0..100; 0x65 burnt out */
    int16_t burning;            /* +0xfe fires on the car */
    int16_t u100, u102;
    int16_t cruise;             /* +0x104 */
    int16_t u106;
    int32_t z_offset;           /* +0x108 height above ground (0x20000 / 0x30000 / 0x40000) */
    int16_t u10c;
    int16_t lane;               /* +0x10e lane offset in the block */
    int16_t falling;            /* +0x110 frames in the air (0 on the ground) */
    int16_t u112, u114;
    int16_t drive_mode;         /* +0x116 MisCar_SetDriveMode1/2 */
    uint8_t u118, counter119;   /* +0x119 */
    int16_t horn;               /* +0x11a horn / siren sound state (tank: turret object) */
    int16_t siren_state;        /* +0x11c lights 1 / 2; 99 parked car alarm (tank, 0x2a: an object) */
    int16_t u11e, u120, u122;
    int16_t u124;               /* +0x124 (100) */
    int16_t u126;
    int16_t owner_status;       /* +0x128 1 normal, 2/3/4 transient, 99 mission-locked (no damage) */
    int16_t u12a, u12c, u12e;
    int16_t u130, u132;
    int16_t script_line;        /* +0x134 MISSION.INI line that created it */
    int16_t horn_time;          /* +0x136 */
    int8_t horn_pattern;        /* +0x138 (-1 none) */
    uint8_t unk139;             /* +0x139 1 = traffic / parked slot owned by the spawner */
    uint8_t u13a, pad13b;
    int16_t player;             /* +0x13c player index of the driver / last attacker */
    int16_t u13e;
    int32_t u140;
    uint8_t physics;            /* +0x144 float physics active (CarPhys_Begin) */
    uint8_t brake_in;           /* +0x145 player's brake (control byte 2) */
    uint8_t handbrake_in;       /* +0x146 player's handbrake (control byte 3, "fire") */
    int8_t gear;                /* +0x147 -1 reverse, 0 none, 1 forward (control byte 5) */
    uint8_t map_hit;            /* +0x148 Car_CollideMap state 0..3 */
    uint8_t obj_hit;            /* +0x149 Car_CollideObjects state 0..3 */
    int16_t brake_peak;         /* +0x14a peak speed for the bus air brake (sound) */
    float thrust;               /* +0x14c engine force (car info +0x80, less with damage) */
    float skid;                 /* +0x150 */
    uint8_t pad154[4];
    float u158[8];              /* +0x158 zeroed by Car_Init */
    float steer_cos, steer_sin; /* +0x178 front wheel direction of the last step */
    float u180, u184;
    float thrust_in;            /* +0x188 drive force of this frame */
    float steer;                /* +0x18c front wheel angle (radians, absolute: body angle + pi/2 straight) */
    PhysBody body;              /* +0x190 */
    uint8_t skid_l, skid_r;     /* +0x1d8 skid mark counters */
    uint8_t pad1da[2];
    CollBox box_saved;          /* +0x1dc pending hitbox (of next_*) */
    int32_t next_x, next_y, next_z;   /* +0x220 */
    int16_t next_heading;       /* +0x22c */
    int16_t u22e;
    float impulse_x, impulse_y; /* +0x230 pending impulse */
    float impulse_px, impulse_py;     /* +0x238 its point (pixels) */
    uint8_t impulse_state;      /* +0x240 1 set, 2 apply next frame */
    uint8_t pad241;
    int16_t speed2;             /* +0x242 distance moved last frame (physics) */
    int32_t u244;               /* +0x244 1: player controls ignored */
    int32_t script_held;        /* +0x248 */
    uint8_t remap;              /* +0x24c */
    uint8_t pad24d[3];
    Sprite spr;                 /* +0x250 x +0x250, y +0x254, z +0x258, zkey +0x25c, angle +0x268 */
    const uint8_t *info;        /* +0x2ac car_info record */
} Car;
GAME_OFS(Car, driver, 0x02); GAME_OFS(Car, control, 0x04); GAME_OFS(Car, active, 0x06);
GAME_OFS(Car, status, 0x08); GAME_OFS(Car, input, 0x0a); GAME_OFS(Car, saved_x, 0x0c); GAME_OFS(Car, front_x, 0x14);
GAME_OFS(Car, speed, 0x1c); GAME_OFS(Car, accel, 0x20); GAME_OFS(Car, model, 0x22); GAME_OFS(Car, door_dx, 0x24);
GAME_OFS(Car, max_speed, 0x28); GAME_OFS(Car, min_speed, 0x2a); GAME_OFS(Car, cam_w, 0x2c); GAME_OFS(Car, depth, 0x30);
GAME_OFS(Car, braking, 0x32); GAME_OFS(Car, vtype, 0x34); GAME_OFS(Car, length, 0x36); GAME_OFS(Car, box, 0x38);
GAME_OFS(Car, u7c, 0x7c); GAME_OFS(Car, enter_delay, 0x80); GAME_OFS(Car, unk88, 0x88);
GAME_OFS(Car, half_w, 0x8a); GAME_OFS(Car, half_l, 0x8c); GAME_OFS(Car, front_heading, 0x90);
GAME_OFS(Car, turn_progress, 0x92); GAME_OFS(Car, turn_dirs, 0x94); GAME_OFS(Car, turn_delta, 0x96);
GAME_OFS(Car, bomb, 0x9c); GAME_OFS(Car, bomb_timer, 0xa0); GAME_OFS(Car, road_dirs, 0xa2); GAME_OFS(Car, ua6, 0xa6);
GAME_OFS(Car, rear_x, 0xac); GAME_OFS(Car, rear_heading, 0xb4); GAME_OFS(Car, brake, 0xb6); GAME_OFS(Car, hit_car, 0xb8);
GAME_OFS(Car, ube, 0xbe); GAME_OFS(Car, unkc0, 0xc0); GAME_OFS(Car, lane_mode, 0xc2); GAME_OFS(Car, sinking, 0xc6);
GAME_OFS(Car, door1, 0xc8); GAME_OFS(Car, keep_active, 0xca); GAME_OFS(Car, uce, 0xce); GAME_OFS(Car, frames, 0xd0);
GAME_OFS(Car, door2, 0xd2); GAME_OFS(Car, prev_dirs, 0xd4); GAME_OFS(Car, rear_door, 0xd6); GAME_OFS(Car, sentinel, 0xd8);
GAME_OFS(Car, siren_tick, 0xda); GAME_OFS(Car, udc, 0xdc); GAME_OFS(Car, unkec, 0xec); GAME_OFS(Car, siren_type, 0xee);
GAME_OFS(Car, turret, 0xf0); GAME_OFS(Car, base_frame, 0xf8); GAME_OFS(Car, hit_dir, 0xfa); GAME_OFS(Car, damage, 0xfc);
GAME_OFS(Car, burning, 0xfe); GAME_OFS(Car, cruise, 0x104); GAME_OFS(Car, z_offset, 0x108); GAME_OFS(Car, lane, 0x10e);
GAME_OFS(Car, falling, 0x110); GAME_OFS(Car, drive_mode, 0x116); GAME_OFS(Car, counter119, 0x119); GAME_OFS(Car, horn, 0x11a);
GAME_OFS(Car, siren_state, 0x11c); GAME_OFS(Car, u124, 0x124); GAME_OFS(Car, owner_status, 0x128); GAME_OFS(Car, u12a, 0x12a);
GAME_OFS(Car, script_line, 0x134); GAME_OFS(Car, horn_time, 0x136); GAME_OFS(Car, horn_pattern, 0x138);
GAME_OFS(Car, unk139, 0x139); GAME_OFS(Car, player, 0x13c); GAME_OFS(Car, physics, 0x144); GAME_OFS(Car, gear, 0x147);
GAME_OFS(Car, map_hit, 0x148); GAME_OFS(Car, obj_hit, 0x149); GAME_OFS(Car, brake_peak, 0x14a);
GAME_OFS(Car, thrust, 0x14c); GAME_OFS(Car, skid, 0x150); GAME_OFS(Car, u158, 0x158); GAME_OFS(Car, steer_cos, 0x178);
GAME_OFS(Car, thrust_in, 0x188); GAME_OFS(Car, steer, 0x18c); GAME_OFS(Car, body, 0x190); GAME_OFS(Car, skid_l, 0x1d8);
GAME_OFS(Car, box_saved, 0x1dc); GAME_OFS(Car, next_x, 0x220); GAME_OFS(Car, next_heading, 0x22c);
GAME_OFS(Car, impulse_x, 0x230); GAME_OFS(Car, impulse_px, 0x238); GAME_OFS(Car, impulse_state, 0x240);
GAME_OFS(Car, speed2, 0x242); GAME_OFS(Car, u244, 0x244); GAME_OFS(Car, script_held, 0x248); GAME_OFS(Car, remap, 0x24c);
GAME_OFS(Car, spr, 0x250); GAME_OFS32(Car, info, 0x2ac); GAME_SIZE32(Car, 0x2b0);

extern Car g_cars[CAR_MAX];                 /* 0x4be248 */
extern int16_t g_car_model_index[CAR_MODELS];   /* 0x4be178: model -> car info record (-1 none) */
extern int g_cars_count;                    /* 0x501554: slots in use up to here (Cars_GetCount) */
extern int g_cars_active;                   /* 0x4be170: active cars of the last Cars_UpdateAll */
extern uint16_t g_traffic_models[3][100];   /* 0x504ce0: shuffled model distributions */
extern int g_traffic_cycle;                 /* 0x504f40 next entry of the traffic model row */
extern int16_t g_car_forced_accel[CAR_MAX]; /* 0x4bde08 (Car_SetForcedAccel 0x4082b0) */

static inline Car *car_get(int n) { return &g_cars[n]; }        /* Car_Get 0x408200 */
void cars_init(void);                       /* Cars_Init 0x4070a0 */
int cars_in_use(void);                      /* slots with status != -1 (for checks) */
/* the style's car info record of a model (via 0x4be178 and the record table), NULL if none */
const uint8_t *car_info_of_model(int model);

/* ---- creation ---- */
/* Car_Init 0x4067c0: slot n becomes a car of `model` at (x, y, z) facing `angle` (z snapped to the
   ground), grid, box and rigid body set up. */
void car_init(int32_t x, int32_t y, int32_t z, int angle, int model, int n);
int car_spawn_ex(int32_t x, int32_t y, int32_t z, int model, int driver, int angle, int remap);            /* Car_SpawnEx 0x4078d0 */
int car_spawn_ex_on_ground(int32_t x, int32_t y, int32_t z, int model, int driver, int angle, int remap);  /* 0x407ab0 */
int car_spawn_on_road(int32_t x, int32_t y, int32_t z, int model, int driver);   /* Car_SpawnOnRoad 0x407310 (0xffff none) */
int car_spawn_with_driver(int32_t x, int32_t y, int32_t z, int model);           /* Car_SpawnWithDriver 0x4075b0 */
/* thunk_Car_SpawnParked 0x4763d0 -> Car_SpawnModel47AtBlock 0x4076f0: model 0x2f on road block (bx, by,
   bz - 1) facing its direction bits, hunter control */
int car_spawn_parked(int bx, int by, int bz);
void car_reset_from_info(int car);          /* Car_ResetFromInfo 0x4063d0 */
void car_delete(int car);                   /* Car_Delete 0x40b660 */

/* ---- per frame ---- */
void cars_update_all(void);                 /* Cars_UpdateAll 0x40adc0 */
void car_update(Car *c);                    /* Car_Update 0x40a640 */
void car_apply_player_controls(Car *c);     /* Car_ApplyPlayerControls 0x40a2e0 */
void car_dummy_move(Car *c);                /* Car_DummyMove 0x409a30 */
void car_update_wreck(Car *c);              /* Car_UpdateWreck 0x408af0 */
void car_stop_physics(Car *c);              /* Car_StopPhysics 0x40a5b0 */
void car_emit_skidmarks(Car *c);            /* Car_EmitSkidmarks 0x409250 */
void car_set_wheel_angles(Car *c, int steer);   /* Car_SetWheelAngles 0x4090b0 */
void car_snap_road_heading(Car *c);         /* Car_SnapHeading 0x409700 (car_snap_heading is 0x475730) */
void car_snap_to_road_dir(Car *c);          /* Car_SnapToRoadDir 0x407e30 */

/* ---- pose and box ---- */
void car_restore_box(Car *c);               /* Car_RestoreBox 0x405a80 */
void car_save_box(Car *c);                  /* Car_SaveBox 0x405b30 */
void car_box_move_x(Car *c);                /* Car_BoxMoveX 0x405be0 */
void car_box_move_y(Car *c);                /* Car_BoxMoveY 0x405c40 */
void car_sync_physics(Car *c);              /* Car_SyncPhysics 0x405ca0 */
void car_commit_move(Car *c);               /* Car_CommitMove 0x405df0 */
void car_bisect_move(Car *c);               /* Car_BisectMove 0x405ed0 */

/* ---- state ---- */
void car_set_physics_control(int n);        /* Car_SetPhysicsControl 0x4082d0 */
static inline void car_set_dummy_control(int n) { g_cars[n].control = 0; }   /* Car_SetDummyControl 0x408300 */
void car_add_damage(Car *c, int n);         /* Car_AddDamage 0x40a200 */
void car_set_damage(Car *c, int n);         /* Car_SetDamage 0x40a280 */
static inline int car_get_damage_by_id(int n) { return (uint8_t)g_cars[n].damage; }   /* 0x40a1e0 (low byte) */
void car_repair(Car *c);                    /* Car_Repair 0x40abd0 */
void car_siren_off(Car *c);                 /* Car_SirenOff 0x40ac30 */
void car_siren_on(Car *c);                  /* Car_SirenOn 0x40ac90 */
void car_reset_siren99(Car *c);             /* Car_ResetSiren99 0x405960 */
bool car_has_door(const Car *c, int n);     /* Car_HasDoor 0x405990 (in fact: has the car info a remap n) */
void car_set_remap(Car *c, int remap);      /* Car_SetRemap 0x4059d0 */
void car_assign_cycle_remap(int car);       /* Car_AssignCycleRemap 0x405a00 */
void car_play_crash_sound(const Car *c);    /* Car_PlayCrashSound 0x406390 */
void car_sync_driver_sprite(int ped);       /* Car_SyncDriverSprite 0x405860 */
void car_on_driver_enter(int car);          /* Car_OnDriverEnter 0x407000 */
void car_set_driver_by_id(int car, int ped);   /* Car_SetDriverById 0x40bc20 */
void car_set_owner_status(int car, int v);  /* Car_SetOwnerStatus 0x40bbc0 */
bool car_info_is_convertible(int car);      /* CarInfo_IsConvertible 0x40bdb0 */
bool car_is_on_screen(const Car *c);        /* Car_IsOnScreen 0x40acd0 */
bool pos_is_near_any_view(int32_t x, int32_t y);   /* Pos_IsNearAnyView 0x40ad40 */
bool car_is_near_view(const Car *c);        /* Car_IsNearView 0x409960 */
bool player_is_car_view_target(int car);    /* Player_IsCarViewTarget 0x462a60 (a player's camera follows it) */
static inline void car_set_status(int n, int s) { g_cars[n].status = (int16_t)s; }   /* Car_SetStatus 0x407070 */
static inline bool car_is_burning(const Car *c) { return c->burning > 0; }          /* Car_IsBurning 0x40be40 */
static inline bool car_is_turning(const Car *c) { return c->turn_delta != 0; }      /* Car_IsTurning 0x40be00 */

/* ---- doors (the car side of entering / leaving: Ped_* call these) ---- */
static inline int car_get_door1(int n) { return g_cars[n].door1; }   /* Car_GetDoor1 0x40b8a0 */
bool car_open_door1_step(int n);            /* Car_OpenDoor1Step 0x40b8c0: true when fully open */
bool car_close_door1_step(int n);           /* Car_CloseDoor1Step 0x40b940: true when closed */
bool car_open_door2_step(int n);            /* Car_OpenDoor2Step 0x40b9c0 */
bool car_close_door2_step(int n);           /* Car_CloseDoor2Step 0x40ba40 */
bool car_open_rear_door_step(int n);        /* Car_OpenRearDoorStep 0x40bac0 */
bool car_close_rear_door_step(int n);       /* Car_CloseRearDoorStep 0x40bb40 */
/* the world position of the driver's door (car info door 0 offset, rotated by the heading), for the
   enter / exit animation: x, y 16.16 (a helper of the port; Ped_* compute the same inline) */
void car_get_door_position(const Car *c, int32_t *x, int32_t *y);

/* ---- camera (Car_GetCamTarget 0x408220: the record 0x50155c) ---- */
typedef struct {
    int32_t x, y, z;            /* z = sprite z - z_offset */
    int16_t w, h;               /* car info width, -length */
    int16_t speed;              /* speed * 3 */
    int16_t angle;
} CarCamTarget;
const CarCamTarget *car_get_cam_target(int n);

/* ---- sound (the fields Snd_GatherLoops and Music_UpdateRadio read; audio.h SndCar) ---- */
void car_fill_snd(int n, SndCar *out);
