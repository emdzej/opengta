#include "player.h"
#include "../exe.h"
#include "car.h"
#include "game.h"
#include "ped.h"
#include "heli.h"
#include "stubs.h"
#include "mission_obj.h"
#include "../audio/audio.h"
#include "../text.h"
#include <stdio.h>
#include <string.h>

Player g_players[PLAYER_MAX] = { 0 };
int g_player_local;
int g_player_viewed;
int g_player_count = 1;
bool g_players_ready;
uint32_t g_player_respawn_block;
static int net_mode;                         /* 0x501d7c: 2 in a network game */

/* Player_First 0x412a70 / Player_Next 0x412a90: the network session's slots in a network game
   (not ported), else player 0 alone. */
int player_first(void) { return net_mode == 2 ? 0 : 0; }   /* (Net_FirstSlot not ported) */
int player_next(int n) { (void)n; return net_mode == 2 ? -1 : -1; }   /* (Net_NextSlot not ported) */

/* Player_SetPed 0x461e90: also makes the ped's control type 8 (Ped_SetControlType). */
void player_set_ped(int n, int ped)
{
    g_players[n].ped = (int16_t)ped;
    ped_set_control_type((int16_t)ped, 8);
}

/* Player_GetRemap 0x461720: table 0x4b21e0 (read from the exe) by colour and player. */
int player_get_remap(int n)
{
    const uint8_t *t = exe_data(0x4b21e0 + (uint32_t)g_players[n].colour * 4 + (uint32_t)n, 1);
    return t ? *t : 0;
}

/* The ammo byte of weapon w (1..4) addressed as the original does, +0x157 + w: weapon 0 reads the
   top byte of wanted_level. */
static uint8_t *ammo_of(Player *p, int w)
{
    static uint8_t none;
    return w >= 0 && w <= 4 ? (uint8_t *)p + 0x157 + w : &none;
}

/* Player_PrevWeapon 0x461a50: the previous weapon with ammo (none: 0), unless the current one is a
   temporary (frenzy) weapon. */
void player_prev_weapon(int n)
{
    Player *p = &g_players[n];
    int w = p->weapon;
    if (*ammo_of(p, w) == 'd') return;
    if (w == 0) w = 5;
    for (;;) {
        if (--w == 0) {
            p->weapon = 0;
            break;
        }
        if (*ammo_of(p, w)) {
            p->weapon = w;
            break;
        }
    }
    ped_set_weapon_raw(p->ped, p->weapon);
}

/* Player_InitAll 0x463290: every record (all four) loses its temporary weapons (the saved weapon and
   ammo come back), armour and speed-up, gets its weapons emptied (or 99 of each with the weapons
   cheat in single player), score 0 (999999999 with the score cheat), multiplier 1 (10 with the
   cheat) and the HUD strings. Then the active players get 4 lives (infinite with more players or
   the option 0x502f35), their camera follows what they control, and a player in a car switches it
   to physics control. */
void player_init_all(void)
{
    bool single = g_player_count == 1;
    for (int n = 0; n < PLAYER_MAX; n++) {
        Player *p = &g_players[n];
        memset(p->stats, 0, sizeof p->stats);
        for (int i = 0; i < 4; i++) {
            if (p->ammo[i] != 'd') continue;
            p->ammo[i] = 0;
            int w = p->saved_weapon;
            p->timers[0] = 0;
            p->weapon = w;
            p->sub_ammo[0] = p->saved_ammo[1];
            *ammo_of(p, w) = p->saved_ammo[0];
            p->sub_ammo[1] = p->saved_ammo[2];
            ped_set_weapon_raw(p->ped, p->weapon);
            if (p->weapon != 0 && *ammo_of(p, p->weapon) == 0) player_prev_weapon(n);
        }
        if (p->armour != 0) {
            p->armour = 0;
            ped_set_flag4a(ped_get(p->ped), 0);
        }
        if (p->speedup != 0) {
            p->speedup = 0;
            ped_set_move_speed(ped_get(p->ped), 0);
        }
        p->timers[1] = -1;
        memset(p->ammo, 0, sizeof p->ammo);
        p->weapon = 0;
        ped_set_weapon_raw(p->ped, 0);
        if (!g_game.opt.cheat_weapons || !single) {
            memset(p->ammo, 0, sizeof p->ammo);
            p->jail_free = 0;
            p->armour = 0;
        } else {
            memset(p->ammo, 99, sizeof p->ammo);
            p->jail_free = 1;
            p->armour = 3;
        }
        p->weapon = 0;
        p->alarm = 300;
        p->bust = 0;
        p->flag1 = 0;
        p->wanted_level = 0;
        p->wanted_points = 0;
        p->u184 = -1;
        p->score = -1;
        p->frags = 0;
        p->frag_done = 0;
        p->u178 = -1;
        p->u17c = 0;
        p->u17e = -1;
        p->u180 = 1;
        p->speedup = 0;
        p->timers[0] = 0;
        p->timers[1] = -1;
        p->timers[2] = -1;
        p->u1a4 = -1;
        p->mult = !g_game.opt.cheat_mult || !single ? 1 : 10;
        if (!g_game.opt.cheat_score || !single) {
            snprintf(p->hud_mult, sizeof p->hud_mult, "%02d", 0);
            snprintf(p->hud_score2, sizeof p->hud_score2, "%09d", 0);
            snprintf(p->hud_score, sizeof p->hud_score, "%09d", 0);
            memset(p->u13c, 0, sizeof p->u13c);
            p->u14c = 0;
        } else {   /* "%02d" of 999999999 is 9 digits: the original overflows hud_mult into hud_score */
            char b[12];
            snprintf(b, sizeof b, "%09d", 999999999);
            memcpy(p->hud_mult, b, 3);
            snprintf(p->hud_score2, sizeof p->hud_score2, "%09d", 999999999);
            snprintf(p->hud_score, sizeof p->hud_score, "%09d", 999999999);
            p->score = 999999999;
            for (int i = 0; i < 4; i++) p->u13c[i] = 0x90009;
            p->u14c = 9;
        }
        p->local1 = p->local2 = 0;
        memset(p->ctl, 0, sizeof p->ctl);
    }
    for (int n = player_first(); n > -1; n = player_next(n)) {
        Player *p = &g_players[n];
        p->lives = g_player_count < 2 && !g_game.opt.infinite_lives ? 4 : -1;
        p->u18c = 0;
        p->u189 = 0;
        p->view_kind = p->ctl_kind;
        p->view_id = p->ctl_id;
        if (p->ctl_kind == PLAYER_IN_CAR) car_set_physics_control((int16_t)p->ctl_id);
        if (p->score == -1) p->score = 0;
    }
    g_players_ready = true;
}

