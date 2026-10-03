/* The ped table, level start, the per-frame update loop and the small accessors of the ped module
   (see ped.h and docs/peds.md). */
#include "ped_internal.h"
#include "train.h"
#include "../exe.h"
#include "../text.h"
#include "mission_obj.h"
#include "sentinel.h"
#include "wanted.h"
#include "../hud/hud.h"
#include <stdio.h>
#include <string.h>

Ped g_peds[PED_MAX] = { 0 };
int g_peds_active;

CollBox g_ped_box;
PedGroup g_ped_groups[PED_GROUPS];
int32_t g_ped_player_ped;
uint8_t g_ped_728448;
int16_t g_ped_frame4;
int8_t g_ped_72844c, g_ped_72844d;
int32_t g_ped_7284b0;
int8_t g_ped_7284c8;
int32_t g_ped_spawn_player;
int8_t g_ped_group_active, g_ped_7284d1, g_ped_spawn_stop, g_ped_7284d8;
int16_t g_ped_7284d4, g_ped_7284d6;
int32_t g_ped_enter_ref[2];
int16_t g_ped_door_step;
uint8_t g_ped_remap_cycle;
int16_t g_ped_74f0f8;
int8_t g_ped_74f0fa, g_ped_74f0fb, g_ped_push_count, g_ped_74f0fd, g_ped_74f0fe;
int16_t g_ped_74f100;
int8_t g_ped_74f102, g_ped_74f103;
int16_t g_ped_groups_used;

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

/* Ped_TestCollision 0x44ee50: a car (kind 6) touching a 1 x 1 box at the ped */
bool ped_test_collision(const Ped *p)
{
    CollBox *b = coll_build_box(p->spr.x, p->spr.y, p->spr.z, 1, 1, 0, 10, &g_ped_box);
    CollHit *h = coll_query_box(b, COLL_CAR, 1, p->id);
    coll_unlock();
    return h != NULL;
}

/* Ped_MakePolice 0x44eea0 */
void ped_make_police(Ped *p)
{
    p->remap = 0;
    sprite_set_remap(&p->spr, 0);
    p->graphic = 1;
}

/* Ped_SetWeapon 0x44eed0: 0 changes nothing, 1..4 set, anything else is fatal (-0x4a) */
void ped_set_weapon(int id, int w)
{
    if (w == 0) return;
    if (w > 0 && w < 5) {
        g_peds[(int16_t)id].weapon = w;
        return;
    }
    game_fatal(-0x4a, 0x158, g_peds[(int16_t)id].weapon);
}

/* Ped_GetGroupLeaderPos 0x44ef10: with a group active (and one group in use), the leader's position;
   true if the leader is alive. */
bool ped_get_group_leader_pos(int32_t *x, int32_t *y)
{
    if (g_ped_group_active != 1 || g_ped_groups_used != 1) return false;
    const Ped *l = &g_peds[g_ped_groups[0].leader];
    *x = l->spr.x;
    *y = l->spr.y;
    return l->health != 0;
}

/* Ped_WalkToCarSideA 0x44efa0 / B 0x44f040: walk to the point of the car's door offset (+0x24 along
   the heading, +0x26 across), on one side or the mirrored one: state 4 (go to), mode 0xb. */
static void walk_to_car_side(Ped *p, int side)
{
    const Car *c = car_get(p->car);
    int a = c->spr.angle & 0x3ff, b = (c->spr.angle + 0x100) & 0x3ff;
    int32_t x, y;
    if (side == 0) {
        x = SIN(b) * c->door_dy - SIN(a) * c->door_dx + c->spr.x;
        y = COS(b) * c->door_dy - COS(a) * c->door_dx + c->spr.y;
    } else {
        x = SIN(a) * c->door_dx + SIN(b) * c->door_dy + c->spr.x;
        y = COS(a) * c->door_dx + COS(b) * c->door_dy + c->spr.y;
    }
    p->target_x = x, p->target_y = y;
    p->walk_x = x, p->walk_y = y;
    p->state = 4;
    p->mode = 0xb;
}
void ped_walk_to_car_side_a(Ped *p) { walk_to_car_side(p, 0); }
void ped_walk_to_car_side_b(Ped *p) { walk_to_car_side(p, 1); }

