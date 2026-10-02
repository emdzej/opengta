/* Players (0x74f148, 4 x 0x1bc): the record, its reset at level start (Player_InitAll 0x463290) and
   the accessors the level start and the frame loop use. Player iteration (Player_First 0x412a70 /
   Player_Next 0x412a90) walks only player 0 outside network games.

   The camera blocks at +4 / +0x18 / +0x48 are the structs of src/render/camera.h (ViewRect, Viewport,
   Camera); player_camera() gathers them, the mode byte and the target into a CameraPlayer for the
   camera functions and player_camera_store() puts them back. */
#pragma once
#include "../render/camera.h"
#include "input.h"
#include "replay.h"
#include "layout.h"
#include <stdbool.h>
#include <stdint.h>

enum { PLAYER_MAX = 4 };

/* controlled / camera target kinds (+0xbc, +0xd0) */
enum { PLAYER_IN_CAR = 0, PLAYER_ON_TRAIN = 1, PLAYER_ON_FOOT = 2 };

typedef struct Player {
    uint8_t bust;               /* +0x000 0 none, 1 just busted, 2 processing */
    uint8_t flag1;              /* +0x001 */
    int16_t ped;                /* +0x002 */
    ViewRect rect;              /* +0x004 view rectangle (Player_GetViewRect 0x462c30) */
    Viewport vp;                /* +0x018 (Player_GetViewport 0x4644e0) */
    Camera cam;                 /* +0x048 (Player_GetCamera 0x464580) */
    int32_t dbg[4];             /* +0x0ac debug free-camera velocities x, y, height, zoom (Player_GetBlockAC 0x4645a0) */
    int32_t ctl_kind;           /* +0x0bc 0 car, 1 train, 2 on foot */
    int32_t ctl_id;             /* +0x0c0 car / train / ped */
    uint8_t pad0c4[0xc];
    int32_t view_kind;          /* +0x0d0 camera target kind (camera.h CAM_TARGET_*) */
    int32_t view_id;            /* +0x0d4 */
    int32_t view_x, view_y, view_z;   /* +0x0d8 fixed camera point (kinds 3 / 4) */
    char name[16];              /* +0x0e4 */
    int32_t score;              /* +0x0f4 */
    uint16_t mult;              /* +0x0f8 */
    uint8_t jail_free;          /* +0x0fa */
    uint8_t pad0fb;
    int32_t stats[10];          /* +0x0fc kill / crime counters */
    char hud_mult[3];           /* +0x124 "%02d" */
    char hud_score[10];         /* +0x127 "%09d" */
    char hud_score2[10];        /* +0x131 "%09d" */
    uint8_t pad13b;
    int32_t u13c[4];            /* +0x13c 0x90009 with the score cheat */
    int16_t u14c;               /* +0x14c 9 with the score cheat */
    uint8_t pad14e[2];
    int16_t wanted_points;      /* +0x150 */
    uint8_t pad152[2];
    int32_t wanted_level;       /* +0x154 */
    uint8_t ammo[4];            /* +0x158 weapons 1..4; 'd' (100) = temporary weapon */
    uint8_t sub_ammo[2];        /* +0x15c 5-shot sub-counters of weapons 2 and 4 */
    uint8_t pad15e[2];
    int32_t weapon;             /* +0x160 */
    int32_t saved_weapon;       /* +0x164 */
    uint8_t saved_ammo[3];      /* +0x168 ammo of the saved weapon, sub_ammo[0..1] */
    uint8_t pad16b;
    int16_t bonus_timer;        /* +0x16c frames left of the bonus chain (Player_AwardBonus sets 13) */
    uint8_t pad16e[2];
    int32_t bonus_kind;         /* +0x170 kind of the chain */
    int16_t bonus_count;        /* +0x174 events in the chain */
    uint8_t pad176[2];
    int32_t u178;               /* +0x178 */
    int16_t u17c, u17e, u180;
    uint8_t pad182[2];
    int32_t u184;               /* +0x184 */
    uint8_t mode;               /* +0x188 camera mode 0..2 */
    uint8_t u189;
    uint8_t pad18a[2];
    int32_t u18c;
    uint8_t train_door;         /* +0x190 */
    int8_t ctl[6];              /* +0x191 held controls (Player_ApplyInput): [0] accelerate / brake value,
                                   [1] (unused on foot), [2] Space (handbrake / jump), [3] steering -7..7,
                                   [4] 1 forward / -1 brake, [5] Tab (horn / special) */
    int8_t lives;               /* +0x197 4, or -1 = infinite */
    int16_t foot_aux;           /* +0x198 */
    uint8_t pad19a[2];
    int32_t frags;              /* +0x19c */
    uint8_t frag_done;          /* +0x1a0 */
    uint8_t pad1a1[3];
    int32_t u1a4;               /* +0x1a4 */
    int16_t armour;             /* +0x1a8 */
    int16_t speedup;            /* +0x1aa */
    int16_t timers[3];          /* +0x1ac frenzy timer and two countdowns (-1 off) */
    uint8_t local1, local2;     /* +0x1b2, +0x1b3 */
    uint8_t colour;             /* +0x1b4 */
    uint8_t pad1b5[3];
    int32_t alarm;              /* +0x1b8 car alarm timer */
} Player;
GAME_OFS(Player, ped, 0x002); GAME_OFS(Player, rect, 0x004); GAME_OFS(Player, vp, 0x018);
GAME_OFS(Player, cam, 0x048); GAME_OFS(Player, dbg, 0x0ac); GAME_OFS(Player, ctl_kind, 0x0bc);
GAME_OFS(Player, ctl_id, 0x0c0); GAME_OFS(Player, view_kind, 0x0d0); GAME_OFS(Player, view_x, 0x0d8);
GAME_OFS(Player, name, 0x0e4); GAME_OFS(Player, score, 0x0f4); GAME_OFS(Player, mult, 0x0f8);
GAME_OFS(Player, jail_free, 0x0fa); GAME_OFS(Player, stats, 0x0fc); GAME_OFS(Player, hud_mult, 0x124);
GAME_OFS(Player, hud_score, 0x127); GAME_OFS(Player, hud_score2, 0x131); GAME_OFS(Player, u13c, 0x13c);
GAME_OFS(Player, u14c, 0x14c); GAME_OFS(Player, wanted_points, 0x150); GAME_OFS(Player, wanted_level, 0x154);
GAME_OFS(Player, ammo, 0x158); GAME_OFS(Player, sub_ammo, 0x15c); GAME_OFS(Player, weapon, 0x160);
GAME_OFS(Player, saved_weapon, 0x164); GAME_OFS(Player, bonus_timer, 0x16c); GAME_OFS(Player, bonus_kind, 0x170); GAME_OFS(Player, bonus_count, 0x174); GAME_OFS(Player, saved_ammo, 0x168); GAME_OFS(Player, u178, 0x178);
GAME_OFS(Player, u17c, 0x17c); GAME_OFS(Player, u180, 0x180); GAME_OFS(Player, u184, 0x184);
GAME_OFS(Player, mode, 0x188); GAME_OFS(Player, u18c, 0x18c); GAME_OFS(Player, train_door, 0x190);
GAME_OFS(Player, ctl, 0x191); GAME_OFS(Player, lives, 0x197); GAME_OFS(Player, foot_aux, 0x198);
GAME_OFS(Player, frags, 0x19c); GAME_OFS(Player, frag_done, 0x1a0); GAME_OFS(Player, u1a4, 0x1a4);
GAME_OFS(Player, armour, 0x1a8); GAME_OFS(Player, speedup, 0x1aa); GAME_OFS(Player, timers, 0x1ac);
GAME_OFS(Player, local1, 0x1b2); GAME_OFS(Player, colour, 0x1b4); GAME_OFS(Player, alarm, 0x1b8);
_Static_assert(sizeof(Player) == 0x1bc, "player record");

