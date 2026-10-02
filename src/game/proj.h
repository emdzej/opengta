/* Projectiles (0x4879b0-0x488e1f): bullets, rockets and flames are objects of the object table
   (obj.h) of types 0x4a, 0x1f and 0x4b, listed in the projectile list 0x785318 (at most 40 ids,
   count 0x785314). Weapon_Fire* (weapon.h) create them; Proj_UpdateAll moves them once a frame, at the
   end of Obj_UpdateAll 0x44d790. Fields of the Obj record a projectile uses:
     +0x02 speed (pixels a frame), +0x04 life (frames; 0 = spent), +0x06 heading (0..1023),
     +0x08 kind (the type again: 0x1f rocket, 0x4a bullet, 0x4b flame), +0x0c animation frame
     (0 = remove), +0x1e the ped that fired it, +0x21 frames left before it enters the grid,
     the sprite (+0x2c) angle = heading - 0x200 for rockets and flames. */
#pragma once
#include <stdint.h>

enum { PROJ_MAX = 40, PROJ_ROCKET = 0x1f, PROJ_BULLET = 0x4a, PROJ_FLAME = 0x4b };

typedef struct {
    int16_t count;              /* 0x785314 */
    int16_t id[PROJ_MAX];       /* 0x785318 object ids */
} ProjList;
extern ProjList g_proj;

void proj_reset(void);                       /* Proj_Reset 0x4879b0 */
void proj_remove(int obj);                   /* Proj_Remove 0x4879c0 */
void proj_update_all(void);                  /* Proj_UpdateAll 0x487a70 (from Obj_UpdateAll 0x44d790) */
