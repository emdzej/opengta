/* The application state machine (one app_frame per platform frame, no blocking loops):
   - APP_FRONT: the frontend, one iteration of WinMain's menu loop (front_frame, 0x437230) per frame, at
     1000/35 Hz;
   - APP_GAME: Game_Run 0x4148a0, one game_run_step per frame at 70 Hz (one tick of the sound timer: the
     game runs a frame every 3rd call, 23.33 a second, as the original); when it ends, the frontend's
     results / cutscene screens (front_game_over), or the same mission again (F12, quit code 3);
   - APP_VIEWER: the development city viewer (viewer_*), a movable camera target without the game.

   Launch params: intro=0 (no intro movie), front=0 (straight into the viewer: map=nyc|sanb|miami, x=, y=, z= the target cell, exe
   convention z = 0 top; default NYC mission 1's player start (105,119,4)), mission=<MISSION.INI section>
   (straight into that mission). Viewer keys: arrows / d-pad move the target (Shift: faster), Page Up /
   Page Down (pad L / R) change z, Esc goes to the frontend. */
#include "app.h"
#include "audio/audio.h"
#include "game/game.h"
#include "game/input.h"
#include "game/mission.h"
#include "hud/hud.h"
#include "movie/intro.h"
#include "exe.h"
#include "front/front.h"
#include "text.h"
#include "game/gmath.h"
#include "map.h"
#include "platform.h"
#include "render/camera.h"
#include "render/city.h"
#include "render/poly.h"
#include "style.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SCREEN_W = 640, SCREEN_H = 480 };   /* the game's in-game mode (0x504cbc: 640x480) */

/* ---- the city viewer state ---- */

typedef struct {
    Map *map;
    Style *style;
    CameraPlayer player;
    CameraWorld world;
    int32_t x, y, z;                       /* target, 16.16 world */
    uint32_t fb[SCREEN_W * SCREEN_H];      /* the back buffer, 32 bpp X8R8G8B8 like the original's */
    uint32_t rgba[SCREEN_W * SCREEN_H];    /* presented copy */
} Viewer;

static Viewer *viewer;

static int param_int(const char *name, int def)
{
    char b[32];
    return plat_param(name, b, sizeof b) ? atoi(b) : def;
}

static void viewer_target(Viewer *v)
{
    v->player.target_kind = CAM_TARGET_PED;
    v->player.target = (CameraTarget){ v->x, v->y, v->z, 8, 8, 0, 0 };
}

static const char *const MAPS[][2] = {
    { "nyc", "GTADATA/NYC.CMP" }, { "sanb", "GTADATA/SANB.CMP" }, { "miami", "GTADATA/MIAMI.CMP" } };

/* city: 0 Liberty City, 1 San Andreas, 2 Vice City */
static bool viewer_init(Viewer *v, int city)
{
    char err[256], msg[320];
    const char *rel = MAPS[city][1];
    if (!(v->map = map_load(rel, err, sizeof err))) { plat_log(err); return false; }
    if (!(v->style = style_load(v->map->style, err, sizeof err))) { plat_log(err); return false; }
    style_convert_palettes(v->style, &PIXFMT_32);
    snprintf(msg, sizeof msg, "OpenGTA: %s (%u blocks), style %d (%d/%d/%d tiles)", rel, v->map->nblocks,
             v->style->number, v->style->nside, v->style->nlid, v->style->naux);
    plat_log(msg);
    poly_set_screen_rows(v->fb, SCREEN_W * 4, SCREEN_H);
    poly_set_clip(0, 0, SCREEN_W - 1, SCREEN_H - 1);
    v->x = (param_int("x", city == 0 ? 105 : 128) * 64 + 32) << 16;
    v->y = (param_int("y", city == 0 ? 119 : 128) * 64 + 32) << 16;
    v->z = param_int("z", 4) * 0x400000 - 0x10000;   /* standing on the lid of block z */
    v->world = (CameraWorld){ .peds = true, .cars = true };
    camera_set_viewport(&v->player, SCREEN_W, SCREEN_H);
    viewer_target(v);
    camera_init(&v->player, &v->world);
    return true;
}

