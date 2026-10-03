/* The hires HUD and frontend (src/render/hires/hires_hud.c, hires_front.c, hires_text.c, skin_ui.c;
   docs/hires.md "HUD and menus") against the real data:
   1. the HUD of mission 1 in the hud_test states (score, lives, wanted level, weapon and items, pager,
      subtitle, arrow, score popups, big message, pause, quit prompt) with hires=2 and 3 and without, each
      run in its own process: the game state and the faithful frames must not depend on the hires HUD;
      with the nearest filter every pixel the faithful HUD drew must come out as an N x N block of its
      colour (the layout is the faithful one times N); comparisons of the three UI filters go to
      out/hires_ui/hud_*.png;
   2. the frontend (intro movie, start menu, options, player select, city select, cutscene still,
      results, credits) with hires=2 and without: the faithful frames must not depend on it, the hires
      frame box-filtered to 640 x 480 must stay close to the faithful one; out/hires_ui/front_*.png;
   3. skins: a generated skin in out/hires_ui/skins/uitest (a glyph of F_MHEAD, a grey digit of SUB2, the
      picture F_UPPER) must show up, the grey glyph tinted by the palette it's drawn with.
     ./build/hires_ui_test            (data root: ./game or OPENGTA_DATA) */
#include "audio/audio.h"
#include "exe.h"
#include "font.h"
#include "front/front.h"
#include "front/images.h"
#include "game/event.h"
#include "game/game.h"
#include "game/gmath.h"
#include "game/mission.h"
#include "game/ped.h"
#include "game/player.h"
#include "hud/hud.h"
#include "hud/hud_internal.h"
#include "movie/intro.h"
#include "platform.h"
#include "png.h"
#include "render/camera.h"
#include "render/hires/hires.h"
#include "render/hires/hires_front.h"
#include "render/hires/hires_hud.h"
#include "render/hires/hires_skin.h"
#include "render/hires/hires_text.h"
#include "render/hires/skin_ui.h"
#include "render/poly.h"
#include "text.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum { W = 640, H = 480 };
static uint32_t fb[W * H], pre_hud[W * H];
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

static uint32_t xrgb_to_abgr(uint32_t c) { return 0xff000000u | (c & 0xff) << 16 | (c & 0xff00) | (c >> 16 & 0xff); }

/* rectangles of images side by side (each already at its final size), 8 grey columns between */
typedef struct { const uint32_t *px; int pitch, x, y; bool xrgb; int scale; } Panel;

static void write_panels(const char *path, const Panel *p, int np, int cw, int ch)
{
    const int gap = 8, ow = np * cw + (np - 1) * gap;
    uint32_t *o = malloc((size_t)ow * ch * 4);
    if (!o) return;
    for (int y = 0; y < ch; y++)
        for (int x = 0; x < ow; x++) {
            int k = x / (cw + gap), xx = x % (cw + gap);
            uint32_t c = 0xff808080u;
            if (xx < cw) {
                const Panel *q = &p[k];
                int s = q->scale ? q->scale : 1;
                c = q->px[(size_t)(q->y + y / s) * q->pitch + q->x + xx / s];
                if (q->xrgb) c = xrgb_to_abgr(c);
            }
            o[(size_t)y * ow + x] = c;
        }
    CHECK(png_write(path, o, ow, ch, PNG_ABGR), "write %s", path);
    printf("  %s\n", path);
    free(o);
}

/* ---------------------------------------------------------------- 1. the HUD */

static int hud_n;                      /* hires scale of this run (0: off) */
static uint32_t frames_crc;
static int presents;
static uint32_t *city;                 /* the hires frame before the HUD */
static double hud_secs;

static void on_hud(void)
{
    memcpy(pre_hud, fb, sizeof fb);
    if (!hud_n) return;
    hires_frame_begin(g_game.map, g_game.style, &g_players[g_player_local].vp);
    int w, h;
    const uint32_t *px = hires_pixels(&w, &h);
    memcpy(city, px, (size_t)w * h * 4);
    hires_hud_begin(g_game.style);
}

