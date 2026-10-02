/* Sprites: Sprite_* 0x47bb10-0x47cd00, the delta overlays and their cache 0x414310-0x414590,
   Tile_SelectSprite* 0x4385b0 / 0x4385e0, Render_QueueVisibleEntities 0x437000 (see sprite.h,
   docs/sprites.md). Module state mirrors the original's globals. */
#include "sprite.h"
#include "../game/gmath.h"
#include "../surface.h"   /* blit_apply_delta */
#include "city.h"
#include "drawlist.h"
#include "poly.h"
#include "poly_sprite.h"
#include <stddef.h>
#include <string.h>

bool sprite_blend_option = true;           /* 0x5031e4 */

static const Style *S;
static SpriteInfo infos[SPRITE_MAX];       /* 0x773e38: pointers to the records in the original */
static SpriteDelta deltas[SPRITE_MAX * 8]; /* the records' delta entries, resolved */
static int ninfo;                          /* 0x774ee4 */
static int16_t group_base[SPRITE_GROUPS];  /* 0x774efa.. (see sprite.h) */
static int16_t group_count[SPRITE_GROUPS];
static int16_t ped_palette;                /* 0x7750d0 */
static DrawNode *levels[SPRITE_LEVELS];    /* 0x774ec8 */

static inline int32_t mul32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }
static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static inline uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* ---- delta composite cache (0x502e88: 10 x {buffer, info, mask, stamp}, stamp counter 0x502f28) ---- */

typedef struct { uint8_t *buf; const SpriteInfo *info; uint32_t mask, stamp; } CacheEntry;
static CacheEntry cache[SPRITE_CACHE];
static uint32_t cache_clock;

/* SpriteCache_Init 0x414590: the buffers are inside the first 64 KB page of the sprite buffer (before
   the graphics), 256-byte rows: small slots at u 0/64/128/192 of rows 0 and 64, big ones at u 0/128 of
   row 128. */
static void sprite_cache_init(uint8_t *area)
{
    static const uint16_t ofs[SPRITE_CACHE] = { 0, 0x40, 0x80, 0xc0, 0x4000, 0x4040, 0x4080, 0x40c0, 0x8000, 0x8080 };
    cache_clock = 0;
    for (int i = 0; i < SPRITE_CACHE; i++) cache[i] = (CacheEntry){ area + ofs[i], NULL, 0, 0 };
}

/* Gfx_CopyRect256 0x47cb90: h rows of w bytes, both 256 bytes wide. */
static void copy_rect256(uint8_t *dst, const uint8_t *src, int w, int h)
{
    for (int r = 0; r < h; r++) memcpy(dst + r * 256, src + r * 256, (size_t)w);
}

/* Apply the deltas of `mask` (bit i = delta i, lowest first) to buf (Blit_ApplyDelta 0x48958a); empty
   deltas are skipped. */
static void apply_deltas(uint8_t *buf, const SpriteInfo *in, uint32_t mask)
{
    for (uint32_t bit = 1, i = 0; mask; bit <<= 1, i++) {
        if (!(mask & bit)) continue;
        if (i < in->ndeltas && in->delta[i].size) blit_apply_delta(buf, in->delta[i].data, in->delta[i].size);
        mask &= ~bit;
    }
}

/* Sprite_GetComposite 0x4143a0. Sprites of up to 0x1000 texels use slots 0..7, bigger ones 8..9. A slot
   holding this sprite with exactly this mask is returned as is; failing that, the last slot holding it
   with a mask m that "fits under" this one (m < mask signed, and (mask - m) | m == mask) gets the
   missing deltas (mask - m) applied on top; failing that, the slot with the oldest stamp is rebuilt from
   the raw graphic. The slot is stamped and takes the mask. Removing a delta therefore always rebuilds. */