static void viewer_input(Viewer *v)
{
    uint8_t keys[KEY_COUNT];
    int dx = 0, dy = 0, dz = 0, fast = 0;
    if (plat_keys(keys)) {
        fast = keys[0x2a] || keys[0x36];
        dy -= keys[0x148], dy += keys[0x150], dx -= keys[0x14b], dx += keys[0x14d];
    }
    uint32_t pad = plat_pad(0);
    dy -= !!(pad & PAD_UP), dy += !!(pad & PAD_DOWN), dx -= !!(pad & PAD_LEFT), dx += !!(pad & PAD_RIGHT);
    uint16_t presses[16];
    int n = plat_key_presses(presses, 16);
    for (int i = 0; i < n; i++) dz += (presses[i] == 0x151) - (presses[i] == 0x149);   /* Page Down / Up */
    static uint32_t prev_pad;
    dz += !!(pad & PAD_R & ~prev_pad) - !!(pad & PAD_L & ~prev_pad);
    prev_pad = pad;
    int32_t step = (fast ? 32 : 8) << 16;   /* world pixels per frame */
    v->x += dx * step, v->y += dy * step;
    int zc = ((v->z + 0x10000) >> 22) + dz;
    if (zc < 0) zc = 0;
    if (zc > 6) zc = 6;
    v->z = zc * 0x400000 - 0x10000;
    const int32_t lim = 256 << 22;
    if (v->x < 0) v->x = 0;
    if (v->x >= lim) v->x = lim - 1;
    if (v->y < 0) v->y = 0;
    if (v->y >= lim) v->y = lim - 1;
}

/* One Game_Frame + Game_Render of the ported parts. */
static void viewer_frame(Viewer *v)
{
    viewer_input(v);
    viewer_target(v);
    style_update_anims(v->style);              /* Game_Update 0x430c00 */
    camera_update(&v->player, &v->world);      /* Camera_Update(-1000) */
    render_compute_visible_rect(&v->player.vp);
    render_copy_camera(&v->player.vp);
    render_draw_city(v->map, v->style, &v->player.vp);
    /* present (Gfx_Present 0x414b10): X8R8G8B8 -> the platform's RGBA bytes */
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        uint32_t c = v->fb[i];
        v->rgba[i] = 0xff000000u | (c & 0xff) << 16 | (c & 0xff00) | (c >> 16 & 0xff);
    }
    plat_present(v->rgba, SCREEN_W, SCREEN_H);
}

static void viewer_free(Viewer *v)
{
    map_free(v->map);
    style_free(v->style);
    v->map = NULL;
    v->style = NULL;
}

/* ---- app ---- */

enum { APP_FRONT, APP_GAME, APP_VIEWER };

static int state;
static Front front;
static Surface front_screen;
static uint32_t game_fb[SCREEN_W * SCREEN_H], game_rgba[SCREEN_W * SCREEN_H];
static int game_section;

/* Gfx_Present 0x414b10: the back buffer (X8R8G8B8) to the platform's RGBA bytes. */
static void game_present_cb(void *ctx)
{
    (void)ctx;
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        uint32_t c = game_fb[i];
        game_rgba[i] = 0xff000000u | (c & 0xff) << 16 | (c & 0xff00) | (c >> 16 & 0xff);
    }
    plat_present(game_rgba, SCREEN_W, SCREEN_H);
}

static void game_fatal_cb(const char *msg) { plat_log(msg); }

static bool start_game(int section)
{
    poly_set_screen_rows(game_fb, SCREEN_W * 4, SCREEN_H);
    poly_set_clip(0, 0, SCREEN_W - 1, SCREEN_H - 1);
    g_game.present = game_present_cb;
    g_game.on_fatal = game_fatal_cb;
    game_set_screen(SCREEN_W, SCREEN_H);
    map_clear_name();
    if (!mission_set_ini_section(section)) { plat_log("OpenGTA: no such mission.ini section"); return false; }
    game_section = section;
    if (!game_run_begin()) plat_log("OpenGTA: the game didn't start");
    state = APP_GAME;
    plat_set_frame_rate(APP_GAME_HZ);
    return true;
}

/* WinMain after Game_Run: the frontend again (or the same mission after F12). */
static bool end_game(void)
{
    int code = game_run_end();
    if (code == GAME_QUIT_RELOAD) return start_game(game_section);
    if (!front_screen.px) return false;   /* started with mission=: quit */
    FrontGameResult res = { .run_code = code, .reason = g_game.result, .local = 0, .score = { 0, -1, -1, -1 } };
    if (!front_game_over(&front, &res)) { plat_log(front.error); return false; }
    state = APP_FRONT;
    plat_set_frame_rate(APP_FRAME_HZ);
    return true;
}

static bool start_viewer(int city)
{
    if (!(viewer = calloc(1, sizeof *viewer))) { plat_log("out of memory"); return false; }
    if (!viewer_init(viewer, city)) return false;
    state = APP_VIEWER;
    return true;
}

static void end_viewer(void)
{
    viewer_free(viewer);
    free(viewer);
    viewer = NULL;
}

/* ---- the frontend's calls into sound and the game (FrontHooks) ---- */

