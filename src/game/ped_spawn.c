/* Ped creation and the ped module's helpers around it (0x44f8d0, 0x452f30-0x4544df): follower groups
   (Ped_LeaveGroup), panic around gunfire (Ped_PanicNear), firing (Ped_FireWeapon), the on-screen tests,
   the ambient pedestrians that appear just outside the view (Ped_SpawnAmbient) and the creators
   (Ped_Create, Ped_CreateInSlot, Ped_CreateSpecial, Ped_CreateWithAnim, Ped_PlaceAndSend). */
#include "ped_internal.h"
#include "../exe.h"
#include "weapon.h"
#include "sentinel.h"

/* the colour tables (read from the exe): 0x4b21b0 the 22 ambient colours in turn, 0x4b20b0 colour ->
   remap; 0x4b213f the remap of a group's members, 0x4b2144 the cop remap */
static uint8_t exe_byte(uint32_t va)
{
    const uint8_t *t = exe_data(va, 1);
    return t ? *t : 0;
}
static uint8_t next_ambient_remap(void)
{
    uint8_t c = exe_byte(0x4b21b0 + g_ped_remap_cycle++);
    if (g_ped_remap_cycle > 0x15) g_ped_remap_cycle = 0;
    return exe_byte(0x4b20b0 + c);
}

/* a slot of [first, end) that is free: no animation, not in a car (7), not a mission ped, not dead
   (0x17, 0xc) */
static int free_slot(int first, int end)
{
    int i;
    for (i = first; i < end; i++) {
        const Ped *p = &g_peds[i];
        if (p->anim == 0 && p->state != 7 && p->u8b != 1 && p->state != 0x17 && p->state != 0xc) break;
    }
    return i;
}

/* Ped_LeaveGroup 0x44f8d0: a follower leaves its group: the members after it move up a place, each
   following the one before it (the first reads the group's count byte and pad as its target: a quirk
   of the original, which addresses the member list one short too early); the follower goes back to
   its own target (+0x5a, or where it stands) unless the leader is dead / stuck (state 1). The leader
   (objective 0x15) leaving disbands the group: every live member chases the player ped (objective
   0x2a, state 1). Clears the group-active flag either way. */
void ped_leave_group(Ped *p)
{
    int32_t player = g_ped_player_ped;
    g_ped_group_active = 0;
    int g = (int8_t)p->group;
    if (g < 0 || g >= PED_GROUPS) return;   /* (the original indexes before the table) */
    PedGroup *G = &g_ped_groups[g];
    if (p->objective != 0x15) {
        int8_t s = (int8_t)p->group_slot;
        for (int i = s; i < G->count - 1; i = s) {
            int16_t m = G->member[i + 1];
            G->member[i] = m;
            g_peds[m].group_slot = (uint8_t)s;
            s++;
            g_peds[G->member[i]].target_ped =
                i == 0 ? (int16_t)((uint8_t)G->count | G->pad03 << 8) : G->member[i - 1];
        }
        int ls = g_peds[G->leader].state;
        if (ls != 1 && ls != 0x17 && ls != 0xc) {
            p->state = 1;
            p->speed = 4;
            p->target_ped = p->u5a;
            if (p->u5a == -1) {
                p->target_x = p->spr.x;
                p->target_y = p->spr.y;
            }
        }
        G->member[s] = -1;
        G->count--;
        return;
    }
    for (int i = 0; i < G->count; i++) {
        Ped *m = &g_peds[G->member[i]];
        if (m->state != 0x17 && m->state != 0xc) {
            m->objective = 0x2a;
            m->state = 1;
            m->speed = 4;
            m->target_ped = (int16_t)player;
            m->walk_x = 0;
        }
    }
    g_ped_7284d6 = 0;
    g_ped_groups_used--;
    G->leader = -1;
    G->count = -1;
}

