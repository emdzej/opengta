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
#include <sys/wait.h>

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

/* ---- damage and door deltas on a skin sprite (sprite 72: 11 deltas) ---- */

/* textures side by side on grey, each scaled to 256 rows (nearest) */
static void write_row(const char *path, HiresTexture *const *t, int n)
{
    const int H2 = 256, gap = 8;
    int ow = gap;
    for (int i = 0; i < n; i++) ow += (t[i] ? t[i]->w * H2 / t[i]->h : 0) + gap;
    uint32_t *o = malloc((size_t)ow * (H2 + 2 * gap) * 4);
    for (int i = 0; i < ow * (H2 + 2 * gap); i++) o[i] = 0xff606060u;
    int x0 = gap;
    for (int i = 0; i < n; i++) {
        if (!t[i]) continue;
        const int w = t[i]->w * H2 / t[i]->h;
        for (int y = 0; y < H2; y++)
            for (int x = 0; x < w; x++) {
                uint32_t c = t[i]->rgba[(size_t)(y * t[i]->h / H2) * t[i]->w + x * t[i]->w / w], a = c >> 24;
                uint32_t bg = 0x60, out = 0xff000000u;
                for (int ch = 0; ch < 3; ch++) out |= (((c >> (8 * ch) & 0xff) * a + bg * (255 - a)) / 255) << (8 * ch);
                o[(size_t)(y + gap) * ow + x0 + x] = out;
            }
        x0 += w + gap;
    }
    CHECK(png_write(path, o, ow, H2 + 2 * gap, PNG_ABGR), "write %s", path);
    free(o);
    printf("  -> %s\n", path);
}

static int differing(const HiresTexture *a, const HiresTexture *b)
{
    int n = 0;
    if (a->w != b->w || a->h != b->h) return -1;
    for (int i = 0; i < a->w * a->h; i++) n += a->rgba[i] != b->rgba[i];
    return n;
}

static void check_damage(Style *s)
{
    const SpriteInfo *in = sprite_get_info(72);
    const uint32_t *own = sprite_remap_clut(in->clut, 0, 0), *rem = sprite_remap_clut(in->clut, 3, sprite_car_palette(2));
    CHECK(in->ndeltas >= 10, "sprite 72 has %d deltas", in->ndeltas);
    /* derived from the original deltas */
    /* the original art with the same deltas, for the picture */
    hires_skins_free();
    HiresTexture *o_dmg = hires_sprite(s, 72, in, own, 0x3f, 0, own), *o_door = hires_sprite(s, 72, in, own, 1u << 9, 0, own);
    CHECK(hires_skins_load("sample", 2, plat_log) == 1, "load sample");
    HiresTexture *plain = hires_sprite(s, 72, in, own, 0, 0, own);
    HiresTexture *dmg = hires_sprite(s, 72, in, own, 0x3f, 0, own);
    HiresTexture *door = hires_sprite(s, 72, in, own, 1u << 9, 0, own);
    HiresTexture *dmg_r = hires_sprite(s, 72, in, rem, 0x3f, 3, own);
    HiresTexture *again = hires_sprite(s, 72, in, own, 0x3f, 0, own);
    int nd = plain && dmg ? differing(plain, dmg) : -1, nr = plain && door ? differing(plain, door) : -1;
    CHECK(plain && plain->w == 248, "skin sprite 72");
    CHECK(dmg && dmg != plain && nd > 0, "deltas 0-5 derived on the skin image (%d pixels changed)", nd);
    CHECK(door && door != plain && nr > 0, "delta 9 (door) derived (%d pixels changed)", nr);
    CHECK(again == dmg, "damaged textures are cached");
    CHECK(dmg_r && dmg_r != dmg, "a remapped car's damage is its own texture");
    printf("  derived deltas: damage 0-5 changes %d of %d pixels, door (delta 9) %d\n", nd, plain ? plain->w * plain->h : 0, nr);
    /* original damaged, original door open | skin, damaged, door open, damaged with remap 3 */
    HiresTexture *row[6] = { o_dmg, o_door, plain, dmg, door, dmg_r };
    write_row("out/skins/damage_2x.png", row, 6);
    /* a delta image in a skin on top: skins/over/style001/sprite/72_delta9.png (a red square) + a copy of
       the sample's sprite (so over answers sprite 72) */
    mkdir("out/skins/over/style001/sprite", 0755);
    size_t n;
    uint8_t *png = read_host("assets/skins/sample/style001/sprite/72.png", &n);
    if (png) {
        FILE *f = fopen("out/skins/over/style001/sprite/72.png", "wb");
        if (f) fwrite(png, 1, n, f), fclose(f);
        free(png);
    }
    static uint32_t ov[62 * 64];
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 62; x++) ov[y * 62 + x] = x >= 2 && x < 14 && y >= 20 && y < 40 ? 0xff2020e0u : 0;
    png_write("out/skins/over/style001/sprite/72_delta9.png", ov, 62, 64, PNG_ABGR);
    CHECK(hires_skins_load("sample,over", 2, plat_log) == 2, "load sample,over");
    HiresTexture *p2 = hires_sprite(s, 72, in, own, 0, 0, own), *door2 = hires_sprite(s, 72, in, own, 1u << 9, 0, own);
    uint32_t c = door2 ? door2->rgba[(30 * door2->h / 64) * door2->w + 8 * door2->w / 62] : 0;
    CHECK(door2 && c == 0xff2020e0u, "delta 9 from 72_delta9.png (pixel %08x)", c);
    CHECK(p2 && door2 && differing(p2, door2) > 0, "the delta image changes the sprite");
    printf("  72_delta9.png over the skin: pixel %08x\n", c);
    HiresTexture *row2[2] = { p2, door2 };
    write_row("out/skins/damage_image_2x.png", row2, 2);
    hires_skins_free();
}