const uint8_t *sprite_get_composite(const Sprite *sp)
{
    const SpriteInfo *in = sp->info;
    uint32_t mask = sp->deltas;
    if (!mask) return in->data;
    int lo = in->size <= 0x1000 ? 0 : 8, hi = in->size <= 0x1000 ? 8 : 10;
    int hit = -1, partial = -1;
    for (int i = lo; i < hi; i++) {
        if (cache[i].info != in) continue;
        if (cache[i].mask == mask) { hit = i; break; }
        uint32_t m = cache[i].mask;
        if ((int32_t)m < (int32_t)mask && ((mask - m) | m) == mask) partial = i;
    }
    if (hit < 0) hit = partial;
    int e;
    if (hit < 0) {
        uint32_t oldest = 0xffffffffu;
        e = 0;
        for (int i = lo; i < hi; i++)
            if (cache[i].stamp < oldest) e = i, oldest = cache[i].stamp;
        copy_rect256(cache[e].buf, in->data, in->w, in->h);
        apply_deltas(cache[e].buf, in, sp->deltas);
    } else {
        e = hit;
        if ((int32_t)cache[e].mask < (int32_t)mask) apply_deltas(cache[e].buf, in, mask - cache[e].mask);
    }
    cache[e].stamp = ++cache_clock;
    cache[e].info = in;
    cache[e].mask = sp->deltas;
    return cache[e].buf;
}

/* ---- loading ---- */

/* Sprite_SetGroupBases 0x47cbd0: base of group g = sum of the counts before it (shorts). */
void sprite_set_group_bases(const uint8_t *numbers)
{
    int16_t b = 0;
    for (int g = 0; g < SPRITE_GROUPS; g++) {
        group_count[g] = (int16_t)rd16(numbers + 2 * g);
        group_base[g] = b;
        b = (int16_t)(b + group_count[g]);
    }
}

int sprite_group_base(int g) { return g >= 0 && g < SPRITE_GROUPS ? group_base[g] : 0; }
int sprite_group_count(int g) { return g >= 0 && g < SPRITE_GROUPS ? group_count[g] : 0; }

/* Sprite_LoadInfo 0x47ca50: the records follow each other ((deltas + 2) * 6 bytes each); every one gets
   a table entry (the rest of the 0x424 are cleared), its graphic and delta offsets become pointers into
   the graphics, then the group bases, the six draw trees and the composite cache (and Coll_Init, not
   part of this module). Style_Load also sets the ped palette base 0x7750d0 after CarInfo_Setup counted
   the car records. */
bool sprite_load_info(const Style *s)
{
    const uint8_t *p = s->sprite_info, *end = p + s->h.sprite_info_size;
    int n = 0, nd = 0;
    memset(infos, 0, sizeof infos);
    while (p < end) {
        if (n >= SPRITE_MAX || end - p < 12) return false;   /* the original overruns the table */
        SpriteInfo *in = &infos[n++];
        in->w = p[0], in->h = p[1], in->ndeltas = p[2];
        in->size = rd16(p + 4), in->clut = rd16(p + 6);
        uint32_t off = rd32(p + 8);
        if (end - p < 12 + 6 * in->ndeltas || nd + in->ndeltas > (int)(sizeof deltas / sizeof *deltas)) return false;
        if (off >= s->h.sprite_graphics_size) return false;
        in->data = s->sprite_graphics + off;
        in->delta = &deltas[nd];
        for (int i = 0; i < in->ndeltas; i++) {
            const uint8_t *d = p + 12 + 6 * i;
            uint32_t doff = rd32(d + 2);
            deltas[nd].size = rd16(d);
            if (doff + deltas[nd].size > s->h.sprite_graphics_size) return false;
            deltas[nd++].data = s->sprite_graphics + doff;
        }
        p += (in->ndeltas + 2) * 6;
    }
    if (s->h.sprite_numbers_size < 2 * SPRITE_GROUPS) return false;
    S = s;
    ninfo = n;
    sprite_set_group_bases(s->sprite_numbers);
    drawlist_init();
    for (int i = 0; i < SPRITE_LEVELS; i++) levels[i] = drawlist_new_root();
    sprite_cache_init(s->sprite_graphics - 0x10000);
    /* 0x7750d0 = car palette base + car records * 12 (records of 0xae + doors * 8 bytes, CarInfo_Setup) */
    int ncar = 0;
    for (uint32_t o = 0; o + 0xae <= s->h.car_size; ncar++) o += 0xae + (int16_t)rd16(s->car_info + o + 0xac) * 8;
    ped_palette = (int16_t)(s->car_pal_base + ncar * CAR_REMAPS);
    return true;
}

int sprite_count(void) { return ninfo; }

const SpriteInfo *sprite_get_info(int n)
{
    return n >= 0 && n < ninfo ? &infos[n] : NULL;
}

