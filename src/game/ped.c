#include "ped.h"
#include "../exe.h"
#include "car.h"
#include "coll.h"
#include "game.h"
#include "player.h"
#include "stubs.h"
#include <string.h>

Ped g_peds[PED_MAX] = { 0 };
int g_peds_active;
static int remap_cycle;                      /* 0x74f0f6 */
static struct { int first_player, u7284b0; } ps;   /* 0x7284cc, 0x7284b0 */

/* The ped colour tables (read from the exe): 0x4b21b0 lists 22 colours that ambient peds cycle
   through, 0x4b20b0 maps a colour to a sprite remap. */
static int colour_remap(int colour)
{
    const uint8_t *t = exe_data(0x4b20b0 + (uint32_t)(colour & 0xff), 1);
    return t ? *t : 0;
}
static int cycle_colour(int i)
{
    const uint8_t *t = exe_data(0x4b21b0 + (uint32_t)i, 1);
    return t ? *t : 0;
}

/* Ped_Reset 0x44f230: every field but the remap (+0x5e) and the sprite. */
void ped_reset(int id)
{
    Ped *p = &g_peds[id];
    p->id = (int16_t)id;
    p->state = 2, p->u74 = 2, p->u7c = 2, p->u80 = 2;
    p->u02 = p->u04 = p->speed = 0;
    p->move_speed = 4;
    p->u0a = p->u0c = p->u0e = 0;
    p->control = 0, p->u12 = 0, p->u14 = -1, p->graphic = 0, p->anim = 0;
    p->u1a = 0, p->player_ctl = 0, p->u1e = 0;
    p->u20 = p->u24 = p->u28 = p->target_x = p->target_y = 0;
    p->u34 = 0xff;
    p->walk_x = p->walk_y = 0;
    p->u40 = -1, p->mode = -1, p->u44 = 0;
    p->firing = p->u47 = p->u48 = 0;
    p->health = 0, p->u4a = 0;
    p->car = -1, p->u4e = -1;
    p->attach_kind = p->attach_id = p->u54 = p->u56 = 0;
    p->carried = p->u5a = p->u5c = -1;
    p->weapon = 0;
    p->u64 = p->u66 = 0;
    p->u68 = 0;
    p->objective = 0x19;
    p->u78 = 8;
    p->u84 = 0, p->target_ped = -1;
    p->u88 = 0, p->group = 0xff, p->group_slot = 0xff, p->u8b = 0, p->train = 0xff;
    p->uec = 0, p->uee = 0, p->uf0 = 0, p->uf4 = 0, p->uf8 = 0, p->ufa = 0, p->ufc = 0, p->ufe = 0;
}

/* Ped_InitAll 0x44f370: every slot reset and free (control -1); +0x12 alternates 0x80 / -0x80; the
   driver slots 200..600 sit in car slot - 200 (state 7, health 100) - 600, the first special slot,
   too (with car 400, past the table). Each sprite is initialised high up (z 0x13f0000) with the
   first ped sprite and the ped palette, and the slots get the ambient colours in turn. */
void ped_init_all(void)
{
    ps.u7284b0 = 0x10;
    remap_cycle = 0;
    ps.first_player = player_first();
    int base = sprite_group_base(SPRITE_GROUP_PED), pal = sprite_ped_palette();
    for (int i = 0; i < PED_MAX; i++) {
        Ped *p = &g_peds[i];
        ped_reset(i);
        p->control = -1;
        p->anim = 0;
        p->u12 = (i & 1) ? -0x80 : 0x80;
        if (i >= PED_DRIVER_FIRST && i <= 600) {
            p->state = 7;   /* written as two shorts: 7, 0 */
            p->car = (int16_t)(i - PED_DRIVER_FIRST);
            p->u14 = (int16_t)(i - PED_DRIVER_FIRST);
            p->health = 100;
        }
        sprite_init(&p->spr, 0, 0, 0x13f0000, 0, base);
        sprite_set_palette(&p->spr, pal);
        int c = cycle_colour(remap_cycle++);
        p->graphic = 0;
        p->remap = (uint8_t)colour_remap(c);
        if (remap_cycle > 0x15) remap_cycle = 0;
        sprite_set_remap(&p->spr, p->remap);
    }
    g_peds_active = 0;
}

/* Ped_SpawnInSlot 0x44f680: the slot keeps its graphic type; z is the depth key, the sprite one pixel
   above it. The car passed (the player's start car) loses its driver. */
void ped_spawn_in_slot(int32_t x, int32_t y, int32_t z, int speed, int angle, int anim, int slot, int car)
{
    Ped *p = &g_peds[(int16_t)slot];
    if (p->anim != 0) return;
    int16_t g = p->graphic;
    ped_reset((int16_t)slot);
    p->graphic = g;
    p->spr.y = y;
    p->spr.angle = (int16_t)angle;
    p->spr.x = x;
    p->spr.zkey = z;
    p->spr.z = z - 0x10000;
    p->car = (int16_t)car;
    p->anim = (int16_t)anim;
    ped_update_sprite(p->id);
    g_peds_active++;
    p->speed = (int16_t)speed;
    p->health = 100;
    coll_insert(COLL_PED, (int16_t)slot, p, p->spr.unk20, p->spr.x, p->spr.y);
    police_update_criminal_target(0, p->car, 1, (int16_t)slot);
    p->control = 0;
    p->weapon = 0;
    p->objective = 0x19;
    p->state = 2;
    p->u78 = 8;
    p->u7c = 2;
    p->u84 = 0;
    if (p->car >= 0 && p->car < CAR_MAX) car_get(p->car)->driver = -1;   /* (the original doesn't check) */
    sprite_set_palette(&p->spr, sprite_ped_palette());
    sprite_set_remap(&p->spr, p->remap);
}

/* Ped_SetPlayerControlled 0x45fa50 */
void ped_set_player_controlled(int id)
{
    g_peds[id].player_ctl = 1;
    g_peds[id].control = 8;
}

/* Ped_SetAppearance 0x45fc10: graphic type and the remap of a colour. Ids outside the table (a failed
   creation, -1) are ignored; the original writes before the table. */
void ped_set_appearance(int id, int graphic, int colour)
{
    id = (int16_t)id;
    if (id < 0 || id >= PED_MAX) return;
    Ped *p = &g_peds[id];
    p->graphic = (int16_t)graphic;
    p->remap = (uint8_t)colour_remap(colour);
    ped_update_sprite(id);
    sprite_set_remap(&p->spr, p->remap);
}

int peds_in_use(void)
{
    int k = 0;
    for (int i = 0; i < PED_MAX; i++) k += g_peds[i].anim != 0;
    return k;
}
