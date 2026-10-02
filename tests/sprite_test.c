/* The sprite system against the real data: Sprite_LoadInfo / Sprite_SetGroupBases on the three
   styles, the draw trees' order, the delta composite cache, and pictures: a sheet of every sprite of
   STYLE001 (out/sprites/sheet_style1.png) and NYC frames with cars at several angles, remaps and damage
   deltas, peds and objects drawn through the draw trees (PNGs in out/sprites/).

     ./build/sprite_test            (data root: ./game or OPENGTA_DATA) */
#include "exe.h"
#include "game/gmath.h"
#include "map.h"
#include "png.h"
#include "render/camera.h"
#include "render/city.h"
#include "render/drawlist.h"
#include "render/poly.h"
#include "render/poly_sprite.h"
#include "render/sprite.h"
#include "style.h"
#include "surface.h"
#include "vfs_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum { W = 640, H = 480 };
static uint32_t fb[W * H];
static int failures;
#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: " __VA_ARGS__); printf("\n"); failures++; } } while (0)

static void set_target(uint32_t *px, int w, int h, const Style *s)
{
    poly_set_screen_rows(px, w * 4, h);
    poly_set_clip(0, 0, w - 1, h - 1);
}

/* ---- car info (CarInfo_Setup 0x40c100 is not ported: the records are read raw here) ---- */
typedef struct { int w, h, sprite, vtype, model, doors; } Car;
static Car cars[64];
static int ncars;

static void read_cars(const Style *s)
{
    ncars = 0;
    for (uint32_t o = 0; o + 0xae <= s->h.car_size && ncars < 64;) {
        const uint8_t *r = s->car_info + o;
        Car *c = &cars[ncars++];
        c->w = (int16_t)(r[0] | r[1] << 8), c->h = (int16_t)(r[2] | r[3] << 8);
        c->vtype = r[0x6a], c->model = r[0x6b], c->doors = (int16_t)(r[0xac] | r[0xad] << 8);
        int g = SPRITE_GROUP_CAR;
        switch (c->vtype) {
        case 0: case 2: g = SPRITE_GROUP_BUS; break;
        case 1: case 4: g = SPRITE_GROUP_CAR; break;
        case 3: g = SPRITE_GROUP_BIKE; break;
        case 8: g = SPRITE_GROUP_TRAIN; break;
        case 9: g = SPRITE_GROUP_TRAM; break;
        case 13: g = SPRITE_GROUP_BOAT; break;
        case 14: g = SPRITE_GROUP_TANK; break;
        }
        c->sprite = (int16_t)(r[6] | r[7] << 8) + sprite_group_base(g);
        o += 0xae + c->doors * 8;
    }
}

/* ---- checks ---- */

static void check_style(int n)
{
    char err[256];
    Style *s = style_load(n, err, sizeof err);
    if (!s) { printf("FAIL: %s\n", err); failures++; return; }
    CHECK(sprite_load_info(s), "style %d: sprite info doesn't parse", n);
    int sum = 0;
    printf("STYLE%03d: %d sprites; groups:", n, sprite_count());
    for (int g = 0; g < SPRITE_GROUPS; g++) {
        printf(" %d@%d", sprite_group_count(g), sprite_group_base(g));
        CHECK(sprite_group_base(g) == sum, "group %d base %d != %d", g, sprite_group_base(g), sum);
        sum += sprite_group_count(g);
    }
    printf("\n");
    CHECK(sum == sprite_count(), "style %d: group counts %d != records %d", n, sum, sprite_count());
    int maxd = 0, nd = 0;
    for (int i = 0; i < sprite_count(); i++) {
        const SpriteInfo *in = sprite_get_info(i);
        CHECK(in->size == in->w * in->h, "sprite %d size %d != %d x %d", i, in->size, in->w, in->h);
        if (in->ndeltas > maxd) maxd = in->ndeltas;
        nd += in->ndeltas;
    }
    read_cars(s);
    printf("  %d deltas (at most %d per sprite), %d car records, car palettes %d.., ped palettes %d..\n", nd, maxd,
           ncars, s->car_pal_base, sprite_ped_palette());
    CHECK(sprite_ped_palette() == s->car_pal_base + ncars * CAR_REMAPS, "ped palette base");
    style_free(s);
}