int sprite_car_palette(int record) { return S ? S->car_pal_base + record * CAR_REMAPS : 0; }
int sprite_ped_palette(void) { return ped_palette; }

/* ---- palettes ---- */

static const uint32_t *palette_entry(int i)
{
    if (i < 0 || (uint32_t)i >= S->npal_index) i = 0;   /* the original reads past the index */
    return style_clut_of(S, S->pal_index[i]);
}

/* Tile_SelectSprite 0x4385b0 */
const uint32_t *sprite_select_palette(int n)
{
    return poly_clut = palette_entry((int16_t)n);
}

/* Tile_SelectSpriteRemap 0x4385e0: remap 0 is the sprite's own palette (sprite base 0x77531c + clut),
   remap r the palette base + r - 1 (16-bit sums). */
const uint32_t *sprite_select_remap(int clut, int remap, int palette)
{
    if (remap == 0) return poly_clut = palette_entry((int16_t)(S->sprite_pal_base + (int16_t)clut));
    return poly_clut = palette_entry((int16_t)(remap - 1 + palette));
}

/* ---- the sprite object ---- */

/* Sprite_Init 0x47c9f0: the depth key starts as z; no remap, no palette, not blended, nothing attached,
   the corner cache invalid (its info pointer cleared; the cached angle is left as it was). */
void sprite_init(Sprite *sp, int32_t x, int32_t y, int32_t z, int angle, int frame)
{
    sp->x = x, sp->z = z, sp->zkey = z, sp->y = y;
    sp->angle = (int16_t)angle;
    sp->remap = 0;
    sp->palette = 0;
    sp->blend = 0;
    sp->next = NULL;
    sp->cached_info = NULL;
    sp->frame = (uint16_t)frame;
    sp->info = sprite_get_info(frame);
    sprite_clear_deltas(sp);
}

/* Sprite_SetFrame 0x47c960 */
void sprite_set_frame(Sprite *sp, int frame)
{
    sp->frame = (uint16_t)frame;
    sp->info = sprite_get_info((uint16_t)frame);
    sprite_clear_deltas(sp);
}

/* Sprite_SaveFrame 0x47c9a0 */
void sprite_save_frame(Sprite *sp)
{
    sp->saved_frame = sp->frame;
    sprite_save_deltas(sp);
}

/* Sprite_AddDelta 0x414310 / Sprite_ToggleDelta 0x414330: only deltas the sprite has; Sprite_RemoveDelta
   0x414360 doesn't check. Bit numbers are bytes, shifted mod 32. */
void sprite_add_delta(Sprite *sp, int n)
{
    if ((uint8_t)n < sp->info->ndeltas) sp->deltas |= 1u << (n & 31);
}

void sprite_toggle_delta(Sprite *sp, int n)
{
    if ((uint8_t)n < sp->info->ndeltas) sp->deltas ^= 1u << (n & 31);
}

void sprite_remove_delta(Sprite *sp, int n) { sp->deltas &= ~(1u << (n & 31)); }
void sprite_clear_deltas(Sprite *sp) { sp->deltas = 0; }
void sprite_save_deltas(Sprite *sp) { sp->saved_deltas = sp->deltas; }

/* ---- drawing ---- */

/* The corners of a w x h sprite around its centre, 16.16 with y up: half width (w - 1) / 2, half height
   h / 2, in the order top left, top right, bottom left, bottom right. Rotated, each corner's integer
   parts (rounded down, so an even width loses a pixel on the right and gains one on the left) are
   multiplied by the 16.16 sine and cosine of the angle: x' = c x - s y, y' = s x + c y. */
static void sprite_corners(const SpriteInfo *in, int angle, int32_t c[8])
{
    const int32_t hw = in->w * 0x8000 - 0x8000, hh = in->h * 0x8000;
    c[0] = -hw, c[1] = hh, c[2] = hw, c[3] = hh, c[4] = -hw, c[5] = -hh, c[6] = hw, c[7] = -hh;
    if ((int16_t)angle == 0) return;
    /* TODO(0x511e28): angles outside 0..1023 index past the table in the original */
    const int a = (int16_t)angle & 0x3ff;
    const int32_t s = math_sin(a), co = math_cos(a);
    const int32_t xl = -hw >> 16, xr = hw >> 16, yt = hh >> 16, yb = -hh >> 16;
    c[0] = mul32(co, xl) - mul32(s, yt), c[1] = mul32(s, xl) + mul32(co, yt);
    c[2] = mul32(co, xr) - mul32(s, yt), c[3] = mul32(s, xr) + mul32(co, yt);
    c[4] = mul32(co, xl) - mul32(s, yb), c[5] = mul32(s, xl) + mul32(co, yb);
    c[6] = mul32(co, xr) - mul32(s, yb), c[7] = mul32(s, xr) + mul32(co, yb);
}

