/* Game core 0x430980-0x430dc0 and the in-game session Game_Run 0x4148a0 (as steps). */
#include "game.h"
#include "../exe.h"
#include "../render/city.h"
#include "../text.h"
#include "../render/sprite.h"
#include "car.h"
#include "coll.h"
#include "event.h"
#include "fileio.h"
#include "gmath.h"
#include "mission.h"
#include "obj.h"
#include "ped.h"
#include "player.h"
#include "route.h"
#include "stubs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

GameState g_game = { 0 };
int g_session_players = 1;
bool g_cheat_ammo_key;
int g_audio_mode = 1;

static int style_number;                     /* 0x7752d8 */
static int style_priority;                   /* 0x7750c4 */
static char map_file[64];                    /* 0x5c1c34 */
static int map_priority;                     /* 0x5c1c24 */
static bool map_named;                       /* 0x5bfbf0 */

/* ---- options ---- */

/* WinMain 0x437230: Game_SetOptions(1,0,1,1,0,1,1,1,1,1,0,0,1,0,1,0,0,0,1,0,0,0,1,1,0,...0,1,0,...0):
   peds, cars, sound, emergency services, trains / lights, sprites, police, blocks, objects on;
   debug keys, demo and the cheats off. */
void game_default_options(GameOptions *o)
{
    memset(o, 0, sizeof *o);
    o->peds = 1, o->cars = 1, o->sound = 1, o->emergency = 1, o->trains = 1, o->draw_sprites = 1;
    o->police = 1, o->opt502f64 = 1, o->draw_blocks = 1, o->opt50319c = 1, o->opt5031dc = 1;
    o->objects = 1, o->opt502f5c = 1, o->blend_sprites = 1;
}

/* Game_SetOptions 0x4146d0 */
void game_set_options(const GameOptions *o)
{
    g_game.opt = *o;
    render_draw_blocks = o->draw_blocks != 0;
    render_draw_sprites = o->draw_sprites != 0;
    sprite_blend_option = o->blend_sprites != 0;
}

/* ---- errors ---- */

void game_set_error_file(const char *name) { snprintf(g_game.error_file, sizeof g_game.error_file, "%s", name); }

_Noreturn void game_fatal(int code, int line, int arg)
{
    g_game.fatal_code = code, g_game.fatal_line = line, g_game.fatal_arg = arg;
    snprintf(g_game.fatal_msg, sizeof g_game.fatal_msg, "Error %d.%d (%d)%s%s", code, line, arg,
             g_game.error_file[0] ? " file " : "", g_game.error_file);
    if (g_game.on_fatal) g_game.on_fatal(g_game.fatal_msg);
    else fprintf(stderr, "%s\n", g_game.fatal_msg);
    exit(1);
}

/* ---- requests (0x430980-0x430a00) ---- */

void game_request_redraw(void)               /* Game_RequestRedraw 0x430980 */
{
    if (g_game.pause_frames != 0 || g_game.phase == 2) g_game.redraw = true;
}
void game_set_pause_frames(int n)            /* Game_SetPauseFrames 0x4309a0 */
{
    if (g_player_count == 1) g_game.pause_frames = n;
}
void game_request_end(int result)            /* Game_RequestEnd 0x4309e0 */
{
    g_game.quit = GAME_QUIT_END;
    g_game.result = result;
}
void game_request_abandon(void)              /* Game_RequestAbandon 0x430a00 */
{
    g_game.quit = GAME_QUIT_ABANDON;
    g_game.result = 7;
}

/* ---- style and map file requests ---- */

void style_reset_request(void) { style_priority = 0, style_number = 0; }    /* 0x47ced0 */
void style_request(int n, int priority)      /* Style_Request 0x47cee0 */
{
    if ((uint8_t)priority >= style_priority && n != 0) style_number = n, style_priority = (uint8_t)priority;
}
int style_requested(void) { return style_number; }
void map_clear_name(void) { map_priority = 0, map_named = false; }       /* 0x438190 */
void map_set_name(const char *name, int priority)   /* Map_SetName 0x4381c0 */
{
    if (name && (uint8_t)priority >= map_priority) {
        snprintf(map_file, sizeof map_file, "%s", name);
        map_priority = (uint8_t)priority;
        map_named = true;
    }
}
const char *map_name(void) { return map_named ? map_file : NULL; }

/* Text_Get 0x47d9a0 (fatal -0x55 when the key is missing, as in the original) */
static const char *text_of(const char *key)
{
    const char *t = text_get(key);
    if (!t) {
        game_set_error_file(text_error());
        game_fatal(-0x55, 0, 0);
    }
    return t;
}