static int order[16], norder;
static void record(void *item) { order[norder++] = (int)(intptr_t)item; }

static void check_drawlist(void)
{
    drawlist_init();
    DrawNode *r = drawlist_new_root();
    /* items 1..6 with keys; equal keys: the later one is drawn after the earlier */
    static const int32_t keys[] = { 50, 10, 70, 50, 30, 70 };
    for (int i = 0; i < 6; i++) drawlist_insert(r, (void *)(intptr_t)(i + 1), keys[i]);
    norder = 0;
    drawlist_walk(r, record);
    static const int want[] = { 3, 6, 1, 4, 5, 2 };
    CHECK(norder == 6 && !memcmp(order, want, sizeof want), "draw list order");
    printf("draw list: order");
    for (int i = 0; i < norder; i++) printf(" %d", order[i]);
    printf(" (keys 70 70 50 50 30 10)\n");
    drawlist_clear();
    norder = 0;
    drawlist_walk(r, record);
    CHECK(norder == 0, "cleared draw list not empty");
    for (int i = 0; i < 310; i++) drawlist_insert(r, (void *)(intptr_t)1, i);
    CHECK(drawlist_count() == DRAWLIST_NODES, "node pool limit");
}

/* the composite of a sprite with mask m, built from scratch */
static void reference_composite(const SpriteInfo *in, uint32_t m, uint8_t *out)
{
    for (int r = 0; r < in->h; r++) memcpy(out + r * 256, in->data + r * 256, in->w);
    for (int i = 0; i < in->ndeltas; i++)
        if (m >> i & 1 && in->delta[i].size) blit_apply_delta(out, in->delta[i].data, in->delta[i].size);
}

static bool same_composite(const SpriteInfo *in, const uint8_t *a, const uint8_t *b)
{
    for (int r = 0; r < in->h; r++)
        if (memcmp(a + r * 256, b + r * 256, in->w)) return false;
    return true;
}

static void check_composites(int car_sprite)
{
    static uint8_t ref[256 * 256];
    Sprite sp;
    sprite_init(&sp, 0, 0, 0, 0, car_sprite);
    const SpriteInfo *in = sp.info;
    CHECK(sprite_get_composite(&sp) == in->data, "no deltas: not the raw graphic");
    sprite_add_delta(&sp, 0);
    const uint8_t *a = sprite_get_composite(&sp);
    reference_composite(in, 1, ref);
    CHECK(a != in->data && same_composite(in, a, ref), "delta 0 composite");
    CHECK(sprite_get_composite(&sp) == a, "exact hit not the same slot");
    sprite_add_delta(&sp, 2);   /* partial hit: delta 2 applied on top of slot a */
    const uint8_t *b = sprite_get_composite(&sp);
    reference_composite(in, 5, ref);
    CHECK(b == a && same_composite(in, b, ref), "partial hit");
    sprite_remove_delta(&sp, 0);   /* mask 4: not under 5 -> rebuilt in another slot */
    const uint8_t *c = sprite_get_composite(&sp);
    reference_composite(in, 4, ref);
    CHECK(c != a && same_composite(in, c, ref), "rebuild after removing a delta");
    sprite_add_delta(&sp, 200);   /* not a delta of this sprite: ignored */
    CHECK(sp.deltas == 4, "add of a missing delta changed the mask");
    sprite_toggle_delta(&sp, 2);
    CHECK(sp.deltas == 0, "toggle");
    printf("composite cache: raw/exact/partial/rebuild ok on sprite %d (%dx%d, %d deltas)\n", car_sprite, in->w, in->h,
           in->ndeltas);
}

