/* Performance of the hires renderer (src/render/hires, docs/hires.md "Performance"), natively:
   mission 1 played by the gasm "driving" key script (tools/screenshots.sh; the same script as
   tests/hires_test.c) at hires = 2, 3 and 4, each in its own process, timing the stages of a frame:
   the game step without the hires pass (simulation + faithful renderer + HUD), the hires city pass
   (hires_frame_begin) and the HUD layer (hires_frame_end); plus the rasteriser's counters (pixels
   written per mode: the overdraw).

   The hires frames of a few presents are kept as references in out/hires/perf/ (raw RGBA): run with
   HIRES_PERF_REF=1 to write them, later runs compare against them (pixels that differ, largest channel
   difference), so an optimisation can be checked for changing the image. Also writes the upscaler
   comparisons (out/hires/upscale_*.png) and times the upscalers.
     ./build/hires_perf_test                 HIRES_TICKS=n (default 1150), HIRES_SCALES=34 (default 234) */
#include "audio/audio.h"
#include "exe.h"
#include "game/game.h"
#include "game/gmath.h"
#include "game/input.h"
#include "game/mission.h"
#include "game/player.h"
#include "hud/hud.h"
#include "platform.h"
#include "png.h"
#include "render/camera.h"
#include "render/hires/hires.h"
#include "render/hires/hires_raster.h"
#include "render/hires/hires_tex.h"
#include "render/hires/hires_upscale.h"
#include "render/poly.h"
#include "style.h"
#include "vfs_host.h"
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

/* ---- mission 1 ---- */

typedef struct {
    int presents;
    double step_ms, city_ms, end_ms;          /* per presented frame */
    double px_opaque, px_keyed, px_blend;     /* per frame, in screen sizes */
    double px_columns, line_texels, spans;    /* per frame: screens, screens, count */
    int tiles, sprites;                       /* converted textures held at the end */
    double tex_mb;
    long ref_px_diff, ref_px;                 /* pixels differing from the references, pixels compared */
    int ref_max, refs;                        /* largest channel difference, references compared */
    uint32_t hires_crc;
} Perf;

static int cur_n, presents;
static double city_s, end_s, test_s;   /* test_s: the test's own work (CRCs, references) */
static Perf *cur;
static const int shots[] = { 30, 140, 250, 320 };

/* FNV-1a over words (the CRC of exe.h is byte-wise and would dominate the timings' wall clock) */
static uint32_t frame_hash(const uint32_t *p, size_t n)
{
    uint32_t h = 0x811c9dc5u;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x01000193u;
    return h;
}

static void ref_path(char *p, size_t cap, int n, int shot) { snprintf(p, cap, "out/hires/perf/ref_%dx_%03d.rgba", n, shot); }

static void check_ref(const uint32_t *px, int w, int h, int shot)
{
    char path[128];
    ref_path(path, sizeof path, cur_n, shot);
    const size_t bytes = (size_t)w * h * 4;
    if (getenv("HIRES_PERF_REF")) {
        FILE *f = fopen(path, "wb");
        if (f) fwrite(px, 1, bytes, f), fclose(f);
        return;
    }
    FILE *f = fopen(path, "rb");
    if (!f) return;
    uint32_t *ref = malloc(bytes);
    if (ref && fread(ref, 1, bytes, f) == bytes) {
        cur->refs++;
        for (size_t i = 0; i < (size_t)w * h; i++) {
            if (px[i] == ref[i]) continue;
            cur->ref_px_diff++;
            for (int k = 0; k < 24; k += 8) {
                int d = abs((int)(px[i] >> k & 0xff) - (int)(ref[i] >> k & 0xff));
                if (d > cur->ref_max) cur->ref_max = d;
            }
        }
        cur->ref_px += (long)w * h;
        if (shot == 140) {   /* the differences as an image: grey = same, red = differs */
            uint32_t *o = malloc(bytes);
            if (o) {
                for (size_t i = 0; i < (size_t)w * h; i++)
                    o[i] = px[i] == ref[i] ? 0xff000000u | ((px[i] >> 2) & 0x3f3f3f) : 0xff0000ffu;
                snprintf(path, sizeof path, "out/hires/perf/diff_%dx_%03d.png", cur_n, shot);
                png_write(path, o, w, h, PNG_ABGR);
                free(o);
            }
        }
    }
    free(ref);
    fclose(f);
}

static const char *cur_upscale;

