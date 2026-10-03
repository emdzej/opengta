/* Weapons fired on foot (0x488e20-0x4892ff) and the tank's gun (Car_FireRocket 0x489300): see weapon.h. */
#include "weapon.h"
#include "ped_internal.h"
#include "proj.h"

/* the new projectile in the list and its common fields */
static Obj *add_proj(int id)
{
    g_proj.id[g_proj.count++] = (int16_t)id;
    return obj_get(id);
}

/* Weapon_FireBullet 0x488e20: a bullet (object 0x4a, speed 15, life 20) from 2 pixels behind the ped
   or, when it runs (speed 3+), from its right side (4 across, 3 back), a pixel under its z; the
   reload counter is 10 for players, 15 for others. A running ped's bullet stays out of the grid for
   2 frames; every bullet leaves the grid at once (Coll_Remove after Obj_Create). */
void weapon_fire_bullet(Ped *p)
{
    if (g_proj.count >= PROJ_MAX || p->health <= 0) return;
    int32_t z = p->spr.z, y = p->spr.y, x = p->spr.x;
    uint16_t a = (uint16_t)p->spr.angle;
    int32_t ox, oy;
    if (p->speed < 3) {
        ox = SIN((int16_t)a) * -2;
        oy = COS((int16_t)a) * -2;
    } else {
        int r = ((int16_t)a - 0x100) & 0x3ff;
        ox = SIN(r) * 4 + SIN((int16_t)a) * -3;
        oy = COS(r) * 4 + COS((int16_t)a) * -3;
    }
    CollBox box;
    coll_build_box(x + ox, y + oy, z - 0x10000, 2, 0xf, a, 0x10, &box);
    coll_compute_bounds(&box);
    if (coll_map_slopes_ex(&box) != -1) return;
    int id = obj_create(x + ox, y + oy, z - 0x10000, PROJ_BULLET, a);
    if ((int16_t)id < 0) return;
    Obj *o = add_proj(id);
    o->speed = 0xf;
    o->heading = (int16_t)(a & 0x3ff);
    o->life = 0x14;
    o->u1e = p->id;
    o->weight = PROJ_BULLET;
    p->u64 = p->player_ctl == 1 ? 10 : 0xf;
    if (p->speed > 2) o->u21 = 2;
    coll_remove(o, o->spr.unk20);
}

/* Weapon_FireBulletFlag 0x488fb0: a bullet, then the reload counter 1 */
void weapon_fire_bullet_flag(Ped *p)
{
    weapon_fire_bullet(p);
    p->u64 = 1;
}

/* Weapon_FireFlame 0x488fd0: a flame (object 0x4b, the ped's speed + 4, life 1, frame 1) 12 pixels
   ahead, or when running 18 ahead and 6 to the right; the slope test uses a box at the ped itself.
   Reload 1. */
void weapon_fire_flame(Ped *p)
{
    if (g_proj.count >= PROJ_MAX || p->health <= 0) return;
    int32_t z = p->spr.z;
    uint16_t a = (uint16_t)p->spr.angle;
    int32_t ox, oy;
    if (p->speed < 3) {
        ox = SIN((int16_t)a) * 0xc;
        oy = COS((int16_t)a) * 0xc;
    } else {
        int r = ((int16_t)a - 0x100) & 0x3ff;
        ox = (SIN((int16_t)a) * 3 + SIN(r)) * 6;
        oy = (COS((int16_t)a) * 3 + COS(r)) * 6;
    }
    ox += p->spr.x;
    oy += p->spr.y;
    CollBox box;
    coll_build_box(p->spr.x, p->spr.y, z - 0x10000, 2, 0x1e, a, 0x10, &box);
    coll_compute_bounds(&box);
    if (coll_map_slopes_ex(&box) != -1) return;
    int id = obj_create(ox, oy, z - 0x10000, PROJ_FLAME, a);
    if ((int16_t)id < 0) return;
    Obj *o = add_proj(id);
    o->speed = (int16_t)(p->speed + 4);
    o->spr.angle = (int16_t)((a - 0x200) & 0x3ff);
    o->heading = (int16_t)(a & 0x3ff);
    o->life = 1;
    o->state = 1;
    o->weight = PROJ_FLAME;
    o->u1e = p->id;
    p->u64 = 1;
}

