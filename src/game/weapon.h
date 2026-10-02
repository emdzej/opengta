/* Weapons fired on foot (0x488e20-0x4892ff): each creates a projectile (proj.h) in front of the ped
   and sets the ped's reload counter (+0x64). Nothing happens with 40 projectiles in flight or a dead
   ped, or when the start point is inside a slope (Coll_MapSlopesEx). */
#pragma once
#include "ped.h"

void weapon_fire_bullet(Ped *p);             /* Weapon_FireBullet 0x488e20 (pistol, machine gun) */
void weapon_fire_bullet_flag(Ped *p);        /* Weapon_FireBulletFlag 0x488fb0 */
void weapon_fire_flame(Ped *p);              /* Weapon_FireFlame 0x488fd0 */
void weapon_fire_rocket(Ped *p);             /* Weapon_FireRocket 0x489150 */
