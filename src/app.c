/* The application state machine (one app_frame per gasm frame, no blocking loops):
   - APP_FRONT: the frontend, one iteration of WinMain's menu loop (front_frame, 0x437230) per frame;
   - APP_LEVEL: the level, for now the development city viewer (viewer_*: the ported in-game frame,
     Game_Frame 0x430b20 + Game_Render 0x430d40, minus what isn't ported yet) around a movable camera
     target. Esc returns to the frontend as an abandoned game. Game_Run 0x4148a0 replaces it.

   Launch params: front=0 (straight into the viewer), map=nyc|sanb|miami, x=, y=, z= (the viewer's target
   cell, exe convention: z = 0 is the top layer; default NYC mission 1's player start (105,119,4)).
   Viewer keys: arrows / d-pad move the target (Shift: faster), Page Up / Page Down (pad L / R) change z. */
#include "app.h"
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

enum { APP_FRONT, APP_LEVEL };

static int state;
static Front front;
static Surface front_screen;

static bool start_level(int city)
{
    if (!(viewer = calloc(1, sizeof *viewer))) { plat_log("out of memory"); return false; }
    if (!viewer_init(viewer, city)) return false;
    state = APP_LEVEL;
    return true;
}

static void end_level(void)
{
    viewer_free(viewer);
    free(viewer);
    viewer = NULL;
}

bool app_init(void)
{
    char err[256];
    if (!exe_init(err, sizeof err)) { plat_log(err); return false; }
    math_init_tables();
    if (!camera_init_tables()) { plat_log("camera tables: exe data missing"); return false; }
    char b[16] = "1";
    if (plat_param("front", b, sizeof b) && !strcmp(b, "0")) {
        char name[16] = "nyc";
        plat_param("map", name, sizeof name);
        int city = 0;
        for (int i = 0; i < 3; i++)
            if (!strcmp(name, MAPS[i][0])) city = i;
        return start_level(city);
    }
    text_init_language(TEXT_ENGLISH);
    front_screen = (Surface){ calloc(SCREEN_W * SCREEN_H, 4), SCREEN_W, SCREEN_H, SCREEN_W };
    front.net_active = true;   /* Net_IsActive: the network entries show (DirectPlay is stubbed) */
    if (!front_screen.px || !front_init(&front)) { plat_log(front.error[0] ? front.error : "frontend failed"); return false; }
    state = APP_FRONT;
    return true;
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
        snprintf(m, sizeof m, "OpenGTA: start mission.ini [%d], level %d, player %d", r.section, r.level, r.player);
        plat_log(m);
        return start_level(r.level / 2 % 3);   /* two levels per city */
    }
    return true;
}

bool app_frame(void)
{
    if (state == APP_FRONT) return front_step_frame();
    uint16_t keys[16];
    int n = plat_key_presses(keys, 16);
    bool esc = false;
    for (int i = 0; i < n; i++) esc |= keys[i] == 0x01;
    if (esc && front_screen.px) {
        /* back to the frontend as an abandoned game (Game_SetExit 0x4309e0 reason 7) */
        end_level();
        FrontGameResult res = { .reason = 7, .local = 0, .score = { 0, -1, -1, -1 } };
        if (!front_game_over(&front, &res)) { plat_log(front.error); return false; }
        state = APP_FRONT;
        return true;
    }
    viewer_frame(viewer);
    return true;
}

void app_exit(void)
{
    if (viewer) end_level();
    if (front_screen.px) front_shutdown(&front);
    free(front_screen.px);
    front_screen.px = NULL;
    exe_free();
}

void app_audio(float *out, unsigned frames) { memset(out, 0, frames * 2 * sizeof *out); }