/* Sprite_WorldToScreen 0x47bb10 with the depth from the high short of z (as both drawers read it). */
static void project(uint32_t x, uint32_t y, int32_t z, int32_t *sx, int32_t *sy)
{
    render_world_to_screen((int32_t)x, (int32_t)y, (int32_t)((uint32_t)(int16_t)(z >> 16) << 16), sx, sy);
}

/* The common tail of Sprite_Draw and Sprite_DrawCached: cull the corners' bounding box against the
   screen (0..w, 0..h of the render camera), select the palette, use the raw graphic for quads smaller
   than 10 pixels (|dy| + |dx| between corners 0 and 3: no deltas from afar), else the composite, and
   rasterise with the vertices in the order top right, top left, bottom left, bottom right. */
static void sprite_rasterise(const Sprite *sp, const int32_t px[4], const int32_t py[4])
{
    int32_t xmax = px[0], xmin = px[0], ymax = py[0], ymin = py[0];
    for (int i = 1; i < 4; i++) {
        if (px[i] > xmax) xmax = px[i];
        if (px[i] < xmin) xmin = px[i];
        if (py[i] > ymax) ymax = py[i];
        if (py[i] < ymin) ymin = py[i];
    }
    if (xmax < 0 || ymax < 0 || xmin >= render_cam.w || ymin >= render_cam.h) return;
    const SpriteInfo *in = sp->info;
    sprite_select_remap(in->clut, sp->remap, sp->palette);
    int32_t dy = py[3] - py[0], dx = px[3] - px[0];
    if (dy < 0) dy = -dy;
    if (dx < 0) dx = -dx;
    const uint8_t *tex = dy + dx < 10 ? in->data : sprite_get_composite(sp);
    if (sp->blend && sprite_blend_option)
        poly_draw_sprite_blend(tex, px[1], px[0], px[2], px[3], py[1], py[0], py[2], py[3], in->w, in->h);
    else
        poly_draw_sprite(tex, px[1], px[0], px[2], px[3], py[1], py[0], py[2], py[3], in->w, in->h);
}

/* Sprite_Draw 0x47bc00: only the centre is projected; the corners are added in screen pixels, so the
   sprite keeps its size whatever the camera height (HUD arrows and markers). */
void sprite_draw(Sprite *sp)
{
    if (!sp->info) return;
    int32_t c[8], cx, cy, px[4], py[4];
    sprite_corners(sp->info, sp->angle, c);
    project((uint32_t)sp->x & 0xffff0000u, (uint32_t)sp->y & 0xffff0000u, sp->z, &cx, &cy);
    for (int i = 0; i < 4; i++) px[i] = (c[2 * i] >> 16) + cx, py[i] = cy - (c[2 * i + 1] >> 16);
    sprite_rasterise(sp, px, py);
}

/* Sprite_DrawCached 0x47c130: the rotated corner offsets are kept in the sprite (+0x28) and recomputed
   only when the angle or the info record changed; each corner is projected on its own (integer part of
   the position plus the 16.16 offset; world y = y - offset). */
void sprite_draw_cached(Sprite *sp)
{
    if (!sp->info) return;
    if (sp->angle != sp->cached_angle || sp->info != sp->cached_info) {
        sprite_corners(sp->info, sp->angle, sp->corner);
        sp->cached_info = sp->info;
        sp->cached_angle = sp->angle;
    }
    const uint32_t X = (uint32_t)sp->x & 0xffff0000u, Y = (uint32_t)sp->y & 0xffff0000u;
    int32_t px[4], py[4];
    for (int i = 0; i < 4; i++)
        project(X + (uint32_t)sp->corner[2 * i], Y - (uint32_t)sp->corner[2 * i + 1], sp->z, &px[i], &py[i]);
    sprite_rasterise(sp, px, py);
}