static void on_present(void *ctx)
{
    (void)ctx;
    presents++;
    frames_crc = frames_crc * 31 + crc32(fb, sizeof fb);
    if (hud_n) {
        double t0 = now();
        hires_hud_end();
        hud_secs += now() - t0;
    }
}

static void frames(int n)
{
    int calls = 0;
    while (n > 0 && calls++ < 20 * n + 100) {
        audio_render(NULL, 551);
        int r = game_run_step();
        if (r == GAME_STEP_FRAME) n--;
        if (r == GAME_STEP_DONE) break;
    }
}

/* With the nearest filter: of the faithful pixels the HUD changed, the share whose hires block isn't all
   that colour (blended arrows and the stretched popups differ by design). */
static double hud_mismatch(const uint32_t *hp, int n, long *changed)
{
    long bad = 0, ch = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            if (fb[y * W + x] == pre_hud[y * W + x]) continue;
            ch++;
            const uint32_t want = xrgb_to_abgr(fb[y * W + x]);
            bool ok = true;
            for (int j = 0; j < n && ok; j++)
                for (int i = 0; i < n && ok; i++) ok = (hp[(size_t)(y * n + j) * W * n + x * n + i] | 0xff000000u) == want;
            if (!ok && getenv("HUI_DEBUG") && bad < 12)
                printf("    mismatch (%d, %d): faithful %08x hires %08x before %08x\n", x, y, want,
                       hp[(size_t)(y * n) * W * n + x * n], xrgb_to_abgr(pre_hud[y * W + x]));
            bad += !ok;
        }
    *changed = ch;
    return ch ? (double)bad / ch : 0;
}

/* The recorded HUD replayed over the hires city with each filter; the nearest one checked. */
static void hud_shot(const char *name, int x0, int y0, int cw, int ch, double max_mismatch)
{
    if (!hud_n) return;
    const int n = hud_n, HW = W * n, HH = H * n;
    static const char *const FILTERS[3] = { "nearest", "bilinear", "scale" };
    uint32_t *out[3];
    for (int f = 0; f < 3; f++) {
        out[f] = malloc((size_t)HW * HH * 4);
        memcpy(out[f], city, (size_t)HW * HH * 4);
        hires_ui_set_filter(FILTERS[f]);
        HrTarget t = { out[f], HW, HH, HW };
        hires_ui_replay(&t, n);
    }
    hires_ui_set_filter("scale");
    long changed;
    double mm = hud_mismatch(out[0], n, &changed);
    printf("  %s (%dx): %d draws recorded, %ld HUD pixels, %.2f%% not matched by the nearest replay\n", name, n,
           hires_ui_count(), changed, mm * 100);
    CHECK(changed > 0, "%s: the faithful HUD drew nothing", name);
    CHECK(mm <= max_mismatch, "%s: %.2f%% of the HUD pixels differ at nearest (max %.2f%%)", name, mm * 100,
          max_mismatch * 100);
    char path[128];
    snprintf(path, sizeof path, "out/hires_ui/hud_%s_%dx.png", name, n);
    Panel p[4] = { { fb, W, x0, y0, true, n }, { out[0], HW, x0 * n, y0 * n, false, 1 },
                   { out[1], HW, x0 * n, y0 * n, false, 1 }, { out[2], HW, x0 * n, y0 * n, false, 1 } };
    write_panels(path, p, 4, cw * n, ch * n);
    if (n == 2 && !strcmp(name, "state")) {
        snprintf(path, sizeof path, "out/hires_ui/hud_%s_%dx_full.png", name, n);
        Panel q[2] = { { fb, W, 0, 0, true, n }, { out[2], HW, 0, 0, false, 1 } };
        write_panels(path, q, 2, HW, HH);
    }
    for (int f = 0; f < 3; f++) free(out[f]);
}

typedef struct { uint32_t state, frames_crc; int presents; double hud_ms; } HudResult;

static uint32_t state_hash(void)
{
    uint32_t h = crc32(g_rng, sizeof g_rng) ^ g_frame * 0x9e3779b9u;
    h = h * 31 + crc32(g_peds, sizeof g_peds);
    h = h * 31 + crc32(g_players, sizeof g_players);
    h = h * 31 + crc32(g_pager.slot, sizeof g_pager.slot);
    return h;
}