/* Weapon_FireRocket 0x489150: a rocket (object 0x1f, speed 10, life 1) from in front of the ped (12
   ahead, 2 right; running 2 ahead, 6 right), its depth key a quarter block higher (at least 1.0);
   reload 20. A running ped's rocket stays out of the grid for a frame. */
void weapon_fire_rocket(Ped *p)
{
    if (g_proj.count >= PROJ_MAX || p->health <= 0) return;
    uint16_t a = (uint16_t)p->spr.angle;
    int32_t z = p->spr.z;
    int r = ((int16_t)a - 0x100) & 0x3ff;
    int32_t ox, oy;
    if (p->speed < 3) {
        ox = SIN(r) + SIN((int16_t)a) * 6;
        oy = COS(r) + COS((int16_t)a) * 6;
    } else {
        ox = SIN(r) * 3 + SIN((int16_t)a);
        oy = COS(r) * 3 + COS((int16_t)a);
    }
    ox = p->spr.x + ox * 2;
    oy = p->spr.y + oy * 2;
    CollBox box;
    coll_build_box(ox, oy, z - 0x10000, 2, 10, a, 0x10, &box);
    coll_compute_bounds(&box);
    if (coll_map_slopes_ex(&box) != -1) return;
    int id = obj_create(ox, oy, z - 0x10000, PROJ_ROCKET, a);
    if ((int16_t)id < 0) return;
    Obj *o = add_proj(id);
    o->speed = 10;
    o->spr.angle = (int16_t)((a - 0x200) & 0x3ff);
    o->heading = (int16_t)(a & 0x3ff);
    o->life = 1;
    o->weight = PROJ_ROCKET;
    o->u1e = p->id;
    p->u64 = 0x14;
    o->spr.zkey -= 0x100000;
    if (o->spr.zkey < 1) o->spr.zkey = 0x10000;
    if (p->speed > 2) {
        o->u21 = 1;
        coll_remove(o, o->spr.unk20);
    }
}

/* Car_FireRocket 0x489300: a tank driven by a player (whose +0x1ae isn't -1) fires a rocket (object
   0x1f, life 1) from 60 pixels along the turret (model 0x25: the turret object at +0x11a; other cars
   their heading), a block higher, at the car's speed + 2 (at least 4), with the sound 0x24; at most
   40 projectiles, not from a wreck (damage 100), 16 frames between shots (+0xd0 counts them). The
   rocket stays in the grid (unlike a ped's). */
void car_fire_rocket(Car *c)
{
    int n = (int16_t)player_find_by_ped(c->driver);
    if (n == -1) return;
    if (g_players[n].timers[1] == -1 || g_proj.count >= PROJ_MAX || c->damage >= 100 || c->frames <= 0xf) return;
    Snd_PlayAtXY(c->spr.x, c->spr.y, 0x24);
    uint16_t a;
    int32_t x, y;
    if (c->model == 0x25) {
        const Obj *t = obj_get(c->horn);
        a = (uint16_t)t->spr.angle, x = t->spr.x, y = t->spr.y;
    } else {
        a = (uint16_t)c->spr.angle, x = c->spr.x, y = c->spr.y;
    }
    int id = obj_create(x + SIN((int16_t)a) * 0x3c, y + COS((int16_t)a) * 0x3c, c->spr.z + 0x10000, PROJ_ROCKET, a);
    if ((int16_t)id < 0) return;
    Obj *o = add_proj(id);
    int16_t sp = (int16_t)(c->speed + 2);
    o->speed = sp;
    if (sp < 4) o->speed = 4;
    o->life = 1;
    o->spr.angle = (int16_t)((a - 0x200) & 0x3ff);
    o->heading = (int16_t)(a & 0x3ff);
    o->weight = PROJ_ROCKET;
    o->u1e = c->driver;
    c->frames = 0;
}
