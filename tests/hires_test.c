/* The hires renderer (src/render/hires, docs/hires.md) against the real data:
   1. scenes of NYC (the render_test cameras: the start, a bridge, slopes, ramps, water) drawn by the
      faithful renderer and by the hires pass at 2x and 4x: the hires frame box-filtered back to 640 x 480
      must stay close to the faithful frame (same view, same faces), animated water must follow the
      tile animation; side-by-side comparisons (faithful scaled up nearest | hires) go to out/hires/;
   2. mission 1 played by a key script (the "driving" screenshot: out of the start, into a car, down the
      road) with hires=1, 2 and 4, each run in its own process: the game state hash and the faithful
      frames (CRC of every presented 640 x 480 frame) must be the same, i.e. the hires pass changes
      nothing but what is shown; frames with sprites and the HUD are written as comparisons.
     ./build/hires_test            (data root: ./game or OPENGTA_DATA) */
#include "audio/audio.h"
#include "exe.h"
#include "game/car.h"
#include "game/event.h"
#include "game/game.h"
#include "game/gmath.h"
#include "game/input.h"
#include "game/mission.h"
#include "game/obj.h"
#include "game/ped.h"
#include "game/player.h"
#include "hud/hud.h"
#include "map.h"
#include "platform.h"
#include "png.h"
#include "render/camera.h"
#include "render/city.h"
#include "render/hires/hires.h"
#include "render/hires/hires_tex.h"
#include "render/poly.h"
#include "style.h"
#include "vfs_host.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum { W = 640, H = 480 };
static uint32_t fb[W * H];
static int failures;

#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: " __VA_ARGS__); printf("\n"); failures++; } } while (0)

uint8_t *plat_load_user_file(const char *name, size_t *size) { (void)name, (void)size; return NULL; }
bool plat_save_user_file(const char *name, const void *data, size_t size) { (void)name, (void)data, (void)size; return true; }
void plat_log(const char *msg) { printf("log: %s\n", msg); }

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + t.tv_nsec * 1e-9;
}

/* ---- comparisons ---- */

static uint32_t xrgb_to_abgr(uint32_t c) { return 0xff000000u | (c & 0xff) << 16 | (c & 0xff00) | (c >> 16 & 0xff); }

/* Faithful pixels (x0, y0, cw x ch) scaled n times, nearest, on the left; the same rectangle of the hires
   frame on the right; 8 grey columns between. */
static void write_comparison(const char *path, const uint32_t *faithful, const uint32_t *hires, int n, int x0, int y0,
                             int cw, int ch)
{
    const int gap = 8, ow = cw * n * 2 + gap, oh = ch * n, hw = W * n;
    uint32_t *o = malloc((size_t)ow * oh * 4);
    if (!o) return;
    for (int y = 0; y < oh; y++)
        for (int x = 0; x < ow; x++) {
            uint32_t c;
            if (x < cw * n) c = xrgb_to_abgr(faithful[(y0 + y / n) * W + x0 + x / n]);
            else if (x < cw * n + gap) c = 0xff808080u;
            else c = hires[(size_t)(y0 * n + y) * hw + x0 * n + (x - cw * n - gap)];
            o[(size_t)y * ow + x] = c;
        }
    CHECK(png_write(path, o, ow, oh, PNG_ABGR), "write %s", path);
    free(o);
}

/* Mean absolute difference per channel between the faithful frame and the hires frame averaged over
   n x n blocks; and the share of pixels off by more than 64 in some channel. */
static double downsampled_error(const uint32_t *faithful, const uint32_t *hires, int n, double *far_share)
{
    double sum = 0;
    long far = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            unsigned acc[3] = { 0, 0, 0 };
            for (int j = 0; j < n; j++)
                for (int i = 0; i < n; i++) {
                    uint32_t c = hires[(size_t)(y * n + j) * W * n + x * n + i];
                    acc[0] += c & 0xff, acc[1] += c >> 8 & 0xff, acc[2] += c >> 16 & 0xff;
                }
            uint32_t f = faithful[y * W + x];
            int fr = f >> 16 & 0xff, fg = f >> 8 & 0xff, fbl = f & 0xff, nn = n * n;
            int dr = abs((int)acc[0] / nn - fr), dg = abs((int)acc[1] / nn - fg), db = abs((int)acc[2] / nn - fbl);
            sum += dr + dg + db;
            far += dr > 64 || dg > 64 || db > 64;
        }
    *far_share = (double)far / (W * H);
    return sum / (3.0 * W * H);
}