/* hud_test's states, with hires scale n (0: off) */
static HudResult hud_run(int n)
{
    HudResult r = { 0 };
    hud_n = n;
    memset(fb, 0, sizeof fb);
    poly_set_screen_rows(fb, W * 4, H);
    poly_set_clip(0, 0, W - 1, H - 1);
    game_set_screen(W, H);
    GameOptions o;
    game_default_options(&o);
    game_set_options(&o);
    g_game.present = on_present;
    g_game.on_fatal = plat_log;
    if (n && (!hires_init(n, fb, W, H) || !(city = malloc((size_t)W * n * H * n * 4)))) return r;
    hud_pre_draw_hook = on_hud;
    map_clear_name();
    map_set_name(NULL, 2);
    if (!mission_set_ini_section(1) || !game_run_begin()) return r;

    frames(30);
    hud_shot("start", 320, 0, 320, 120, 0.02);
    Player *p = &g_players[0];
    p->score = 12345, p->mult = 3, p->wanted_level = 3, p->weapon = 1, p->ammo[0] = 25;
    p->armour = 3, p->jail_free = 1, p->speedup = 100, p->timers[1] = 25 * 42;
    pager_add_message("WELCOME TO LIBERTY CITY");
    hud_show_subtitle(4, text_get("1002") ? text_get("1002") : "Hello");
    const Ped *d = ped_get(p->ped);
    hud_arrow_to_pos((d->spr.x >> 16) + 400, (d->spr.y >> 16) - 300, d->spr.z >> 16);
    frames(30);
    /* blended HUD sprites (the arrows, the marker over the player) mix with the hires city below: not exact */
    hud_shot("state", 0, 0, 320, 160, 0.03);
    hud_shot("subtitle", 0, 400, 640, 80, 0.03);
    hud_add_score_popup(d->spr.x, d->spr.y, d->spr.z, 5000, 0);
    frames(2);
    hud_shot("popup", 220, 160, 200, 120, 0.25);   /* the popup digits are stretched by Poly_DrawRect */
    hud_show_big_message(text_get("2500"), 1);
    frames(2);
    hud_shot("big", 120, 60, 400, 140, 0.03);
    hud_pause_on();
    frames(2);
    hud_shot("pause", 160, 160, 320, 160, 0.03);
    hud_pause_off();
    hud_toggle_quit_prompt();
    frames(2);
    hud_shot("quit", 120, 180, 400, 120, 0.03);
    hud_toggle_quit_prompt();
    frames(20);
    r.state = state_hash();
    r.frames_crc = frames_crc;
    r.presents = presents;
    r.hud_ms = presents ? hud_secs * 1000 / presents : 0;
    game_run_end();
    return r;
}

static bool forked(void *res, size_t size, void (*fn)(void *res, int arg), int arg)
{
    int fd[2];
    if (pipe(fd)) return false;
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        close(fd[0]);
        fn(res, arg);
        fflush(stdout);
        ssize_t w = write(fd[1], res, size);
        _exit(w == (ssize_t)size ? 0 : 1);
    }
    close(fd[1]);
    ssize_t got = read(fd[0], res, size);
    close(fd[0]);
    int st;
    waitpid(pid, &st, 0);
    if (got != (ssize_t)size) memset(res, 0, size);
    return got == (ssize_t)size;
}

static void hud_child(void *res, int n) { *(HudResult *)res = hud_run(n); }

static void test_hud(void)
{
    printf("HUD:\n");
    HudResult a, b, c;
    forked(&a, sizeof a, hud_child, 0);
    forked(&b, sizeof b, hud_child, 2);
    forked(&c, sizeof c, hud_child, 3);
    printf("  off: %d presents, state %08x, faithful frames %08x\n", a.presents, a.state, a.frames_crc);
    printf("  2x:  %d presents, state %08x, faithful frames %08x, hires HUD %.2f ms/frame\n", b.presents, b.state,
           b.frames_crc, b.hud_ms);
    printf("  3x:  %d presents, state %08x, faithful frames %08x, hires HUD %.2f ms/frame\n", c.presents, c.state,
           c.frames_crc, c.hud_ms);
    CHECK(a.presents > 50, "the HUD run presented %d frames", a.presents);
    CHECK(a.state == b.state && a.state == c.state, "the game state depends on the hires HUD");
    CHECK(a.frames_crc == b.frames_crc && a.frames_crc == c.frames_crc, "the faithful frames depend on the hires HUD");
}

