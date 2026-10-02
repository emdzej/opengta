/* The ported city renderer against the real data: NYC (and the other cities) loaded with Map_Load /
   Style_Load, the camera settled on a target with the ported Camera_InitAll / Camera_Update, frames
   drawn by Render_DrawCity into a 640x480 32 bpp surface. Writes out/render/<name>.png, checks that
   the frame is covered and that rendering is deterministic, prints a CRC per frame.

     ./build/render_test            (data root: ./game or OPENGTA_DATA) */
#include "exe.h"
#include "game/gmath.h"
#include "map.h"
#include "render/camera.h"
#include "render/city.h"
#include "render/poly.h"
#include "style.h"
#include "vfs_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum { W = 640, H = 480 };
static uint32_t fb[W * H];
static int failures;

/* ---- a minimal PNG writer (stored deflate blocks) ---- */
static void put32(FILE *f, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
    fwrite(b, 1, 4, f);
}
static void chunk(FILE *f, const char *type, const uint8_t *data, size_t n)
{
    put32(f, (uint32_t)n);
    uint8_t *buf = malloc(n + 4);
    memcpy(buf, type, 4);
    memcpy(buf + 4, data, n);
    fwrite(buf, 1, n + 4, f);
    put32(f, crc32(buf, n + 4));
    free(buf);
}
static void write_png(const char *path, const uint32_t *px, int w, int h)
{
    FILE *f = fopen(path, "wb");
    if (!f) { printf("  can't write %s\n", path); return; }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ihdr[13] = { 0, 0, (uint8_t)(w >> 8), (uint8_t)w, 0, 0, (uint8_t)(h >> 8), (uint8_t)h, 8, 2, 0, 0, 0 };
    chunk(f, "IHDR", ihdr, 13);
    size_t raw_n = (size_t)h * (1 + 3 * w);
    uint8_t *raw = malloc(raw_n);
    for (int y = 0; y < h; y++) {
        uint8_t *r = raw + (size_t)y * (1 + 3 * w);
        r[0] = 0;
        for (int x = 0; x < w; x++) {
            uint32_t c = px[y * w + x];   /* 0x00RRGGBB; never drawn: magenta */
            if (c == 0xffffffffu) c = 0xff00ff;
            r[1 + 3 * x] = (uint8_t)(c >> 16), r[2 + 3 * x] = (uint8_t)(c >> 8), r[3 + 3 * x] = (uint8_t)c;
        }
    }
    size_t nblk = (raw_n + 65534) / 65535, zn = 2 + raw_n + nblk * 5 + 4;
    uint8_t *z = malloc(zn), *o = z;
    *o++ = 0x78, *o++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_n; i++) a = (a + raw[i]) % 65521, b = (b + a) % 65521;
    for (size_t p = 0; p < raw_n; p += 65535) {
        size_t n = raw_n - p < 65535 ? raw_n - p : 65535;
        *o++ = p + n == raw_n;
        *o++ = (uint8_t)n, *o++ = (uint8_t)(n >> 8), *o++ = (uint8_t)~n, *o++ = (uint8_t)(~n >> 8);
        memcpy(o, raw + p, n), o += n;
    }
    uint32_t ad = b << 16 | a;
    *o++ = (uint8_t)(ad >> 24), *o++ = (uint8_t)(ad >> 16), *o++ = (uint8_t)(ad >> 8), *o++ = (uint8_t)ad;
    chunk(f, "IDAT", z, (size_t)(o - z));
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
}

/* ---- scenes ---- */

/* A camera target on the lid of block (x, y, z) (MISSION.INI's convention: objects at z * 64); kind
   CAR with a speed and heading zooms out and looks ahead; dbg_height raises the camera like the debug
   free camera (Camera_DebugMove 0x43cb20 offsets). */
typedef struct { const char *name; int x, y, z; int kind, speed, angle; int frames; int dbg_height; } Scene;

static CameraWorld world = { .peds = true, .cars = true };

/* One frame of Game_Frame + Game_Render: anims, camera, visible rect, camera copy, city. */
static void frame(CameraPlayer *p, const Map *m, Style *s, bool draw)
{
    style_update_anims(s);
    camera_update(p, &world);
    render_compute_visible_rect(&p->vp);
    render_copy_camera(&p->vp);
    if (draw) render_draw_city(m, s, &p->vp);
}

