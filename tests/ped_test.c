/* The player on foot against the real data: mission 1 started as Game_Run does, then the in-game loop
   (game_run_step, paced by the audio rendered as in the app) driven by held keys through the input
   path (input_feed_held -> Input_ReadControls -> Player_ApplyInput -> Ped_UpdateAll). Checks the
   control word, walking (speed, heading, the walk cycle frames), turning, running into a building,
   determinism (the same script twice gives the same positions), and renders frames to out/ped/.
     ./build/ped_test            (data root: ./game or OPENGTA_DATA) */
#include "audio/audio.h"
#include "exe.h"
#include "game/car.h"
#include "game/coll.h"
#include "game/event.h"
#include "game/game.h"
#include "game/gmath.h"
#include "game/input.h"
#include "game/mission.h"
#include "game/ped.h"
#include "game/player.h"
#include "platform.h"
#include "png.h"
#include "render/camera.h"
#include "render/poly.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum { W = 640, H = 480 };
static uint32_t fb[W * H];
static int failures;

#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: " __VA_ARGS__); printf("\n"); failures++; } } while (0)

uint8_t *plat_load_user_file(const char *name, size_t *size) { (void)name, (void)size; return NULL; }
bool plat_save_user_file(const char *name, const void *data, size_t size) { (void)name, (void)data, (void)size; return true; }
void plat_log(const char *msg) { printf("log: %s\n", msg); }
static void on_present(void *ctx) { (void)ctx; }

static bool start(int section)
{
    GameOptions o;
    game_default_options(&o);
    game_set_options(&o);
    g_game.present = on_present;
    map_clear_name();
    map_set_name(NULL, 2);
    if (!mission_set_ini_section(section)) return false;
    poly_set_screen_rows(fb, W * 4, H);
    poly_set_clip(0, 0, W - 1, H - 1);
    return game_run_begin();
}

/* ---- held keys ---- */
static uint8_t held[KEY_COUNT];
enum { K_UP = 0x148, K_DOWN = 0x150, K_LEFT = 0x14b, K_RIGHT = 0x14d, K_FIRE = 0x1d };

/* One game frame: host frames (one 70 Hz tick of audio each, as the app runs) until game_run_step has
   done a frame, with the held keys fed every host frame. */
static void game_frame_step(void)
{
    for (int calls = 0; calls < 16; calls++) {
        input_feed_held(held);
        audio_render(NULL, 315);
        int r = game_run_step();
        if (r != GAME_STEP_WAIT) return;
    }
    CHECK(0, "game_run_step never ran a frame");
}

static const Ped *me(void) { return ped_get(g_players[0].ped); }

static void shot(const char *name)
{
    char path[128];
    /* fb holds the frame the last game_run_step drew */
    snprintf(path, sizeof path, "out/ped/%s.png", name);
    CHECK(png_write(path, fb, W, H, PNG_XRGB), "write %s", path);
    /* and the middle of the screen (where the camera keeps the player) magnified 3x */
    enum { C = 120, Z = 3 };
    static uint32_t zoom[C * Z * C * Z];
    for (int y = 0; y < C * Z; y++)
        for (int x = 0; x < C * Z; x++) zoom[y * C * Z + x] = fb[(H / 2 - C / 2 + y / Z) * W + W / 2 - C / 2 + x / Z];
    snprintf(path, sizeof path, "out/ped/%s_zoom.png", name);
    CHECK(png_write(path, zoom, C * Z, C * Z, PNG_XRGB), "write %s", path);
    const Ped *p = me();
    printf("  %s: ped at (%d, %d, %d) angle %#x anim %#x frame %d speed %d\n", path, p->spr.x >> 16, p->spr.y >> 16,
           p->spr.z >> 16, p->spr.angle, p->anim, p->spr.frame, p->speed);
}

/* ---- the control word ---- */
static void check_control_word(void)
{
    /* Input_ActionPressed / Released on a fresh state */
    input_reset_state();
    uint32_t w = 0;
    input_action_pressed(INPUT_LEFT, &w);
    CHECK(w == (1u | (uint32_t)(-7 & 0xf) << 9), "left pressed %#x", w);
    w = 0;
    input_action_released(INPUT_LEFT, &w);
    CHECK(w == 1u, "left released %#x", w);
    w = 0;
    input_action_pressed(INPUT_ACCEL, &w);
    CHECK(w == (2u | 3u << 15), "accelerate %#x", w);
    w = 0;
    input_action_pressed(INPUT_FIRE, &w);
    CHECK(w == 0x200008u, "fire %#x", w);
    w = 0;
    input_action_pressed(INPUT_ENTER, &w);
    input_action_released(INPUT_ENTER, &w);
    CHECK(w == 0x400000u, "enter: no release event %#x", w);
    input_reset_state();
    /* the key path: Up held -> accelerate, an unbound key -> a key event in the high bits */
    input_flush_keys();
    input_init();
    CHECK(g_input_keys[INPUT_ACCEL] == 0x148 && g_input_keys[INPUT_FIRE] == 0x1d && g_input_keys[INPUT_ENTER] == 0x1c,
          "default bindings %#x %#x %#x", g_input_keys[INPUT_ACCEL], g_input_keys[INPUT_FIRE], g_input_keys[INPUT_ENTER]);
    input_post_key(0x148);
    w = input_read_controls();
    CHECK(w == (2u | 3u << 15), "Up -> %#x", w);
    input_post_key(0x1c8);
    w = input_read_controls();
    CHECK(w == 2u, "Up released -> %#x", w);
    input_post_key(0x3f);   /* F5: not bound */
    input_post_key(0x148);
    w = input_read_controls();
    CHECK(w == (0x40u | 0x3fu << 23), "F5 -> %#x (one key event ends the frame's events)", w);
    w = input_read_controls();
    CHECK(w == (2u | 3u << 15), "the queued Up next frame -> %#x", w);
    input_post_key(0x1c8);
    input_read_controls();
    printf("control word: left %#x, accelerate %#x, fire %#x, F5 key event %#x\n", 1u | 9u << 9, 2u | 3u << 15, 0x200008u,
           0x40u | 0x3fu << 23);
}

