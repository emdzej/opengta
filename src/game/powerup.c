/* Power-ups (0x46a0a0-0x46a99f; see powerup.h). */
#include "powerup.h"
#include "car.h"
#include "game.h"
#include "obj.h"
#include "ped.h"
#include "player.h"
#include "stubs.h"
#include "../audio/audio.h"
#include "../exe.h"
#include "../hud/hud.h"
#include "../text.h"
#include <stdio.h>

PowerUp g_powerups[POWERUP_MAX];

/* PowerUp_InitAll 0x46a0a0 */
void powerup_init_all(void)
{
    for (int i = 0; i < POWERUP_MAX; i++) g_powerups[i] = (PowerUp){ 0, 0, 0, -1, 0, 0, 0 };
}

/* PowerUp_Add 0x46a0d0: the first record without an object; a help sign (14) is object 0x5f shown
   at once (visible 2), the rest a crate 0x54 (visible 1). */
int powerup_add(int type, int value, int32_t x, int32_t y, int32_t z)
{
    int i = 0;
    while (i < POWERUP_MAX && g_powerups[i].obj != -1) i++;
    if (i == POWERUP_MAX) return 0;
    PowerUp *p = &g_powerups[i];
    int vis = type == 0xe ? 2 : 1;
    p->obj = (int16_t)obj_create(x, y, z, type == 0xe ? 0x5f : 0x54, 0);
    if (p->obj == -1) return 0;
    p->visible = vis;
    p->z = z;
    p->x = x;
    p->type = type;
    p->value = value;
    p->y = y;
    return 1;
}

/* the object a revealed power-up of `type` shows (-1: none) */
static int revealed_object(int type)
{
    switch (type) {
    case 1: return 0x4e;
    case 2: return 0x4f;
    case 3: return 0x50;
    case 4: return 0x51;
    case 6: return 0x61;
    case 9: return 0x65;
    case 10: return 0x60;
    case 11: return 100;
    case 12: return 0x62;
    case 13: case 15: return 99;
    default: return -1;
    }
}

/* PowerUp_Reveal 0x46a190: the first record at exactly (x, y) (in use or not): its object goes, the
   crate sound (0xb), the power-up's object and the broken crate (0x55, a pixel lower) appear. Without
   an object for the type the record is cleared (its value stays). Revealing again replaces the object
   and adds another broken crate. */
void powerup_reveal(int32_t x, int32_t y)
{
    int i = 0;
    while (i < POWERUP_MAX && (g_powerups[i].x != x || g_powerups[i].y != y)) i++;
    if (i == POWERUP_MAX) return;
    PowerUp *p = &g_powerups[i];
    if (p->obj >= 0) obj_delete(p->obj);   /* (a free record's -1 would index before the table) */
    Snd_PlayAt(p->x, p->y, p->z, 0xb);
    int t = revealed_object(p->type);
    p->obj = t < 0 ? -1 : (int16_t)obj_create(p->x, p->y, p->z, t, 0);
    obj_create(p->x, p->y, p->z - 1, 0x55, 0);
    if (p->obj == -1) {
        p->x = p->y = p->z = 0;
        p->visible = 0;
        p->type = 0;
        return;
    }
    p->visible = 2;
}

/* PowerUp_RemoveAt 0x46a4a0 */
bool powerup_remove_at(int32_t x, int32_t y)
{
    for (int i = 0; i < POWERUP_MAX; i++) {
        PowerUp *p = &g_powerups[i];
        if (p->x != x || p->y != y || p->obj == -1) continue;
        obj_delete(p->obj);
        *p = (PowerUp){ 0, 0, 0, -1, 0, 0, 0 };
        return true;
    }
    return false;
}

/* PowerUp_ExistsAt 0x46a530 */
bool powerup_exists_at(int32_t x, int32_t y)
{
    for (int i = 0; i < POWERUP_MAX; i++)
        if (g_powerups[i].x == x && g_powerups[i].y == y && g_powerups[i].obj != -1) return true;
    return false;
}

/* a pick-up message for the local player: the FXT text of key (the key strings are the exe's) */
static void message(int n, uint32_t key_va)
{
    if (n == g_player_local) hud_show_zone_text(text_get(exe_str(key_va)), 0);
}