/* ---- level files ---- */

bool game_map_load(void)
{
    if (!map_named) game_fatal(-0x5c, 0x66, 0);   /* map name not specified */
    char path[96], err[256];
    snprintf(path, sizeof path, exe_str(0x4abea8), map_file);   /* "..\gtadata\%s" */
    game_set_error_file(path);
    map_free(g_game.map);
    g_game.map = map_load(textfile_rel(path), err, sizeof err);
    if (!g_game.map) {
        snprintf(g_game.error_file, sizeof g_game.error_file, "%s", err);
        game_fatal(-2, 0x13, 0);
    }
    Map *m = g_game.map;
    obj_set_map_objects(m->objects, (int)m->object_size);
    route_load_cmp(m->routes, (int)m->route_size);
    area_set_nav_data(m->nav, (int)m->nav_size);
    for (int n = player_first(); n > -1; n = player_next(n)) {
        camera_debug_stop(n);
        g_players[n].mode = 0;   /* Player_SetMode188 0x4644a0 */
        if (n != g_player_local) {
            CameraPlayer cp;
            player_camera(n, &cp);
            camera_set_viewport(&cp, 0x140, 200);
            player_camera_store(n, &cp);
        }
    }
    style_request(m->style, 1);
    area_localize_names();
    return true;
}

/* Style_LoadRequested 0x47d390 and the parts of Style_Load 0x47cf10 that live here: Coll_Init (called
   by Sprite_LoadInfo) and Obj_LoadInfos. */
static void style_load_requested(void)
{
    char err[256];
    if (style_number == 0) game_fatal(-0x5d, 0x12, 0);
    style_free(g_game.style);
    g_game.style = style_load(style_number, err, sizeof err);
    if (!g_game.style) {
        snprintf(g_game.error_file, sizeof g_game.error_file, "%s", err);
        game_fatal(-2, 0x67, style_number);
    }
    coll_init();
    if (!obj_load_infos(g_game.style->object_info, (int)g_game.style->h.object_info_size,
                        sprite_group_base(SPRITE_GROUP_OBJECT)))
        game_fatal(-0x18, 0x28, g_obj_info_count * 4 - 0x400);
}

/* ---- the cameras of all players (Camera_InitAll / Camera_Update with -1000) ---- */

static void cameras_init_all(void)
{
    for (int n = player_first(); n > -1; n = player_next(n)) {
        CameraPlayer cp;
        CameraWorld w = player_camera_world(n);
        player_camera(n, &cp);
        camera_init(&cp, &w);
        player_camera_store(n, &cp);
    }
}

static void cameras_update_all(void)
{
    for (int n = player_first(); n > -1; n = player_next(n)) {
        CameraPlayer cp;
        CameraWorld w = player_camera_world(n);
        player_camera(n, &cp);
        camera_update(&cp, &w);
        player_camera_store(n, &cp);
    }
}

/* ---- Game_Init 0x430a20 ---- */

/* Everything a level starts with, in the original's order. The RNG is reseeded before the tables
   that use it, so every start of a level is the same. */
void game_init(void)
{
    const GameOptions *o = &g_game.opt;
    stubs_reset();
    player_clear_init_flag();
    audio_enter_game();
    g_audio_mode = 1;
    event_init();
    replay_begin();
    g_game.redraw = false;
    g_game.pause_frames = 0;
    hud_init();
    if (!math_random_reset()) game_fatal(-2, 0, 0x4b3824);
    if (o->peds) ped_init_all();
    if (o->cars) {
        cars_init();
        heli_init();
    }
    if (o->objects) obj_init_from_map();
    if (o->trains) lights_init();
    sentinel_init_all();
    path_reset();
    if (o->trains) train_init_all();
    proj_reset();
    fire_init();
    expl_init();
    blockanim_reset();
    hunt_init();
    mission_load();
    mission_free_ini();
    snd_reset(style_number);
    g_game.phase = 1;
    g_game.step_mode = 0;
    g_game.quit = GAME_RUNNING;
    g_game.step_count = 0;
    g_game.result = 0;
    player_init_all();
    cameras_init_all();
    cameras_update_all();
}

/* Game_Shutdown 0x430b10 */
void game_shutdown(void)
{
    replay_end_save();
    hud_free_fonts();
    snd_stop_all();
}

/* ---- the frame ---- */

/* Game_Frame 0x430b20: one logic tick unless paused. Phase 0 (the debug single step) runs one tick and
   freezes (phase 2); while frozen only a requested redraw updates the camera. While paused by
   pause_frames only the big message and the tile animation run. The renderer's camera copy is taken
   every frame. */