/* Ped_PanicNear 0x452f30: shots / explosions at (x, y, z) by `ped` (-1: no one). Every ambient ped of
   the 3 x 3 cells around, within half a block in z, walking or standing, slower than 4, alive and not
   already fleeing (state 1) reacts: unless it is walking somewhere (states 3, 4; objectives 0x15 /
   0x38 always react) it flees (state 1, speed 4) from the shooter (target ped) or the point; a group
   member (objective 0x16) makes its leader flee instead. Fleeing from a ped in a car it runs from the
   car (scream B), from a ped on foot it faces away from it (scream C). Then one of three screams
   (0x17, 0x16, 0x15 in turn, 0x74f0fe). */
void ped_panic_near(int32_t x, int32_t y, int32_t z, int ped)
{
    ped = (int16_t)ped;
    for (CollHit *h = coll_query_block_all(x, y, z, COLL_PED); h; h = h->next) {
        Ped *q = h->owner;
        int a = q->anim;
        if (!(z - 0x200000 <= q->spr.z && q->spr.z <= z + 0x200000 && ((a > 0 && a < 0x11) || a == 0x88) &&
              q->player_ctl == 0 && q->control == 0 && q->speed < 4 && q->id != ped && q->state != 1 &&
              q->health != 0))
            continue;
        if ((q->state != 4 && q->state != 3) || q->objective == 0x15 || q->objective == 0x38) {
            if (q->objective != 0x16) {
                q->state = 1;
                q->u12 = 0;
                q->walk_x = 0;
                q->speed = 4;
                if (ped == -1) {
                    q->target_x = x;
                    q->target_y = y;
                }
                q->target_ped = (int16_t)ped;
            } else {
                Ped *l = &g_peds[g_ped_groups[(int8_t)q->group].leader];
                if (l->health != 0) {
                    q->walk_x = 0;   /* (the member's walk target, not the leader's) */
                    l->state = 1;
                    l->target_ped = (int16_t)g_ped_player_ped;
                }
                if (ped == -1) {
                    l->target_ped = -1;
                    l->target_x = x;
                    l->target_y = y;
                }
                q = l;
            }
            if (ped != -1) {
                int t = q->target_ped;
                /* (a target of -1 reads the record before the table in the original; the port
                   treats it as a ped on foot that isn't the shooter) */
                if (t >= 0 && g_peds[t].state == 7) {
                    if (g_peds[t].car < 0) {
                        q->target_ped = -1;
                        q->state = 2;
                        q->u7c = 2;
                        q->speed = 1;
                    } else {
                        const Car *c = car_get(g_peds[t].car);
                        q->spr.angle = (int16_t)math_atan2(q->spr.y - c->spr.y, q->spr.x - c->spr.x);
                        q->speed = 4;
                        Snd_PlayVoiceB(q->spr.x, q->spr.y, q->spr.z);
                    }
                } else {
                    if (t >= 0 && q->target_ped == ped)
                        q->spr.angle = (int16_t)math_atan2(q->spr.y - g_peds[t].spr.y, q->spr.x - g_peds[t].spr.x);
                    Snd_PlayVoiceC(q->spr.x, q->spr.y, q->spr.z);
                    q->speed = 4;
                }
            }
            if (g_ped_74f0fe >= 0 && g_ped_74f0fe <= 2)
                Snd_PlayAt(q->spr.x, q->spr.y, q->spr.z, 0x17 - g_ped_74f0fe);
        }
        if (++g_ped_74f0fe > 2) g_ped_74f0fe = 0;
    }
    coll_unlock();
}

/* Ped_ClearTargetsInBlock 0x453280: ambient peds (0..199) walking to block (x, y) stop doing so */
void ped_clear_targets_in_block(int32_t x, int32_t y)
{
    for (int i = 0; i < PED_DRIVER_FIRST; i++) {
        Ped *p = &g_peds[i];
        if (p->walk_x >> 22 == x >> 22 && ((p->walk_y ^ y) & 0xffc00000) == 0) p->walk_x = 0, p->walk_y = 0;
    }
}

