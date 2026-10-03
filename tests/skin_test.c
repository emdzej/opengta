/* Skins over the hires renderer (src/render/hires/hires_skin.c, docs/skins.md) against the real data and
   the sample skin (assets/skins/sample, made by tools/make-sample-skin.py):
   - the PNG decoder: the sample's RGB, palette + tRNS and RGBA images (every row filter), a round trip
     through tests/png.h's writer, refusals;
   - the overlay stack: replaced tiles and sprites come from the skin, the rest from the original art; a
     second skin stacked on top wins where it has an image; remapped sprites recoloured through a mask;
     images box-filtered down at small scales;
   - renders: NYC scenes and mission 1's start with the sample skin at 2x and 1x -> PNGs in out/skins/.
     ./build/skin_test            (data root: ./game or OPENGTA_DATA) */
#include "exe.h"
#include "game/game.h"
#include "game/gmath.h"
#include "game/input.h"
#include "audio/audio.h"
#include "game/mission.h"
#include "game/player.h"
#include "hud/hud.h"
#include "map.h"
#include "png.h"
#include "render/camera.h"
#include "render/city.h"
#include "render/hires/hires.h"
#include "render/hires/hires_png.h"
#include "render/hires/hires_skin.h"
#include "render/hires/hires_tex.h"
#include "render/poly.h"
#include "render/sprite.h"
#include "style.h"
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

/* ---- files: skins/sample from assets/, skins/over from out/skins/over (made here) ---- */

static uint8_t *read_host(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(n > 0 ? (size_t)n : 1);
    if (d && fread(d, 1, (size_t)n, f) != (size_t)n) free(d), d = NULL;
    fclose(f);
    if (d && size) *size = (size_t)n;
    return d;
}

static int reads;
static uint8_t *skin_read(const char *rel, size_t *size)
{
    char p[512];
    reads++;
    if (!strncmp(rel, "skins/over/", 11)) snprintf(p, sizeof p, "out/skins/over/%s", rel + 11);
    else snprintf(p, sizeof p, "assets/%s", rel);
    return read_host(p, size);
}

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (f) fputs(text, f), fclose(f);
}

/* ---- PNG ---- */

static uint32_t *decode_file(const char *path, int *w, int *h, char *err)
{
    size_t n;
    uint8_t *d = read_host(path, &n);
    if (!d) { snprintf(err, 160, "missing"); return NULL; }
    uint32_t *px = hires_png_decode(d, n, w, h, err, 160);
    free(d);
    return px;
}

static void check_png(void)
{
    char err[160];
    int w, h;
    uint32_t *road = decode_file("assets/skins/sample/style001/lid/1.png", &w, &h, err);
    CHECK(road && w == 256 && h == 256, "road (RGB): %s", road ? "size" : err);
    if (road) {
        uint32_t c = road[100 * 256 + 20];   /* the dotted line: rows 96..119 of a 48-row period, x 18..29 */
        CHECK(c == 0xffd2c828u, "road line pixel %08x", c);
        CHECK(road[0] >> 24 == 0xff, "RGB is opaque");
    }
    uint32_t *pave = decode_file("assets/skins/sample/style001/lid/8.png", &w, &h, err);
    CHECK(pave && w == 256 && h == 256, "pavement (palette): %s", pave ? "size" : err);
    uint32_t *car = decode_file("assets/skins/sample/style001/sprite/72.png", &w, &h, err);
    CHECK(car && w == 248 && h == 256, "car (RGBA): %s", car ? "size" : err);
    if (car) CHECK(car[0] == 0 && car[128 * 248 + 124] >> 24 == 0xff, "car alpha: corner %08x, centre %08x", car[0], car[128 * 248 + 124]);
    printf("  decoded: RGB 256x256, palette+tRNS 256x256, RGBA 248x256\n");
    free(road), free(pave), free(car);

    /* round trip through the tests' writer (stored deflate, filter 0) */
    uint32_t img[37 * 23];
    for (int i = 0; i < 37 * 23; i++) img[i] = 0xff000000u | (uint32_t)(i * 2654435761u >> 8);
    CHECK(png_write("out/skins/roundtrip.png", img, 37, 23, PNG_ABGR), "write roundtrip");
    uint32_t *rt = decode_file("out/skins/roundtrip.png", &w, &h, err);
    CHECK(rt && w == 37 && h == 23 && !memcmp(rt, img, sizeof img), "round trip: %s", rt ? "pixels differ" : err);
    free(rt);

    /* refusals: 16-bit, interlaced, not a PNG */
    size_t n;
    uint8_t *d = read_host("out/skins/roundtrip.png", &n);
    if (d) {
        uint8_t *b = malloc(n);
        memcpy(b, d, n), b[24] = 16;
        uint32_t *x = hires_png_decode(b, n, &w, &h, err, sizeof err);
        CHECK(!x && strstr(err, "8 bits"), "16-bit refused: %s", err);
        printf("  16-bit: \"%s\"\n", err);
        memcpy(b, d, n), b[28] = 1;
        x = hires_png_decode(b, n, &w, &h, err, sizeof err);
        CHECK(!x && strstr(err, "interlaced"), "interlaced refused: %s", err);
        printf("  interlaced: \"%s\"\n", err);
        x = hires_png_decode((const uint8_t *)"GIF89a....", 10, &w, &h, err, sizeof err);
        CHECK(!x, "not a PNG refused");
        free(b), free(d);
    }
}