/* ---------------------------------------------------------------- 2. the frontend */

static Surface scr;
static uint8_t held[KEY_COUNT];
static uint64_t front_hash;
static int front_n;
static const uint32_t *front_px;
static double movie_secs;
static bool front_skinned;   /* skin shots: not compared with the faithful frame */
static int movie_frames;

static uint64_t fnv(const void *p, size_t n, uint64_t h)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 0x100000001b3ull;
    return h;
}

static FrontStep front_tick(Front *f, uint16_t key)
{
    FrontInput in = { &key, key != 0, held };
    bool movie = movie_intro_playing();
    double t0 = now();
    hires_front_begin();
    FrontStep r = front_frame(f, &in, &scr);
    int w, h;
    front_px = hires_front_end(&w, &h);
    if (movie && front_px) movie_secs += now() - t0, movie_frames++;
    front_hash = fnv(scr.px, (size_t)W * H * 4, front_hash);
    return r;
}

static FrontStep front_press(Front *f, uint16_t key, int idle)
{
    FrontStep r = front_tick(f, key);
    for (int i = 0; i < idle && r.code == FRONT_CONTINUE; i++) r = front_tick(f, 0);
    return r;
}

/* the faithful frontend frame next to the hires one (full, and a crop with the three filters) */
static void front_shot(const char *name, int x0, int y0, int cw, int ch)
{
    if (!front_n || !front_px) return;
    const int n = front_n, HW = W * n, HH = H * n;
    double sum = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int acc[3] = { 0, 0, 0 };
            for (int j = 0; j < n; j++)
                for (int i = 0; i < n; i++) {
                    uint32_t c = front_px[(size_t)(y * n + j) * HW + x * n + i];
                    acc[0] += c & 0xff, acc[1] += c >> 8 & 0xff, acc[2] += c >> 16 & 0xff;
                }
            uint32_t f = scr.px[y * W + x];
            for (int k = 0; k < 3; k++) sum += abs(acc[k] / (n * n) - (int)(f >> (8 * k) & 0xff));
        }
    double err = sum / (3.0 * W * H);
    printf("  %s (%dx): %d draws, mean difference to the faithful frame %.2f\n", name, n, hires_ui_count(), err);
    CHECK(front_skinned || err < 6.0, "%s: the hires frontend is far from the faithful one (%.2f)", name, err);
    char path[128];
    snprintf(path, sizeof path, "out/hires_ui/front_%s_%dx.png", name, n);
    Panel p[2] = { { scr.px, W, 0, 0, false, n }, { front_px, HW, 0, 0, false, 1 } };
    write_panels(path, p, 2, HW, HH);
    if (cw <= 0) return;
    static const char *const FILTERS[3] = { "nearest", "bilinear", "scale" };
    uint32_t *out[3];
    for (int f = 0; f < 3; f++) {
        out[f] = calloc((size_t)HW * HH, 4);
        hires_ui_set_filter(FILTERS[f]);
        HrTarget t = { out[f], HW, HH, HW };
        hires_ui_replay(&t, n);
    }
    hires_ui_set_filter("scale");
    snprintf(path, sizeof path, "out/hires_ui/front_%s_%dx_filters.png", name, n);
    Panel q[4] = { { scr.px, W, x0, y0, false, n }, { out[0], HW, x0 * n, y0 * n, false, 1 },
                   { out[1], HW, x0 * n, y0 * n, false, 1 }, { out[2], HW, x0 * n, y0 * n, false, 1 } };
    write_panels(path, q, 4, cw * n, ch * n);
    for (int f = 0; f < 3; f++) free(out[f]);
}

enum { K_ESC = 0x01, K_ENTER = 0x1c, K_SPACE = 0x39, K_DOWN = 0x150, K_RIGHT = 0x14d };

typedef struct { uint64_t hash; int frames; double movie_ms; } FrontResult;