/* Camera_DebugMove 0x43cb20: the debug velocities grow by one per frame in their direction and move
   the camera's debug offsets (x by dbg[3], y by dbg[2], zoom by dbg[0], height by dbg[1]). */
void camera_debug_move(int n)
{
    int32_t *v = g_players[n].dbg;
    for (int i = 0; i < 4; i++) {
        if (v[i] > 0) v[i]++;
        else if (v[i] < 0) v[i]--;
    }
    Camera *c = &g_players[n].cam;
    c->dbg_x += v[3];
    c->dbg_y += v[2];
    c->dbg_height += v[1];
    c->dbg_zoom += v[0];
}

/* Camera_DebugStop 0x43cbb0 */
void camera_debug_stop(int n) { memset(g_players[n].dbg, 0, sizeof g_players[n].dbg); }

/* Player_GetViewTargetPos 0x462d50 with the records of Car_GetCamTarget 0x408220 (kind 0),
   Ref_GetKind1PosRect 0x45fb60 (kind 1, a ridden train: its position) and Ped_GetPosRect 0x45fb00
   (kind 2) and Heli_GetPos 0x40dc80 (kind 5). */
CameraTarget player_view_target(int n)
{
    const Player *p = &g_players[n];
    CameraTarget t = { 0 };
    switch (p->view_kind) {
    case CAM_TARGET_CAR: {
        const Car *c = car_get((int16_t)p->view_id);
        t.x = c->spr.x;
        t.y = c->spr.y;
        t.z = c->spr.z - c->z_offset;
        t.angle = c->spr.angle;
        t.speed = (int16_t)(c->speed * 3);
        t.h = (int16_t)-c->length;
        t.w = c->cam_w;
        break;
    }
    case CAM_TARGET_KIND1: {   /* a ridden train: Ref_GetKind1PosRect 0x45fb60 */
        const int32_t *r = ref_get_kind1_pos_rect((int16_t)p->view_id);
        t.x = r[0], t.y = r[1], t.z = r[2];
        break;
    }
    case CAM_TARGET_HELI: {   /* Heli_GetPos 0x40dc80 */
        const HeliPos *h = heli_get_pos();
        t.x = h->x, t.y = h->y, t.z = h->z;
        t.speed = (int16_t)h->speed;
        break;
    }
    case CAM_TARGET_PED: {
        const Ped *d = ped_get((int16_t)p->view_id);
        t.x = d->spr.x;
        t.y = d->spr.y;
        t.z = d->spr.z;
        t.speed = d->speed;
        t.w = 8;
        t.h = 8;
        t.angle = d->spr.angle;
        break;
    }
    default:
        t.x = p->view_x;
        t.y = p->view_y;
        t.z = p->view_z;
        break;
    }
    return t;
}

void player_camera(int n, CameraPlayer *cp)
{
    const Player *p = &g_players[n];
    cp->rect = p->rect;
    cp->vp = p->vp;
    cp->cam = p->cam;
    cp->mode = p->mode;
    cp->target_kind = p->view_kind;
    cp->target = player_view_target(n);
}

void player_camera_store(int n, const CameraPlayer *cp)
{
    Player *p = &g_players[n];
    p->rect = cp->rect;
    p->vp = cp->vp;
    p->cam = cp->cam;
    p->mode = cp->mode;
}

/* The state Camera_Follow consults: the feature switches, Car_IsModel7or33 0x405920 of a viewed car,
   Ped_IsState9 / Ped_IsDead / Ped_IsState10 of the player's ped. */
