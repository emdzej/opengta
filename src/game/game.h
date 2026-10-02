/* Game core (0x430980-0x430dc0) and the in-game session (Game_Run 0x4148a0): level start, the frame
   step order, the feature switches, pause / step modes and the exit request. See docs/game-core.md.

   Game_Run is a blocking loop in the original. Here it is split into game_run_begin (everything
   before the loop: mission.ini, map, style, Game_Init), game_run_step (one iteration of the loop; no
   blocking: it returns GAME_STEP_WAIT while the 70 Hz pacing timer hasn't reached its 3 ticks) and
   game_run_end (everything after the loop). */
#pragma once
#include "../map.h"
#include "../render/camera.h"
#include "../style.h"
#include <stdbool.h>
#include <stdint.h>

/* The 43 launch options of Game_SetOptions 0x4146d0, in argument order, with the address each one is
   stored at. WinMain 0x437230 passes the defaults of game_default_options(). The names of the
   switches whose use isn't known are their addresses. */
typedef struct {
    int32_t peds;               /*  1 0x502f40 peds */
    int32_t opt502f68;          /*  2 0x502f68 */
    int32_t cars;               /*  3 0x503180 cars */
    int32_t sound;              /*  4 0x502f6c sound */
    int32_t timing;             /*  5 0x502f50 frame time measurement */
    int32_t emergency;          /*  6 0x5031cc emergency services / police (the mission header can change it) */
    int32_t trains;             /*  7 0x502f48 trains and traffic lights */
    int32_t draw_sprites;       /*  8 0x5031a4 */
    int32_t police;             /*  9 0x502f58 police (with `emergency`, Police_InitForMission) */
    int32_t opt502f64;          /* 10 0x502f64 */
    int32_t opt5031a0;          /* 11 0x5031a0 */
    int32_t opt502f70;          /* 12 0x502f70 */
    int32_t draw_blocks;        /* 13 0x5031c8 */
    int32_t direct_start;       /* 14 0x5031d8 WinMain skips the frontend */
    int32_t opt50319c;          /* 15 0x50319c */
    int32_t opt5031f0;          /* 16 0x5031f0 */
    int32_t opt503190;          /* 17 0x503190 */
    int32_t opt5031b8;          /* 18 0x5031b8 */
    int32_t opt5031dc;          /* 19 0x5031dc */
    int32_t adiag;              /* 20 0x502f54 sentinel debug log "adiag" */
    int32_t opt502f44;          /* 21 0x502f44 */
    int32_t opt502f3c;          /* 22 0x502f3c */
    int32_t objects;            /* 23 0x5031e0 */
    int32_t opt502f5c;          /* 24 0x502f5c */
    int32_t debug_keys;         /* 25 0x503194 debug keys and free camera */
    int32_t opt503178;          /* 26 0x503178 */
    int32_t opt502f60;          /* 27 0x502f60 */
    int32_t opt503188;          /* 28 0x503188 */
    int32_t no_camera_init;     /* 29 0x502f38 Camera_InitAll skips the zoom-in */
    int32_t no_timer;           /* 30 0x5031d0 Timer_Start doesn't start the 70 Hz timer */
    int32_t opt5031e8;          /* 31 0x5031e8 */
    int32_t opt502f4c;          /* 32 0x502f4c */
    uint8_t blend_sprites;      /* 33 0x5031e4 */
    uint8_t debug_text;         /* 34 0x5031e6 */
    uint8_t no_patrols;         /* 35 0x5031ec Police_InitForMission spawns no patrol cars */
    uint8_t infinite_lives;     /* 36 0x502f35 */
    uint8_t opt50317c;          /* 37 0x50317c */
    uint8_t opt5031c4;          /* 38 0x5031c4 */
    uint8_t cheat_mult;         /* 39 0x502f74 multiplier 10 */
    uint8_t cheat_score;        /* 40 0x50318c score 999999999 */
    uint8_t cheat_weapons;      /* 41 0x5031e5 99 of every weapon, armour */
    uint8_t demo;               /* 42 0x5031bc demo: the level ends after 10000 frames */
    uint8_t opt50317d;          /* 43 0x50317d */
} GameOptions;

/* Game_Run results (0x51322c): what WinMain does next */
enum { GAME_RUNNING = 0, GAME_QUIT_END = 1, GAME_QUIT_ABANDON = 2, GAME_QUIT_RELOAD = 3, GAME_QUIT_NET = 4 };
/* game_run_step results */
enum { GAME_STEP_FRAME, GAME_STEP_WAIT, GAME_STEP_DONE };