static FrontResult front_run(int n)
{
    FrontResult r = { 0 };
    front_n = n;
    text_init_language(TEXT_ENGLISH);
    scr = (Surface){ calloc(W * H, 4), W, H, W };
    front_hash = 0xcbf29ce484222325ull;
    if (n && !hires_front_init(n)) return r;
    movie_intro_set_enabled(true);
    static Front f;
    f.net_active = true;
    if (!front_init(&f)) { printf("  front_init: %s\n", f.error); return r; }
    int k = 0;
    for (; k < 60 && movie_intro_playing(); k++) front_tick(&f, 0);
    front_shot("movie", 0, 0, 0, 0);
    front_press(&f, K_SPACE, 1);   /* ends the movie */
    front_press(&f, 0, 30);
    front_shot("start", 160, 200, 320, 140);
    front_press(&f, K_DOWN, 0), front_press(&f, K_DOWN, 0), front_press(&f, K_DOWN, 0);
    front_press(&f, K_ENTER, 4);
    front_shot("options", 0, 0, 0, 0);
    front_press(&f, K_ESC, 2);
    front_press(&f, K_ENTER, 6);
    front_shot("players", 200, 180, 240, 180);
    front_press(&f, K_ENTER, 6);
    front_shot("main", 0, 120, 320, 200);
    FrontStep st = front_press(&f, K_ENTER, 4);
    if (st.code == FRONT_PLAY) {
        FrontGameResult res = { 0 };
        res.reason = 1;
        res.score[0] = res.text_score = 4321;
        for (int i = 1; i < 4; i++) res.score[i] = -1;
        res.mission_total = 12, res.secret_total = 5;
        snprintf(res.text, sizeof res.text, "%s", text_get("2500") ? text_get("2500") : "MISSION COMPLETE!");
        if (front_game_over(&f, &res)) {
            front_press(&f, 0, 100);
            front_shot("cutscene", 0, 360, 640, 120);
            front_press(&f, K_ENTER, 6);
            front_shot("results", 0, 0, 0, 0);
            front_press(&f, K_ENTER, 6);
        }
    }
    front_press(&f, K_ESC, 2);
    front_press(&f, K_ESC, 2);
    front_press(&f, K_ESC, 0);
    for (int i = 0; i < 200; i++) front_tick(&f, 0);
    front_shot("credits", 0, 192, 320, 160);
    r.hash = front_hash;
    r.frames = k;
    r.movie_ms = movie_frames ? movie_secs * 1000 / movie_frames : 0;
    front_shutdown(&f);
    hires_front_init(0);
    return r;
}

static void front_child(void *res, int n) { *(FrontResult *)res = front_run(n); }

static void test_front(void)
{
    printf("frontend:\n");
    FrontResult a, b, c;
    forked(&a, sizeof a, front_child, 0);
    forked(&b, sizeof b, front_child, 2);
    forked(&c, sizeof c, front_child, 4);
    printf("  off: faithful frames %016llx\n", (unsigned long long)a.hash);
    printf("  2x:  faithful frames %016llx, a movie frame %.2f ms\n", (unsigned long long)b.hash, b.movie_ms);
    printf("  4x:  faithful frames %016llx, a movie frame %.2f ms\n", (unsigned long long)c.hash, c.movie_ms);
    CHECK(a.hash == b.hash && a.hash == c.hash, "the faithful frontend depends on the hires frontend");
    CHECK(a.frames > 10, "the movie played %d frames", a.frames);
}

/* ---------------------------------------------------------------- 3. skins */