/* ---- city frames with sprites ---- */

/* A stand-in for the collision grid (0x5278f8): entries {kind, owner, next} per 128-pixel cell. */
typedef struct Entry { int kind; void *owner; struct Entry *next; } Entry;
typedef struct { Sprite sp; } Ent;   /* an entity whose sprite is at +0 (cars have it at +0x250) */
static Entry *grid[128][128];
static Entry entries[256];
static Ent ents[256];
static int nents;

static void grid_walk(void *ctx, int cx, int cy, SpriteVisitFn visit, void *vctx)
{
    for (Entry *e = grid[cy][cx]; e; e = e->next) visit(vctx, e->kind, e->owner);
}
static Sprite *grid_embedded(void *ctx, int kind, void *owner) { return &((Ent *)owner)->sp; }

enum { GROUND = 4 * 0x400000 - 0x10000 };   /* just above the lid of layer 4 (the street): tree 3 */

static Sprite *add(int kind, int wx, int wy, int angle, int frame)
{
    Ent *e = &ents[nents];
    Entry *g = &entries[nents++];
    sprite_init(&e->sp, wx << 16, wy << 16, GROUND, angle, frame);
    int cx = wx >> 7 & 127, cy = wy >> 7 & 127;
    *g = (Entry){ kind, e, grid[cy][cx] };
    grid[cy][cx] = g;
    return &e->sp;
}

static CameraWorld cworld = { .peds = true, .cars = true };

static void setup_camera(CameraPlayer *p, const Map *m, Style *s, int bx, int by, int bz, int dbg)
{
    memset(p, 0, sizeof *p);
    camera_set_viewport(p, W, H);
    p->target_kind = CAM_TARGET_PED;
    p->target = (CameraTarget){ (bx * 64 + 32) << 16, (by * 64 + 32) << 16, bz * 0x400000 - 0x10000, 8, 8, 0, 0 };
    camera_init(p, &cworld);
    p->cam.dbg_height = dbg;
    for (int i = 0; i < 300; i++) {
        style_update_anims(s);
        camera_update(p, &cworld);
    }
    (void)m;
}

/* Game_Frame + Game_Render: clear the trees, camera, queue the visible entities, city, sprites. */
static uint32_t frame(CameraPlayer *p, const Map *m, const Style *s, const SpriteWorld *w)
{
    sprite_clear_levels();
    camera_compute_view_rect(p);
    render_compute_visible_rect(&p->vp);
    render_copy_camera(&p->vp);
    memset(fb, 0xff, sizeof fb);
    render_queue_visible_entities(w, &p->rect);
    render_draw_city(m, s, &p->vp);
    /* TODO: Render_DrawCity calls Sprite_DrawLevel(z) between its layers; until city.c has that hook
       the levels are drawn here after the whole city (so sprites also cover the buildings above them). */
    return crc32(fb, sizeof fb);
}