CameraWorld player_camera_world(int n)
{
    const Player *p = &g_players[n];
    CameraWorld w = { .peds = g_game.opt.peds != 0, .cars = g_game.opt.cars != 0 };
    if (p->view_kind == CAM_TARGET_CAR) {
        int m = car_get((int16_t)p->view_id)->model;
        w.car_model_7_or_33 = m == 7 || m == 0x21;
    }
    if (p->ped >= 0 && p->ped < PED_MAX) {
        const Ped *d = ped_get(p->ped);
        w.ped_state9_or_dead = d->state == 9 || d->health == 0;   /* Ped_IsState9 0x460770, Ped_IsDead 0x4772c0 */
        w.ped_state10 = d->state == 10;   /* Ped_IsState10 0x460790 */
    }
    return w;
}

/* ======== the player module's accessors and actions ======== */

/* The player whose ped is `ped` (Player_FindByPed 0x464440 returns 0xffff, i.e. -1, for none). */
int player_find_by_ped(int ped)
{
    for (int n = player_first(); n > -1; n = player_next(n))
        if ((int16_t)ped == g_players[n].ped) return n;
    return -1;
}

/* The *ByPed accessors read the record of index -1 (before the table) when no player has the ped;
   every caller asks for a player's ped, so the port reads player 0 there instead. */
static Player *by_ped(int ped)
{
    int n = player_find_by_ped(ped);
    return &g_players[n < 0 ? 0 : n];
}
/* Player_GetAccelByPed 0x4645c0 (i 0), _GetAxis2ByPed 0x464610 (1), _GetFireByPed 0x464660 (2),
   _GetSteerByPed 0x4646b0 (3), _GetMoveFlagByPed 0x464700 (4), _GetJumpByPed 0x464750 (5): the held
   control byte, sign-extended. */
int player_ctl_by_ped(int ped, int i) { return by_ped(ped)->ctl[i]; }
int16_t player_get_ped_field198(int ped) { return by_ped(ped)->foot_aux; }    /* 0x461e40 */
void player_set_ped_field198(int ped, int v) { by_ped(ped)->foot_aux = (int16_t)v; }   /* 0x461df0 */

/* Player_NextWeapon 0x4619f0: the next weapon with ammo, 0 after the last; not while a temporary
   (frenzy) weapon is held. */
void player_next_weapon(int n)
{
    Player *p = &g_players[n];
    int w = p->weapon;
    if (*ammo_of(p, w) == 'd') return;
    for (;;) {
        if (++w == 5) {
            p->weapon = 0;
            break;
        }
        if (*ammo_of(p, w)) {
            p->weapon = w;
            break;
        }
    }
    ped_set_weapon_raw(p->ped, p->weapon);
}

/* Player_SelectWeapon 0x461b00 */
void player_select_weapon(int n, int w)
{
    Player *p = &g_players[n];
    p->weapon = w;
    ped_set_weapon_raw(p->ped, w);
    if (w != 0 && *ammo_of(p, w) == 0) player_prev_weapon(n);
}

/* Player_AddAmmo 0x461b50: ammo + count capped at 99 (the sum is a byte: a wrap past 255 stays
   small), the 5-shot counters of weapons 2 and 4 refilled, and the weapon selected unless a temporary
   one is held. A temporary weapon's own ammo isn't touched. */
void player_add_ammo(int n, int w, int count)
{
    Player *p = &g_players[n];
    uint8_t *a = ammo_of(p, w);
    if (*a == 'd') return;
    *a = (uint8_t)(*a + (int8_t)count);
    if (*a > 99) *a = 99;
    if (w == 2) p->sub_ammo[0] = 5;
    else if (w == 4) p->sub_ammo[1] = 5;
    if (*ammo_of(p, p->weapon) != 'd') {
        p->weapon = w;
        ped_set_weapon_raw(p->ped, w);
    }
}

/* Player_StartFrenzyWeapon 0x461bd0: the current weapon and its counters saved, weapon w with the
   temporary marker 100 for `frames`. */
void player_start_frenzy_weapon(int n, int w, int frames)
{
    Player *p = &g_players[n];
    Snd_PlayVoice(3);
    p->saved_weapon = p->weapon;
    p->saved_ammo[1] = p->sub_ammo[0];
    p->saved_ammo[0] = *ammo_of(p, p->weapon);
    p->saved_ammo[2] = p->sub_ammo[1];
    *ammo_of(p, w) = 'd';
    p->timers[0] = (int16_t)frames;
    p->weapon = w;
    ped_set_weapon_raw(p->ped, w);
}

/* Player_EndFrenzyWeapon 0x461c70: every temporary weapon emptied and the saved weapon back. */
void player_end_frenzy_weapon(int n)
{
    Player *p = &g_players[n];
    for (int i = 0; i < 4; i++) {
        if (p->ammo[i] != 'd') continue;
        p->ammo[i] = 0;
        p->timers[0] = 0;
        p->weapon = p->saved_weapon;
        p->sub_ammo[0] = p->saved_ammo[1];
        *ammo_of(p, p->weapon) = p->saved_ammo[0];
        p->sub_ammo[1] = p->saved_ammo[2];
        ped_set_weapon_raw(p->ped, p->weapon);
        if (p->weapon != 0 && *ammo_of(p, p->weapon) == 0) player_prev_weapon(n);
    }
}