extern Player g_players[PLAYER_MAX];        /* 0x74f148 */
extern int g_player_local;                  /* 0x74f83c local (input / HUD) player */
extern int g_player_viewed;                 /* 0x74f844 player being processed */
extern int g_player_count;                  /* 0x74f84c 1 single player */
extern bool g_players_ready;                /* 0x74f848 */
extern uint32_t g_player_respawn_block;     /* 0x74f850 first police station: x << 8 | y | z << 16 */

static inline Player *player_get(int n) { return &g_players[n]; }
int player_first(void);                     /* Player_First 0x412a70 */
int player_next(int n);                     /* Player_Next 0x412a90 */
static inline void player_set_viewed(int n) { g_player_viewed = n; }      /* 0x461740 */
static inline int player_get_viewed(void) { return g_player_viewed; }     /* 0x461750 */
static inline bool player_is_viewed_local(void) { return g_player_viewed == g_player_local; }   /* 0x461760 */
static inline void player_clear_init_flag(void) { g_players_ready = false; }   /* 0x463280 */
static inline void player_set_controlled(int n, int kind, int id)        /* Player_SetControlled 0x462c50 */
{
    g_players[n].ctl_kind = kind, g_players[n].ctl_id = id;
}
void player_set_ped(int n, int ped);        /* Player_SetPed 0x461e90 */
int player_get_remap(int n);                /* Player_GetRemap 0x461720 */
void player_prev_weapon(int n);             /* Player_PrevWeapon 0x461a50 */
void player_init_all(void);                 /* Player_InitAll 0x463290 */
void camera_debug_move(int n);              /* Camera_DebugMove 0x43cb20 */
void camera_debug_stop(int n);              /* Camera_DebugStop 0x43cbb0 */
static inline uint32_t input_get_high_bits(uint32_t w) { return w & 0x40 ? w >> 23 : 0; }   /* 0x4643a0 */