/* the weapon table 0x4a8c18: {FXT key, default ammo} per weapon */
static uint32_t weapon_key(int w) { return exe_u32(0x4a8c18 + (uint32_t)w * 8); }
static int weapon_default_ammo(int w) { return (int)exe_u32(0x4a8c1c + (uint32_t)w * 8); }

/* Car_DampThrust 0x40c0c0 is the car module's (stubs.h) */

/* PowerUp_Collect 0x46a560 */
void powerup_collect(int n, int32_t x, int32_t y, int how)
{
    (void)how;
    Player *pl = &g_players[n];
    for (int i = 0; i < POWERUP_MAX; i++) {
        PowerUp *p = &g_powerups[i];
        if (p->x != x || p->y != y || p->obj == -1) continue;
        int type = p->type, value = p->value;
        bool take;
        switch (type) {
        case 1: case 2: case 3: case 4:
            /* Player_GetAmmo 0x461ae0 reads +0x157 + weapon: ammo[weapon - 1] */
            if (value < 100) take = pl->ammo[type - 1] != 99;
            else take = !player_has_temp_weapon(n);
            break;
        case 10: take = pl->armour != 3; break;            /* Player_GetArmour 0x463680 */
        case 12: take = pl->jail_free == 0; break;         /* Player_GetFlagFA 0x461fc0 */
        default: take = true; break;
        }
        if (!take) continue;
        obj_delete(p->obj);
        switch (type) {
        case 1: case 2: case 3: case 4:
            message(n, weapon_key(type));
            if (value == 0) player_add_ammo(n, type, weapon_default_ammo(type));
            else if (value < 100) player_add_ammo(n, type, value);
            else player_start_frenzy_weapon(n, type, value - 100);
            break;
        case 6: case 7: case 8: {
            int m = (int16_t)n, kind = player_get_controlled_kind(m), id = player_get_controlled_id(m);
            if (kind == PLAYER_IN_CAR) {
                message(m, 0x4b22cc);   /* car speed */
                car_damp_thrust(car_get(id));
            } else if (kind == PLAYER_ON_FOOT) {
                message(m, 0x4b22d8);   /* speed */
                ped_set_move_speed(ped_get(id), 1);
                player_set_speedup_timer(n, 0x177);
            } else if (kind != PLAYER_ON_TRAIN) {
                game_fatal(-0x4a, 0x15b, kind);
            }
            break;
        }
        case 9: {
            message(n, 0x4b22c4);   /* bribe */
            int ped = player_get_ped(n);
            player_clear_wanted_level(ped);
            player_clear_wanted_points(ped);
            break;
        }
        case 10:
            message(n, 0x4b22bc);   /* armour */
            ped_set_flag4a(ped_get(player_get_ped((int16_t)n)), 1);
            player_set_armour(n, 3);
            break;
        case 11:
            message(n, 0x4b22b0);   /* multiplier */
            player_add_multiplier(n, 1);
            break;
        case 12:
            message(n, 0x4b22a4);   /* jail free */
            pl->jail_free = 1;      /* Player_SetFlagFA 0x461fe0 */
            break;
        case 13:
            message(n, 0x4b229c);   /* life */
            player_add_life(n);
            break;
        case 14: {
            static char key[16];    /* 0x502f78 */
            snprintf(key, sizeof key, exe_str(0x4b2294), value);   /* "help%d" */
            hud_show_subtitle(3, text_get(key));
            break;
        }
        case 15:
            if (n == g_player_local) {
                hud_show_zone_text(text_get(exe_str(0x4b229c)), 0);
                Snd_PlayVoice(0xb);
            }
            player_add_life(n);
            break;
        }
        int sample = p->type >= 1 && p->type <= 4 ? 2 : p->type % 2 == 0 ? 4 : 3;
        Snd_PlayAt(x, y, p->z, sample);
        *p = (PowerUp){ 0, 0, 0, -1, 0, 0, 0 };
        return;
    }
}

int powerups_in_use(void)
{
    int k = 0;
    for (int i = 0; i < POWERUP_MAX; i++) k += g_powerups[i].obj != -1;
    return k;
}