bool player_has_frenzy_weapon(int n)         /* Player_HasFrenzyWeapon 0x461d10 */
{
    for (int i = 0; i < 4; i++)
        if (g_players[n].ammo[i] == 'd') return true;
    return false;
}

/* Player_HasTempWeapon 0x4647e0: the timer +0x1ae running, or a temporary weapon */
bool player_has_temp_weapon(int n)
{
    if (g_players[(int16_t)n].timers[1] != -1) return true;
    return player_has_frenzy_weapon(n);
}

/* Player_UseAmmo 0x461d40: one shot; the machine gun (2) and the flamethrower (4) take one unit of
   ammo every 5 shots (their sub-counters). With the weapon empty the player falls back to the pistol
   when it has pistol ammo, else to no weapon. */
void player_use_ammo(int n)
{
    Player *p = &g_players[n];
    int w = p->weapon;
    uint8_t *a = ammo_of(p, w);
    if (*a == 'd') return;
    if (*a != 0) {
        if (w == 2) {
            if (--p->sub_ammo[0] == 0) p->sub_ammo[0] = 5, (*a)--;
        } else if (w == 4) {
            if (--p->sub_ammo[1] == 0) p->sub_ammo[1] = 5, (*a)--;
        } else {
            (*a)--;
        }
    }
    if (*ammo_of(p, p->weapon) == 0) {
        p->weapon = p->ammo[0] != 0;
        ped_set_weapon_raw(p->ped, p->weapon);
    }
}

/* Player_ClearWeapons 0x464ba0: the four ammo bytes (one int store) and the weapon */
void player_clear_weapons(int n)
{
    Player *p = &g_players[n];
    memset(p->ammo, 0, sizeof p->ammo);
    p->weapon = 0;
    ped_set_weapon_raw(p->ped, 0);
}

/* Player_RemovePowerUps 0x4637b0: armour and speed-up off (on the ped too), the +0x1ae timer off */
void player_remove_power_ups(int n)
{
    Player *p = &g_players[(int16_t)n];
    if (p->armour != 0) {
        p->armour = 0;
        ped_set_flag4a(ped_get(p->ped), 0);
    }
    if (p->speedup != 0) {
        p->speedup = 0;
        ped_set_move_speed(ped_get(p->ped), 0);
    }
    p->timers[1] = -1;
}

void player_halve_multiplier(int n)          /* Player_HalveMultiplier 0x464be0 (at least 1) */
{
    Player *p = &g_players[(int16_t)n];
    p->mult >>= 1;
    if (p->mult == 0) p->mult = 1;
}
void player_add_multiplier(int n, int d) { g_players[(int16_t)n].mult = (uint16_t)(g_players[(int16_t)n].mult + d); }

/* Player_AddLife 0x4630b0: not past 99 ('c'), and an infinite count (-1) stays */
void player_add_life(int n)
{
    int8_t *l = &g_players[(int8_t)n].lives;
    if (*l != 'c' && *l != -1) (*l)++;
}

void player_dec_armour(int n)                /* Player_DecArmour 0x4636a0 */
{
    Player *p = &g_players[(int16_t)n];
    if (--p->armour < 0) p->armour = 0;
}

/* Player_Wasted 0x463100: voice 12, "WASTED" (FXT key 0x4b2234) for the local player, temporary
   weapons given back, armour and speed-up off, the +0x1ae timer off, all ammo and the weapon gone,
   the wanted state of the ped cleared (Wanted_ClearForPed), a life taken. True if a life was left (or
   lives are infinite). */
bool player_wasted(int n)
{
    n = (int8_t)n;
    Player *p = &g_players[n];
    Snd_PlayVoice(0xc);
    if (n == g_player_local) hud_show_big_message_lo(text_get(exe_str(0x4b2234)));
    for (int i = 0; i < 4; i++) {
        if (p->ammo[i] != 'd') continue;
        p->ammo[i] = 0;
        p->timers[0] = 0;
        p->weapon = p->saved_weapon;
        p->sub_ammo[0] = p->saved_ammo[1];
        *ammo_of(p, p->weapon) = p->saved_ammo[0];
        p->sub_ammo[1] = p->saved_ammo[2];
        player_select_weapon(n, p->weapon);
    }
    if (p->armour != 0) {
        p->armour = 0;
        ped_set_flag4a(ped_get(p->ped), 0);
    }
    if (p->speedup != 0) {
        p->speedup = 0;
        ped_set_move_speed(ped_get(p->ped), 0);
    }
    p->timers[1] = -1;
    memset(p->ammo, 0, sizeof p->ammo);
    p->weapon = 0;
    ped_set_weapon_raw(p->ped, 0);
    wanted_clear_for_ped(p->ped);
    if (p->lives > 0) {
        p->lives--;
        return true;
    }
    return p->lives == -1;
}

/* Player_Busted 0x464820: once: the bust state 1 (Player_UpdateAll respawns the player two frames
   later), voice 12, "BUSTED" (0x4b223c) for the local player, the game paused 50 frames. */