/* ---- walking ---- */
typedef struct { int32_t x, y, z; int16_t angle, anim; } Pose;
static Pose pose(void) { const Ped *p = me(); return (Pose){ p->spr.x, p->spr.y, p->spr.z, p->spr.angle, p->anim }; }

/* The script: stand 5, walk 30 (Up), turn left while walking 8, walk 20, turn right 4, back (Down)
   10, stand 5. Returns a hash of the per-frame poses; poses[] gets them. */
enum { SCRIPT_FRAMES = 82 };
static Pose poses[SCRIPT_FRAMES];
static uint32_t run_script(bool shots)
{
    uint32_t h = 2166136261u;
    /* the player starts beside its car, facing it: face along the pavement (-y, up the screen) */
    ped_get(g_players[0].ped)->spr.angle = 0x200;
    for (int f = 0; f < SCRIPT_FRAMES; f++) {
        memset(held, 0, sizeof held);
        if (f >= 5 && f < 63) held[K_UP] = 1;
        if (f >= 35 && f < 43) held[K_LEFT] = 1;
        if (f >= 63 - 0 && f < 67) held[K_RIGHT] = 1, held[K_UP] = 0;
        if (f >= 67 && f < 77) held[K_DOWN] = 1;
        game_frame_step();
        poses[f] = pose();
        const uint8_t *b = (const uint8_t *)&poses[f];
        for (size_t i = 0; i < sizeof(Pose); i++) h = (h ^ b[i]) * 16777619u;
        if (shots && (f == 4 || f == 20 || f == 23 || f == 40 || f == 62 || f == 75)) {
            char name[32];
            snprintf(name, sizeof name, "walk_%02d", f);
            shot(name);
        }
    }
    memset(held, 0, sizeof held);
    return h;
}

static void check_walk(void)
{
    const Pose *P = poses;
    /* standing still */
    CHECK(P[4].x == P[0].x && P[4].y == P[0].y, "moved while standing");
    CHECK(P[4].anim == 0x88 || P[4].anim == 1, "standing anim %#x", P[4].anim);
    /* walking: about 4 pixels a frame along the heading (sin, cos), the walk cycle 1..8 */
    int32_t dx = P[30].x - P[10].x, dy = P[30].y - P[10].y;
    int a = P[10].angle;
    int32_t ex = math_sin(a) * 4 * 20, ey = math_cos(a) * 4 * 20;
    printf("walk: 20 frames moved (%d, %d) pixels, heading %#x, expected about (%d, %d)\n", dx >> 16, dy >> 16, a, ex >> 16,
           ey >> 16);
    CHECK(abs((dx - ex) >> 16) <= 24 && abs((dy - ey) >> 16) <= 24, "walk distance (%d, %d) vs (%d, %d)", dx >> 16, dy >> 16,
          ex >> 16, ey >> 16);
    int cycle = 0;
    for (int f = 10; f < 30; f++) cycle |= P[f].anim >= 1 && P[f].anim <= 0x10 ? 1 << (P[f].anim & 15) : 0;
    CHECK(__builtin_popcount((unsigned)cycle) >= 4, "walk cycle frames %#x", cycle);
    /* turning left: the heading grows (16, then 48 a frame) */
    int t = (P[42].angle - P[34].angle) & 0x3ff;
    printf("turn: left 8 frames turned %#x\n", t);
    CHECK(t > 0x100 && t < 0x200, "left turn %#x", t);
    /* backing: Down moves against the heading */
    int32_t bx = P[76].x - P[68].x, by = P[76].y - P[68].y;
    int b = P[70].angle;
    int64_t dot = (int64_t)(bx >> 8) * (math_sin(b) >> 8) + (int64_t)(by >> 8) * (math_cos(b) >> 8);
    printf("back: 8 frames moved (%d, %d)\n", bx >> 16, by >> 16);
    CHECK(dot < 0, "Down did not back off");
}