/* ---- the overlay stack ---- */

static void check_lookup(Style *s)
{
    const SpriteInfo *in72 = sprite_get_info(72), *in73 = sprite_get_info(73);
    const uint32_t *own72 = sprite_remap_clut(in72->clut, 0, 0);
    /* the sample alone, at 2x */
    CHECK(hires_skins_load("sample", 2, plat_log) == 1, "load sample");
    HiresTexture *road = hires_tile(s, s->lid_remap[1], s->lid_clut[1][0], 0, s->lid_clut[1][0]);
    HiresTexture *road_r2 = hires_tile(s, s->lid_remap[1], s->lid_clut[1][2], 2, s->lid_clut[1][0]);
    HiresTexture *lid9 = hires_tile(s, s->lid_remap[9], s->lid_clut[9][0], 0, s->lid_clut[9][0]);
    HiresTexture *wall = hires_tile(s, s->side_remap[138], s->side_clut[138][2], 2, s->side_clut[138][0]);
    HiresTexture *aux25 = hires_tile(s, s->aux_base + 25, s->aux_side_clut[0][0], 0, s->aux_side_clut[0][0]);
    HiresTexture *car = hires_sprite(s, 72, in72, own72, 0, 0, own72);
    const uint32_t *rem = sprite_remap_clut(in72->clut, 3, sprite_car_palette(2));
    HiresTexture *car_r = hires_sprite(s, 72, in72, rem, 0, 3, own72);
    HiresTexture *car73 = hires_sprite(s, 73, in73, sprite_remap_clut(in73->clut, 0, 0), 0, 0, sprite_remap_clut(in73->clut, 0, 0));
    CHECK(road && road->w == 256, "lid 1 from the skin (%d)", road ? road->w : 0);
    CHECK(road_r2 && road && road_r2->w == road->w, "lid 1 remap 2: no _r2 image, the plain one shaded like the remap");
    if (road_r2 && road) {
        unsigned long l0 = 0, l2 = 0;
        for (int i = 0; i < road->w * road->h; i++) l0 += road->rgba[i] >> 8 & 0xff, l2 += road_r2->rgba[i] >> 8 & 0xff;
        printf("  lid 1 remap 2: %s (mean green %lu -> %lu)\n", road_r2 == road ? "the plain image" : "the plain image shaded",
               l0 / (road->w * road->h), l2 / (road->w * road->h));
    }
    CHECK(lid9 && lid9->w == 64, "lid 9 falls back to the original (%d)", lid9 ? lid9->w : 0);
    CHECK(wall && wall->w == 256, "side 138 from the skin");
    CHECK(aux25 && aux25->w == 256, "aux 25 from the skin");
    CHECK(car && car->w == 248 && car->h == 256, "sprite 72 from the skin");
    CHECK(car73 && car73->w == in73->w, "sprite 73 falls back to the original");
    bool recoloured = car_r && car_r != car && car_r->w == car->w;
    if (recoloured) {
        uint32_t a = car->rgba[230 * 248 + 124], b = car_r->rgba[230 * 248 + 124], g = car->rgba[1 * 248 + 124];
        printf("  sprite 72 paint %08x, remap 3 of car record 2 %08x (glass/edge %08x)\n", a, b, g);
        recoloured = a != b;
    }
    CHECK(recoloured, "sprite 72 remapped: recoloured through its mask");
    int loaded, missing;
    hires_skin_stats(&loaded, &missing);
    printf("  sample at 2x: lid 1 %dx%d, side 138, aux 25, sprite 72 from the skin; lid 9 and sprite 73 original "
           "(%d images loaded, %d files missing)\n", road ? road->w : 0, road ? road->h : 0, loaded, missing);
    int r0 = reads;
    hires_tile(s, s->lid_remap[9], s->lid_clut[9][0], 0, s->lid_clut[9][0]);
    hires_tile(s, s->lid_remap[1], s->lid_clut[1][0], 0, s->lid_clut[1][0]);
    CHECK(reads == r0, "lookups are cached (%d more reads)", reads - r0);

    /* a second skin on top: its lid 1 wins, the sample still answers lid 8 */
    mkdir("out/skins/over", 0755);
    mkdir("out/skins/over/style001", 0755);
    mkdir("out/skins/over/style001/lid", 0755);
    write_file("out/skins/over/skin.ini", "name = test overlay\nauthor = skin_test\nscale = 2\n");
    static uint32_t solid[32 * 32];
    for (int i = 0; i < 32 * 32; i++) solid[i] = 0xff00ffffu;   /* yellow */
    png_write("out/skins/over/style001/lid/1.png", solid, 32, 32, PNG_ABGR);
    CHECK(hires_skins_load("sample,over", 2, plat_log) == 2, "load sample,over");
    HiresTexture *t1 = hires_tile(s, s->lid_remap[1], s->lid_clut[1][0], 0, s->lid_clut[1][0]), *t8 = hires_tile(s, s->lid_remap[8], s->lid_clut[8][0], 0, s->lid_clut[8][0]);
    CHECK(t1 && t1->w == 32 && t8 && t8->w == 256, "stack sample,over: lid 1 from over, lid 8 from sample");
    CHECK(hires_skins_load("over,sample", 2, plat_log) == 2, "load over,sample");
    t1 = hires_tile(s, s->lid_remap[1], s->lid_clut[1][0], 0, s->lid_clut[1][0]);
    CHECK(t1 && t1->w == 256, "stack over,sample: lid 1 from sample");
    printf("  stacks: sample,over -> lid 1 %s; over,sample -> lid 1 from sample\n", "from over (32x32)");

    /* 1x: the 256-texel images are box-filtered to 128 */
    CHECK(hires_skins_load("sample", 1, plat_log) == 1, "load at 1x");
    t1 = hires_tile(s, s->lid_remap[1], s->lid_clut[1][0], 0, s->lid_clut[1][0]);
    car = hires_sprite(s, 72, in72, own72, 0, 0, own72);
    CHECK(t1 && t1->w == 128 && car && car->w == 124, "1x: lid 1 %d, sprite 72 %d", t1 ? t1->w : 0, car ? car->w : 0);
    printf("  at 1x: lid 1 %dx%d, sprite 72 %dx%d\n", t1 ? t1->w : 0, t1 ? t1->h : 0, car ? car->w : 0, car ? car->h : 0);
    CHECK(hires_skins_load("nosuch", 2, plat_log) == 0 && hires_overlay_count() == 0, "a missing skin loads nothing");
    hires_skins_free();
}

