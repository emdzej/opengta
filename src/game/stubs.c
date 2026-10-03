/* Stubs for the subsystems not ported yet (see stubs.h). Each one says what the original does. */
#include "stubs.h"
#include "../render/sprite.h"
#include "coll.h"
#include "game.h"
#include "obj.h"
#include "ped.h"
#include "mission_obj.h"
#include "route.h"
#include <string.h>

int stub_calls[STUB_COUNT];

void stubs_reset(void)
{
    memset(stub_calls, 0, sizeof stub_calls);
}

/* ---- Game_Run ---- */
void tune_load_file(const char *name) { (void)name; }        /* optional ..\gtadata\config.ini */
bool net_reset_sync(void) { return true; }                    /* single player: always starts */
void net_unk_44b900(void) {}
/* net_end_game (Net_EndGame 0x412d00): stubbed by the frontend, src/front/front_net.c */
void net_sync_frame_inputs(uint32_t *controls) { (void)controls; }   /* single player: nothing to exchange */
/* 640x480, the format of PIXFMT_32. Gfx_SelectMode ends in HUD_LoadFonts with res 2 for modes taller
   than 400 lines (Gfx_SetVideoMode 0x414db0 calls HUD_SetViewSize first). */
void gfx_select_mode(void)
{
    int w = g_game.screen_w ? g_game.screen_w : 640, h = g_game.screen_h ? g_game.screen_h : 480;
    hud_set_view_size(w, h);
    hud_load_fonts(h > 400 ? 2 : 1);
}
/* one mode, 640x480x32 (the original lists what DirectDraw offers, names "w x h x bits" via ostream) */
static const GfxModeEntry gfx_modes[1] = { { "640x480x32", 0, { 0 }, NULL } };
static const uint8_t gfx_per_column[3] = { 1, 0, 0 };
void gfx_get_mode_lists(const void **modes, int *count, int *columns, const uint8_t **per_column)
{
    *modes = gfx_modes, *count = 1, *columns = 3, *per_column = gfx_per_column;
}
int gfx_get_mode_index(void) { return 0; }
void gfx_select_mode_index(int i) { (void)i; gfx_select_mode(); }

/* ---- Game_Init / Game_Shutdown ---- */

/* ---- the frame ---- */

/* ---- Game_HandleKey ---- */
void net_build_chat_prefix(int to) { (void)to; }

/* ---- events ---- */

/* ---- Mission_Load ---- */




/* ---- objects ---- */
/* Car_DampThrust 0x40c0c0: thrust below a fraction of the car info's is scaled (constants 0x4a7358 /
   0x4a7350): not ported */
void car_damp_thrust(Car *c) { (void)c; }
/* FireEngine_Dispatch 0x42ec70: the nearest of the 4 fire stations sends a fire engine (FireEngine_Spawn
   0x42e920, car model 0x2a, sentinel type 6) to the fire {object, x, y, z}: AI, not ported (no engine) */
int fire_engine_dispatch(const int16_t info[4], int slot) { (void)info, (void)slot; return -1; }

/* ---- player module (the accessors as in the original; the rest does nothing yet) ---- */
#include "player.h"
/* Player_AddScore 0x461f20: score += points * multiplier (with the HUD and frenzy cases): not ported. */

/* ---- other subsystems the mission interpreter calls ---- */

/* ---- what the mission helpers (mission_obj.c, dummy.c) call ---- */
void front_get_multi_target(uint8_t *kind, uint8_t *value) { *kind = 0, *value = 0; }
int32_t g_police_no_patrols;

/* ---- for the MissionOp_* handlers (mission_ops.c) ---- */
void camera_start_transition(int n) { (void)n; }
int ambulance_busy_sentinel(void) { return g_path_owner; }   /* 0x4b3094 is the path search owner (path.c) */

/* ---- what the player / ped modules call ---- */

/* ---- what the mission runtime objects (trigger.c) call ---- */
int player_get_view_id(int n) { return (int16_t)g_players[n].view_id; }   /* 0x462fa0 */
int player_get_multiplier(int n) { return (int16_t)g_players[(int16_t)n].mult; }   /* 0x463090 */
/* Player_TrainCrashKick 0x463b70 (player module): every player riding train t (kind 1) dies (its ped's +0x49 = 0) */
int player_train_crash_kick(int t) { int r = 0; for (int n = player_first(); n > -1; n = player_next(n)) if (g_players[n].ctl_kind == 1 && g_players[n].ctl_id == (t & 0xff)) g_peds[g_players[n].ped].health = 0, r = 1; return r; }
void player_award_bonus(int n, int kind, int32_t x, int32_t y, int32_t z, int a, int cause)
{
    (void)n, (void)kind, (void)x, (void)y, (void)z, (void)a, (void)cause;
}
/* Map_GetLidBelow 0x4387b0: the lid tile of block (x, y, (z >> 22) + 1) (16.16), 0 above the column */
int map_get_lid_below(int32_t x, int32_t y, int32_t z)
{
    const Map *m = g_game.map;
    const int16_t *col = map_column(m, x >> 22 & 0xff, y >> 22 & 0xff);
    int l = (z >> 22) + 1;
    if (col[0] > l) return 0;
    return m->blocks[col[l - col[0] + 1]].lid;
}
/* Map_TestBlockAttr 0x44b310: a block's attributes (the last block's type map is cached, 0x6b3ea8):
   1 railway, 2 crossing (lights bits = 1), 3 road / 6 / 7 types, 4 type map without the type bits,
   5 the type map's high bits, 6 the lights bits, 7 lights 4 / 5 (the value), 8 lights 2, 9 pavement. Outside the
   map 0. */