/* 0x47c050 (no function in the Ghidra project): the draw tree callback. The sprite, then every sprite
   chained at +0x50, each temporarily placed relative to the parent: its x/y offset rotated by the
   parent's angle (integer parts times sine/cosine, as for corners), x = parent x + x', y = parent y - y',
   z = parent z - z offset, angle = parent angle + its own (mod 1024); restored after drawing. */
void sprite_draw_with_attached(Sprite *sp)
{
    sprite_draw_cached(sp);
    for (Sprite *c = sp->next; c; c = c->next) {
        const int32_t ox = c->x, oy = c->y, oz = c->z;
        const int16_t oa = c->angle;
        int32_t rx = ox, ry = oy;
        if (sp->angle != 0) {
            const int a = sp->angle & 0x3ff;   /* (unmasked in the original) */
            const int32_t s = math_sin(a), co = math_cos(a), xi = ox >> 16, yi = oy >> 16;
            rx = mul32(co, xi) - mul32(s, yi);
            ry = mul32(co, yi) + mul32(s, xi);
        }
        c->x = (int32_t)((uint32_t)sp->x + (uint32_t)rx);
        c->y = (int32_t)((uint32_t)sp->y - (uint32_t)ry);
        c->z = (int32_t)((uint32_t)sp->z - (uint32_t)oz);
        c->angle = (int16_t)((sp->angle + oa) & 0x3ff);
        sprite_draw_cached(c);
        c->x = ox, c->y = oy, c->z = oz, c->angle = oa;
    }
}

/* Sprite_DrawScreen 0x47bbc0: the sprite's own palette, Blit_Tile32 0x4894a1 (256-byte rows, texel 0
   transparent) at (x, y) of the poly_sprite target, no clipping in the original (dropped here). */
void sprite_draw_screen(int x, int y, const SpriteInfo *in)
{
    poly_sprite_blit_tile(x, y, in->data, in->w, in->h, sprite_select_remap(in->clut, 0, 0));
}

/* ---- draw trees ---- */

void sprite_clear_levels(void) { drawlist_clear(); }

/* Sprite_Queue 0x47c940: into tree zkey >> 22, keyed by zkey. Keys outside the 6 layers index past the
   roots in the original (0x774ee0 is a temporary of Sprite_DrawCached): dropped here. */
void sprite_queue(Sprite *sp)
{
    int level = sp->zkey >> 22;
    if (level < 0 || level >= SPRITE_LEVELS) return;   /* TODO(0x774ec8): out-of-range trees */
    drawlist_insert(levels[level], sp, sp->zkey);
}

static void draw_item(void *item) { sprite_draw_with_attached(item); }

/* Sprite_DrawLevel 0x47c030 */
void sprite_draw_level(int z)
{
    if (z < 0 || z >= SPRITE_LEVELS) return;
    drawlist_walk(levels[z], draw_item);
}

/* ---- Render_QueueVisibleEntities 0x437000 ---- */

typedef struct { const SpriteWorld *w; } QueueCtx;

static void queue_entry(void *vctx, int kind, void *owner)
{
    const SpriteWorld *w = ((QueueCtx *)vctx)->w;
    Sprite *sp = NULL;
    switch (kind) {
    case 1: case 3: case 6: case 0x1e: sp = w->embedded(w->ctx, kind, owner); break;
    case 0xc: case 0xd: sp = owner; break;
    case 7: case 8: case 10: case 0xe: case 0x13: if (w->trains_lights) sp = owner; break;
    default: break;
    }
    if (sp) sprite_queue(sp);
}

static int cell_clamp(int v) { return v < 0 ? 0 : v > 0x7f ? 0x7f : v; }

/* Cells of 128 pixels from one left of the view rect to one right of it (clamped to 0..127), columns
   outer, rows inner, each cell's list in order. */
void render_queue_visible_entities(const SpriteWorld *w, const ViewRect *v)
{
    const int x0 = cell_clamp((v->left >> 7) - 1), x1 = cell_clamp((v->right >> 7) + 1);
    const int y0 = cell_clamp((v->top >> 7) - 1), y1 = cell_clamp((v->bottom >> 7) + 1);
    QueueCtx q = { w };
    for (int x = x0; x <= x1; x++)
        for (int y = y0; y <= y1; y++) w->walk_cell(w->ctx, x, y, queue_entry, &q);
}