/* ---- a remap index map: sprite/<n>_index.png recolours each pixel through the index it names ---- */

static void check_index(Style *s)
{
    /* ped sprite: a grey image whose left half names the sprite's most used index, the right half 0 */
    const int n = sprite_group_base(SPRITE_GROUP_PED);
    const SpriteInfo *in = sprite_get_info(n);
    int used[256] = { 0 }, e = 1;
    for (int v = 0; v < in->h; v++)
        for (int u = 0; u < in->w; u++) used[in->data[v * 256 + u]]++;
    const uint32_t *own = sprite_remap_clut(in->clut, 0, 0);
    int r = 1;
    const uint32_t *rem = NULL;
    for (int k = 1; k < 256; k++)
        if (k && used[k] > used[e]) e = k;
    /* a remap that changes that index */
    for (r = 1; r < 64; r++) {
        rem = sprite_remap_clut(in->clut, r, sprite_ped_palette());
        if ((rem[e * 64] & 0xffffff) != (own[e * 64] & 0xffffff)) break;
    }
    CHECK(r < 64, "a ped remap that changes index %d", e);
    const int w = in->w * 2, h = in->h * 2;
    uint32_t *img = malloc((size_t)w * h * 4), *ix = malloc((size_t)w * h * 4);
    for (int i = 0; i < w * h; i++) img[i] = 0xff808080u, ix[i] = 0xff000000u | (i % w < w / 2 ? (uint32_t)e : 0);
    char dir[128], path[192];
    snprintf(dir, sizeof dir, "out/skins/over/style001/sprite");
    mkdir(dir, 0755);
    snprintf(path, sizeof path, "%s/%d.png", dir, n);
    png_write(path, img, w, h, PNG_ABGR);
    snprintf(path, sizeof path, "%s/%d_index.png", dir, n);
    png_write(path, ix, w, h, PNG_ABGR);
    CHECK(hires_skins_load("over", 2, plat_log) == 1, "load over");
    HiresTexture *plain = hires_sprite(s, n, in, own, 0, 0, own), *t = hires_sprite(s, n, in, rem, 0, r, own);
    CHECK(plain && t && t != plain, "the remap is its own texture");
    if (plain && t) {
        uint32_t left = t->rgba[(t->h / 2) * t->w + 1], right = t->rgba[(t->h / 2) * t->w + t->w - 2];
        /* left: the remap's colour of index e, scaled by the grey (0x80) against index e's own brightness */
        uint32_t o = own[e * 64] & 0xffffff, c = rem[e * 64] & 0xffffff;
        unsigned lo = (((o >> 16) & 0xff) * 77 + ((o >> 8) & 0xff) * 150 + (o & 0xff) * 29) / 256 + 1;
        int off = 0;
        for (int ch = 0; ch < 3; ch++) {   /* rgba: R in the low byte; c: 0xRRGGBB */
            unsigned want = ((c >> (16 - 8 * ch)) & 0xff) * 0x80 / lo;
            if (want > 255) want = 255;
            int d = abs((int)((left >> (8 * ch)) & 0xff) - (int)want);
            if (d > off) off = d;
        }
        CHECK(off <= 2, "index %d recoloured: %08x, %d off the remap's colour %06x shaded", e, left, off, c);
        CHECK(right == 0xff808080u, "index 0 keeps the skin's colour (%08x)", right);
        printf("  %d_index.png: index %d in remap %d -> %08x, index 0 -> %08x\n", n, e, r, left, right);
    }
    hires_skins_free();
    free(img), free(ix);
}