int map_test_block_attr(int what, int bx, int by, int bz)
{
    static int16_t cx = -1, cy = -1, cz = -1;   /* 0x4b1de4.. */
    static uint32_t t;                          /* 0x6b3ea8 */
    bx = (int16_t)bx, by = (int16_t)by, bz = (int16_t)bz;
    if (bx < 0 || bx > 0xff || by < 0 || by > 0xff || bz < 0 || bz > 5) return 0;
    if (bx != cx || by != cy || bz != cz) {
        t = map_get_type_map(g_game.map, bx, by, bz);
        cx = (int16_t)bx, cy = (int16_t)by, cz = (int16_t)bz;
    }
    int k;
    switch ((int16_t)what) {
    case 1: return (t & 0x800000) != 0;
    case 2: return (t >> 16 & 7) == 1;
    case 3: k = t >> 4 & 7; return k == 2 || k == 6 || k == 7;
    case 4: return (int)(t & 0xffffff0f);
    case 5: return (int)(t >> 8 & 0xffff3f);
    case 6: return (int)(t >> 16 & 7);
    case 7: k = t >> 16 & 7; return k == 5 || k == 4 ? k : 0;
    case 8: return (t >> 16 & 7) == 2;
    case 9: return (t >> 4 & 7) == 3;
    }
    return 0;
}

/* ---- what the car module (car.c, carcoll.c) calls ---- */
void car_fire_rocket(Car *c) { (void)c; }
/* ---- what the projectiles (proj.c) call ---- */

/* ---- what the police / wanted level call (real ports, here until their modules have them) ---- */
#include "../exe.h"
#include "../hud/hud.h"
/* Area_GetSample 0x44b7b0: the first nav zone with a sample containing block (x, y), except sample 1
   in style 1 and 11 in style 3 (as Area_GetName): its sample and the compass part of the zone the
   block is in (Area_SubDirection through the table 0x4b1dec); 0, 0 outside every zone. */
void area_get_sample(uint8_t x, uint8_t y, uint8_t *area, uint8_t *dir)
{
    int style = style_requested();
    const uint8_t *map = exe_data(0x4b1dec, 16);
    for (int i = 0; i < g_nav_count; i++) {
        const NavZone *z = &g_nav[i];
        if (z->sample == 0 || x < z->x || y < z->y || x >= z->x + z->w || y >= z->y + z->h) continue;
        if ((style == 1 && z->sample == 1) || (style == 3 && z->sample == 0xb)) continue;
        int d = area_sub_direction((uint8_t)(x - z->x), (uint8_t)(y - z->y), z->w, z->h);
        *dir = map ? map[d & 0xf] : 0;
        *area = z->sample;
        return;
    }
    *dir = 0;
    *area = 0;
}
/* Player_IncKills 0x462960: the counters at +0xfc are shorts: [kind] this life, [10 + kind] total (the
   original's "below 0x8000" test on a short is always true: they wrap) */
void player_inc_kills(int n, int kind)
{
    int16_t *k = (int16_t *)(void *)g_players[n].stats;
    k[kind] = (int16_t)(k[kind] + 1);
    k[10 + kind] = (int16_t)(k[10 + kind] + 1);
}
/* Player_SetViewFixed4 0x462d00: the camera of player n on the fixed point (x, y, z), kind 4 */
void player_set_view_fixed4(int32_t x, int32_t y, int32_t z, int n)
{
    Player *p = &g_players[n];
    p->view_x = x, p->view_kind = 4, p->view_id = 0, p->view_y = y, p->view_z = z;
}
/* the fire engines (fire module) as Sentinel_DriveCar calls them */
int fire_engine_update(Sentinel *s) { (void)s; return 0; }   /* FireEngine_Update 0x42f460 (0: don't drive on) */
void fire_engine_remove(Sentinel *s) { (void)s; }            /* FireEngine_Remove 0x42e870 */
/* Ref_GetKind1PosRect 0x45fb60 (ped module): x, y, z of the train a ped rides (Train_Command 7 fills
   the board record); the original copies the whole record with speed / 10 into 0x74f10c */
#include "train.h"
const int32_t *ref_get_kind1_pos_rect(int train)
{
    static int32_t rec[3];
    train_command(7, train);
    const TrainBoardInfo *r = train_get_board_info();
    rec[0] = r->x, rec[1] = r->y, rec[2] = r->z;
    return rec;
}