void game_frame(void)
{
    const GameOptions *o = &g_game.opt;
    if (o->debug_keys) {
        const int32_t *d = g_players[player_get_viewed()].dbg;
        if ((d[0] || d[1] || d[3] || d[2]) && (g_game.pause_frames != 0 || g_game.phase == 2)) g_game.redraw = true;
    }
    if (o->draw_sprites) sprite_clear_levels();
    const Viewport *vp = &g_players[g_player_local].vp;
    if (g_game.pause_frames == 0) {
        if (g_game.phase == 0) {
            game_update();
            cameras_update_all();
            render_compute_visible_rect(&g_players[g_player_local].vp);
            g_game.phase = 2;
            render_copy_camera(vp);
            return;
        }
        if (g_game.phase == 1) {
            game_update();
        } else {
            if (!g_game.redraw) {
                render_copy_camera(vp);
                return;
            }
            g_game.redraw = false;
        }
        cameras_update_all();
        render_compute_visible_rect(&g_players[g_player_local].vp);
        render_copy_camera(vp);
        return;
    }
    if (g_game.pause_frames > 0) {
        g_game.pause_frames--;
        hud_tick_big_message();
        style_update_anims(g_game.style);
    }
    render_copy_camera(vp);
}

/* Game_Update 0x430c00: the demo countdown (10000 frames, shown in seconds of 25 frames), then the
   subsystems in the original's order. Trains and lights report 20 when all is well. */
void game_update(void)
{
    const GameOptions *o = &g_game.opt;
    if (o->demo) {
        if (g_frame < 10001) {
            char b[16];
            snprintf(b, sizeof b, exe_str(0x4b07cc), (int)(10000 - g_frame) / 25);   /* "%d" */
            hud_clear_zone_text(0xb4);
            hud_show_zone_text(b, 0xb4);
        } else {
            g_game.quit = GAME_QUIT_END;
            g_game.result = 0xb;
        }
    }
    style_update_anims(g_game.style);
    event_tick();
    if (o->cars) {
        cars_update_all();
        heli_update();
    }
    if (o->peds) ped_update_all();
    if (o->trains) {
        int r = train_update_all();
        if (r != 20) game_fatal(-0xc1, 0x17d, r);
        r = lights_update();
        if (r != 20) game_fatal(-0xc0, 0x17d, r);
        junction_update_override_timers();
    }
    if (o->objects) obj_update_all();
    if (o->emergency) emergency_update_all();
    expl_update_all();
    blockanim_tick();
    mission_update();
    hud_update();
    replay_tick_frame();
    player_update_all();
}

/* The entity side of Render_QueueVisibleEntities 0x437000: the collision grid's cells. */
static void world_walk_cell(void *ctx, int cx, int cy, SpriteVisitFn visit, void *vctx)
{
    (void)ctx;
    if (cx < 0 || cy < 0 || cx >= COLL_GRID || cy >= COLL_GRID) return;
    for (CollNode *n = g_coll_grid[cy][cx]; n; n = n->next) visit(vctx, n->kind, n->owner);
}
static Sprite *world_embedded(void *ctx, int kind, void *owner)
{
    (void)ctx;
    switch (kind) {
    case COLL_PED: return &((Ped *)owner)->spr;
    case COLL_OBJECT: {
        Obj *o = owner;
        return g_obj_infos[o->type]->status == OBJ_STATUS_INVISIBLE ? NULL : &o->spr;
    }
    case COLL_CAR: return &((Car *)owner)->spr;
    default: return NULL;   /* kind 0x1e: the heli (not ported) */
    }
}

/* Game_Render 0x430d40: in the debug step mode only every 5th frame is drawn. */
void game_render(void)
{
    const GameOptions *o = &g_game.opt;
    SpriteWorld w = { NULL, world_walk_cell, world_embedded, o->trains != 0 };
    const Player *p = &g_players[g_player_local];
    if (g_game.step_mode != 0) {
        if (g_game.step_mode == 1 && g_game.step_count-- == 0) {
            if (o->draw_sprites) render_queue_visible_entities(&w, &p->rect);
            render_draw_city(g_game.map, g_game.style, &p->vp);
            hud_draw();
            g_game.step_count = 4;
        }
        return;
    }
    if (o->draw_sprites) render_queue_visible_entities(&w, &p->rect);
    render_draw_city(g_game.map, g_game.style, &p->vp);
    hud_draw();
}

