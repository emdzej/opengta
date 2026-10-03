/* Explosions (0x425170-0x426320; see expl.h). */
#include "expl.h"
#include "car.h"
#include "coll.h"
#include "game.h"
#include "gmath.h"
#include "mission_obj.h"
#include "obj.h"
#include "ped.h"
#include "player.h"
#include "powerup.h"
#include "stubs.h"
#include "trigger.h"
#include "../audio/audio.h"
#include "../map.h"

ExplSlot g_expl[EXPL_SLOTS];
ExplDelayed g_expl_delayed[EXPL_DELAYED];
static CollBox car_box;                        /* 0x50f7a0 */

static int expl_base(void) { return sprite_group_base(SPRITE_GROUP_EX); }   /* 0x774ef4 */

/* Expl_Init 0x425c50: every slot free, its sprite at (0, 0, 0x140) on the first explosion frame */
void expl_init(void)
{
    for (int i = 0; i < EXPL_SLOTS; i++) {
        ExplSlot *e = &g_expl[i];
        e->frame = 0;
        sprite_init(&e->spr, 0, 0, 0x140, 0, expl_base());
        e->u60 = -1;
        g_expl_delayed[i].timer = 0;
        g_expl_delayed[i].owner = -1;
    }
}

/* one quarter of an explosion: slot `i` at (x, y) on frame `frame` (only inside the world) */
static bool place_part(int i, int32_t x, int32_t y, int frame)
{
    if (x <= 0 || x >= 0x40000000 || y <= 0 || y >= 0x40000000) return false;
    ExplSlot *e = &g_expl[i];
    e->frame = (int16_t)frame;
    e->spr.x = x;
    e->spr.y = y;
    return true;
}
static void show_part(int i)
{
    ExplSlot *e = &g_expl[i];
    e->tick = 0;
    coll_insert(COLL_EXPLOSION, i, &e->spr, e->spr.unk20, e->spr.x, e->spr.y);
    sprite_set_frame(&e->spr, e->frame + expl_base() - 1);
}

/* Expl_Create 0x425170: the first group of four free slots (none: nothing happens, not even the sound
   or the blast). The blast is applied first, then the quarters: top left at (x - 32, y - 32) pixels
   on frame 1, top right (x + 31) on 13, bottom left (y + 32) on 25, bottom right on 37. The picture
   floats 16..31 pixels (random) above z with its depth key a layer higher (at least 1.0), or sits at
   1.0 when z is that low. A quarter outside the world is left out; the others copy z and the key
   from the first slot even when the first was left out (then they keep that slot's old values). */
void expl_create(int32_t x, int32_t y, int32_t z, int owner)
{
    int g = 0;
    for (; g < EXPL_GROUPS; g++)
        if (!g_expl[g].frame && !g_expl[g + 1].frame && !g_expl[g + 2].frame && !g_expl[g + 3].frame) break;
    if (g_expl[g].frame != 0 || g == EXPL_GROUPS) return;
    int r = (int16_t)math_random() % 16;
    Snd_PlayAtXY(x, y, 0x27);
    expl_damage_area(x, y, z, owner);
    ExplSlot *a = &g_expl[g];
    int32_t ly = y - 0x200000, rx = x + 0x1f0000, lx = x - 0x200000, hy = y + 0x200000;
    if (place_part(g, lx, ly, 1)) {
        if ((r + 0x10) * 0x10000 < z) {
            a->spr.z = z - (r + 0x10) * 0x10000;
            a->spr.zkey = a->spr.z - 0x400000;
            if (a->spr.zkey < 1) a->spr.zkey = 0x10000;
        } else {
            a->spr.z = 0x10000;
            a->spr.zkey = 0x10000;
        }
        show_part(g);
    }
    static const int frames[3] = { 0xd, 0x19, 0x25 };
    const int32_t px[3] = { rx, lx, rx }, py[3] = { ly, hy, hy };
    for (int k = 0; k < 3; k++) {
        int i = g + 1 + k;
        if (!place_part(i, px[k], py[k], frames[k])) continue;
        g_expl[i].spr.z = a->spr.z;
        g_expl[i].spr.zkey = a->spr.zkey;
        show_part(i);
    }
}