/* ---- the skin_template tool (built next to this test): template and --validate ---- */

static char tool[512];

static int run_tool(const char *args, char *out, size_t cap)
{
    char cmd[1024];
    snprintf(cmd, sizeof cmd, "\"%s\" %s 2>&1", tool, args);
    FILE *p = popen(cmd, "r");
    if (!p) return -1;
    size_t n = fread(out, 1, cap - 1, p);
    out[n] = 0;
    int st = pclose(p);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static void copy_file(const char *from, const char *to)
{
    size_t n;
    uint8_t *d = read_host(from, &n);
    FILE *f = d ? fopen(to, "wb") : NULL;
    if (f) fwrite(d, 1, n, f), fclose(f);
    free(d);
}

static bool any_png(const char *dir)
{
    char cmd[600], line[16];
    snprintf(cmd, sizeof cmd, "find \"%s\" -iname '*.png' | head -1", dir);
    FILE *p = popen(cmd, "r");
    bool found = p && fgets(line, sizeof line, p);
    if (p) pclose(p);
    return found;
}

static void check_tool(void)
{
    static char out[65536];
    FILE *t = fopen(tool, "rb");
    if (!t) { printf("  (no %s: skipped)\n", tool); return; }
    fclose(t);
    /* the template: manifest, checklist, skin.ini, folders, and not one image */
    if (system("rm -rf out/skins/template")) printf("  (rm failed)\n");
    int st = run_tool("out/skins/template", out, sizeof out);
    CHECK(st == 0, "skin_template out/skins/template: exit %d\n%s", st, out);
    size_t n;
    char *json = (char *)read_host("out/skins/template/skin.json", &n);
    char *md = (char *)read_host("out/skins/template/CHECKLIST.md", NULL);
    CHECK(json && strstr(json, "\"style\": 1") && strstr(json, "\"aux_frames\": [25, 26, 27"), "skin.json: style 1, the water animation");
    CHECK(json && strstr(json, "{ \"n\": 72, \"group\": \"car\""), "skin.json: sprite 72 is a car");
    CHECK(md && strstr(md, "`lid/1.png`") && strstr(md, "`sprite/72.png`: 62 x 64"), "CHECKLIST.md lists lid 1, sprite 72");
    char *ini = (char *)read_host("out/skins/template/skin.ini", NULL);
    CHECK(ini && strstr(ini, "scale ="), "skin.ini template");
    free(ini);
    struct stat sb;
    CHECK(!stat("out/skins/template/style001/sprite", &sb) && S_ISDIR(sb.st_mode), "the folders");
    CHECK(!any_png("out/skins/template"), "the template holds no image");
    printf("  template: skin.json %zu bytes, CHECKLIST.md, skin.ini, folders, no images\n", json ? n : 0);
    free(json), free(md);
    /* the sample is valid */
    st = run_tool("--validate assets/skins/sample", out, sizeof out);
    CHECK(st == 0 && strstr(out, " 0 errors, 0 warnings"), "sample validates clean: exit %d\n%s", st, out);
    printf("  --validate sample: %s", strstr(out, "images;") ? strstr(out, "images;") - 3 : "?\n");
    /* a broken skin: every kind of mistake is caught */
    const char *B = "out/skins/broken", *S = "assets/skins/sample/style001";
    char a[300], b[300];
    if (system("rm -rf out/skins/broken")) printf("  (rm failed)\n");
    const char *dirs[] = { "", "/style001", "/style001/lid", "/style001/sprite", "/style001/side", "/style001/water", "/font", "/font/BIG1", "/pictures" };
    for (size_t i = 0; i < sizeof dirs / sizeof *dirs; i++) snprintf(a, sizeof a, "%s%s", B, dirs[i]), mkdir(a, 0755);
    snprintf(a, sizeof a, "%s/skin.ini", B), write_file(a, "name = broken\nscale = 9\ncolour = red\n");
    static const struct { const char *from, *to; } cp[] = {
        { "lid/1.png", "style001/lid/01.png" },     { "lid/1.png", "style001/lid/1_r7.png" }, { "lid/1.png", "style001/lid/999.png" },
        { "lid/8.png", "style001/sprite/72.png" },  { "sprite/72.png", "style001/sprite/72_mask.png" },
        { "sprite/72.png", "style001/sprite/72_delta20.png" }, { "lid/1.png", "style001/side/5.PNG" },
        { "lid/1.png", "style001/water/1.png" },    { "lid/1.png", "font/BIG1/5.png" },       { "lid/1.png", "pictures/NOPE.png" },
    };
    for (size_t i = 0; i < sizeof cp / sizeof *cp; i++) {
        snprintf(a, sizeof a, "%s/%s", S, cp[i].from), snprintf(b, sizeof b, "%s/%s", B, cp[i].to);
        copy_file(a, b);
    }
    snprintf(a, sizeof a, "%s/style001/lid/3.png", B), write_file(a, "not an image");
    snprintf(a, sizeof a, "%s/notes.doc", B), write_file(a, "hello");
    st = run_tool("--validate out/skins/broken", out, sizeof out);
    static const char *const want[] = {
        "lid/01.png: leading zeros", "lid/1_r7.png: lid variants are 0..3", "lid/999.png: style 1 has lid 0..",
        "72_mask.png: 248 x 256 doesn't match sprite/72.png (256 x 256)", "72_delta20.png: sprite 72 has 11 deltas",
        "side/5.PNG: the game asks for \".png\" in lower case", "water/1.png: unknown folder", "lid/3.png: not a PNG",
        "font/BIG1/5.png: BIG1 has glyphs 33..", "pictures/NOPE.png: not a picture", "notes.doc: not read by the game",
        "scale = 9", "unknown key 'colour'",
    };
    int caught = 0;
    for (size_t i = 0; i < sizeof want / sizeof *want; i++) {
        bool ok = strstr(out, want[i]) != NULL;
        CHECK(ok, "validator should report \"%s\"", want[i]);
        caught += ok;
    }
    CHECK(st == 1, "a broken skin exits 1 (%d)", st);
    printf("  --validate broken: %d of %zu mistakes reported, exit %d\n", caught, sizeof want / sizeof *want, st);
    if (failures) printf("%s", out);
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

int main(int argc, char **argv)
{
    (void)argc;
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
    printf("deltas on skin sprites:\n");
    check_damage(s);
    check_index(s);
    printf("skin_template:\n");
    {
        const char *slash = strrchr(argv[0], '/');
        snprintf(tool, sizeof tool, "%.*sskin_template", slash ? (int)(slash - argv[0] + 1) : 0, argv[0]);
        check_tool();
    }
    printf("renders:\n");
    render_scenes(m, s);
    style_free(s);
    map_free(m);
    render_mission();
    printf(failures ? "FAIL (%d)\n" : "PASS\n", failures);
    return failures != 0;
}