/* Ped_UpdateSprite 0x44f100: the sprite of the anim state from the table 0x4b1e90 (one short per
   state, read from the exe; -1 keeps the frame), offset by weapon:
   - firing on foot as a player (objective 0x25) while walking / running (anim 1..0x10): no weapon
     punches (+0xad, only in the run cycle 8.. with speed), pistol +0x63, machine gun +0x88, rocket
     launcher +0x9a, flamethrower +0x76; a firing ped that isn't a player in the walk cycle uses
     frame 0x5a;
   - frames 0x59 / 0x5a (standing): machine gun +0xf (cops) / +0x3f, rocket +0x51, flame +0x2d;
   - anims 0xe9..0xed of a cop: -0x49.
   Each graphic type has 0xbd frames from the ped sprite base. */
void ped_update_sprite(int id)
{
    Ped *p = &g_peds[(int16_t)id];
    int a = p->anim;
    const uint8_t *t = exe_data(0x4b1e90 + 2 * (uint32_t)(uint16_t)a, 2);
    int16_t f = t ? (int16_t)(t[0] | t[1] << 8) : -1;
    bool weapon_pose = false;
    if (p->firing == 1) {
        if (a > 0 && a < 0x11 && p->objective == 0x25) {
            switch (p->weapon) {
            case 0: if (a > 7 && p->speed > 0) f = (int16_t)(f + 0xad); break;
            case 1: f = (int16_t)(f + 99); break;
            case 2: f = (int16_t)(f + 0x88); break;
            case 3: f = (int16_t)(f + 0x9a); break;
            case 4: f = (int16_t)(f + 0x76); break;
            }
        }
        if (p->objective != 0x25 && a >= 1 && a <= 0x10) {
            f = 0x5a;
            weapon_pose = true;
        }
    }
    if (weapon_pose || f == 0x59 || f == 0x5a) {
        switch (p->weapon) {
        case 2: f = (int16_t)(f + (p->graphic == 1 ? 0xf : 0x3f)); break;
        case 3: f = (int16_t)(f + 0x51); break;
        case 4: f = (int16_t)(f + 0x2d); break;
        }
    }
    if (a > 0xe8 && a < 0xee && p->graphic == 1) f = (int16_t)(f - 0x49);
    if (f != -1)
        sprite_set_frame(&p->spr, (uint16_t)(p->graphic * 0xbd + sprite_group_base(SPRITE_GROUP_PED) + f));
}

/* Ped_GetCopLook 0x44f210: graphic 1 with the cop remap (0x4b2144, read from the exe) */
void ped_get_cop_look(int16_t *graphic, uint8_t *remap)
{
    *graphic = 1;
    const uint8_t *r = exe_data(0x4b2144, 1);
    *remap = r ? *r : 0;
}

/* Ped_Reset 0x44f230: every field but the remap (+0x5e) and the sprite. */
void ped_reset(int id)
{
    Ped *p = &g_peds[id];
    p->id = (int16_t)id;
    p->state = 2, p->u74 = 2, p->u7c = 2, p->u80 = 2;
    p->turn = p->accel = p->speed = 0;
    p->move_speed = 4;
    p->anim_tick = p->u0c = p->u0e = 0;
    p->control = 0, p->u12 = 0, p->u14 = -1, p->graphic = 0, p->anim = 0;
    p->idle_count = 0, p->player_ctl = 0, p->u1e = 0;
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
    p->uec = 0, p->uee = 0, p->prev_angle = 0, p->uf4 = 0, p->uf8 = 0, p->ufa = 0, p->ufc = 0, p->ufe = 0;
}

/* Ped_InitAll 0x44f370: the module's counters cleared, every slot reset and free (control -1);
   +0x12 alternates 0x80 / -0x80; the driver slots 200..600 sit in car slot - 200 (state 7, health
   100) - 600, the first special slot, too (with car 400, past the table). Each sprite is initialised
   high up (z 0x13f0000) with the first ped sprite and the ped palette, and the slots get the ambient
   colours in turn. */