/* Expl_CarExplode 0x425480: at the car's centre (its z), then at the 4 corners of its box, 8 pixels
   under each corner's ground */
void expl_car_explode(int car)
{
    const Car *c = car_get(car);
    int32_t x = c->spr.x, y = c->spr.y, z = c->spr.z;
    expl_create(x, y, z, c->player);
    const CollBox *b = coll_build_box(x, y, z, c->half_w, c->half_l, c->spr.angle, c->depth, &car_box);
    for (int i = 0; i < 4; i++) expl_create(b->x[i], b->y[i], b->gz[i] - 0x80000, c->player);
}

/* Expl_AtFaceIfSolid 0x425960: outside blocks 1..255 x 0..255 (or an unknown face) there is no solid
   test: it explodes anyway (an unknown face inside the range doesn't). The explosion is at the face's
   middle a layer up (z - 1 pixel, at least 0); the fires 4 / 8 pixels out from it and 10 / 6 along
   the face, each only where World_AnyThingAt finds no fire and z is above layer 1. The first test is
   given block coordinates where it takes 16.16 (it looks near the map's corner), the second the
   position, as in the original. */
bool expl_at_face_if_solid(int bx, int by, int bz, int face, int owner)
{
    int dx = 0, dy = 0, angle = 0;
    if (bx > 0 && bx < 0x100 && by >= 0 && by < 0x100) {
        switch (face) {
        case 0: dx = -1, angle = 0x300; break;
        case 1: dx = 1, angle = 0x100; break;
        case 2: dy = -1, angle = 0x200; break;
        case 3: dy = 1, angle = 0; break;
        default: return false;
        }
        if (!map_is_face_solid(g_game.map, bx, by, bz, face)) return false;
    }
    int32_t x = (dx + 1 + bx * 2) * 0x200000, y = (dy + 1 + by * 2) * 0x200000;
    int32_t z = bz * 0x400000 - 0x10000;
    if (z < 0) z = 0;
    expl_create(x, y, z, owner);
    int32_t fx = mission_clamp_coord(dx * 0x40000 + x), fy = mission_clamp_coord(dy * 0xa0000 + y);
    if (!world_any_thing_at(fx >> 22, fy >> 22) && z > 0x400000) obj_create_animated(fx, fy, z - 1, 0x12, -1, angle);
    fx = mission_clamp_coord(dx * 0x80000 + x), fy = mission_clamp_coord(dy * 0x60000 + y);
    if (!world_any_thing_at(fx, fy) && z > 0x400000) obj_create_animated(fx, fy, z - 1, 0x12, -1, angle);
    return true;
}

/* Expl_UpdateAll 0x425b60: every 2 frames a slot steps; the first quarter leaves smoke (object 0x33,
   a block's half to the bottom right, a pixel lower) on its frame 10; frames 12, 24, 36 and 48 end a
   quarter. Then the delayed explosions count down. */
void expl_update_all(void)
{
    for (int i = 0; i < EXPL_SLOTS; i++) {
        ExplSlot *e = &g_expl[i];
        if (!e->frame || ++e->tick != 2) continue;
        e->tick = 0;
        e->frame++;
        if (e->frame == 10) obj_create(e->spr.x + 0x200000, e->spr.y + 0x200000, e->spr.z + 0x10000, 0x33, 0);
        if (e->frame == 0xc || e->frame == 0x18 || e->frame == 0x24 || e->frame == 0x30) {
            coll_remove(&e->spr, e->spr.unk20);
            e->frame = 0;
        }
        sprite_set_frame(&e->spr, e->frame + expl_base() - 1);
    }
    for (int i = 0; i < EXPL_DELAYED; i++) {
        ExplDelayed *d = &g_expl_delayed[i];
        if (d->timer && --d->timer == 0) expl_create(d->x, d->y, d->z, d->owner);
    }
}