/* an RGBA PNG (colour type 6, stored deflate) */
static bool png_write_rgba(const char *path, const uint32_t *px, int w, int h)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ihdr[13] = { (uint8_t)(w >> 24), (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w,
                         (uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h, 8, 6, 0, 0, 0 };
    png_chunk_(f, "IHDR", ihdr, 13);
    size_t row = 1 + 4 * (size_t)w, raw_n = (size_t)h * row;
    uint8_t *raw = malloc(raw_n);
    for (int y = 0; y < h; y++) {
        raw[y * row] = 0;
        memcpy(raw + y * row + 1, px + (size_t)y * w, (size_t)w * 4);   /* R, G, B, A bytes */
    }
    size_t nblk = (raw_n + 65534) / 65535, zn = 2 + raw_n + nblk * 5 + 4;
    uint8_t *z = malloc(zn), *o = z;
    *o++ = 0x78, *o++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_n; i++) a = (a + raw[i]) % 65521, b = (b + a) % 65521;
    size_t p = 0;
    do {
        size_t n = raw_n - p < 65535 ? raw_n - p : 65535;
        *o++ = p + n == raw_n;
        *o++ = (uint8_t)n, *o++ = (uint8_t)(n >> 8), *o++ = (uint8_t)~n, *o++ = (uint8_t)(~n >> 8);
        memcpy(o, raw + p, n), o += n, p += n;
    } while (p < raw_n);
    uint32_t ad = b << 16 | a;
    *o++ = (uint8_t)(ad >> 24), *o++ = (uint8_t)(ad >> 16), *o++ = (uint8_t)(ad >> 8), *o++ = (uint8_t)ad;
    png_chunk_(f, "IDAT", z, (size_t)(o - z));
    png_chunk_(f, "IEND", NULL, 0);
    bool ok = !ferror(f);
    fclose(f);
    free(raw);
    free(z);
    return ok;
}

static uint8_t *test_reader(const char *path, size_t *size)
{
    char p[320];
    snprintf(p, sizeof p, "out/hires_ui/%s", path);
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(n > 0 ? (size_t)n : 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    if (b) *size = (size_t)n;
    return b;
}

static void make_skin(void)
{
    mkdir("out/hires_ui/skins", 0755);
    mkdir("out/hires_ui/skins/uitest", 0755);
    mkdir("out/hires_ui/skins/uitest/font", 0755);
    mkdir("out/hires_ui/skins/uitest/font/F_MHEAD", 0755);
    mkdir("out/hires_ui/skins/uitest/font/SUB2", 0755);
    mkdir("out/hires_ui/skins/uitest/pictures", 0755);
    FILE *f = fopen("out/hires_ui/skins/uitest/skin.ini", "w");
    if (f) fputs("name = hires_ui_test\nauthor = tests/hires_ui_test.c (generated, no game data)\nscale = 4\n", f), fclose(f);
    static uint32_t px[256 * 256];
    /* 'a' (97) of F_MHEAD: a pure red triangle on transparency */
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++) px[y * 128 + x] = abs(x - 64) * 2 < y ? 0xff0000ffu : 0;
    png_write_rgba("out/hires_ui/skins/uitest/font/F_MHEAD/97.png", px, 128, 128);
    /* '1' (49) of SUB2: a grey ring (tinted) */
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            int d2 = (x - 32) * (x - 32) + (y - 32) * (y - 32);
            px[y * 64 + x] = d2 < 900 && d2 > 400 ? 0xffc0c0c0u : 0;
        }
    png_write_rgba("out/hires_ui/skins/uitest/font/SUB2/49.png", px, 64, 64);
    /* F_UPPER: a blue-green gradient, 256 x 68 (stretched over 640 x 168) */
    for (int y = 0; y < 68; y++)
        for (int x = 0; x < 256; x++) px[y * 256 + x] = 0xff000000u | (uint32_t)(x) << 16 | (uint32_t)(y * 3) << 8;
    png_write_rgba("out/hires_ui/skins/uitest/pictures/F_UPPER.png", px, 256, 68);
}