/* Game_Present 0x430da0 (Gfx_Present 0x414b10: the caller's present callback) */
void game_present(void)
{
    if (g_game.step_mode != 0 && (g_game.step_mode != 1 || g_game.step_count != 4)) return;
    if (g_game.present) g_game.present(g_game.present_ctx);
}

/* Game_HandleKey 0x430dc0: in-game keys by scan code (bit 7: released). The HUD gets the key first.
   Unknown keys return false; every handled one, used or not, true. */
bool game_handle_key(int key)
{
    GameState *g = &g_game;
    bool dbg = g->opt.debug_keys != 0;
    int32_t *d = g_players[player_get_viewed()].dbg;
    if (hud_handle_key(key)) return true;
    switch (key) {
    case 0x44:   /* F10 */
        if (player_is_viewed_local()) hud_restore_subtitle();
        break;
    case 0x01:   /* Esc */
        if (g_players[player_get_viewed()].local1) {   /* Player_GetLocalFlag1b2 0x463890 */
            hud_toggle_video_menu();
            break;
        }
        if (player_is_viewed_local()) {
            if (replay_is_playing()) {
                g->quit = GAME_QUIT_ABANDON;
                g->result = 7;
            } else {
                hud_toggle_quit_prompt();
            }
        }
        break;
    case 0x1a: if (dbg) d[0] = -1; break;     /* [ */
    case 0x1b: if (dbg) d[0] = 1; break;      /* ] */
    case 0x25: if (dbg) d[1] = -1; break;     /* K */
    case 0x26: if (dbg) d[1] = 1; break;      /* L */
    case 0x2e:                                /* C */
        if (dbg && player_is_viewed_local()) hud_toggle_debug();
        break;
    case 0x37:                                /* keypad * */
        if (dbg || (g_cheat_ammo_key && g_player_count == 1))
            for (int w = 4; w >= 1; w--) player_add_ammo(player_get_viewed(), w, 99);
        break;
    case 0x38:                                /* Alt: render every 5th frame */
        if (dbg) g->step_mode = 1, g->step_count = 4;
        break;
    case 0x3b: net_build_chat_prefix(1); break;   /* F1..F4: chat */
    case 0x3c: net_build_chat_prefix(2); break;
    case 0x3d: net_build_chat_prefix(3); break;
    case 0x3e: net_build_chat_prefix(-1); break;
    case 0x3f:                                /* F5 */
        if (player_is_viewed_local()) music_next_station();
        break;
    case 0x40:                                /* F6: freeze */
        if (g_session_players == 1) {
            if (g->phase == 1) {
                hud_pause_on();
                g->phase = 2;
                snd_pause();
            } else {
                hud_pause_off();
                g->phase = 1;
                snd_resume();
            }
        }
        break;
    case 0x41:                                /* F7 */
        if (player_is_viewed_local()) pager_resume();
        break;
    case 0x42:                                /* F8: frame limiter */
        if (g_session_players == 1) {
            g->speed_limit = !g->speed_limit;
            hud_clear_zone_text(0xca);
            hud_show_zone_text(text_of(exe_str(g->speed_limit ? 0x4b0b88 : 0x4b0b98)), 0xca);
        }
        break;
    case 0x43:                                /* F9 */
        if (player_is_viewed_local()) hud_refresh_zone();
        break;
    case 0x48: if (dbg) d[2] = -1; break;     /* cursor keys: debug camera */
    case 0x50: if (dbg) d[2] = 1; break;
    case 0x4b: if (dbg) d[3] = -1; break;
    case 0x4d: if (dbg) d[3] = 1; break;
    case 0x4e: if (dbg) g->phase = 0; break;  /* keypad +: one tick */
    case 0x57: hud_toggle_video_menu(); break;   /* F11 */
    case 0x58:                                /* F12: reload */
        if (dbg) {
            hud_show_zone_text(text_of(exe_str(0x4b0b80)), 1);
            g->quit = GAME_QUIT_RELOAD;
            g->result = 0;
        }
        break;
    case 0x9a: if (dbg && d[0] < 0) d[0] = 0; break;   /* releases */
    case 0x9b: if (dbg && d[0] > 0) d[0] = 0; break;
    case 0xa5: if (dbg && d[1] < 0) d[1] = 0; break;
    case 0xa6: if (dbg && d[1] > 0) d[1] = 0; break;
    case 0xc8: if (dbg && d[2] < 0) d[2] = 0; break;
    case 0xd0: if (dbg && d[2] > 0) d[2] = 0; break;
    case 0xcb: if (dbg && d[3] < 0) d[3] = 0; break;
    case 0xcd: if (dbg && d[3] > 0) d[3] = 0; break;
    case 0xb8: if (dbg) g->step_mode = 0; break;
    case 0x147:                               /* Home: snap the camera */
        if (dbg) {
            int n = player_get_viewed();
            CameraPlayer cp;
            CameraWorld w = player_camera_world(n);
            player_camera(n, &cp);
            camera_snap(&cp, &w);
            player_camera_store(n, &cp);
            if (g->pause_frames != 0 || g->phase == 2) g->redraw = true;
        }
        break;
    default: return false;
    }
    return true;
}