/* ---- a building ---- */
/* From the player's block, the nearest building block (ground type 5) on the player's layer in one
   of the four directions. */
static int find_wall_dir(int *dist)
{
    const Ped *p = me();
    int bx = p->spr.x >> 22, by = p->spr.y >> 22, bz = p->spr.z >> 22;
    static const int dirs[4][3] = { { 0, 1, 0 }, { 1, 0, 0x100 }, { 0, -1, 0x200 }, { -1, 0, 0x300 } };
    int best = -1, bd = 99;
    for (int d = 0; d < 4; d++)
        for (int k = 1; k < 8; k++) {
            int x = bx + dirs[d][0] * k, y = by + dirs[d][1] * k;
            if (x < 0 || x > 255 || y < 0 || y > 255) break;
            int t = (g_game.map->type_cache[bz][y][x] & 0x70) >> 4;
            if (t == 5) {
                if (k < bd) bd = k, best = dirs[d][2];
                break;
            }
            if (t == 0) break;   /* a drop */
        }
    *dist = bd;
    return best;
}

static void check_wall(void)
{
    int dist;
    int a = find_wall_dir(&dist);
    CHECK(a >= 0, "no building near the start");
    if (a < 0) return;
    Ped *p = ped_get(g_players[0].ped);
    p->spr.angle = (int16_t)a;
    printf("wall: a building %d blocks away at heading %#x\n", dist, a);
    memset(held, 0, sizeof held);
    held[K_UP] = 1;
    int32_t last_x = p->spr.x, last_y = p->spr.y;
    int still = 0;
    for (int f = 0; f < 40 + dist * 20; f++) {
        game_frame_step();
        if (p->spr.x == last_x && p->spr.y == last_y) still++;
        else still = 0;
        last_x = p->spr.x, last_y = p->spr.y;
        int t = (g_game.map->type_cache[p->spr.z >> 22][p->spr.y >> 22][p->spr.x >> 22] & 0x70) >> 4;
        CHECK(t != 5, "the ped walked into a building at (%d, %d)", p->spr.x >> 16, p->spr.y >> 16);
        if (t == 5) break;
    }
    memset(held, 0, sizeof held);
    printf("wall: stopped %d frames at (%d, %d) angle %#x\n", still, p->spr.x >> 16, p->spr.y >> 16, p->spr.angle);
    CHECK(still >= 5 || p->spr.angle != a, "never stopped or turned at the building");
    shot("wall");
}

int main(void)
{
    char err[256];
    if (!vfs_mount_default()) { printf("SKIP: no data root\n"); return 0; }
    if (!exe_init(err, sizeof err)) { printf("SKIP: %s\n", err); return 0; }
    math_init_tables();
    if (!camera_init_tables()) { printf("FAIL: camera tables\n"); return 1; }
    mkdir("out", 0755);
    mkdir("out/ped", 0755);
    game_set_screen(W, H);

    check_control_word();

    CHECK(start(1), "mission 1 start");
    const Ped *p = me();
    printf("mission 1: player ped %d at (%d, %d, %d) angle %#x anim %#x state %d\n", p->id, p->spr.x >> 16, p->spr.y >> 16,
           p->spr.z >> 16, p->spr.angle, p->anim, p->state);
    uint32_t h1 = run_script(true);
    check_walk();
    check_wall();
    /* stand 300 frames: ambient peds appear around the view (Ped_SpawnAmbient) and wander */
    memset(held, 0, sizeof held);
    int most = 0;
    for (int f = 0; f < 300; f++) {
        game_frame_step();
        int k = 0;
        for (int i = 0; i < PED_DRIVER_FIRST; i++) k += ped_get(i)->anim != 0;
        if (k > most) most = k;
        if (f == 150) shot("ambient_150");
    }
    printf("ambient peds: at most %d at once in 300 frames\n", most);
    CHECK(most > 0, "no ambient ped spawned");
    int ambient = 0;
    for (int i = 0; i < PED_DRIVER_FIRST; i++) ambient += ped_get(i)->anim != 0;
    printf("ambient peds active: %d (counter %d); view rect %d..%d x %d..%d\n", ambient, g_peds_active,
           g_players[0].rect.left, g_players[0].rect.right, g_players[0].rect.top, g_players[0].rect.bottom);
    for (int i = 0; i < PED_MAX; i++)
        if (ped_get(i)->anim) printf("    ped %d anim %#x state %d at (%d, %d)\n", i, ped_get(i)->anim, ped_get(i)->state, ped_get(i)->spr.x >> 16, ped_get(i)->spr.y >> 16);
    shot("ambient");
    game_run_end();

    CHECK(start(1), "mission 1 restart");
    uint32_t h2 = run_script(false);
    printf("script pose hash %08x / %08x\n", h1, h2);
    CHECK(h1 == h2, "the same script gave different poses");
    game_run_end();

    printf(failures ? "FAIL (%d)\n" : "PASS\n", failures);
    return failures != 0;
}