void ped_init_all(void)
{
    g_ped_7284b0 = 0x10;
    g_ped_728448 = 0;
    g_ped_74f0fd = g_ped_74f0fe = 0;
    g_ped_door_step = 0;
    g_ped_72844c = 0;
    g_ped_7284d8 = g_ped_7284c8 = 0;
    g_ped_74f0fa = g_ped_74f0fb = 0;
    g_ped_74f103 = g_ped_74f102 = 0;
    g_ped_72844d = g_ped_7284d1 = 0;
    g_ped_push_count = 0;
    g_ped_spawn_stop = 0;
    g_ped_7284d6 = 0;
    g_ped_group_active = 0;
    g_ped_groups_used = 0;
    g_ped_74f100 = 0;
    g_ped_remap_cycle = 0;
    g_ped_frame4 = 0;
    g_ped_spawn_player = player_first();
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
        int c = cycle_colour(g_ped_remap_cycle++);
        p->graphic = 0;
        p->remap = (uint8_t)colour_remap(c);
        if (g_ped_remap_cycle > 0x15) g_ped_remap_cycle = 0;
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

/* Ped_StartFiring 0x453210: on foot the ped fires unless it is in anim 0x2b / 0x73 or dead; in a
   car its driver (no checks). */
void ped_start_firing(const int32_t *ref)
{
    if (ref[0] == 2) {
        Ped *p = &g_peds[(int16_t)ref[1]];
        if (p->anim != 0x2b && p->health != 0 && p->anim != 0x73) p->firing = 1;
    }
    if (ref[0] == 0) g_peds[car_get((int16_t)ref[1])->driver].firing = 1;
}
/* Ped_StopFiring 0x453260: on foot only (a driver keeps firing) */
void ped_stop_firing(const int32_t *ref)
{
    if (ref[0] == 2) g_peds[(int16_t)ref[1]].firing = 0;
}

/* Ped_SetTurnInput 0x45f670: for a player ped alive and not dying: standing (0x88) starts walking
   (frame 1) unless in state 9; outside the walk / run frames (and anims 0x62, 0x63, action 0x12,
   state 0x18) the ped doesn't turn. Else the input becomes 4 x itself, clamped to ±0x30 (so a held
   key turns 16 then 48 units a frame); action 0x12 keeps it in +0x12 instead of the turn. */
void ped_set_turn_input(int16_t *turn, int id)
{
    Ped *p = &g_peds[id];
    if (p->player_ctl != 1) return;
    if (p->state != 0x17 && p->health != 0) {
        if (p->state != 9 && p->anim == 0x88) p->anim = 1;
        int a = p->anim;
        if ((a < 1 || a > 0x10) && p->u7c != 0x12 && a != 0x62 && a != 99 && p->state != 0x18) {
            p->turn = 0;
            p->walk_x = 0;
            return;
        }
        int16_t v = *turn;
        if (v >= 1) {
            v = (int16_t)(v << 2);
            if (v > 0x30) v = 0x30;
        } else if (v < 0) {
            v = (int16_t)(v << 2);
            if (v < -0x30) v = -0x30;
        }
        *turn = v;
        if (p->u7c == 0x12) {
            p->turn = 0;
            p->u12 = v;
            p->walk_x = 0;
            return;
        }
        p->turn = v;
        p->walk_x = 0;
        return;
    }
    p->turn = 0;
    p->u12 = 0;
    *turn = 0;
}

/* Ped_SetDestination 0x45f780: walk target, angle (also the heading now) and mode; the ped walks at
   speed 4 (the original first sets 1 when standing still, then 4). A ped of control 2 riding an
   object (kind 2) whose rider is a ped (object +0x18 = 4) updates that rider. */
void ped_set_destination(Ped *p, int32_t x, int32_t y, int angle, int mode)
{
    p->walk_x = x;
    p->walk_y = y;
    p->u44 = (int16_t)angle;
    p->mode = (int16_t)mode;
    p->spr.angle = (int16_t)angle;
    p->u48 = 0;
    if (p->speed == 0 && p->anim < 0x11) p->speed = 1;
    p->speed = 4;
    p->u12 = 0;
    if (p->control == 2 && p->attach_kind == 2) {
        Obj *o = obj_get(p->attach_id);
        if (o->attach_kind == 4) ped_update_riding(&g_peds[o->owner]);
    }
}

/* Ped_SetDestObjective36 0x45f810 */
void ped_set_dest_objective36(Ped *p, int32_t x, int32_t y, int angle)
{
    p->walk_x = x;
    p->target_x = x;
    p->objective = 0x36;
    p->state = 4;
    p->walk_y = y;
    p->target_y = y;
    p->spr.angle = (int16_t)angle;
}

/* Ped_SetPlayerControlled 0x45fa50 */
void ped_set_player_controlled(int id)
{
    g_peds[id].player_ctl = 1;
    g_peds[id].control = 8;
}

/* Ped_ClearPlayerControlled 0x45fa70 */
void ped_clear_player_controlled(int id)
{
    g_peds[id].player_ctl = 0;
    g_peds[id].control = 0;
    g_peds[id].objective = 0x19;
}

/* Ped_Remove 0x45faa0: unless it carries something */
void ped_remove(int id)
{
    Ped *p = &g_peds[(int16_t)id];
    if (p->carried != -1) return;
    p->anim = 0;
    p->car = -1;
    p->health = 0;
    ped_update_sprite(p->id);
    g_peds_active--;
    coll_remove(p, p->spr.unk20);
}

/* the position record Ped_GetPosRect and Ref_GetKind1PosRect fill (0x74f10c) */
static CameraTarget pos_rec;

/* Ped_GetPosRect 0x45fb00: the camera / target record of a ped (one static record) */
const CameraTarget *ped_get_pos_rect(int id)
{
    CameraTarget *r = &pos_rec;
    const Ped *p = &g_peds[id];
    r->x = p->spr.x, r->y = p->spr.y, r->z = p->spr.z;
    r->speed = p->speed;
    r->w = 8, r->h = 8;
    r->angle = p->spr.angle;
    return r;
}

/* Ref_GetKind1PosRect 0x45fb60: the same record for a ridden train (camera / target kind 1): the
   board info Train_Command 7 fills, its speed divided by 10 */
const CameraTarget *ref_get_kind1_pos_rect(int train)
{
    train_command(7, train);
    const TrainBoardInfo *b = train_get_board_info();
    CameraTarget *r = &pos_rec;
    r->x = b->x, r->z = b->z, r->angle = b->angle, r->y = b->y;
    r->speed = (int16_t)(b->speed / 10);
    r->w = b->w, r->h = b->h;
    return r;
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

/* Ped_EnterExitKey 0x45f5e0: on foot (kind 2) the ped gives up its walk target and tries to get
   into a car; otherwise (in a car or on a train) a ped still alive gets out (Ped_PlayerExitCar). */
void ped_enter_exit_key(int32_t *ref, int ped)
{
    if (ref[0] == 2) {
        Ped *p = &g_peds[ref[1]];
        p->u78 = 8;
        p->mode = -1;
        p->walk_x = 0;
        ped_try_enter_car(ref);
    } else if (g_peds[(int16_t)ped].health > 0) {
        ped_player_exit_car(ref, ped);
    }
}

/* Ped_EnterExitKeyAlt 0x45f650: on foot try a car, else get out at the mission's park exit point */
void ped_enter_exit_key_alt(int32_t *ref)
{
    if (ref[0] != 2) ped_exit_car_at_park_point(ref);
    else ped_try_enter_car(ref);
}

/* ---- Ped_UpdateAll 0x45cd50 ---- */

/* The player part: the steering turns the ped (Player_SetFootAux198 / Ped_SetTurnInput), the
   accelerate / brake input walks it: forward the speed grows by one a frame from 4 (Ped_Process caps
   it at the move speed), back it is -2. */
static void update_player_controls(Ped *p)
{
    if (p->player_ctl == 1 && p->walk_x == 0) {
        if (p->state == 9 || p->health < 1 || p->anim == 0x2b) {
            g_peds[0].turn = 0;   /* the original clears ped 0's turn (an absolute address), not this one's */
        } else {
            int s = player_ctl_by_ped(p->id, 3);
            int n = player_find_by_ped(p->id);
            player_set_foot_aux198(n, s < 0 ? -1 : s < 1 ? 0 : 1);
            int16_t v = player_get_ped_field198(p->id);
            player_set_ped_field198(p->id, v);
            p->turn = p->u7c == 0x12 ? (int16_t)(v / 2) : v;
        }
    }
    if (p->player_ctl == 1) {
        int a = p->anim;
        if (p->u7c != 0x12 && ((a > 0 && a < 0x11) || a == 0x88 || p->firing == 1 || a == 0x30)) {
            int16_t acc = (int16_t)player_ctl_by_ped(p->id, 0);
            p->accel = acc;
            if (acc > 0) p->accel = 3;
            if (p->accel == 0) {
                if (p->walk_x < 1) p->speed = 0;
            } else if (p->accel == 3) {
                if ((int16_t)player_ctl_by_ped(p->id, 4) == 1) {
                    if (p->speed == 0 && p->firing == 1) {
                        p->anim = 9;
                        p->speed = 4;
                    }
                    if (p->speed == 0) {
                        p->speed = 4;
                        p->walk_x = 0;
                    } else {
                        p->walk_x = 0;
                        p->speed++;
                    }
                } else {
                    if (p->anim == 0x62 || p->anim == 99 || p->u7c == 0x14) p->anim = 1;
                    p->speed = -2;
                    p->walk_x = 0;
                }
            } else {
                p->speed = 0;
            }
        }
    } else {
        p->accel = 1;
    }
}

/* Ped_UpdateAll 0x45cd50: every slot in use, in order:
   1. the frame's start angle kept (+0xf0); a player ped gets objective 0x25 (no turning in states 9 /
      10, action 0x14 backing ends), and on foot (state 2, action 2) the player controls what the
      camera follows;
   2. the player controls (above), unless in actions 0x11 / 0x13 or states 5, 0x17, 0x15, 10;
   3. walk frame 1 at speed 0 stands (0x88); fire held fires (Ped_FireWeapon) when possible; a
      non-player ped standing for 16 frames starts walking (not with objective 8); a ped carrying an
      object loses health;
   4. dying: health 0 -> blood (object 0x3f on the ground, its angle the ped id), anim 0x2d, state
      0x17 (in state 0x18 the ped just vanishes);
   5. the turn applied (Ped_Animate when standing turns), off-screen ambient peds removed, dead ones
      after 1000 frames (with their ambulance call), then Ped_Process (or Ped_UpdateRiding for riders);
   then up to two ambient peds spawned while fewer than 200 are active. */
void ped_update_all(void)
{
    int wander = 0;
    for (int i = 0; i < PED_MAX; i++) {
        Ped *p = &g_peds[i];
        p->prev_angle = p->spr.angle;
        if (p->anim == 0) continue;
        if (p->player_ctl == 1) {
            int st = p->state;
            p->objective = 0x25;
            if (st == 10 || st == 9) p->turn = 0;
            if (p->u7c == 0x14 && p->speed < 0) p->u7c = 2;
            g_ped_player_ped = p->id;
            if (st == 2 && p->u7c == 2) player_control_view_target((int16_t)player_find_by_ped(p->id));
        }
        if (p->u7c != 0x11 && p->u7c != 0x13 && p->state != 5 && p->state != 0x17 && p->state != 0x15 &&
            p->state != 10)
            update_player_controls(p);
        if (p->anim == 1 && p->speed == 0) p->anim = 0x88;
        if (p->firing == 1 && p->anim != 0x2b && p->state != 0x17 && p->state != 5 && p->state != 0xc &&
            p->state != 10 && p->attach_kind < 1 && p->u7c != 0x12 && p->u7c != 0x13 && p->u7c != 0x11 &&
            p->state != 9 && p->state != 7 && p->state != 0x15)
            ped_fire_weapon(p);
        if (p->anim == 0x88 && p->player_ctl != 1 && p->objective != 8 && ++p->idle_count > 0x10) {
            p->idle_count = 0;
            p->anim = 1;
            p->speed = 1;
        }
        if (p->speed < 0 && p->player_ctl != 1) p->speed = 1;
        if (p->carried != -1) {
            p->health--;
            if (p->player_ctl != 1) p->speed = 4;
        }
        if (p->health < 1) {
            int a = p->anim;
            p->health = 0;
            if (a != 0 && p->state != 0x17 && p->state != 0xc && p->state != 7) {
                if (p->state == 0x18) {
                    p->anim = 0;
                    coll_remove(p, p->spr.unk20);
                } else {
                    if (a != 0x2c) Snd_PlayAt(p->spr.x, p->spr.y, p->spr.z, 0x19);
                    int32_t gz = ped_ground_z(p->spr.x, p->spr.y, p->spr.z);
                    obj_create(p->spr.x, p->spr.y, gz - 1, 0x3f, p->id);   /* (the ped id as the angle) */
                    p->anim = 0x2d;
                }
                p->state = 0x17;
                p->u78 = 8;
                p->attach_kind = 0;
            }
        }
        if (p->anim == 0) continue;
        if (p->turn != 0) {
            p->spr.angle = (int16_t)((p->spr.angle + p->turn) & 0x3ff);
            if (p->speed == 0) ped_animate(p);
        }
        if (p->u48 == 1 && p->anim < 0x11) p->speed = 0;
        if (p->firing == 0 && p->anim == 99) p->anim = 0x88;
        if (p->anim != 0 && p->attach_kind == 0 && p->u7c != 0x13 && !ped_is_visible_recent(p) &&
            p->state != 0x17 && p->state != 0xc && p->attach_id == 0 && p->control == 0 && p->player_ctl == 0 &&
            p->objective != 0x15 && p->state != 1 && p->state != 4 && p->u8b == 0 && p->state != 0x18) {
            p->anim = 0;
            ped_update_sprite(p->id);
            g_peds_active--;
            coll_remove(p, p->spr.unk20);
            g_ped_spawn_stop = 0;
        }
        if ((p->state == 0x17 || p->state == 0xc) && p->u8b == 0 && p->u0e > 1000 &&
            (p->graphic != 1 || p->remap != 0) && p->anim != 0) {
            p->anim = 0;
            ped_update_sprite(p->id);
            g_peds_active--;
            coll_remove(p, p->spr.unk20);
            p->state = 2;
            ambulance_cancel_for_ped(p->id);
        }
        if (p->attach_kind != 0) {
            ped_update_riding(p);
            continue;
        }
        bool run;
        if (p->objective == 0x15 || p->state == 4 || p->state == 1 || p->u0e < 0x32 || p->walk_x != 0)
            run = p->spr.x != 0;
        else if (p->graphic != 1 || p->remap != 0 || p->spr.x == 0)
            run = p->objective == 0x24 && p->spr.x != 0;
        else
            run = true;
        if (run) {
            ped_process(p);
            if (p->objective == 0x19) wander++;
        }
    }
    if (g_peds_active < 200)
        for (int c = 0; c < 2; c++) {
            ped_spawn_ambient(-1);
            if (g_ped_spawn_stop == 1) break;
        }
    if (++g_ped_frame4 == 4) g_ped_frame4 = 0;
    if (g_game.opt.debug_text) {
        static char buf[64];   /* 0x502f78 */
        hud_clear_zone_text(0x60);
        snprintf(buf, sizeof buf, exe_str(0x4b21c8), (int16_t)wander);   /* "peds : %d" */
        hud_show_zone_text(buf, 0x60);
    }
}

int peds_in_use(void)
{
    int k = 0;
    for (int i = 0; i < PED_MAX; i++) k += g_peds[i].anim != 0;
    return k;
}