void player_busted(int n)
{
    Player *p = &g_players[n];
    if (p->bust != 0) return;
    p->bust = 1;
    Snd_PlayVoice(0xc);
    if (n == g_player_local) hud_show_big_message_lo(text_get(exe_str(0x4b223c)));
    game_set_pause_frames(0x32);
}

/* Player_AddScore 0x461f20: points x multiplier, capped at 999999999, with a floating score */
void player_add_score(int n, int points, int x, int y, int z, int popup)
{
    Player *p = &g_players[n];
    int v = (int)p->mult * points;
    p->score += v;
    if (p->score > 999999999) p->score = 999999999;
    if ((int16_t)popup != 0) hud_add_score_popup(x, y, z, v, n);
}
void player_sub_score(int n, int points)     /* Player_SubScore 0x461f90 (not below 0) */
{
    Player *p = &g_players[n];
    p->score -= points;
    if (p->score < 0) p->score = 0;
}
int player_get_score(int n) { return g_players[n].score; }
const char *player_get_name(int n) { return g_players[n].name; }
const int32_t *player_get_view_rect(int n) { return &g_players[n].rect.left; }

/* Player_GetControlledPos 0x462ef0: Car_GetCamTarget 0x408220, Ped_GetPosRect 0x45fb00 (one static
   record each in the original); trains (Ref_GetKind1PosRect) and the heli (Heli_GetPos) aren't
   ported and give a zero record; other kinds are fatal (-0x4a). */
const int32_t *player_get_controlled_pos(int n)
{
    static CameraTarget rec;
    const Player *p = &g_players[n];
    memset(&rec, 0, sizeof rec);
    switch (p->ctl_kind) {
    case 0: {
        const Car *c = car_get((int16_t)p->ctl_id);
        rec.x = c->spr.x, rec.y = c->spr.y, rec.z = c->spr.z - c->z_offset;
        rec.angle = c->spr.angle, rec.speed = (int16_t)(c->speed * 3);
        rec.h = (int16_t)-c->length, rec.w = c->cam_w;
        break;
    }
    case 2: {
        const Ped *d = ped_get((int16_t)p->ctl_id);
        rec.x = d->spr.x, rec.y = d->spr.y, rec.z = d->spr.z;
        rec.w = rec.h = 8, rec.speed = d->speed, rec.angle = d->spr.angle;
        break;
    }
    case 1: case 5: break;
    default: game_fatal(-0x4a, 0x123, p->ctl_kind);
    }
    return &rec.x;
}

void player_set_view_target(int n, int kind, int id) { g_players[n].view_kind = kind, g_players[n].view_id = id; }
void player_set_view_fixed(int32_t x, int32_t y, int32_t z, int n)   /* Player_SetViewFixed 0x462cb0 */
{
    Player *p = &g_players[n];
    p->view_x = x, p->view_kind = 3, p->view_id = 0, p->view_y = y, p->view_z = z;
}
/* Player_ControlViewTarget 0x463000: what the camera follows becomes controlled (not a fixed point) */
void player_control_view_target(int n)
{
    Player *p = &g_players[n];
    if (p->view_kind != 3 && p->view_kind != 4) p->ctl_kind = p->view_kind, p->ctl_id = p->view_id;
}
void player_reset_view(int n) { g_players[n].view_kind = g_players[n].ctl_kind, g_players[n].view_id = g_players[n].ctl_id; }

/* Player_RetargetCamera 0x463b00: every player whose camera follows (kind, id) follows the new one */
void player_retarget_camera(int kind, int id, int new_kind, int new_id)
{
    for (int n = player_first(); n > -1; n = player_next(n)) {
        Player *p = &g_players[n];
        if (p->view_kind == kind && p->view_id == (int16_t)id) {
            p->view_kind = new_kind;
            p->view_id = (int16_t)new_id;
            camera_start_transition(n);
        }
    }
}

/* Player_EnterCar 0x463a10: the player on foot as `ped` now drives `car` (kind 0); a camera that
   followed the ped follows the car (which gets control 1) with a transition and the car's name shown
   to the local player. Space / Tab held states are dropped. */
void player_enter_car(int ped, int car)
{
    int n;
    for (n = player_first(); n > -1; n = player_next(n))
        if (g_players[n].ctl_kind == 2 && g_players[n].ctl_id == (int16_t)ped) break;
    if (n < 0) return;
    Player *p = &g_players[n];
    if (p->view_kind == p->ctl_kind && p->view_id == p->ctl_id) {
        p->view_kind = 0;
        p->view_id = (int16_t)car;
        car_get((int16_t)car)->control = 1;
        camera_start_transition(n);
        if (n == g_player_local) hud_show_car_name((int16_t)car);
    }
    p->ctl[2] = 0, p->ctl[5] = 0;
    p->ctl_kind = 0;
    p->ctl_id = (int16_t)car;
}

/* Player_ExitCar 0x463c40: the player whose ped is `ped` gets it back under control (with the
   player's weapon) - for every player whose ped it is, as the loop goes - and the player driving
   `car` is on foot as `ped` (move flag 1); a camera on the car follows the ped and the car loses its
   control (-1, unless 3). */