/* Ped_FireWeapon 0x4532f0: unarmed, a punch (action 0x14; standing still: anim 0xa9); armed, while
   fire is held and the reload delay (+0x64) is over: nearby peds panic, a standing ped takes the
   firing pose (0x62), ambient peds stop, and the weapon fires with its sound (pistol 0x21, machine
   gun 0x22 counting its shots in +0x47, rocket 0x24, flamethrower 0x23; others are fatal -0x4a); a
   player uses up the ammo. */
void ped_fire_weapon(Ped *p)
{
    int w = p->weapon;
    if (w == 0 && p->u7c != 0x14) {
        if (p->speed == 0) p->anim = 0xa9;
        if (p->speed >= 0) p->u7c = 0x14;
        return;
    }
    if (p->firing == 0 || w == 0 || p->u64 >= 1) return;
    ped_panic_near(p->spr.x, p->spr.y, p->spr.z, p->id);
    if (p->anim == 0x88) p->anim = 0x62;
    p->idle_count = 0;
    if (p->player_ctl != 1) p->speed = 0;
    switch (p->weapon) {
    case 0: break;
    case 1:
        Snd_PlayUI(p->spr.x, p->spr.y, p->spr.z, 0x21);
        weapon_fire_bullet(p);
        break;
    case 2:
        Snd_PlayUI(p->spr.x, p->spr.y, p->spr.z, 0x22);
        weapon_fire_bullet_flag(p);
        p->u47++;
        break;
    case 3:
        Snd_PlayUI(p->spr.x, p->spr.y, p->spr.z, 0x24);
        weapon_fire_rocket(p);
        break;
    case 4:
        Snd_PlayUI(p->spr.x, p->spr.y, p->spr.z, 0x23);
        weapon_fire_flame(p);
        break;
    default:
        game_fatal(-0x4a, 0x158, p->weapon);
    }
    int n = player_find_by_ped(p->id);
    if (n != -1) player_use_ammo(n);
}

/* ---- on screen ---- */

/* the pixel position (the high halves of x, y, as shorts) inside a player's view rect grown by m,
   and inside the map's 16-pixel border */
static bool in_view(int px, int py, int m)
{
    for (int n = player_first(); n > -1; n = player_next(n)) {
        const int32_t *r = player_get_view_rect(n);   /* left, right, top, bottom */
        if (r[0] - m <= px && r[2] - m <= py && px <= r[1] + m && py <= r[3] + m && px > 0xf && py > 0xf &&
            px < 0x3ff1 && py < 0x3ff1)
            return true;
    }
    return false;
}
/* inside the map past a quarter block from the edges (all three tests end with it) */
static bool in_map(const Ped *p)
{
    return p->spr.x > 0x100000 && p->spr.y > 0x100000 && p->spr.x < 0x3ff00000 && p->spr.y < 0x3ff00000;
}

/* Ped_IsOnScreen 0x453480: in any player's view */
bool ped_is_on_screen(const Ped *p)
{
    bool on = in_view((int16_t)(p->spr.x >> 16), (int16_t)(p->spr.y >> 16), 0);
    return in_map(p) && on;
}

/* Ped_IsVisibleRecent 0x453540: in a view now, a mission ped (+0x8b), or out of view for fewer than
   30 frames (+0x0e counts the frames out of view; seen or a mission ped, it restarts) */
bool ped_is_visible_recent(const Ped *cp)
{
    Ped *p = (Ped *)cp;   /* (it keeps the counter) */
    bool on = in_view((int16_t)(p->spr.x >> 16), (int16_t)(p->spr.y >> 16), 0);
    if (p->u8b == 1) {
        on = true;
        p->u0e = 0;
    } else if (!on) {
        if (++p->u0e < 0x1e) on = true;
    } else {
        p->u0e = 0;
    }
    return in_map(p) && on;
}

