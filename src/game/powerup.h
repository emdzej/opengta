/* Power-ups (0x46a0a0-0x46a99f): 256 records at 0x74f858. A power-up starts as a crate (object 0x54;
   the help signs of type 14 as object 0x5f, already visible); smashing the crate (a rocket, a car,
   a ped's punch, an explosion: PowerUp_Reveal) replaces it with the power-up's own object and the
   broken crate (0x55); touching that object collects it (PowerUp_Collect, from the ped and car
   collision code). Types: 1-4 weapons (pistol, machine gun, rocket launcher, flamethrower; value 0 =
   the default ammo of the exe table 0x4a8c18, < 100 that ammo, >= 100 a temporary weapon for
   value - 100 frames), 6-8 speed-up, 9 bribe (wanted level cleared), 10 armour (3 hits), 11
   multiplier + 1, 12 get out of jail free, 13 extra life, 14 help sign (text "help<value>"), 15 extra
   life with a voice. Types 5, 7, 8 and 14 have no revealed object: their crate just disappears. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { POWERUP_MAX = 256 };

typedef struct {
    int32_t x, y, z;            /* +0x00 16.16, as given to PowerUp_Add */
    int32_t obj;                /* +0x0c the crate or power-up object, -1 free */
    int32_t visible;            /* +0x10 1 crate, 2 revealed */
    int32_t type;               /* +0x14 */
    int32_t value;              /* +0x18 */
} PowerUp;
_Static_assert(sizeof(PowerUp) == 0x1c, "power-up record");

extern PowerUp g_powerups[POWERUP_MAX];      /* 0x74f858 */

void powerup_init_all(void);                 /* PowerUp_InitAll 0x46a0a0 */
/* PowerUp_Add 0x46a0d0: a power-up of `type` with `value` at (x, y, z); 1, or 0 when the table is full
   or the object can't be created. */
int powerup_add(int type, int value, int32_t x, int32_t y, int32_t z);
void powerup_reveal(int32_t x, int32_t y);   /* PowerUp_Reveal 0x46a190: the crate at exactly (x, y) */
bool powerup_remove_at(int32_t x, int32_t y);   /* PowerUp_RemoveAt 0x46a4a0 */
bool powerup_exists_at(int32_t x, int32_t y);   /* PowerUp_ExistsAt 0x46a530 */
/* PowerUp_Collect 0x46a560: player n takes the power-up at exactly (x, y), if it may (a full weapon,
   a temporary weapon held, full armour or a jail-free card held refuse). `how` (0 in a car, 2 on foot)
   is what the callers pass; the original doesn't read it. */
void powerup_collect(int n, int32_t x, int32_t y, int how);
int powerups_in_use(void);                   /* records with an object (for checks) */