void player_exit_car(int ped, int car)
{
    int n = player_first();
    if (n < 0) return;
    for (;;) {
        if ((int16_t)ped == g_players[n].ped) {
            ped_set_player_controlled((int16_t)ped);
            ped_set_weapon_raw((int16_t)ped, g_players[n].weapon);
        }
        if (g_players[n].ctl_kind == 0 && g_players[n].ctl_id == (int16_t)car) break;
        n = player_next(n);
        if (n < 0) return;
    }
    Player *p = &g_players[n];
    if (p->view_kind == p->ctl_kind && p->view_id == p->ctl_id) {
        p->view_kind = 2;
        p->view_id = (int16_t)ped;
        Car *c = car_get((int16_t)car);
        if (c->control != 3) c->control = -1;
        camera_start_transition(n);
    }
    p->ctl_id = (int16_t)ped;
    p->ctl_kind = 2;
    p->ctl[4] = 1;
    p->ctl[2] = 0, p->ctl[5] = 0;
}

/* Player_ExitTrain 0x463be0 */
void player_exit_train(int n)
{
    Player *p = &g_players[n];
    ped_enter_exit_key_alt(&p->ctl_kind);
    if (p->view_kind == 3) p->view_kind = 4;
    p->ctl[2] = 0, p->ctl[5] = 0;
}

/* Player_BoardTrain 0x463d50: kind 1, the ped's train (+0x8c, a signed byte) */
void player_board_train(int ped)
{
    const Ped *d = ped_get((int16_t)ped);
    int n = player_find_by_ped(ped);
    Player *p = &g_players[n < 0 ? 0 : n];
    p->view_kind = 1, p->ctl_kind = 1;
    p->view_id = p->ctl_id = (int8_t)d->train;
}

/* Player_SetOnFoot 0x463dd0 */
void player_set_on_foot(int ped)
{
    int n = player_find_by_ped(ped);
    Player *p = &g_players[n < 0 ? 0 : n];
    p->view_kind = 2, p->ctl_kind = 2;
    p->view_id = p->ctl_id = (int16_t)ped;
    ped_set_player_controlled((int16_t)ped);
    p->ctl[2] = 0, p->ctl[5] = 0;
}

/* Player_OnLeaveVehicle 0x463e50: the ped no longer player-controlled; a car goes back to dummy control */
void player_on_leave_vehicle(int n)
{
    Player *p = &g_players[n];
    ped_clear_player_controlled(p->ped);
    switch (p->ctl_kind) {
    case 0: car_set_dummy_control((int16_t)p->ctl_id); break;
    case 1: case 2: case 5: break;
    default: game_fatal(-0x4a, 0x138, p->ctl_kind);
    }
}

/* Player_SetFootAux198 0x463990: on foot only. The turn input +0x198 starts at +4 (left) or -4
   (right) unless it already turns that way, 0 without steering; then Ped_SetTurnInput scales it. */
void player_set_foot_aux198(int n, int dir)
{
    Player *p = &g_players[n];
    if (p->ctl_kind != 2) return;
    if (dir == -1) {
        if (p->foot_aux < 1) p->foot_aux = 4;
    } else if (dir == 0) {
        p->foot_aux = 0;
    } else if (dir == 1 && p->foot_aux >= 0) {
        p->foot_aux = -4;
    }
    ped_set_turn_input(&p->foot_aux, p->ctl_id);
}

/* ---- wanted level, by ped ---- */
void player_clear_wanted_points(int ped)     /* Player_ClearWantedPoints 0x4617a0 */
{
    for (int n = (int8_t)player_first(); n > -1; n = (int8_t)player_next(n))
        if (g_players[n].ped == (int16_t)ped) g_players[n].wanted_points = 0;
}
void player_add_wanted_points(int ped, int pts)   /* Player_AddWantedPoints 0x4617e0 */
{
    for (int n = (int8_t)player_first(); n > -1; n = (int8_t)player_next(n)) {
        Player *p = &g_players[n];
        if (p->ped != (int16_t)ped) continue;
        p->wanted_points = (int16_t)(p->wanted_points + pts);
        if (p->wanted_points > 2000) p->wanted_points = 2000;
    }
}
int player_get_wanted_points(int ped)        /* Player_GetWantedPoints 0x461840 */
{
    for (int n = (int8_t)player_first(); n > -1; n = (int8_t)player_next(n))
        if (g_players[n].ped == (int16_t)ped) return g_players[n].wanted_points;
    return -1;
}
void player_clear_wanted_level(int ped)      /* Player_ClearWantedLevel 0x461890 */
{
    for (int n = (int8_t)player_first(); n > -1; n = (int8_t)player_next(n))
        if (g_players[n].ped == (int16_t)ped) g_players[n].wanted_level = 0;
}
void player_set_wanted_level(int ped, int lvl)    /* Player_SetWantedLevel 0x461910 */
{
    for (int n = (int8_t)player_first(); n > -1; n = (int8_t)player_next(n))
        if (g_players[n].ped == (int16_t)ped) g_players[n].wanted_level = lvl;
}
int player_get_wanted_level_by_ped(int ped)  /* Player_GetWantedLevelByPed 0x461960 */
{
    for (int n = (int8_t)player_first(); n > -1; n = (int8_t)player_next(n))
        if (g_players[n].ped == (int16_t)ped) return g_players[n].wanted_level;
    return 5;
}
bool player_any_wanted(void)                 /* Player_AnyWanted 0x4618d0 */
{
    for (int n = (int8_t)player_first(); n > -1; n = (int8_t)player_next(n))
        if (g_players[n].wanted_level > 2) return true;
    return false;
}
bool player_any_field1a4_is(int v)           /* Player_AnyField1A4Is 0x4629d0 */
{
    for (int n = (int8_t)player_first(); n > -1; n = (int8_t)player_next(n))
        if (g_players[n].u1a4 == v) return true;
    return false;
}