static void skin_child(void *res, int n)
{
    int *ok = res;
    *ok = 0;
    hires_skin_reader = test_reader;
    make_skin();
    if (hires_skins_load("uitest", n, plat_log) != 1) { printf("  the test skin didn't load\n"); return; }
    text_init_language(TEXT_ENGLISH);
    scr = (Surface){ calloc(W * H, 4), W, H, W };
    front_n = n;
    if (!hires_front_init(n)) return;
    movie_intro_set_enabled(false);
    static Front f;
    f.net_active = true;
    if (!front_init(&f)) return;
    front_skinned = true;
    front_press(&f, 0, 40);   /* the start menu ("PLAY" in F_MHEAD) */
    int red = 0;
    for (int i = 0; i < W * n * H * n; i++) red += (front_px[i] & 0xffffff) == 0x0000ff;
    printf("  skin: %d pure red hires pixels (the F_MHEAD 'a' of \"Play\" replaced)\n", red);
    *ok |= red > 50 ? 1 : 0;
    front_shot("skin_start", 0, 0, 0, 0);
    front_press(&f, K_DOWN, 0), front_press(&f, K_DOWN, 0), front_press(&f, K_DOWN, 0);
    front_press(&f, K_ENTER, 4);   /* options: drawn with the animated logo; Esc back */
    front_press(&f, K_ESC, 2);
    front_press(&f, K_ENTER, 6);   /* player select */
    front_press(&f, K_ENTER, 6);   /* city select: MMISS / MTEXT / MHEAD texts */
    /* F_UPPER is drawn by the results screen (front_draw_background(s, 0, ...)) */
    FrontStep st = front_press(&f, K_ENTER, 4);
    if (st.code == FRONT_PLAY) {
        FrontGameResult res = { 0 };
        res.reason = 7;   /* abandoned: straight to the results */
        for (int i = 1; i < 4; i++) res.score[i] = -1;
        if (front_game_over(&f, &res)) {
            front_press(&f, 0, 8);
            uint32_t c = front_px[(size_t)(84 * n) * W * n + 320 * n];   /* the middle of F_UPPER */
            printf("  skin: F_UPPER middle pixel %08x (gradient: ~ff%02x..%02x00)\n", c, 128, 126);
            *ok |= (c & 0xff) < 16 && (c >> 16 & 0xff) > 100 ? 2 : 0;
            front_shot("skin_results", 0, 0, 0, 0);
        }
    }
    /* the grey '1' of SUB2 tinted with two palettes */
    char err[128];
    if (font_hud_load(2, err, sizeof err)) {
        const Font *sub = hud_fonts.sub;
        int i = '1' - sub->first;
        static uint32_t red_pal[256], green_pal[256];
        for (int e = 0; e < 256; e++) red_pal[e] = surface_rgb(200, 0, 0), green_pal[e] = surface_rgb(0, 200, 0);
        const uint32_t *a = skin_ui_glyph(sub, '1', sub->glyph[i].px, sub->glyph[i].w, sub->height, red_pal, 1, false,
                                          hires_ui_clut_hash(red_pal, 1), n);
        const uint32_t *b = skin_ui_glyph(sub, '1', sub->glyph[i].px, sub->glyph[i].w, sub->height, green_pal, 1, false,
                                          hires_ui_clut_hash(green_pal, 1), n);
        uint32_t ca = 0, cb = 0;
        for (int k = 0; a && b && k < sub->glyph[i].w * n * sub->height * n; k++) {
            if (a[k] >> 24 == 0xff) ca = a[k];
            if (b[k] >> 24 == 0xff) cb = b[k];
        }
        printf("  skin: grey SUB2 '1' tinted %08x (red palette), %08x (green palette)\n", ca, cb);
        *ok |= a && b && (ca & 0xff) > 100 && !(ca >> 8 & 0xff) && (cb >> 8 & 0xff) > 100 && !(cb & 0xff) ? 4 : 0;
    }
    front_shutdown(&f);
}

static void test_skins(void)
{
    printf("skins:\n");
    int ok = 0;
    forked(&ok, sizeof ok, skin_child, 2);
    CHECK(ok & 1, "the skin's F_MHEAD glyph wasn't drawn");
    CHECK(ok & 2, "the skin's F_UPPER picture wasn't drawn");
    CHECK(ok & 4, "the grey skin glyph wasn't tinted by the palette");
}

int main(void)
{
    char err[256];
    if (!vfs_mount_default()) { printf("SKIP: no data root\n"); return 0; }
    if (!exe_init(err, sizeof err)) { printf("SKIP: %s\n", err); return 0; }
    math_init_tables();
    if (!camera_init_tables()) { printf("FAIL: camera tables\n"); return 1; }
    text_init_language(TEXT_ENGLISH);
    mkdir("out", 0755);
    mkdir("out/hires_ui", 0755);
    test_hud();
    test_front();
    test_skins();
    printf(failures ? "hires_ui_test: %d FAILED\n" : "hires_ui_test: ok\n", failures);
    return failures != 0;
}