/* Player_GetViewTargetPos 0x462d50: the target record of the player's camera target kind. */
CameraTarget player_view_target(int n);
/* CameraPlayer view of player n (target filled by player_view_target), and back. */
void player_camera(int n, CameraPlayer *cp);
void player_camera_store(int n, const CameraPlayer *cp);
/* The CameraWorld for player n (feature switches, viewed car / ped states). */
CameraWorld player_camera_world(int n);

/* ---- the player module's accessors and actions (0x4616b0-0x464c90) ---- */
void player_apply_input(uint32_t control);   /* Player_ApplyInput 0x463ec0 (the viewed player) */
void player_toggle_vehicle(void);            /* Player_ToggleVehicle 0x4642b0 */
void player_update_all(void);                /* Player_UpdateAll 0x464880 */
int player_find_by_ped(int ped);             /* Player_FindByPed 0x464440 (-1 none) */
void player_next_weapon(int n);              /* Player_NextWeapon 0x4619f0 */
void player_select_weapon(int n, int w);     /* Player_SelectWeapon 0x461b00 */
void player_add_ammo(int n, int w, int count);   /* Player_AddAmmo 0x461b50 */
void player_use_ammo(int n);                 /* Player_UseAmmo 0x461d40 */
void player_start_frenzy_weapon(int n, int w, int frames);   /* Player_StartFrenzyWeapon 0x461bd0 */
void player_end_frenzy_weapon(int n);        /* Player_EndFrenzyWeapon 0x461c70 */
bool player_has_frenzy_weapon(int n);        /* Player_HasFrenzyWeapon 0x461d10 */
bool player_has_temp_weapon(int n);          /* Player_HasTempWeapon 0x4647e0 */
void player_clear_weapons(int n);            /* Player_ClearWeapons 0x464ba0 */
void player_remove_power_ups(int n);         /* Player_RemovePowerUps 0x4637b0 */
void player_halve_multiplier(int n);         /* Player_HalveMultiplier 0x464be0 */
void player_add_multiplier(int n, int d);    /* Player_AddMultiplier 0x463070 */
void player_add_life(int n);                 /* Player_AddLife 0x4630b0 (to 99; -1 infinite stays) */
bool player_wasted(int n);                   /* Player_Wasted 0x463100 (true: a life left) */
void player_busted(int n);                   /* Player_Busted 0x464820 */
void player_add_score(int n, int points, int x, int y, int z, int popup);   /* Player_AddScore 0x461f20 */
void player_sub_score(int n, int points);    /* Player_SubScore 0x461f90 */
int player_get_score(int n);                 /* Player_GetScore 0x462b70 (= Player_GetScore_004643b0 0x4643b0) */
const char *player_get_name(int n);          /* Player_GetName 0x462bd0 */
const int32_t *player_get_view_rect(int n);  /* Player_GetViewRect 0x462c30: left, right, top, bottom (pixels) */
/* Player_GetControlledPos 0x462ef0: the target record (x, y, z 16.16, ...) of what the player controls */
const int32_t *player_get_controlled_pos(int n);
static inline int player_get_controlled_kind(int n) { return g_players[n].ctl_kind; }        /* 0x462fc0 */
static inline int player_get_controlled_id(int n) { return (int16_t)g_players[n].ctl_id; }   /* 0x462fe0 */
static inline int player_get_view_kind(int n) { return g_players[n].view_kind; }             /* 0x462f80 */
static inline int player_get_ped(int n) { return g_players[n].ped; }                         /* 0x461ec0 */
static inline bool player_is_local(int n) { return (int8_t)n == g_player_local; }            /* 0x461780 */
static inline int player_get_weapon(int n) { return g_players[n].weapon; }                   /* 0x461ac0 */
static inline int player_get_wanted_level(int n) { return g_players[n].wanted_level; }       /* 0x4619b0 */
static inline int player_get_wanted_points_idx(int n) { return g_players[n].wanted_points; } /* 0x4619d0 */
void player_set_view_target(int n, int kind, int id);   /* Player_SetViewTarget 0x462c80 */
void player_set_view_fixed(int32_t x, int32_t y, int32_t z, int n);   /* Player_SetViewFixed 0x462cb0 (kind 3) */
void player_control_view_target(int n);      /* Player_ControlViewTarget 0x463000 */
void player_reset_view(int n);               /* Player_ResetView 0x463040 */
void player_retarget_camera(int kind, int id, int new_kind, int new_id);   /* Player_RetargetCamera 0x463b00 */
void player_enter_car(int ped, int car);     /* Player_EnterCar 0x463a10 */
void player_exit_car(int ped, int car);      /* Player_ExitCar 0x463c40 */
void player_exit_train(int n);               /* Player_ExitTrain 0x463be0 */
void player_board_train(int ped);            /* Player_BoardTrain 0x463d50 */
void player_set_on_foot(int ped);            /* Player_SetOnFoot 0x463dd0 */
void player_on_leave_vehicle(int n);         /* Player_OnLeaveVehicle 0x463e50 */
void player_set_foot_aux198(int n, int dir); /* Player_SetFootAux198 0x463990 (dir -1 left, 0, 1 right) */
/* the held controls of the player whose ped is `ped` (the *ByPed accessors 0x4645c0..0x464750; no such
   player reads the record before the table, as the original does: callers only ask for players) */