/* Pos_IsNearScreen 0x453630: a point (16.16) within 0x140 pixels of any player's view (the pixel
   coordinates as ints here, not shorts) */
bool pos_is_near_screen(int32_t x, int32_t y)
{
    return in_view(x >> 16, y >> 16, 0x140);
}

/* Ped_IsNearScreen 0x4536d0: a ped within 0x140 pixels of any player's view */
bool ped_is_near_screen(const Ped *p)
{
    bool on = in_view((int16_t)(p->spr.x >> 16), (int16_t)(p->spr.y >> 16), 0x140);
    return in_map(p) && on;
}

/* ---- creation ---- */

/* Ped_Create 0x453e90: the first free ambient slot (0..199) at (x, y) with z snapped to the bottom
   of its layer (at most layer 4.97), speed, angle and anim; health 100 (0 for anim 0x2c, a corpse).
   kind 0x16 makes it the next follower of the group being formed (objective 0x16, state 4, following
   the previous member 0x7284d4, the group's remap 0x4b213f); else it wanders (objective 0x19).
   Returns the slot, -1 if none. */
int ped_create(int32_t x, int32_t y, int32_t z, int speed, int angle, int anim, int kind)
{
    int i = free_slot(0, PED_DRIVER_FIRST);
    if (i == PED_DRIVER_FIRST) return -1;
    Ped *p = &g_peds[i];
    ped_reset(i);
    p->spr.x = x;
    p->health = 0;
    int32_t zz = (int32_t)(((uint32_t)z & 0xffc00000u) + 0x3f0000);
    p->objective = 0x19;
    p->spr.y = y;
    p->spr.angle = (int16_t)angle;
    p->spr.z = zz;
    if (zz > 0x13f0000) p->spr.z = 0x13f0000;
    p->anim = (int16_t)anim;
    ped_update_sprite(i);
    g_peds_active++;
    p->speed = (int16_t)speed;
    p->u5a = -1;
    p->control = 0;
    p->health = p->anim == 0x2c ? 0 : 100;
    coll_insert(COLL_PED, i, p, p->spr.unk20, p->spr.x, p->spr.y);
    p->u84 = 0;
    p->u78 = 8;
    if ((int16_t)kind == 0x16) {
        int g = g_ped_groups_used;
        PedGroup *G = &g_ped_groups[g < PED_GROUPS ? g : 0];   /* (one group in practice) */
        p->objective = 0x16;
        p->target_ped = g_ped_7284d4;
        p->state = 4;
        p->u7c = 2;
        g_ped_7284d4 = p->id;
        p->group = (uint8_t)g;
        int8_t c = G->count;
        p->group_slot = (uint8_t)c;
        p->graphic = 0;
        G->member[c] = p->id;
        G->count++;
        p->remap = exe_byte(0x4b213f);
        ped_update_sprite(i);
        sprite_set_remap(&p->spr, p->remap);
        return i;
    }
    p->objective = 0x19;
    p->state = 2;
    p->u7c = 2;
    p->target_ped = -1;
    return i;
}

/* Ped_CreateInSlot 0x454090: ped `slot`, if free, walking (anim 1) at (x, y) with z snapped like
   Ped_Create, standing still at first. */
void ped_create_in_slot(int32_t x, int32_t y, int32_t z, int angle, int slot)
{
    slot = (int16_t)slot;
    Ped *p = &g_peds[slot];
    if (p->anim != 0) return;
    ped_reset(slot);
    p->spr.x = x;
    int32_t zz = (int32_t)(((uint32_t)z & 0xffc00000u) + 0x3f0000);
    p->spr.y = y;
    p->spr.angle = (int16_t)angle;
    p->spr.z = zz;
    if (zz > 0x13f0000) p->spr.z = 0x13f0000;
    p->anim = 1;
    ped_update_sprite(slot);
    g_peds_active++;
    p->u5a = -1;
    p->u5c = -1;
    p->speed = 0;
    p->health = 100;
    p->idle_count = 0;
    p->anim_tick = 0;
    p->u0e = 0;
    p->control = 0;
    coll_insert(COLL_PED, slot, p, p->spr.unk20, p->spr.x, p->spr.y);
}