/* ---- scenes (render_test's cameras) ---- */

typedef struct { const char *name; int x, y, z; int frames; int dbg_height; } Scene;
static CameraWorld world = { .peds = true, .cars = true };

static void frame(CameraPlayer *p, const Map *m, Style *s, bool draw)
{
    style_update_anims(s);
    camera_update(p, &world);
    render_compute_visible_rect(&p->vp);
    render_copy_camera(&p->vp);
    if (draw) render_draw_city(m, s, &p->vp);
}

static void settle(CameraPlayer *p, const Map *m, Style *s, const Scene *sc)
{
    memset(p, 0, sizeof *p);
    camera_set_viewport(p, W, H);
    p->target_kind = CAM_TARGET_PED;
    p->target = (CameraTarget){ (sc->x * 64 + 32) << 16, (sc->y * 64 + 32) << 16, sc->z * 0x400000 - 0x10000, 8, 8, 0, 0 };
    camera_init(p, &world);
    p->cam.dbg_height = sc->dbg_height;
    for (int i = 0; i < sc->frames; i++) frame(p, m, s, false);
}

/* The faithful frame and the hires frame of the current camera; both start black. */
static const uint32_t *draw_both(CameraPlayer *p, const Map *m, Style *s, int n, double *secs)
{
    memset(fb, 0, sizeof fb);
    CHECK(hires_init(n, fb, W, H), "hires_init(%d)", n);
    frame(p, m, s, true);
    double t0 = now();
    hires_frame_begin(m, s, &p->vp);
    hires_frame_end();
    if (secs) *secs = now() - t0;
    int w, h;
    return hires_pixels(&w, &h);
}

static void run_scenes(void)
{
    char err[256], path[256];
    Map *m = map_load("GTADATA/NYC.CMP", err, sizeof err);
    if (!m) { CHECK(0, "%s", err); return; }
    Style *s = style_load(m->style, err, sizeof err);
    if (!s) { CHECK(0, "%s", err); map_free(m); return; }
    style_convert_palettes(s, &PIXFMT_32);
    static const Scene scenes[] = {
        { "start", 105, 119, 4, 300, 0 },  { "bridge", 55, 149, 2, 300, 0 },     { "slope", 99, 54, 4, 300, 0 },
        { "water", 60, 144, 5, 300, 0 },   { "ramp_ns", 221, 76, 4, 300, -200 }, { "ramp_ew", 29, 41, 4, 300, -200 },
        { "start_far", 105, 119, 4, 300, -400 },
    };
    for (unsigned i = 0; i < sizeof scenes / sizeof *scenes; i++) {
        const Scene *sc = &scenes[i];
        for (int n = 2; n <= 4; n += 2) {
            CameraPlayer p;
            settle(&p, m, s, sc);
            double secs;
            const uint32_t *hp = draw_both(&p, m, s, n, &secs);
            double far, e = downsampled_error(fb, hp, n, &far);
            printf("  %-9s %dx: mean error %5.2f, %4.1f%% far off; hires pass %.1f ms -> ", sc->name, n, e, far * 100,
                   secs * 1000);
            if (n == 2) snprintf(path, sizeof path, "out/hires/%s_2x.png", sc->name), write_comparison(path, fb, hp, 2, 0, 0, W, H);
            else snprintf(path, sizeof path, "out/hires/%s_4x_crop.png", sc->name), write_comparison(path, fb, hp, 4, 220, 165, 200, 150);
            printf("%s\n", path);
            CHECK(e < 14 && far < 0.06, "%s %dx: the hires frame doesn't match the faithful one (error %.2f, %.1f%% far)",
                  sc->name, n, e, far * 100);
        }
    }

    /* animated water: step the tile animation; the hires frame changes exactly when the faithful one does */
    {
        CameraPlayer p;
        settle(&p, m, s, &scenes[3]);
        uint32_t fprev = 0, hprev = 0;
        int changes = 0, agree = 0, steps = 24;
        for (int k = 0; k < steps; k++) {
            const uint32_t *hp = draw_both(&p, m, s, 2, NULL);
            uint32_t fc = crc32(fb, sizeof fb), hc = crc32(hp, (size_t)W * H * 16);
            if (k > 0) {
                changes += fc != fprev;
                agree += (fc != fprev) == (hc != hprev);
            }
            if (k == 5 || k == 6) {
                snprintf(path, sizeof path, "out/hires/water_anim%d_2x.png", k);
                write_comparison(path, fb, hp, 2, 0, 0, W, H);
            }
            fprev = fc, hprev = hc;
        }
        printf("  water animation: %d of %d steps change the faithful frame, the hires frame agrees on %d\n", changes,
               steps - 1, agree);
        CHECK(changes > 0 && agree == steps - 1, "water animation: %d changes, %d agree", changes, agree);
    }
    int nt, ns;
    hires_texture_stats(&nt, &ns);
    printf("  textures converted: %d tiles\n", nt);
    hires_init(0, NULL, 0, 0);
    style_free(s);
    map_free(m);
}