/* the heading away from the blast at (x, y) for something at (ox, oy), by quadrant (the callers
   overwrite it with Math_Atan2 or not, see below) */
static int16_t quadrant(int32_t x, int32_t y, int32_t ox, int32_t oy)
{
    int16_t a = x < ox ? 0x100 : 0x300;
    a = y < oy ? 0 : 0x200;
    if (ox < x && oy < y) a = 0x280;
    if (x < ox && oy < y) a = 0x180;
    if (ox < x && y < oy) a = 0x380;
    if (x < ox && y < oy) a = 0x80;
    return a;
}

/* the player a ped's death counts for: in single player a ped seen lately is the player's (the owner
   stays changed for the rest of the blast, as in the original) */
static void credit_kill(Ped *p, int *owner)
{
    if (g_player_count == 1 && ped_is_visible_recent(p)) *owner = player_first();
    if (*owner < 0) return;
    p->u5a = (int16_t)player_get_ped(*owner);
    score_ped_killed(p, 2);
    police_report_crime(1, p->u5a, 8, p->spr.x, p->spr.y, p->spr.z);
}

/* Expl_DamageArea 0x425ca0: everything of the 3 x 3 cells around (x, y) whose box touches the layer of
   z (z rounded down to the layer, minus a pixel):
   - peds alive, not dead (0x17) or in a car (6, 7), at most a layer below: within 60 pixels they are
     thrown away from the blast (speed 16, angle by quadrant); within 35 they die (anim 0x5a, state 2,
     action 2; already falling 0x2b: 0x2d; state 0xc keeps its anim), further out they catch fire
     (object 0x2e on the ped, sound 0x18, a ped not driven by a player stops where it is), lose 10
     health and face away from the blast (Math_Atan2) when the fire took. The owner scores the kill and
     the police hear of it (crime 8).
   - objects without an owner and at rest within 75 pixels: kicked away (speed 10, Obj_Kick); a gas
     tank (0x45, state 1) within 25 goes off 5 frames later (a delayed explosion 5 pixels lower) and
     turns to state 7. Crates (0x54) open (PowerUp_Reveal). The power-ups, smashed things and other
     fixed types listed in the switch are left alone. The list is unlocked inside the loop before the
     kick, as in the original.
   - then cars (a new query): within 64 pixels a car not wrecked yet is wrecked (damage 100, +0x114 =
     10) and blamed on the owner (crime 7 reported); the tank (model 0x25) takes 4 damage up to 100. */