/* ---- input ---- */

/* Player_ApplyInput 0x463ec0: the control word of the viewed player into its held controls. In a car
   or on foot: steering -> ctl[3]; accelerate -> ctl[0] with ctl[4] = 1; brake -> ctl[0] with
   ctl[4] = -1 (on a train they command the train, on foot accelerating also may respawn the ped
   beside its car); Space -> ctl[2]; fire starts / stops firing (in a car only with a driver: the
   car is told either way, Car_OnDriverEnter; on a train it boards or leaves); enter / exit (not with
   +0x189, the game frozen or paused); next / previous weapon; Tab -> ctl[5] (the horn in a car, a
   sound on foot). */
void player_apply_input(uint32_t w)
{
    int n = g_player_viewed;
    Player *p = &g_players[n];
    if (w & 1) {
        int s = (int)(w >> 9) & 0xf;
        int8_t v = (int8_t)s;
        if (s > 8) v = (int8_t)(v - 0x10);
        if (p->ctl_kind == 0 || p->ctl_kind == 2) p->ctl[3] = v;
    }
    if (w & 2) {
        int v = (int)(w >> 15) & 3;
        if (p->ctl_kind == 0) {
            p->ctl[0] = (int8_t)v, p->ctl[4] = 1;
        } else if (p->ctl_kind == 1) {
            if (v) train_command(3, (uint8_t)p->ctl_id);
        } else if (p->ctl_kind == 2) {
            p->ctl[0] = (int8_t)v, p->ctl[4] = 1;
            ped_respawn_beside_car(v / 3, p->ctl_id);
        }
    }
    if (w & 0x80) {
        int8_t v = (int8_t)((w >> 13) & 3);
        if (p->ctl_kind == 0 || p->ctl_kind == 2) {
            p->ctl[4] = -1, p->ctl[0] = v;
        } else if (p->ctl_kind == 1) {
            if (v) train_command(4, (uint8_t)p->ctl_id);
        }
    }
    if ((w & 4) && (p->ctl_kind == 0 || p->ctl_kind == 2)) p->ctl[2] = (int8_t)((w >> 20) & 1);
    if (w & 8) {
        if (!(w & 0x200000)) {
            ped_stop_firing(&p->ctl_kind);
        } else if (p->ctl_kind == 0) {
            int car = (int16_t)p->ctl_id;
            if (car_get(car)->driver != -1) ped_start_firing(&p->ctl_kind);
            car_on_driver_enter(car);
        } else if (p->ctl_kind == 1) {
            bool other = false;
            if (train_is_boarded((uint8_t)p->ctl_id) == 1) {
                /* the original compares each boarded player's index with the fire bit itself
                   (0x200000), so any boarded player counts */
                for (int m = player_first(); m > -1; m = player_next(m))
                    if (g_players[m].train_door == 1 && (uint32_t)m != (w & 0x200000)) other = true;
                if (!other) {
                    train_command(2, (uint8_t)p->ctl_id);
                    p->train_door = 0;
                }
            } else {
                train_command(1, (uint8_t)p->ctl_id);
                p->train_door = 1;
                police_report_crime(2, p->ped, 4, 0, 0, 0);
            }
            ped_start_firing(&p->ctl_kind);
        } else if (p->ctl_kind == 2) {
            ped_start_firing(&p->ctl_kind);
        } else {
            game_fatal(-0x4a, 0x1ee, p->ctl_kind);
        }
    }
    if ((w & 0x400000) && p->u189 == 0 && !game_is_frozen() && !game_is_paused()) player_toggle_vehicle();
    p = &g_players[g_player_viewed];
    if ((w & 0x10) && (w & 0x40000)) player_next_weapon(g_player_viewed);
    if ((w & 0x100) && (w & 0x20000)) player_prev_weapon(g_player_viewed);
    if (w & 0x20) {
        int v = (int)(w >> 19) & 1;
        p->ctl[5] = (int8_t)v;
        if (p->ctl_kind == 0) {
            car_set_horn_by_id(p->ctl_id, v);
        } else if (p->ctl_kind == 2 && v) {
            CameraTarget t = player_view_target(g_player_viewed);   /* (the player record in the original) */
            Snd_PlayAtObject(t.x, t.y, t.z);
        }
    }
}

/* Player_ToggleVehicle 0x4642b0: the enter / exit key. On foot with the camera on a train, the
   player controls the train first (kind 1). When the camera follows what the player controls, it
   follows the new thing after the key, with a transition. Space / Tab held states are dropped. */
