/* The car table (0x4be248, 400 x 0x2b0; Car_Get 0x408200) and its level-start initialisation
   (Cars_Init 0x4070a0, Traffic_InitModelTables 0x418f80). Car n's driver is ped 200 + n.
   Field knowledge collected from docs/re/inventory-1..4; per-car behaviour is not ported yet. */
#pragma once
#include "../render/sprite.h"
#include "coll.h"
#include "layout.h"
#include <stdint.h>

enum { CAR_MAX = 400, CAR_MODELS = 100 };

typedef struct Car {
    int16_t id;                 /* +0x00 own index */
    int16_t driver;             /* +0x02 driver ped id (-1 none) */
    int16_t control;            /* +0x04 0 traffic dummy, 1 physics / player, 2/3/9/10 AI driver, 0x32 parked */
    int16_t active;             /* +0x06 visible to a player (or bomb / fire / AI) this frame */
    int16_t status;             /* +0x08 -1 free slot; 7 fallen bike / burnt frame */
    int16_t input;              /* +0x0a accel / steer input of dummies */
    uint8_t pad0c[8];
    int32_t front_x, front_y;   /* +0x14, +0x18 front wheel point */
    int16_t speed;              /* +0x1c signed */
    uint8_t pad1e[4];
    int16_t model;              /* +0x22 index into the model table 0x4be178 */
    int16_t door_dx, door_dy;   /* +0x24, +0x26 driver's door offset (MisCar_PutPlayerIn) */
    int16_t max_speed;          /* +0x28 from car info */
    uint8_t pad2a[2];
    int16_t cam_w;              /* +0x2c size in the camera target record */
    uint8_t pad2e[6];
    int16_t vtype;              /* +0x34 car info +0x6a: 0 bus, 3 bike, 4 car, 8 train, 9 tram, 13 boat, 14 tank */
    int16_t length;             /* +0x36 */
    CollBox box;                /* +0x38 current hitbox */
    uint8_t pad7c[0xc];
    int16_t unk88;              /* +0x88 1 for parked / player cars */
    int16_t half_w, half_l;     /* +0x8a, +0x8c pixels */
    uint8_t pad8e[2];
    int16_t front_heading;      /* +0x90 */
    int16_t turn_progress;      /* +0x92 */
    uint8_t pad94[2];
    int16_t turn_delta;         /* +0x96 */
    uint8_t pad98[4];
    int32_t bomb;               /* +0x9c bomb state (1 armed .. 6) */
    int16_t bomb_timer;         /* +0xa0 */
    uint16_t road_dirs;         /* +0xa2 road direction bits of the block (and its high bits) */
    uint8_t pada4[8];
    int32_t rear_x, rear_y;     /* +0xac, +0xb0 rear wheel point */
    int16_t rear_heading;       /* +0xb4 */
    uint8_t padb6[0xa];
    int16_t unkc0;              /* +0xc0 1 with Car_SetPhysicsControl */
    uint8_t padc2[0x12];
    int16_t prev_dirs;          /* +0xd4 */
    uint8_t padd6[2];
    int16_t sentinel;           /* +0xd8 AI driver record (0x507ea0) */
    uint8_t padda[0x12];
    int16_t unkec;              /* +0xec 1 for mission cars with a killer driver (MisCar_MakeKiller) */
    int16_t siren_type;         /* +0xee */
    int16_t turret;             /* +0xf0 tank turret angle / bike lean */
    uint8_t padf2[0xa];
    int16_t damage;             /* +0xfc 0..100 */
    int16_t burning;            /* +0xfe */
    uint8_t pad100[4];
    int16_t cruise;             /* +0x104 */
    uint8_t pad106[2];
    int32_t z_offset;           /* +0x108 above ground */
    uint8_t pad10c[2];
    int16_t lane;               /* +0x10e lane offset in the block */
    uint8_t pad110[0xa];
    int16_t horn;               /* +0x11a */
    int16_t siren_state;        /* +0x11c 99: parked car with its alarm armed (Obj_InitFromMap) */
    uint8_t pad11e[0xa];
    int16_t owner_status;       /* +0x128 1 normal, 99 mission-locked; the driver flag of Car_SpawnEx */
    uint8_t pad12a[0xa];
    int16_t script_line;        /* +0x134 MISSION.INI line that created it */
    uint8_t pad136[3];
    uint8_t unk139;             /* +0x139 cleared by Cars_Init / MisCar_Create; nonzero = slot reserved */
    uint8_t pad13a[2];
    int16_t player;             /* +0x13c player index of the driver / last attacker */
    uint8_t pad13e[6];
    uint8_t physics;            /* +0x144 float physics active (CarPhys_Begin) */
    uint8_t pad145[7];
    float thrust;               /* +0x14c */
    float skid;                 /* +0x150 */
    uint8_t pad154[0x88];       /* +0x188..+0x1d4 rigid body floats */
    CollBox box_saved;          /* +0x1dc pending hitbox */
    int32_t next_x, next_y, next_z, next_heading;   /* +0x220..+0x22c committed by Car_CommitMove */
    uint8_t pad230[0x18];
    int16_t script_held;        /* +0x248 */
    uint8_t pad24a[2];
    uint8_t remap;              /* +0x24c */
    uint8_t pad24d[3];
    Sprite spr;                 /* +0x250 x +0x250, y +0x254, z +0x258, angle +0x268, info +0x298 */
    const uint8_t *info;        /* +0x2ac car_info record */
} Car;
GAME_OFS(Car, driver, 0x02); GAME_OFS(Car, control, 0x04); GAME_OFS(Car, active, 0x06);
GAME_OFS(Car, status, 0x08); GAME_OFS(Car, input, 0x0a); GAME_OFS(Car, front_x, 0x14);
GAME_OFS(Car, speed, 0x1c); GAME_OFS(Car, model, 0x22); GAME_OFS(Car, door_dx, 0x24);
GAME_OFS(Car, max_speed, 0x28); GAME_OFS(Car, cam_w, 0x2c); GAME_OFS(Car, vtype, 0x34);
GAME_OFS(Car, length, 0x36); GAME_OFS(Car, box, 0x38); GAME_OFS(Car, unk88, 0x88);
GAME_OFS(Car, half_w, 0x8a); GAME_OFS(Car, front_heading, 0x90); GAME_OFS(Car, turn_progress, 0x92);
GAME_OFS(Car, turn_delta, 0x96); GAME_OFS(Car, bomb, 0x9c); GAME_OFS(Car, bomb_timer, 0xa0);
GAME_OFS(Car, road_dirs, 0xa2); GAME_OFS(Car, rear_x, 0xac); GAME_OFS(Car, rear_heading, 0xb4);
GAME_OFS(Car, unkc0, 0xc0); GAME_OFS(Car, prev_dirs, 0xd4); GAME_OFS(Car, sentinel, 0xd8); GAME_OFS(Car, unkec, 0xec); GAME_OFS(Car, siren_type, 0xee);
GAME_OFS(Car, turret, 0xf0); GAME_OFS(Car, damage, 0xfc); GAME_OFS(Car, burning, 0xfe);
GAME_OFS(Car, cruise, 0x104); GAME_OFS(Car, z_offset, 0x108); GAME_OFS(Car, lane, 0x10e);
GAME_OFS(Car, horn, 0x11a); GAME_OFS(Car, siren_state, 0x11c); GAME_OFS(Car, owner_status, 0x128);
GAME_OFS(Car, script_line, 0x134); GAME_OFS(Car, unk139, 0x139); GAME_OFS(Car, player, 0x13c);
GAME_OFS(Car, physics, 0x144); GAME_OFS(Car, thrust, 0x14c); GAME_OFS(Car, skid, 0x150);
GAME_OFS(Car, box_saved, 0x1dc); GAME_OFS(Car, next_x, 0x220); GAME_OFS(Car, next_heading, 0x22c);
GAME_OFS(Car, script_held, 0x248); GAME_OFS(Car, remap, 0x24c); GAME_OFS(Car, spr, 0x250);
GAME_OFS32(Car, info, 0x2ac); GAME_SIZE32(Car, 0x2b0);

extern Car g_cars[CAR_MAX];                 /* 0x4be248 */
extern int16_t g_car_model_index[CAR_MODELS];   /* 0x4be178: model -> car info record (-1 none) */
extern int g_cars_count;                    /* 0x501554: slots in use up to here (Cars_GetCount) */
extern uint16_t g_traffic_models[3][100];   /* 0x504ce0: shuffled model distributions */

static inline Car *car_get(int n) { return &g_cars[n]; }        /* Car_Get 0x408200 */
void cars_init(void);                       /* Cars_Init 0x4070a0 */
void traffic_init_model_tables(void);       /* Traffic_InitModelTables 0x418f80 */
void car_set_physics_control(int n);        /* Car_SetPhysicsControl 0x4082d0 */
static inline void car_set_dummy_control(int n) { g_cars[n].control = 0; }   /* Car_SetDummyControl 0x408300 */
int cars_in_use(void);                      /* slots with status != -1 (for checks) */
/* the style's car info record of a model (via 0x4be178 and the record table 0x501574), NULL if none */
const uint8_t *car_info_of_model(int model);
extern int g_traffic_cycle;                 /* 0x504f40 next entry of the traffic model row */