/* Ped_CreateSpecial 0x454180: a special ped (slots 600..619) with the cop look, control 2, riding
   (attach kind / id, offsets +0x56 / +0x54) at (x, y, z) (z kept within 0x10000..0x13f0000, the depth
   key z - 1). Returns the slot, -1 if none. */
int ped_create_special(int32_t x, int32_t y, int32_t z, int speed, int angle, int attach_kind, int attach_id,
                       int off56, int off54)
{
    int i = free_slot(PED_SPECIAL_FIRST, PED_MAX);
    if (i == PED_MAX) return -1;
    ped_reset(i);
    Ped *p = &g_peds[i];
    p->spr.y = y;
    p->spr.angle = (int16_t)angle;
    p->spr.x = x;
    p->spr.z = z;
    if (z > 0x13f0000) p->spr.z = 0x13f0000;
    if (p->spr.z < 0x10001) p->spr.z = 0x10000;
    p->spr.zkey = z - 1;
    p->anim = 1;
    ped_update_sprite(i);
    uint8_t r = exe_byte(0x4b2144);
    p->graphic = 1;
    p->remap = r;
    sprite_set_remap(&p->spr, r);
    p->speed = (int16_t)speed;
    p->attach_id = (int16_t)attach_id;
    g_peds_active++;
    p->u56 = (int16_t)off56;
    p->attach_kind = (int16_t)attach_kind;
    p->health = 100;
    p->car = -1;
    p->u54 = (int16_t)off54;
    p->control = 2;
    coll_insert(COLL_PED, i, p, p->spr.unk20, p->spr.x, p->spr.y);
    return i;
}

/* Ped_CreateWithAnim 0x4542e0: an ambient ped (0..199) with the next ambient colour at (x, y, z)
   (z as given), speed, angle and anim, state 0x16, +0x84 = 0x14. Returns the slot, -1 if none. */
int ped_create_with_anim(int32_t x, int32_t y, int32_t z, int speed, int angle, int anim)
{
    int i = free_slot(0, PED_DRIVER_FIRST);
    if (i == PED_DRIVER_FIRST) return -1;
    Ped *p = &g_peds[i];
    ped_reset(i);
    p->graphic = 0;
    p->remap = next_ambient_remap();
    sprite_set_remap(&p->spr, p->remap);
    p->spr.x = x;
    p->spr.y = y;
    p->speed = 4;
    p->health = 100;
    p->spr.angle = (int16_t)angle;
    p->spr.z = z;
    p->anim = (int16_t)anim;
    ped_update_sprite(i);
    p->u0e = 0;
    p->control = 0;
    g_peds_active++;
    p->speed = (int16_t)speed;
    p->u84 = 0x14;
    p->state = 0x16;
    coll_insert(COLL_PED, i, p, p->spr.unk20, p->spr.x, p->spr.y);
    return i;
}

/* Ped_CreateAnim41 0x454430: Ped_CreateWithAnim at speed 2 with anim 0x41; true if created */
bool ped_create_anim41(int32_t x, int32_t y, int32_t z, int angle)
{
    return (int16_t)ped_create_with_anim(x, y, z, 2, angle, 0x41) != -1;
}

/* Ped_PlaceAndSend 0x454460: ped `id` put at (x, y, z) facing `angle` and sent to (dx, dy) (mode
   0x39, state 0x16). The grid node is inserted again without being removed first, as in the
   original. Returns 1. */
int ped_place_and_send(int id, int32_t x, int32_t y, int32_t z, int angle, int32_t dx, int32_t dy)
{
    Ped *p = &g_peds[(int16_t)id];
    p->spr.x = x;
    p->spr.y = y;
    p->spr.z = z;
    coll_insert(COLL_PED, id, p, p->spr.unk20, p->spr.x, p->spr.y);
    p->spr.angle = (int16_t)angle;
    ped_set_destination(p, dx, dy, angle, 0x39);
    p->accel = 1;
    p->state = 0x16;
    return 1;
}