/* ---- mission 1 with hires on and off ---- */

/* The game state that must not depend on the hires pass: every car, ped, object and player record
   (their pointers lead into static tables: the sprite infos, the car infos, the entity arrays), the RNGs, the mission, the frame counter. */
static uint32_t state_hash(void)
{
    uint32_t h = crc32(g_rng, sizeof g_rng) ^ g_frame * 0x9e3779b9u;
    h = h * 31 + crc32(g_cars, sizeof g_cars);
    h = h * 31 + crc32(g_peds, sizeof g_peds);
    h = h * 31 + crc32(g_objs, sizeof g_objs);
    h = h * 31 + crc32(g_players, sizeof g_players);
    h = h * 31 + crc32(g_mission.commands, sizeof g_mission.commands);
    h = h * 31 + crc32(g_mission.objects, sizeof g_mission.objects);
    return h;
}

typedef struct { uint32_t state, frames_crc, hires_crc; int presents; double hires_ms; } RunResult;

static int run_n, presents;
static uint32_t frames_crc, hires_crc;
static const int shots[] = { 30, 140, 320 };

static void on_present(void *ctx)
{
    (void)ctx;
    presents++;
    frames_crc = frames_crc * 31 + crc32(fb, sizeof fb);
    if (run_n == 1) return;
    hires_frame_end();
    int w, h;
    const uint32_t *hp = hires_pixels(&w, &h);
    hires_crc = hires_crc * 31 + crc32(hp, (size_t)w * h * 4);
    for (unsigned i = 0; i < sizeof shots / sizeof *shots; i++)
        if (presents == shots[i]) {
            char path[128];
            if (run_n == 2) snprintf(path, sizeof path, "out/hires/mission1_%03d_2x.png", presents), write_comparison(path, fb, hp, 2, 0, 0, W, H);
            else snprintf(path, sizeof path, "out/hires/mission1_%03d_4x_crop.png", presents), write_comparison(path, fb, hp, 4, 220, 165, 200, 150);
        }
}

static double hires_secs;
static void on_hud(void)
{
    double t0 = now();
    hires_frame_begin(g_game.map, g_game.style, &g_players[g_player_local].vp);
    hires_secs += now() - t0;
}

/* The app's start_game (src/app.c) and frame loop with the gasm "driving" key script: 70 Hz ticks, 315
   audio frames each (22050 / 70), keys held per tick. */