static void scenes(const Map *m, Style *s)
{
    CameraPlayer p;
    setup_camera(&p, m, s, 105, 119, 4, -150);
    const int cx = p.vp.x, cy = p.vp.y;
    printf("scene at (105,119,4): camera x %d y %d height %d scale %d\n", cx, cy, p.vp.height, p.vp.scale);
    memset(grid, 0, sizeof grid);
    nents = 0;
    /* row 0: one car model at angles 0, 256, 512, 768 and an arbitrary one */
    static const int angles[] = { 0, 256, 512, 768, 100 };
    const Car *car = &cars[3];
    for (int i = 0; i < 5; i++) add(SPRITE_KIND_CAR, cx - 200 + i * 100, cy - 130, angles[i], car->sprite);
    /* row 1: remaps 0, 1, 4, 8, 12 of another model (record 5, the ambulance, has 12 identical remaps) (car palettes: base + record * 12 + remap - 1) */
    for (int i = 0; i < 5; i++) {
        static const int remaps[] = { 0, 1, 4, 8, 12 };
        Sprite *sp = add(SPRITE_KIND_CAR, cx - 200 + i * 100, cy - 40, 0, cars[1].sprite);
        sprite_set_palette(sp, sprite_car_palette(1));
        sprite_set_remap(sp, remaps[i]);
    }
    /* row 2: damage / door deltas: none, 0, 0..3, every delta, all at an angle */
    for (int i = 0; i < 5; i++) {
        Sprite *sp = add(SPRITE_KIND_CAR, cx - 200 + i * 100, cy + 50, i == 4 ? 900 : 0, cars[0].sprite);
        int nd = sp->info->ndeltas;
        if (i == 1) sprite_add_delta(sp, 0);
        if (i == 2) for (int d = 0; d < 4; d++) sprite_add_delta(sp, d);
        if (i >= 3) for (int d = 0; d < nd; d++) sprite_add_delta(sp, d);
    }
    /* row 3: peds (remaps through the ped palettes), objects, a blended car with a ped attached */
    const int ped = sprite_group_base(SPRITE_GROUP_PED), obj = sprite_group_base(SPRITE_GROUP_OBJECT);
    for (int i = 0; i < 6; i++) {
        Sprite *sp = add(SPRITE_KIND_PED, cx - 220 + i * 24, cy + 130, i * 170, ped + i * 3);
        sprite_set_palette(sp, sprite_ped_palette());
        sprite_set_remap(sp, i);
    }
    static const int objs[] = { 0, 36, 120, 300, 413 };
    for (int i = 0; i < 5; i++) add(0xc, cx - 60 + i * 40, cy + 130, 0, obj + objs[i]);
    Sprite *bl = add(SPRITE_KIND_CAR, cx + 180, cy + 140, 128, cars[2].sprite);
    sprite_set_blend(bl);
    static Sprite rider;   /* attached: offset 0, +20 (ahead), drawn turned with the car */
    sprite_init(&rider, 0, 20 << 16, 0, 256, ped);
    bl->next = &rider;
    SpriteWorld w = { NULL, grid_walk, grid_embedded, false };
    uint32_t a = frame(&p, m, s, &w);
    /* Sprite_Draw: screen-sized (the HUD arrow), at the camera centre */
    Sprite arrow;
    sprite_init(&arrow, cx << 16, cy << 16, GROUND, 64, sprite_group_base(SPRITE_GROUP_ARROW) + 48);
    sprite_draw(&arrow);
    /* Poly_DrawRect: a packed copy of a sprite at 2x, 1x and 1/2 (one pixel per texel: gaps at 2x) */
    static uint8_t packed[256 * 256];
    const SpriteInfo *in = sprite_get_info(car->sprite);
    for (int r = 0; r < in->h; r++) memcpy(packed + r * in->w, in->data + r * 256, in->w);
    sprite_select_remap(in->clut, 0, 0);
    poly_draw_rect(4, 4 + in->w * 2, 4, 4 + in->h * 2, in->w - 1, in->h - 1, packed);
    poly_draw_rect(8 + in->w * 2, 8 + in->w * 3, 4, 4 + in->h, in->w - 1, in->h - 1, packed);
    poly_draw_rect(12 + in->w * 3, 12 + in->w * 3 + in->w / 2, 4, 4 + in->h / 2, in->w - 1, in->h - 1, packed);
    poly_draw_rect(-20, -20 + in->w, 300, 300 + in->h, in->w - 1, in->h - 1, packed);   /* left clip */
    png_write("out/sprites/nyc_start.png", fb, W, H, PNG_XRGB);
    printf("  %d sprites queued (%d draw nodes), crc %08x -> out/sprites/nyc_start.png\n", nents, drawlist_count(), a);
    CHECK(drawlist_count() == nents, "queued %d of %d", drawlist_count(), nents);
    uint32_t b = frame(&p, m, s, &w);
    CHECK(a == b, "not deterministic (%08x vs %08x)", a, b);
    /* the trains switch: kinds 7/8/10/0xe/0x13 are only queued with it */
    add(8, cx, cy, 0, ped);
    frame(&p, m, s, &w);
    CHECK(drawlist_count() == nents - 1, "kind 8 queued without the trains switch");
    w.trains_lights = true;
    frame(&p, m, s, &w);
    CHECK(drawlist_count() == nents, "kind 8 not queued with the trains switch");

    /* zoomed out further: tiny sprites skip the deltas (|dx| + |dy| < 10) */
    setup_camera(&p, m, s, 105, 119, 4, -600);
    w.trains_lights = false;
    nents--;
    frame(&p, m, s, &w);
    png_write("out/sprites/nyc_far.png", fb, W, H, PNG_XRGB);
    printf("  far: height %d -> out/sprites/nyc_far.png\n", p.vp.height);
}