static void hk_volumes(int sfx, int music)   /* Front_ApplyVolumes 0x4280b0 */
{
    Snd_SetSfxEnabled(sfx != 0);
    Snd_SetSfxVolume(sfx);
    Snd_SetMusicEnabled(music != 0);
    Snd_SetMusicVolume(music);
}
static void hk_enter(int sfx, int music)     /* Front_Enter 0x42b690 */
{
    Audio_EnterFrontend();
    Snd_SetSfxVolume(sfx);
    if (music >= 0) Snd_SetMusicVolume(music);
}
static void hk_mission(int section) { mission_set_ini_section(section); }
static void hk_level_options(int pager, int effects, int sequential)
{
    hud_set_pager_speed(pager);   /* HUD_SetPagerSpeed 0x482140 */
    (void)effects;                /* 0x5031e4: the effects option */
    if (sequential >= 0) Music_SetSequential(sequential != 0);
}

static bool init_front(void)
{
    char b[8];
    /* Movie_PlayIntro 0x44b160 (MOVIE.SMK) before the frontend, as WinMain does; intro=0 skips it */
    movie_intro_set_enabled(!(plat_param("intro", b, sizeof b) && !strcmp(b, "0")));
    text_init_language(TEXT_ENGLISH);
    front_screen = (Surface){ calloc(SCREEN_W * SCREEN_H, 4), SCREEN_W, SCREEN_H, SCREEN_W };
    front.net_active = true;   /* Net_IsActive: the network entries show (DirectPlay is stubbed) */
    front.hooks = (FrontHooks){
        .sample = Snd_PlaySampleN, .sfx = Snd_PlaySfx, .voice_status = Snd_GetCutsceneVoiceStatus,
        .voice_play = Snd_PlayCutsceneVoice, .voice_stop = Snd_StopCutsceneVoice, .volumes = hk_volumes,
        .enter = hk_enter, .update = Audio_Update, .mission = hk_mission, .level_options = hk_level_options,
    };
    if (!front_screen.px || !front_init(&front)) { plat_log(front.error[0] ? front.error : "frontend failed"); return false; }
    state = APP_FRONT;
    plat_set_frame_rate(APP_FRAME_HZ);
    return true;
}

bool app_init(void)
{
    char err[256], b[16];
    if (!exe_init(err, sizeof err)) { plat_log(err); return false; }
    math_init_tables();
    if (!camera_init_tables()) { plat_log("camera tables: exe data missing"); return false; }
    GameOptions o;
    game_default_options(&o);   /* WinMain's Game_SetOptions 0x4146d0 (also the sound options) */
    game_set_options(&o);
    if (plat_param("front", b, sizeof b) && !strcmp(b, "0")) {
        char name[16] = "nyc";
        plat_param("map", name, sizeof name);
        int city = 0;
        for (int i = 0; i < 3; i++)
            if (!strcmp(name, MAPS[i][0])) city = i;
        return start_viewer(city);
    }
    if (plat_param("mission", b, sizeof b)) {
        text_init_language(TEXT_ENGLISH);
        return start_game(atoi(b));
    }
    return init_front();
}

static bool front_step_frame(void)
{
    uint16_t keys[64];
    uint8_t held[KEY_COUNT];
    FrontInput in = { keys, plat_key_presses(keys, 64), plat_keys(held) ? held : NULL };
    FrontStep r = front_frame(&front, &in, &front_screen);
    plat_present(front_screen.px, SCREEN_W, SCREEN_H);
    if (r.code == FRONT_QUIT) return false;
    if (r.code == FRONT_PLAY) {
        char m[96];
        snprintf(m, sizeof m, "OpenGTA: mission.ini [%d], level %d, player %d", r.section, r.level, r.player);
        plat_log(m);
        return start_game(r.section);
    }
    return true;
}

bool app_frame(void)
{
    if (state == APP_FRONT) return front_step_frame();
    if (state == APP_GAME) {
        uint8_t held[KEY_COUNT];   /* the keyboard state Input_ReadControls 0x432e00 reads */
        if (plat_keys(held)) input_feed_held(held);
        return game_run_step() == GAME_STEP_DONE ? end_game() : true;
    }
    uint16_t keys[16];
    int n = plat_key_presses(keys, 16);
    for (int i = 0; i < n; i++)
        if (keys[i] == 0x01) {   /* Esc: the viewer back to the frontend */
            end_viewer();
            return front_screen.px || init_front();
        }
    viewer_frame(viewer);
    return true;
}

void app_exit(void)
{
    if (viewer) end_viewer();
    if (state == APP_GAME) game_run_end();
    if (front_screen.px) front_shutdown(&front);
    free(front_screen.px);
    front_screen.px = NULL;
    exe_free();
}

void app_audio(float *out, unsigned frames) { audio_render(out, frames); }