static RunResult run_mission(int n, int ticks)
{
    RunResult r = { 0 };
    memset(fb, 0, sizeof fb);
    GameOptions o;
    game_default_options(&o);
    game_set_options(&o);
    poly_set_screen_rows(fb, W * 4, H);
    poly_set_clip(0, 0, W - 1, H - 1);
    g_game.present = on_present;
    g_game.on_fatal = plat_log;
    game_set_screen(W, H);
    map_clear_name();
    run_n = n;
    if (!hires_init(n > 1 ? n : 0, fb, W, H)) return r;
    hud_pre_draw_hook = n > 1 ? on_hud : NULL;
    if (!mission_set_ini_section(1) || !game_run_begin()) return r;
    static uint8_t held[KEY_COUNT];
    for (int t = 0; t < ticks; t++) {
        memset(held, 0, sizeof held);
        if (t >= 100 && t < 160) held[0x14d] = 1;                 /* Right */
        if (t >= 170 && t < 175) held[0x1c] = 1;                  /* Enter: into the car */
        if (t >= 400 && t < 1100) held[0x148] = 1;                /* Up */
        if (t >= 430 && t < 480) held[0x14d] = 1;
        input_feed_held(held);
        audio_render(NULL, 315);
        if (game_run_step() == GAME_STEP_DONE) break;
    }
    r.state = state_hash();
    r.frames_crc = frames_crc;
    r.hires_crc = hires_crc;
    r.presents = presents;
    r.hires_ms = presents ? hires_secs * 1000 / presents : 0;
    game_run_end();
    return r;
}

/* each run in a fresh process: nothing carries over from one level start to the next */
static RunResult run_forked(int n, int ticks)
{
    RunResult r = { 0 };
    int fd[2];
    if (pipe(fd)) return r;
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        close(fd[0]);
        RunResult c = run_mission(n, ticks);
        ssize_t w = write(fd[1], &c, sizeof c);
        _exit(w == (ssize_t)sizeof c ? 0 : 1);
    }
    close(fd[1]);
    ssize_t got = read(fd[0], &r, sizeof r);
    close(fd[0]);
    int st;
    waitpid(pid, &st, 0);
    if (got != (ssize_t)sizeof r) memset(&r, 0, sizeof r);
    return r;
}

static void run_missions(void)
{
    const int ticks = getenv("HIRES_TICKS") ? atoi(getenv("HIRES_TICKS")) : 1150;
    RunResult a = run_forked(1, ticks), b = run_forked(2, ticks), c = run_forked(4, ticks), d = run_forked(1, ticks);
    printf("  hires=1: %d presents, state %08x, faithful frames %08x\n", a.presents, a.state, a.frames_crc);
    printf("  hires=1: %d presents, state %08x, faithful frames %08x (again)\n", d.presents, d.state, d.frames_crc);
    printf("  hires=2: %d presents, state %08x, faithful frames %08x, hires frames %08x, hires pass %.1f ms/frame\n",
           b.presents, b.state, b.frames_crc, b.hires_crc, b.hires_ms);
    printf("  hires=4: %d presents, state %08x, faithful frames %08x, hires frames %08x, hires pass %.1f ms/frame\n",
           c.presents, c.state, c.frames_crc, c.hires_crc, c.hires_ms);
    CHECK(a.presents > 300, "mission 1 ran only %d frames", a.presents);
    CHECK(a.state == d.state && a.frames_crc == d.frames_crc, "two hires=1 runs differ: the test isn't deterministic");
    CHECK(a.state == b.state && a.state == c.state, "the game state depends on the hires pass");
    CHECK(a.frames_crc == b.frames_crc && a.frames_crc == c.frames_crc, "the faithful frames depend on the hires pass");
    CHECK(a.presents == b.presents && a.presents == c.presents, "present counts differ");
}

int main(void)
{
    char err[256];
    if (!vfs_mount_default()) { printf("SKIP: no data root\n"); return 0; }
    if (!exe_init(err, sizeof err)) { printf("SKIP: %s\n", err); return 0; }
    math_init_tables();
    if (!camera_init_tables()) { printf("FAIL: camera tables\n"); return 1; }
    mkdir("out", 0755);
    mkdir("out/hires", 0755);
    poly_set_screen_rows(fb, W * 4, H);
    poly_set_clip(0, 0, W - 1, H - 1);
    printf("scenes (NYC):\n");
    if (!getenv("HIRES_TICKS")) run_scenes();
    printf("mission 1, key script, hires 1 / 2 / 4:\n");
    run_missions();
    printf(failures ? "FAIL (%d)\n" : "PASS\n", failures);
    return failures != 0;
}