int player_ctl_by_ped(int ped, int i);
int16_t player_get_ped_field198(int ped);    /* Player_GetPedField198 0x461e40 */
void player_set_ped_field198(int ped, int v);   /* Player_SetPedField198 0x461df0 */
void player_add_wanted_points(int ped, int pts);   /* Player_AddWantedPoints 0x4617e0 (to 2000) */
int player_get_wanted_points(int ped);       /* Player_GetWantedPoints 0x461840 (-1 none) */
void player_clear_wanted_points(int ped);    /* Player_ClearWantedPoints 0x4617a0 */
void player_set_wanted_level(int ped, int lvl);    /* Player_SetWantedLevel 0x461910 */
void player_clear_wanted_level(int ped);     /* Player_ClearWantedLevel 0x461890 */
int player_get_wanted_level_by_ped(int ped); /* Player_GetWantedLevelByPed 0x461960 (5 none) */
bool player_any_wanted(void);                /* Player_AnyWanted 0x4618d0 (level > 2) */
void player_dec_armour(int n);               /* Player_DecArmour 0x4636a0 */
bool player_any_field1a4_is(int v);          /* Player_AnyField1A4Is 0x4629d0 */
static inline void player_set_field1a4(int n, int v) { g_players[n].u1a4 = v; }            /* 0x4629b0 */
static inline void player_set_flag001(int n) { g_players[n].flag1 = 1; }                   /* 0x463830 */
static inline void player_set_timer18c(int n, int v) { g_players[n].u18c = v; }            /* 0x4647a0 */
static inline void player_set_flag189(int n, int v) { g_players[n].u189 = (uint8_t)v; }    /* 0x4647c0 */
static inline void player_set_timer1ae(int n, int v) { g_players[(int16_t)n].timers[1] = (int16_t)v; }   /* 0x463730 */
static inline void player_set_timer1b0(int n, int v) { g_players[(int16_t)n].timers[2] = (int16_t)v; }   /* 0x463770 */
static inline void player_set_armour(int n, int v) { g_players[(int16_t)n].armour = (int16_t)v; }       /* 0x4636d0 */
static inline void player_set_speedup_timer(int n, int v) { g_players[(int16_t)n].speedup = (int16_t)v; }   /* 0x463710 */