/* The middle of the hires frame (the faithful 160 x 120 around the player), for the upscaler comparisons */
static void write_middle(const uint32_t *px, int w, int h, int shot)
{
    const int cw = w / 4, ch = h / 4, x0 = (w - cw) / 2, y0 = (h - ch) / 2;
    uint32_t *o = malloc((size_t)cw * ch * 4);
    if (!o) return;
    for (int y = 0; y < ch; y++) memcpy(o + (size_t)y * cw, px + (size_t)(y0 + y) * w + x0, (size_t)cw * 4);
    char path[128];
    snprintf(path, sizeof path, "out/hires/upscale_mission_%03d_%s.png", shot, cur_upscale);
    png_write(path, o, cw, ch, PNG_ABGR);
    free(o);
}

static void on_present(void *ctx)
{
    (void)ctx;
    presents++;
    double t0 = now();
    hires_frame_end();
    end_s += now() - t0;
    int w, h;
    const uint32_t *hp = hires_pixels(&w, &h);
    double t1 = now();
    cur->hires_crc = cur->hires_crc * 31 + frame_hash(hp, (size_t)w * h);
    for (unsigned i = 0; i < sizeof shots / sizeof *shots; i++)
        if (presents == shots[i]) {
            check_ref(hp, w, h, shots[i]);
            if (cur_upscale && cur_n == 4) write_middle(hp, w, h, shots[i]);
        }
    test_s += now() - t1;
}

static void on_hud(void)
{
    double t0 = now();
    hires_frame_begin(g_game.map, g_game.style, &g_players[g_player_local].vp);
    city_s += now() - t0;
}

/* tests/hires_test.c's run_mission: the app's start_game and its 70 Hz frame loop, the "driving" keys */
static Perf run_mission(int n, int ticks, const char *upscale)
{
    Perf r = { 0 };
    cur = &r;
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
    cur_n = n;
    cur_upscale = upscale;
    if (!hires_init(n, fb, W, H)) return r;
    if (upscale && !hires_upscale_set(upscale)) printf("  unknown upscaler %s\n", upscale);
    hud_pre_draw_hook = on_hud;
    if (!mission_set_ini_section(1) || !game_run_begin()) return r;
    static uint8_t held[KEY_COUNT];
    hr_stats = (HrStats){ 0 };
    double t0 = now();
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
    double total = now() - t0;
    r.presents = presents;
    hires_texture_stats(&r.tiles, &r.sprites);
    r.tex_mb = r.tiles * 64.0 * 64 * 4 * hires_upscale_factor() * hires_upscale_factor() / 1048576.0;
    if (presents) {
        const double f = 1000.0 / presents, scr = (double)W * H * n * n * presents;
        r.city_ms = city_s * f, r.end_ms = end_s * f, r.step_ms = (total - city_s - end_s - test_s) * f;
        r.px_opaque = hr_stats.px[HR_OPAQUE] / scr, r.px_keyed = hr_stats.px[HR_KEYED] / scr;
        r.px_blend = hr_stats.px[HR_BLEND] / scr;
        r.px_columns = hr_stats.px_columns / scr, r.line_texels = hr_stats.line_texels / scr;
        r.spans = (double)hr_stats.spans / presents;
    }
    game_run_end();
    return r;
}