typedef struct {
    GameOptions opt;
    Map *map;                   /* the loaded city (Map_Load 0x438200) */
    Style *style;               /* its style (Style_Load 0x47cf10) */
    int32_t quit;               /* 0x51322c */
    int32_t result;             /* 0x513230: 1 success, 2 failed, 3 dead, 7 abandoned, 0xb demo over ... */
    int32_t pause_frames;       /* 0x513234 */
    bool redraw;                /* 0x513238 one redraw while paused */
    int32_t step_mode;          /* 0x51323c render every 5th frame (debug key) */
    int32_t phase;              /* 0x513240 0 single step, 1 running, 2 frozen (F6) */
    int32_t step_count;         /* 0x513244 */
    bool speed_limit;           /* 0x502f34 the 3-tick frame limiter (F8 toggles) */
    uint32_t controls[4];       /* 0x5031a8 per-player control words of this frame */
    int32_t frame_time, frame_time_sum, frame_time_n;   /* 0x5031d4, 0x5031c0, 0x502f30 (timing option) */
    /* 70 Hz pacing timer (Timer_Start 0x47dc00, ticks 0x775580) driven by the caller's clock */
    bool timer_on;              /* 0x775584 */
    uint32_t timer_ticks;       /* 0x775580 */
    uint64_t timer_us;          /* sub-tick remainder of the caller's clock */
    int sub;                    /* game_run_step: 0 at the top of the loop, 1 waiting for the timer */
    /* fatal errors (Error_Fatal 0x422900) */
    void (*on_fatal)(const char *msg);  /* called before exiting (default: the message to stderr) */
    int fatal_code, fatal_line, fatal_arg;
    char error_file[64];        /* Error_SetFileName 0x4228c0 */
    char fatal_msg[160];
    int screen_w, screen_h;     /* 0x504cc0, 0x504cc4 */
    /* presenting (Gfx_Present 0x414b10) */
    void (*present)(void *ctx);
    void *present_ctx;
} GameState;

extern GameState g_game;
extern int g_session_players;               /* 0x74f838 players in the session (Net_InitPlayers 0x44b8b0) */
extern bool g_cheat_ammo_key;               /* 0x503198 frontend cheat: keypad * gives ammo */
extern int g_audio_mode;                    /* 0x5031f8 1 in game, 2 frontend sound */

void game_default_options(GameOptions *o);  /* the values WinMain 0x437230 passes */
void game_set_options(const GameOptions *o);/* Game_SetOptions 0x4146d0 */

/* Error_Fatal 0x422900: the original shuts down (Sys_Shutdown 0x4371e0), shows "Error <code>.<module>"
   with the message of the code (Error_GetMessage 0x422b90) and exits. The port reports the code, the
   module line and the argument through on_fatal and exits. */
_Noreturn void game_fatal(int code, int line, int arg);
void game_set_error_file(const char *name);

void game_request_redraw(void);             /* Game_RequestRedraw 0x430980 */
void game_set_pause_frames(int n);          /* Game_SetPauseFrames 0x4309a0 */
static inline bool game_is_paused(void) { return g_game.pause_frames != 0; }   /* 0x4309c0 */
static inline bool game_is_frozen(void) { return g_game.phase == 2; }          /* 0x4309d0 */
void game_request_end(int result);          /* Game_RequestEnd 0x4309e0 */
void game_request_abandon(void);            /* Game_RequestAbandon 0x430a00 */

/* Map_Load 0x438200 including the parts that map.c leaves to other modules (Obj_SetMapObjects,
   Route_LoadCmp, Area_SetNavData, the per-player camera resets, Style_Request); false if fatal. */
bool game_map_load(void);
void game_init(void);                       /* Game_Init 0x430a20 */
void game_shutdown(void);                   /* Game_Shutdown 0x430b10 */
void game_frame(void);                      /* Game_Frame 0x430b20 */
void game_update(void);                     /* Game_Update 0x430c00 */
void game_render(void);                     /* Game_Render 0x430d40 */
void game_present(void);                    /* Game_Present 0x430da0 */
bool game_handle_key(int key);              /* Game_HandleKey 0x430dc0 (scan code) */

/* The screen mode (Gfx_SetVideoMode 0x414db0: 0x504cc0 / 0x504cc4), which also sets the local
   player's viewport (Player_SetViewport 0x464500). The frontend's mode (640 x 480) is set before the
   first level; Game_Run selects the in-game mode again after Game_Init. */
void game_set_screen(int w, int h);

/* Game_Run 0x4148a0 as steps. begin: false if the network start fails (quit = 4: run_end next).
   step: one loop iteration; elapsed_us is the caller's time since the last call (feeds the 70 Hz
   timer). GAME_STEP_DONE once quit is set: call game_run_end. */
bool game_run_begin(void);
int game_run_step(uint64_t elapsed_us);
int game_run_end(void);                     /* returns the quit code */

/* Style request (Style_ResetRequest 0x47ced0 / Style_Request 0x47cee0) and map name (Map_ClearName
   0x438190 / Map_SetName 0x4381c0): the next level's files, by priority. */
void style_reset_request(void);
void style_request(int n, int priority);
int style_requested(void);                  /* 0x7752d8 (Style_GetNumber 0x47cec0 once loaded) */
void map_clear_name(void);
void map_set_name(const char *name, int priority);
const char *map_name(void);                 /* 0x5c1c34, NULL if none set */