void game_set_screen(int w, int h)
{
    g_game.screen_w = w, g_game.screen_h = h;
    CameraPlayer cp;
    player_camera(g_player_local, &cp);
    camera_set_viewport(&cp, w, h);
    player_camera_store(g_player_local, &cp);
}

/* ---- Game_Run 0x4148a0 ---- */

/* Timer_Start 0x47dc00: the 70 Hz tick counter (with sound on and the option 0x5031d0 off). */
static void timer_start(void)
{
    if (g_game.opt.sound && !g_game.opt.no_timer) {
        g_game.timer_ticks = 0;
        g_game.timer_us = 0;
        g_game.timer_on = true;
    }
}
static void timer_stop(void) { g_game.timer_on = false; }   /* Timer_Stop 0x47dcb0 */

bool game_run_begin(void)
{
    GameState *g = &g_game;
    g->speed_limit = true;
    style_reset_request();
    mission_read_ini();
    game_map_load();
    style_load_requested();
    game_init();
    tune_load_file(exe_str(0x4ac060));   /* "config.ini" */
    g->sub = 0;
    if (!net_reset_sync()) {
        game_shutdown();
        net_unk_44b900();
        g->quit = GAME_QUIT_NET;
        return false;
    }
    gfx_select_mode();   /* Gfx_SelectMode 0x414cc0 -> Gfx_SetVideoMode */
    game_set_screen(g->screen_w ? g->screen_w : 640, g->screen_h ? g->screen_h : 480);
    style_convert_palettes(g->style, &PIXFMT_32);
    g->frame_time_sum = 0;
    g->frame_time_n = 0;
    timer_start();
    return true;
}

/* One iteration of the loop: present the previous frame, read and exchange the controls, wait for the
   3rd tick of the 70 Hz timer (about 23 frames a second; F8 turns the limit off), apply each player's
   controls and keys, then Game_Frame, Game_Render and the sound. */
int game_run_step(uint64_t elapsed_us)
{
    GameState *g = &g_game;
    if (g->quit != GAME_RUNNING) return GAME_STEP_DONE;
    if (g->timer_on) {
        g->timer_us += elapsed_us * 70;
        g->timer_ticks += (uint32_t)(g->timer_us / 1000000);
        g->timer_us %= 1000000;
    }
    if (g->sub == 0) {
        game_present();
        g->controls[g_player_local] = input_read_controls();
        net_sync_frame_inputs(g->controls);
        g->sub = 1;
    }
    if (g->speed_limit && g->timer_on) {   /* Timer_WaitTicks 0x47dc80 */
        if (g->timer_ticks < 3) return GAME_STEP_WAIT;
        g->timer_ticks = 0;
    }
    g->sub = 0;
    for (int n = player_first(); n > -1; n = player_next(n)) {
        player_set_viewed(n);
        uint32_t c = g->controls[n];
        if (c) {
            player_apply_input(c);
            uint32_t k = input_get_high_bits(c);
            if (k) game_handle_key((int)k);
        }
        camera_debug_move(n);
    }
    game_frame();
    game_render();
    if (g->opt.sound) {
        if (g_audio_mode == 1) snd_update_game();
        else if (g_audio_mode == 2) snd_update_frontend();
    }
    if (g->opt.timing) {
        if (++g->frame_time_n == 100) g->frame_time_sum = 0, g->frame_time_n = 0;
    }
    return g->quit == GAME_RUNNING ? GAME_STEP_FRAME : GAME_STEP_DONE;
}

int game_run_end(void)
{
    GameState *g = &g_game;
    if (g->quit != GAME_QUIT_NET) {
        if (g->present) g->present(g->present_ctx);   /* Gfx_Present */
        timer_stop();
        game_shutdown();
        net_unk_44b900();
    }
    net_end_game();
    mission_free_ini();
    style_free(g->style);
    g->style = NULL;
    map_free(g->map);
    g->map = NULL;
    if (g->opt.sound) {
        music_shutdown();
        music_init();
    }
    return g->quit;
}