/* ---- the sheet: every sprite unrotated (Sprite_DrawScreen), packed in shelves ---- */

static void sheet(const Style *s, const char *path)
{
    enum { SW = 2048, GAP = 3 };
    int x = GAP, y = GAP, shelf = 0;
    for (int i = 0; i < sprite_count(); i++) {   /* height first */
        const SpriteInfo *in = sprite_get_info(i);
        if (x + in->w + GAP > SW) x = GAP, y += shelf + GAP, shelf = 0;
        x += in->w + GAP;
        if (in->h > shelf) shelf = in->h;
    }
    int sh = y + shelf + GAP;
    uint32_t *px = malloc((size_t)SW * sh * 4);
    for (int i = 0; i < SW * sh; i++) px[i] = 0x303030;
    set_target(px, SW, sh, s);
    x = GAP, y = GAP, shelf = 0;
    for (int i = 0; i < sprite_count(); i++) {
        const SpriteInfo *in = sprite_get_info(i);
        if (x + in->w + GAP > SW) x = GAP, y += shelf + GAP, shelf = 0;
        for (int k = -1; k <= in->w; k++) px[(y - 1) * SW + x + k] = px[(y + in->h) * SW + x + k] = 0x505050;
        sprite_draw_screen(x, y, in);
        x += in->w + GAP;
        if (in->h > shelf) shelf = in->h;
    }
    png_write(path, px, SW, sh, PNG_XRGB);
    printf("sheet: %d sprites, %dx%d -> %s\n", sprite_count(), SW, sh, path);
    free(px);
    set_target(fb, W, H, s);
}

int main(void)
{
    char err[256];
    if (!vfs_mount_default()) { printf("SKIP: no data root\n"); return 0; }
    if (!exe_init(err, sizeof err)) { printf("SKIP: %s\n", err); return 0; }
    math_init_tables();
    if (!camera_init_tables()) { printf("FAIL: camera tables\n"); return 1; }
    mkdir("out", 0755);
    mkdir("out/sprites", 0755);

    for (int n = 1; n <= 3; n++) check_style(n);
    check_drawlist();

    Map *m = map_load("GTADATA/NYC.CMP", err, sizeof err);
    if (!m) { printf("FAIL: %s\n", err); return 1; }
    Style *s = style_load(m->style, err, sizeof err);
    if (!s) { printf("FAIL: %s\n", err); return 1; }
    style_convert_palettes(s, &PIXFMT_32);
    if (!sprite_load_info(s)) { printf("FAIL: sprite info\n"); return 1; }
    read_cars(s);
    set_target(fb, W, H, s);
    check_composites(cars[0].sprite);
    sheet(s, "out/sprites/sheet_style1.png");
    scenes(m, s);

    style_free(s);
    map_free(m);
    printf(failures ? "FAIL (%d)\n" : "PASS\n", failures);
    return failures != 0;
}
