/* The Hells Angels gang and the hunting cars (0x431500-0x4325ff): a mission's gang cars (Gang_AddCar);
   when a player rides one of them for 30 frames the rest hunt him (Gang_Update). Hunting cars
   (control 0x32) are driven by Hunt_UpdateCar 0x431f70 from a 20-entry table (0x5132a0): toward a
   car, a ped or a block. The gang lists themselves are mission_obj.c's (g_gang_lists_*). See
   docs/police.md. */
#pragma once
#include "car.h"
#include <stdint.h>

enum { HUNT_MAX = 20 };

/* A hunting car (0x1c bytes). Modes (+0x04): 4 and 10 (Hunt_AddBlockTarget / 2) drive to the block
   at +0x08, every other mode (the gang's 6, the dummies' 1, script values) to the target ped's block
   (or its car's); mode 3 doesn't push through the car ahead and starts the destroy timer on arrival. */
typedef struct {
    int16_t car;                /* +0x00 the hunting car (-1 free) */
    int16_t target;             /* +0x02 the ped hunted (-1) */
    uint8_t mode;               /* +0x04 (0xff) */
    uint8_t u05;
    int16_t heading;            /* +0x06 the heading a turn aims at (-1) */
    uint8_t bx, by, bz;         /* +0x08 target block */
    uint8_t u0b;
    int32_t last_x, last_y;     /* +0x0c, +0x10 the car's position at the last step (-1) */
    uint8_t timer;              /* +0x14 counts up once started; at 30 the car is destroyed */
    uint8_t fresh;              /* +0x15 1 = just set */
    uint8_t pad16[2];
    int32_t turn;               /* +0x18 0 none, 1 / 2 turning at a junction, 3 / 4 turning to the target */
} Hunt;
_Static_assert(sizeof(Hunt) == 0x1c, "hunt record");

extern Hunt g_hunts[HUNT_MAX];              /* 0x5132a0 */

void gang_init(void);                       /* Gang_Init 0x431500 */
void gang_add_car(int car);                 /* Gang_AddCar 0x431530 */
void gang_update(void);                     /* Gang_Update 0x4315b0 */
void hunt_init(void);                       /* Hunt_Init 0x4317f0 */
void hunt_remove(int car);                  /* Hunt_Remove 0x431840 */
int hunt_add_car_target(int car, int target, int mode);       /* Hunt_AddCarTarget 0x4318e0 (-1) */
int hunt_add_block_target(int car, int bx, int by, int bz);   /* Hunt_AddBlockTarget 0x4319b0 (-1) */
int hunt_add_block_target2(int car, int bx, int by, int bz);  /* Hunt_AddBlockTarget2 0x431ab0 (-1) */
int hunt_steer_towards(Car *c, int angle);  /* Hunt_SteerTowards 0x431bb0 */
int hunt_probe_ahead(Car *c, int32_t *x, int32_t *y);   /* Hunt_ProbeAhead 0x431cb0: road dir bits */
void hunt_update_car(Car *c);               /* Hunt_UpdateCar 0x431f70 */