void player_toggle_vehicle(void)
{
    int n = g_player_viewed;
    Player *p = &g_players[n];
    if (p->ctl_kind == 2 && p->view_kind == 1) p->ctl_kind = 1;
    if (p->ctl_kind == p->view_kind && p->ctl_id == p->view_id) {
        ped_enter_exit_key(&p->ctl_kind, p->ped);
        if (p->view_kind != p->ctl_kind || p->ctl_id != p->view_id) {
            p->view_kind = p->ctl_kind;
            p->view_id = p->ctl_id;
            camera_start_transition(n);
        }
    } else {
        ped_enter_exit_key(&p->ctl_kind, p->ped);
    }
    p = &g_players[(int16_t)g_player_viewed];
    p->ctl[2] = 0, p->ctl[5] = 0;
}

/* Player_UpdateCarAlarmTimer 0x464c10: in a car, +0x1b8 counts down while the car's +0x11a is 1
   (else 300); once negative the car's siren / alarm is switched off every frame. */
static void player_update_car_alarm_timer(void)
{
    for (int n = player_first(); n > -1; n = player_next(n)) {
        Player *p = &g_players[n];
        if (p->ctl_kind != 0) continue;
        Car *c = car_get((int16_t)p->ctl_id);
        if (p->alarm < 0) car_siren_off(c);
        if (c->horn == 1) p->alarm--;
        else p->alarm = 300;
    }
}

/* Player_UpdateAll 0x464880, per frame:
   1. a busted player (state 1) is processed the frame after (state 2): frenzy weapon ended, +0x1ae
      off, unless the jail-free card (+0xfa, then used up) multiplier halved, power-ups and weapons
      lost; wanted points and level cleared, respawn at the police station, camera snap, the
      criminal record shown;
   2. a camera on a fixed point (kind 3) goes back to the player once what it controls nears the
      view's edge (64 pixels);
   3. the countdowns: +0x18c, the bonus chain (+0x16c; at 0 it forgets the chain), the frenzy timer
      (its end gives the weapon back), +0x1ae, +0x1b0; on foot the speed-up timer (its end slows the
      ped) and armour (none: the ped's flag off);
   4. the car alarm timer; frags in network games (Player_UpdateFrags 0x464c90, not ported). */
void player_update_all(void)
{
    for (int n = player_first(); n > -1; n = player_next(n)) {
        Player *p = &g_players[n];
        if (p->bust == 0) continue;
        if (p->bust == 1) {
            p->bust = 2;
        } else if (p->bust == 2) {
            player_end_frenzy_weapon(n);
            g_players[(int16_t)n].timers[1] = -1;
            if (p->jail_free == 0) {
                player_halve_multiplier(n);
                player_remove_power_ups(n);
                player_clear_weapons(n);
            } else {
                p->jail_free = 0;
            }
            player_clear_wanted_points(p->ped);
            player_clear_wanted_level(p->ped);
            player_respawn_at_station(ped_get(p->ped), 0);
            CameraPlayer cp;
            CameraWorld cw = player_camera_world(0);
            player_camera(0, &cp);   /* Camera_Snap(0) */
            camera_snap(&cp, &cw);
            player_camera_store(0, &cp);
            police_show_criminal_record();
            p->bust = 0;
        } else {
            game_fatal(-0x4a, 0x1fc, p->bust);
        }
    }
    for (int n = player_first(); n > -1; n = player_next(n)) {
        Player *p = &g_players[n];
        if (p->view_kind != 3) continue;
        const int32_t *pos = player_get_controlled_pos(n);
        int x = (int16_t)(pos[0] >> 16), y = (int16_t)(pos[1] >> 16);
        if (x < p->rect.left + 0x40 || y < p->rect.top + 0x40 || p->rect.right - 0x40 < x ||
            p->rect.bottom - 0x40 < y) {
            player_reset_view(n);
            camera_start_transition(n);
        }
    }
    for (int n = (int16_t)player_first(); n > -1; n = (int16_t)player_next(n))
        if (g_players[n].u18c > 0) g_players[n].u18c--;
    for (int n = (int8_t)player_first(); n > -1; n = (int8_t)player_next(n)) {
        Player *p = &g_players[n];
        if (p->bonus_timer < 1) p->bonus_kind = 0, p->bonus_count = 0;
        else p->bonus_timer--;
    }
    for (int n = (int8_t)player_first(); n > -1; n = (int8_t)player_next(n)) {
        Player *p = &g_players[n];
        if (p->timers[0] != 0 && --p->timers[0] == 0) player_end_frenzy_weapon(n);
        if (p->timers[1] >= 0) p->timers[1]--;
        if (p->timers[2] >= 0) p->timers[2]--;
        int id = (int16_t)p->ctl_id;
        if (p->ctl_kind != 0 && p->ctl_kind != 1) {
            if (p->ctl_kind == 2) {
                if (p->speedup != 0 && --p->speedup == 0) ped_set_move_speed(ped_get(id), 0);
                if (p->armour < 1) ped_set_flag4a(ped_get(id), 0);
            } else {
                game_fatal(-0x4a, 0x160, p->ctl_kind);
            }
        }
    }
    player_update_car_alarm_timer();
    if (g_player_count < 2) return;
    /* Player_UpdateFrags 0x464c90: network games only (not ported) */
}
