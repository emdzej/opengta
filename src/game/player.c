#include "player.h"
#include "../exe.h"
#include "car.h"
#include "game.h"
#include "ped.h"
#include "stubs.h"
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

/* Player_GetViewTargetPos 0x462d50 with the records of Car_GetCamTarget 0x408220 (kind 0) and
   Ped_GetPosRect 0x45fb00 (kind 2). Kinds 1 (trains) and 5 (the heli) aren't ported: they give the
   fixed point. */
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