void expl_damage_area(int32_t x, int32_t y, int32_t z, int owner)
{
    int32_t layer = (int32_t)((uint32_t)z & 0xffc00000u);
    for (CollHit *h = coll_query_block_all(x, y, layer - 0x10000, 0); h; h = h->next) {
        if (h->kind == COLL_PED) {
            Ped *p = ped_get(((const Ped *)h->owner)->id);
            if (p->state == 0x17 || p->health <= 0 || p->state == 7 || p->state == 6) continue;
            if (layer - 0x400000 > (int32_t)((uint32_t)p->spr.z & 0xffc00000u)) continue;
            int32_t px = p->spr.x, py = p->spr.y;
            int dx = (px - x) >> 16, dy = (py - y) >> 16;
            if (dx <= -0x3d || dy <= -0x3d || dx >= 0x3d || dy >= 0x3d) continue;
            p->speed = 0x10;
            p->idle_count = 0;
            p->anim_tick = 0;
            p->spr.angle = quadrant(x, y, px, py);
            if (dx < -0x23 || dy < -0x23 || dx > 0x23 || dy > 0x23) {
                if (p->carried == -1) {
                    p->carried = (int16_t)obj_create_animated(px, py, p->spr.z - 0x10000, 0x2e, p->id, 2);
                    Snd_PlayAt(p->spr.x, p->spr.y, p->spr.z, 0x18);
                    if (p->player_ctl != 1) {
                        p->state = 1;
                        p->target_x = p->spr.x;
                        p->target_y = p->spr.y;
                        p->target_ped = -1;
                    }
                    if (p->carried != -1) p->spr.angle = (int16_t)math_atan2(p->spr.y - y, p->spr.x - x);
                    credit_kill(p, &owner);
                }
                p->health = (int8_t)(p->health - 10);
                ped_update_sprite(p->id);
            } else {
                if (p->anim == 0x2b || p->state == 0xc) {
                    if (p->state != 0xc) p->anim = 0x2d;
                } else {
                    p->anim = 0x5a;
                    p->state = 2;
                    p->u7c = 2;
                }
                credit_kill(p, &owner);
                p->health = 0;
                ped_update_sprite(p->id);
            }
        } else if (h->kind == COLL_OBJECT) {
            Obj *o = h->owner;
            switch (o->type) {
            case 0x54:
                powerup_reveal(o->spr.x, o->spr.y);
                /* fall through */
            case 0x1a: case 0x1b: case 0x1c: case 0x1e: case 0x28: case 0x29: case 0x4e: case 0x4f: case 0x50:
            case 0x51: case 0x52: case 0x53: case 0x5e: case 0x5f: case 0x60: case 0x61: case 0x62: case 99:
            case 100: case 0x65:
                coll_unlock();
                break;
            default: {
                if (o->owner != -1 || o->speed != 0) break;
                int32_t ox = o->spr.x, oy = o->spr.y;
                int dx = (ox - x) >> 16, dy = (oy - y) >> 16;
                if (dx <= -0x4b || dy <= -0x4b || dx >= 0x4b || dy >= 0x4b) break;
                o->spr.angle = quadrant(x, y, ox, oy);
                o->spr.angle = (int16_t)math_atan2(oy - y, ox - x);
                coll_unlock();
                obj_kick(o->spr.x, o->spr.y, o->id, 10, o->spr.angle);
                if (dx > -0x19 && dy > -0x19 && dx < 0x19 && dy < 0x19 && o->type == 0x45 && o->state == 1) {
                    int i = 0;
                    while (i < EXPL_DELAYED && g_expl_delayed[i].timer != 0) i++;
                    if (i != EXPL_DELAYED) {
                        ExplDelayed *d = &g_expl_delayed[i];
                        d->z = o->spr.z - 0x50000;
                        d->timer = 5;
                        d->x = o->spr.x;
                        d->y = o->spr.y;
                        d->owner = (int16_t)owner;
                    }
                    o->state = 7;
                }
            }
            }
        }
    }
    coll_unlock();
    for (CollHit *h = coll_query_block_all(x, y, layer - 0x10000, COLL_CAR); h; h = h->next) {
        if (h->kind != COLL_CAR) continue;
        Car *c = h->owner;
        if (c->model == 0x25) {
            if (c->damage <= 0x60) c->damage += 4;
        } else if (c->damage < 100) {
            int dx = (c->spr.x - x) >> 16, dy = (c->spr.y - y) >> 16;
            if (dx < -0x40 || dy < -0x40 || dx > 0x40 || dy > 0x40) continue;
            c->damage = 100;
            c->u114 = 10;
            c->player = (int16_t)owner;
            if (owner > -1) police_report_crime(1, player_get_ped(owner), 7, c->spr.x, c->spr.y, c->spr.z);
        }
    }
    coll_unlock();
}

int expl_active(void)
{
    int n = 0;
    for (int i = 0; i < EXPL_SLOTS; i++) n += g_expl[i].frame != 0;
    return n;
}