/* ---- renders ---- */

static CameraWorld world = { .peds = true, .cars = true };

static void frame(CameraPlayer *p, const Map *m, Style *s, bool draw)
{
    style_update_anims(s);
    camera_update(p, &world);
    render_compute_visible_rect(&p->vp);
    render_copy_camera(&p->vp);
    if (draw) render_draw_city(m, s, &p->vp);
}

/* faithful scaled up | hires with the skin, side by side */
static void write_pair(const char *path, const uint32_t *hp, int n)
{
    const int gap = 8, ow = W * n * 2 + gap, oh = H * n;
    uint32_t *o = malloc((size_t)ow * oh * 4);
    for (int y = 0; y < oh; y++)
        for (int x = 0; x < ow; x++) {
            uint32_t c;
            if (x < W * n) {
                uint32_t f = fb[(y / n) * W + x / n];
                c = 0xff000000u | (f & 0xff) << 16 | (f & 0xff00) | (f >> 16 & 0xff);
            } else if (x < W * n + gap) c = 0xff808080u;
            else c = hp[(size_t)y * W * n + x - W * n - gap];
            o[(size_t)y * ow + x] = c;
        }
    CHECK(png_write(path, o, ow, oh, PNG_ABGR), "write %s", path);
    free(o);
    printf("  -> %s\n", path);
}