static Perf run_forked(int n, int ticks, const char *upscale)
{
    Perf r = { 0 };
    int fd[2];
    if (pipe(fd)) return r;
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        close(fd[0]);
        Perf c = run_mission(n, ticks, upscale);
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

static void print_perf(int n, const char *what, const Perf *p)
{
    printf("  %dx%s: %d presents; per frame: game %.2f ms, hires city %.2f ms, HUD layer %.2f ms; "
           "pixels/screen: opaque %.2f, keyed %.2f, blended %.2f; hires frames %08x\n",
           n, what, p->presents, p->step_ms, p->city_ms, p->end_ms, p->px_opaque, p->px_keyed, p->px_blend, p->hires_crc);
    printf("        column spans %.2f screens, line texels %.3f screens, %.0f spans a frame; textures: %d tiles (%.1f MB), %d sprites\n",
           p->px_columns, p->line_texels, p->spans, p->tiles, p->tex_mb, p->sprites);
    if (p->refs)
        printf("        vs references (%d frames): %ld of %ld pixels differ (%.4f%%), largest difference %d\n", p->refs,
               p->ref_px_diff, p->ref_px, 100.0 * p->ref_px_diff / (double)p->ref_px, p->ref_max);
}

static void run_missions(void)
{
    const int ticks = getenv("HIRES_TICKS") ? atoi(getenv("HIRES_TICKS")) : 1150;
    const char *scales = getenv("HIRES_SCALES") ? getenv("HIRES_SCALES") : "234";
    mkdir("out/hires/perf", 0755);
    for (const char *c = scales; *c; c++) {
        int n = *c - '0';
        if (n < 1 || n > HIRES_MAX) continue;
        Perf p = run_forked(n, ticks, NULL);
        print_perf(n, "", &p);
        CHECK(p.presents > 300 || ticks < 1150, "mission 1 ran only %d frames", p.presents);
    }
    if (!getenv("HIRES_PERF_REF") && !getenv("HIRES_NO_UPSCALE")) {
        static const char *const ups[] = { "none", "scale2x", "scale4x", "xbr", "xbr4" };
        for (unsigned i = 0; i < sizeof ups / sizeof *ups; i++) {
            Perf p = run_forked(4, ticks, ups[i]);
            char what[32];
            snprintf(what, sizeof what, " upscale=%s", ups[i]);
            print_perf(4, what, &p);
        }
    }
}

/* ---- the upscalers on the start scene's tiles ---- */

/* A row of tiles: the original (nearest, 4x), bilinear (the renderer's filter, 4x), each upscaler's
   output drawn by the bilinear filter at 4x; into out/hires/upscale_<what>.png. */
static void upscale_comparisons(void)
{
    char err[256];
    Map *m = map_load("GTADATA/NYC.CMP", err, sizeof err);
    if (!m) { CHECK(0, "%s", err); return; }
    Style *s = style_load(m->style, err, sizeof err);
    if (!s) { CHECK(0, "%s", err); map_free(m); return; }
    style_convert_palettes(s, &PIXFMT_32);
    static const struct { const char *name; int kind; int n; } picks[] = {
        { "road", HIRES_LID, 1 }, { "pavement", HIRES_LID, 8 }, { "lid20", HIRES_LID, 20 }, { "lid60", HIRES_LID, 60 },
        { "side138", HIRES_SIDE, 138 }, { "side30", HIRES_SIDE, 30 }, { "side90", HIRES_SIDE, 90 },
    };
    static const char *const modes[] = { "none", "scale2x", "xbr" };
    enum { NM = 3, Z = 8, TW = 32 * Z, GAP = 4 };   /* the top left quarter of each tile, 8x */
    const int np = (int)(sizeof picks / sizeof *picks);
    const int ow = NM * (TW + GAP) + TW, oh = np * (TW + GAP);
    uint32_t *o = calloc((size_t)ow * oh, 4);
    if (!o) return;
    for (size_t i = 0; i < (size_t)ow * oh; i++) o[i] = 0xff404040u;
    static uint32_t tgt[4 * TW * TW];
    for (int p = 0; p < np; p++) {
        int t = picks[p].kind == HIRES_LID ? s->lid_base + picks[p].n : s->side_base + picks[p].n;
        const uint32_t *clut = picks[p].kind == HIRES_LID ? s->lid_clut[picks[p].n][0] : s->side_clut[picks[p].n][0];
        for (int k = 0; k <= NM; k++) {
            hires_textures_reset();
            CHECK(hires_upscale_set(k == 0 ? "none" : modes[k - 1 < 0 ? 0 : k - 1]), "upscale mode");
            HiresTexture *tex = hires_tile_original(s, t, clut);
            if (!tex) continue;
            HrTarget T = { tgt, 2 * TW, 2 * TW, 2 * TW };
            /* the whole tile on the square: an opaque face */
            hr_nearest = k == 0;
            hr_face_horiz(&T, 0, tex, 0, 2 * TW * HR_SUB, 0, 2 * TW * HR_SUB, 0, 2 * TW * HR_SUB);
            hr_nearest = false;
            for (int y = 0; y < TW; y++) memcpy(o + (size_t)(p * (TW + GAP) + y) * ow + k * (TW + GAP), tgt + y * 2 * TW, TW * 4);
        }
    }
    hires_upscale_set("none");
    CHECK(png_write("out/hires/upscale_tiles.png", o, ow, oh, PNG_ABGR), "write upscale_tiles.png");
    printf("  out/hires/upscale_tiles.png: columns original (nearest), bilinear, scale2x, xbr (each tile's top left quarter, 8x)\n");
    free(o);

    /* timing: every lid and side converted and upscaled */
    for (int k = 1; k < NM; k++) {
        hires_textures_reset();
        hires_upscale_set(modes[k]);
        double t0 = now();
        int cnt = 0;
        for (int t = s->side_base; t < s->aux_base && cnt < 2000; t++, cnt++) hires_tile_original(s, t, s->side_clut[0][0]);
        printf("  %s: %d tiles in %.1f ms (%.3f ms each)\n", modes[k], cnt, (now() - t0) * 1000, (now() - t0) * 1000 / cnt);
    }
    hires_upscale_set("none");
    hires_textures_reset();
    style_free(s);
    map_free(m);
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
    if (!getenv("HIRES_NO_UPSCALE")) {
        printf("upscalers:\n");
        upscale_comparisons();
    }
    printf("mission 1, key script, per stage:\n");
    run_missions();
    printf(failures ? "FAIL (%d)\n" : "PASS\n", failures);
    return failures != 0;
}