/* Ped_SpawnAmbient 0x4537b0: one pedestrian for the next player's view (Net_Query1: player 0 outside
   network games), in the first free ambient slot when kind is -1 (the original only works that way:
   with a slot given it writes through a null record). It starts 64 pixels outside the view, on the
   side the player's ped faces (any side in turn while the ped stands still; a player in a car takes
   the car's heading and z), at a random place along that edge, walking into the view (two in ten
   walk along it the other way: 0x74f0fd counts to 10), snapped to its block plus 16..48 pixels
   (0x7284b0). It is created only on flat pavement out of every view (64-pixel margin) with nothing
   in its block. Every 100th becomes the leader of a follower group of six (objective 0x15) while no
   group exists. Sets 0x7284d2 when it creates one. */
void ped_spawn_ambient(int kind)
{
    int slot = (int16_t)kind;
    Ped *s = NULL;
    if (slot == -1) {
        for (slot = 0; slot < PED_DRIVER_FIRST; slot++) {
            s = &g_peds[slot];
            int st = s->state;
            if (s->anim == 0 && st != 7 && st != 6 && s->u8b != 1 && st != 0xc && st != 0x17) break;
        }
    }
    if (slot == PED_DRIVER_FIRST) return;
    if (!s) s = &g_peds[slot];
    g_ped_spawn_player = 0;   /* Net_Query1 0x412ab0: 0 outside network games */
    const int32_t *r = player_get_view_rect(g_ped_spawn_player);
    Ped *pp = &g_peds[player_get_ped(g_ped_spawn_player)];
    int16_t left = (int16_t)r[0];
    int16_t right = (int16_t)(r[1] + 0x40);
    int16_t top = (int16_t)(r[2] - 0x40);
    int16_t bottom = (int16_t)(r[3] + 0x40);
    s->spr.z = pp->spr.z;
    if (player_get_controlled_kind(g_ped_spawn_player) == 0) {
        const Car *c = car_get(player_get_controlled_id(g_ped_spawn_player));
        if (pp->state == 7) {   /* (it writes the player's ped) */
            pp->spr.angle = c->spr.angle;
            pp->spr.z = c->spr.z;
        }
    }
    int a = pp->spr.angle, side;
    if ((a < 0x80 && a >= 0) || a > 0x37f) side = 2;
    else if (a < 0x80 || a > 0x17f) side = a > 0x27f ? 0 : 3;
    else side = 1;
    if (pp->speed == 0) {
        side = g_ped_728448;
        if (++g_ped_728448 > 3) g_ped_728448 = 0;
    }
    if (++g_ped_74f0fd > 9) g_ped_74f0fd = 0;
    int16_t l40 = (int16_t)(left - 0x40);
    switch (side) {
    case 0:   /* the left edge */
        s->spr.x = (int32_t)l40 << 16;
        s->spr.y = ((int16_t)math_random() * (bottom - top) / 0x7fff + top) * 0x10000;
        s->spr.angle = g_ped_74f0fd > 7 ? 0x300 : 0x100;
        break;
    case 1:   /* the right edge */
        s->spr.x = (int32_t)right << 16;
        s->spr.y = ((int16_t)math_random() * (bottom - top) / 0x7fff + top) * 0x10000;
        s->spr.angle = g_ped_74f0fd < 8 ? 0x300 : 0x100;
        break;
    case 2:   /* the bottom edge */
        s->spr.y = (int32_t)bottom << 16;
        s->spr.x = ((int16_t)math_random() * (right - l40) / 0x7fff + l40) * 0x10000;
        s->spr.angle = g_ped_74f0fd > 7 ? 0 : 0x200;
        break;
    default:   /* the top edge */
        s->spr.y = (int32_t)top << 16;
        s->spr.x = ((int16_t)math_random() * (right - l40) / 0x7fff + l40) * 0x10000;
        s->spr.angle = g_ped_74f0fd < 8 ? 0 : 0x200;
        break;
    }
    Ped *p = &g_peds[slot];
    int16_t off = (int16_t)g_ped_7284b0;
    p->spr.x = (int32_t)((uint32_t)p->spr.x & 0xffc00000u);
    p->spr.y = (int32_t)((uint32_t)p->spr.y & 0xffc00000u);
    p->spr.x = ((int16_t)(p->spr.x >> 16) + off) * 0x10000;
    g_ped_7284b0++;
    p->spr.y = ((int16_t)(p->spr.y >> 16) + off) * 0x10000;
    if (g_ped_7284b0 > 0x30) g_ped_7284b0 = 0x10;
    int16_t px = (int16_t)(p->spr.x >> 16), py = (int16_t)(p->spr.y >> 16);
    if (!(px > 0x10 && py > 0x10 && py < 0x3ff1 && px < 0x3ff1)) return;
    int32_t gz = ped_ground_z(p->spr.x, p->spr.y, p->spr.z);
    p->spr.z = gz - 0x10000;
    uint8_t c = ped_type_cache(p->spr.x >> 22, p->spr.y >> 22, (gz - 0x10000) >> 22);
    if ((int8_t)c < 0) return;   /* a slope */
    int ground = c >> 4 & 7;
    int ix = p->spr.x >> 16, iy = p->spr.y >> 16;
    for (int n = player_first(); n > -1; n = player_next(n)) {
        const int32_t *v = player_get_view_rect(n);
        if (v[0] - 0x40 <= ix && v[2] - 0x40 <= iy && ix <= v[1] + 0x40 && iy <= v[3] + 0x40) return;
    }
    if (ground != 3) return;
    if (!coll_query_block(p->spr.x, p->spr.y, p->spr.z, 0, 1)) {
        g_ped_7284d6++;
        ped_reset(slot);
        p->anim = 1;
        p->graphic = 0;
        p->remap = next_ambient_remap();
        sprite_set_remap(&p->spr, p->remap);
        p->u0e = 0;
        ped_update_sprite(slot);
        g_peds_active++;
        p->speed = 1;
        p->health = 100;
        p->weapon = 0;
        p->control = 0;
        if (g_ped_7284d6 == 100 && g_ped_groups_used == 0) {
            /* the leader of a group of six followers, all created where it stands (their offsets of
               -0x10..+4 are in 16.16 units: fractions of a pixel) */
            p->state = 2;
            p->u7c = 2;
            p->objective = 0x15;
            p->u78 = 8;
            p->u84 = 0;
            p->graphic = 0;
            p->remap = exe_byte(0x4b213f);
            ped_update_sprite(slot);
            sprite_set_remap(&p->spr, p->remap);
            int g = g_ped_groups_used;
            p->group = (uint8_t)g;
            g_ped_7284d4 = p->id;
            p->group_slot = 0;
            g_ped_groups[g].leader = p->id;
            g_ped_groups[g].member[0] = p->id;
            g_ped_groups[g].count = 1;
            for (int k = 0, d = -0x10; k < 6; k++, d += 4)
                ped_create(p->spr.x + d, p->spr.y + d, p->spr.z, 1, p->spr.angle, 1, 0x16);
            g_ped_group_active = 1;
            g_ped_groups_used++;
        } else {
            p->objective = 0x19;
            p->state = 2;
            p->u78 = 8;
            p->u7c = 2;
            p->u84 = 0;
        }
        ambulance_cancel_for_ped(p->id);
        coll_insert(COLL_PED, slot, p, p->spr.unk20, p->spr.x, p->spr.y);
        g_ped_spawn_stop = 1;
    }
    coll_unlock();
}