static void render_scenes(const Map *m, Style *s)
{
    static const struct { const char *name; int x, y, z; } sc[] = { { "start", 105, 119, 4 }, { "water", 60, 144, 5 } };
    char path[128];
    for (int i = 0; i < 2; i++)
        for (int n = 1; n <= 2; n++) {
            CameraPlayer p;
            memset(&p, 0, sizeof p);
            camera_set_viewport(&p, W, H);
            p.target_kind = CAM_TARGET_PED;
            p.target = (CameraTarget){ (sc[i].x * 64 + 32) << 16, (sc[i].y * 64 + 32) << 16, sc[i].z * 0x400000 - 0x10000, 8, 8, 0, 0 };
            camera_init(&p, &world);
            for (int k = 0; k < 300; k++) frame(&p, m, s, false);
            CHECK(hires_skins_load("sample", n, plat_log) == 1, "load");
            memset(fb, 0, sizeof fb);
            CHECK(hires_init(n, fb, W, H), "hires_init");
            frame(&p, m, s, true);
            hires_frame_begin(m, s, &p.vp);
            hires_frame_end();
            int w, h;
            const uint32_t *hp = hires_pixels(&w, &h);
            snprintf(path, sizeof path, "out/skins/%s_%dx.png", sc[i].name, n);
            write_pair(path, hp, n);
        }
    hires_init(0, NULL, 0, 0);
    hires_skins_free();
}

/* mission 1's start with the skin at 2x: the parked car (sprite 72) and the HUD over it */
static int presents;
static void on_present(void *ctx)
{
    (void)ctx;
    hires_frame_end();
    if (++presents == 40) {
        int w, h;
        write_pair("out/skins/mission1_2x.png", hires_pixels(&w, &h), 2);
    }
}
static void on_hud(void) { hires_frame_begin(g_game.map, g_game.style, &g_players[g_player_local].vp); }

static void render_mission(void)
{
    GameOptions o;
    game_default_options(&o);
    game_set_options(&o);
    memset(fb, 0, sizeof fb);
    poly_set_screen_rows(fb, W * 4, H);
    poly_set_clip(0, 0, W - 1, H - 1);
    g_game.present = on_present;
    g_game.on_fatal = plat_log;
    game_set_screen(W, H);
    map_clear_name();
    CHECK(hires_skins_load("sample", 2, plat_log) == 1, "load");
    CHECK(hires_init(2, fb, W, H), "hires_init");
    hud_pre_draw_hook = on_hud;
    if (!mission_set_ini_section(1) || !game_run_begin()) { CHECK(0, "mission 1 didn't start"); return; }
    static uint8_t held[KEY_COUNT];
    for (int t = 0; t < 130 && presents < 40; t++) {
        input_feed_held(held);
        audio_render(NULL, 315);
        if (game_run_step() == GAME_STEP_DONE) break;
    }
    CHECK(presents >= 40, "only %d frames", presents);
    game_run_end();
    hud_pre_draw_hook = NULL;
    hires_init(0, NULL, 0, 0);
    hires_skins_free();
}

int main(void)
{
    char err[256];
    if (!vfs_mount_default()) { printf("SKIP: no data root\n"); return 0; }
    if (!exe_init(err, sizeof err)) { printf("SKIP: %s\n", err); return 0; }
    uint8_t *ini = read_host("assets/skins/sample/skin.ini", NULL);
    free(ini);
    if (!ini) { printf("SKIP: no assets/skins/sample (tools/make-sample-skin.py)\n"); return 0; }
    math_init_tables();
    if (!camera_init_tables()) { printf("FAIL: camera tables\n"); return 1; }
    mkdir("out", 0755);
    mkdir("out/skins", 0755);
    hires_skin_reader = skin_read;
    poly_set_screen_rows(fb, W * 4, H);
    poly_set_clip(0, 0, W - 1, H - 1);
    printf("PNG decoder:\n");
    check_png();
    Map *m = map_load("GTADATA/NYC.CMP", err, sizeof err);
    Style *s = m ? style_load(m->style, err, sizeof err) : NULL;
    if (!s) { printf("FAIL: %s\n", err); return 1; }
    style_convert_palettes(s, &PIXFMT_32);
    printf("overlay stack (NYC, style %d):\n", s->number);
    check_lookup(s);
    printf("renders:\n");
    render_scenes(m, s);
    style_free(s);
    map_free(m);
    render_mission();
    printf(failures ? "FAIL (%d)\n" : "PASS\n", failures);
    return failures != 0;
}