static uint32_t render_scene(const Map *m, Style *s, const Scene *sc, const char *city)
{
    CameraPlayer p;
    memset(&p, 0, sizeof p);
    camera_set_viewport(&p, W, H);
    p.target_kind = sc->kind;
    /* a ped standing on the lid of block (x, y, z) (MISSION.INI coordinates): Map_GetGroundZ 0x4544e0
       gives (z - 1) * 0x400000 + 0x3f0000 for the cell above it */
    p.target = (CameraTarget){ (sc->x * 64 + 32) << 16, (sc->y * 64 + 32) << 16, sc->z * 0x400000 - 0x10000,
                              (int16_t)(sc->kind == CAM_TARGET_PED ? 8 : 32), (int16_t)(sc->kind == CAM_TARGET_PED ? 8 : 64),
                              (int16_t)sc->speed, (int16_t)sc->angle };
    camera_init(&p, &world);
    p.cam.dbg_height = sc->dbg_height;
    for (int i = 0; i < sc->frames; i++) frame(&p, m, s, false);
    /* unwritten pixels keep 0xffffffff: converted CLUT words have byte 3 clear, so it can't be drawn */
    memset(fb, 0xff, sizeof fb);
    poly_recip_oob = 0;
    frame(&p, m, s, true);
    uint32_t crc = crc32(fb, sizeof fb);
    int covered = 0;
    for (int i = 0; i < W * H; i++) covered += fb[i] != 0xffffffffu;
    char path[256];
    snprintf(path, sizeof path, "out/render/%s_%s.png", city, sc->name);
    write_png(path, fb, W, H);
    printf("  %-10s (%3d,%3d,%d) cam x %d y %d height %d zoom %d scale %d: crc %08x, %5.1f%% covered, oob %u -> %s\n",
           sc->name, sc->x, sc->y, sc->z, p.vp.x, p.vp.y, p.vp.height, p.vp.zoom, p.vp.scale, crc,
           100.0 * covered / (W * H), poly_recip_oob, path);
    if (covered < W * H * 9 / 10) { printf("  FAIL: %s covers only %d pixels\n", sc->name, covered); failures++; }
    return crc;
}

static void run_city(const char *rel, const char *city, const Scene *scenes, int n)
{
    char err[256];
    Map *m = map_load(rel, err, sizeof err);
    if (!m) { printf("FAIL: %s\n", err); failures++; return; }
    Style *s = style_load(m->style, err, sizeof err);
    if (!s) { printf("FAIL: %s\n", err); failures++; map_free(m); return; }
    style_convert_palettes(s, &PIXFMT_32);
    printf("%s: style %d, %u blocks, extents x %d..%d y %d..%d; %d/%d/%d tiles, %d anims\n", rel, s->number,
           m->nblocks, m->min_x, m->max_x, m->min_y, m->max_y, s->nside, s->nlid, s->naux, s->nanims);
    for (int i = 0; i < n; i++) {
        uint32_t a = render_scene(m, s, &scenes[i], city);
        if (i == 0) {   /* determinism: the same scene again from a fresh style */
            style_free(s);
            s = style_load(m->style, err, sizeof err);
            style_convert_palettes(s, &PIXFMT_32);
            uint32_t b = render_scene(m, s, &scenes[i], city);
            if (a != b) { printf("  FAIL: %s not deterministic (%08x vs %08x)\n", scenes[i].name, a, b); failures++; }
        }
    }
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
    mkdir("out/render", 0755);
    poly_set_screen_rows(fb, W * 4, H);
    poly_set_clip(0, 0, W - 1, H - 1);

    /* NYC: mission 1's player start (MISSION.INI [1]: 294 PLAYER at (105,119,4)) and other areas */
    static const Scene nyc[] = {
        { "start", 105, 119, 4, CAM_TARGET_PED, 0, 0, 300, 0 },
        { "bridge", 55, 149, 2, CAM_TARGET_PED, 0, 0, 300, 0 },
        { "slope", 99, 54, 4, CAM_TARGET_PED, 0, 0, 300, 0 },
        { "highrise", 90, 50, 4, CAM_TARGET_PED, 0, 0, 300, 0 },
        { "fastcar", 141, 171, 4, CAM_TARGET_CAR, 0x30, 0, 300, 0 },
        { "water", 60, 144, 5, CAM_TARGET_PED, 0, 0, 300, 0 },
        { "slope_far", 99, 54, 4, CAM_TARGET_PED, 0, 0, 300, -400 },
        { "start_far", 105, 119, 4, CAM_TARGET_PED, 0, 0, 300, -400 },
        { "bridge_far", 55, 149, 2, CAM_TARGET_PED, 0, 0, 300, -250 },
        { "ramp_ns", 221, 76, 4, CAM_TARGET_PED, 0, 0, 300, -200 },
        { "ramp_ew", 29, 41, 4, CAM_TARGET_PED, 0, 0, 300, -200 },
    };
    run_city("GTADATA/NYC.CMP", "nyc", nyc, (int)(sizeof nyc / sizeof *nyc));
    /* the player starts of San Andreas mission 103 and Vice City mission 203 */
    static const Scene sanb[] = { { "start", 168, 110, 4, CAM_TARGET_PED, 0, 0, 300, 0 } };
    run_city("GTADATA/SANB.CMP", "sanb", sanb, 1);
    static const Scene miami[] = { { "start", 228, 211, 4, CAM_TARGET_PED, 0, 0, 300, 0 } };
    run_city("GTADATA/MIAMI.CMP", "miami", miami, 1);
    printf(failures ? "FAIL (%d)\n" : "PASS\n", failures);
    return failures != 0;
}
