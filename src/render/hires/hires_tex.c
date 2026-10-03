/* Texture conversion and the overlay stack (see hires_tex.h). */
#include "hires_tex.h"
#include "../../surface.h"   /* blit_apply_delta */
#include <stdlib.h>
#include <string.h>

/* ---- the overlay stack ---- */

enum { OVERLAYS_MAX = 8 };
static struct { HiresOverlayFn fn; void *ctx; } overlays[OVERLAYS_MAX];
static int noverlays;

void hires_overlay_push(HiresOverlayFn fn, void *ctx)
{
    if (noverlays < OVERLAYS_MAX) overlays[noverlays].fn = fn, overlays[noverlays++].ctx = ctx;
}
void hires_overlay_clear(void) { noverlays = 0; }
int hires_overlay_count(void) { return noverlays; }

static HiresTexture *overlay_lookup(const HiresAsset *a)
{
    for (int i = noverlays - 1; i >= 0; i--) {
        HiresTexture *t = overlays[i].fn(overlays[i].ctx, a);
        if (t) return t;
    }
    return NULL;
}

/* ---- caches ---- */

enum { TILE_SLOTS = 4096, TILE_MAX = 3072, SPRITE_SLOTS = 2048, SPRITE_MAX_HELD = 1536 };

typedef struct { bool used; int tile; const uint32_t *clut; HiresTexture tex; } TileEntry;
typedef struct { bool used; int n; const uint32_t *clut; uint32_t deltas; HiresTexture tex; } SpriteEntry;

static TileEntry *tiles;
static SpriteEntry *sprites;
static int ntiles, nsprites;
/* what the cached conversions were made from */
static struct { const Style *s; int number; const uint32_t *clut; const uint8_t *buf; PixelFormat fmt; } from;

static void free_tiles(void)
{
    if (tiles)
        for (int i = 0; i < TILE_SLOTS; i++)
            if (tiles[i].used) hr_texture_free(&tiles[i].tex), tiles[i].used = false;
    ntiles = 0;
}

static void free_sprites(void)
{
    if (sprites)
        for (int i = 0; i < SPRITE_SLOTS; i++)
            if (sprites[i].used) hr_texture_free(&sprites[i].tex), sprites[i].used = false;
    nsprites = 0;
}

void hires_textures_reset(void)
{
    free_tiles();
    free_sprites();
    memset(&from, 0, sizeof from);
}

void hires_texture_stats(int *t, int *s)
{
    *t = ntiles;
    *s = nsprites;
}

static bool ready(const Style *s)
{
    if (!tiles && !(tiles = calloc(TILE_SLOTS, sizeof *tiles))) return false;
    if (!sprites && !(sprites = calloc(SPRITE_SLOTS, sizeof *sprites))) return false;
    if (from.s != s || from.number != s->number || from.clut != s->clut || from.buf != s->buf ||
        memcmp(&from.fmt, &s->clut_fmt, sizeof from.fmt)) {
        hires_textures_reset();
        from.s = s, from.number = s->number, from.clut = s->clut, from.buf = s->buf, from.fmt = s->clut_fmt;
    }
    return true;
}

static inline uint32_t mix(uint32_t h, uint32_t v)
{
    h ^= v;
    h *= 0x9e3779b1u;
    return h ^ h >> 15;
}

/* ---- tiles ---- */

static const uint8_t *tile_texels(const Style *s, int t);

void hires_tile_kind(const Style *s, int t, int *kind, int *n)
{
    if (t < s->lid_base) *kind = HIRES_SIDE, *n = t - s->side_base;
    else if (t < s->aux_base) *kind = HIRES_LID, *n = t - s->lid_base;
    else *kind = HIRES_AUX, *n = t - s->aux_base;
}

/* 64 x 64 texels of tile t (page t >> 4, u = (t & 3) * 64, v = ((t >> 2) & 3) * 64, as Poly_SelectTile
   0x497dfc addresses it), each through the paged CLUT (colour e 64 words after colour e - 1); texel 0
   keeps its colour but gets alpha 0. */
static bool convert_tile(const Style *s, int t, const uint32_t *clut, HiresTexture *tex)
{
    const uint8_t *src = tile_texels(s, t);
    if (!src) return false;
    uint32_t *px = malloc(64 * 64 * 4);
    if (!px) return false;
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            uint8_t e = src[y * 256 + x];
            px[y * 64 + x] = hr_from_xrgb(clut[e * 64], e ? 0xff : 0);
        }
    *tex = (HiresTexture){ 64, 64, px, NULL, true };
    return true;
}

HiresTexture *hires_tile_original(const Style *s, int t, const uint32_t *clut)
{
    if (!clut || !ready(s)) return NULL;
    uint32_t h = mix(mix(0x1234567u, (uint32_t)t), (uint32_t)(clut - s->clut));
    for (uint32_t i = 0;; i++) {
        TileEntry *e = &tiles[(h + i) & (TILE_SLOTS - 1)];
        if (!e->used) {
            if (ntiles >= TILE_MAX) {   /* full: start over (deterministic, and rare) */
                free_tiles();
                return hires_tile_original(s, t, clut);
            }
            if (!convert_tile(s, t, clut, &e->tex)) return NULL;
            e->used = true, e->tile = t, e->clut = clut;
            ntiles++;
            return &e->tex;
        }
        if (e->tile == t && e->clut == clut) return &e->tex;
    }
}

static const uint8_t *tile_texels(const Style *s, int t)
{
    if (t < 0 || (uint32_t)t * 0x1000 >= STYLE_TILES_MAX) return NULL;
    return s->buf + style_tile_page(t) + ((t >> 2) & 3) * 0x4000 + (t & 3) * 0x40;
}

HiresTexture *hires_tile(const Style *s, int t, const uint32_t *clut, int remap, const uint32_t *base_clut)
{
    if (noverlays) {
        int kind, n;
        hires_tile_kind(s, t, &kind, &n);
        HiresAsset a = { s->number, kind, n, remap, clut, base_clut, tile_texels(s, t), NULL };
        HiresTexture *o = overlay_lookup(&a);
        if (o) return o;
    }
    return hires_tile_original(s, t, clut);
}

/* ---- sprites ---- */

/* The graphic with the delta mask applied, lowest delta first, empty ones skipped (as Sprite_GetComposite
   0x4143a0 builds a composite from scratch; the hires renderer keeps its own copies and never touches
   the faithful renderer's composite cache). */
static bool convert_sprite(const SpriteInfo *in, const uint32_t *clut, uint32_t deltas, HiresTexture *tex)
{
    static uint8_t work[256 * 256];
    const int w = in->w, h = in->h;
    if (!w || !h) return false;
    const uint8_t *g = in->data;
    if (deltas) {
        for (int r = 0; r < h; r++) memcpy(work + r * 256, in->data + r * 256, (size_t)w);
        for (uint32_t bit = 1, i = 0; deltas; bit <<= 1, i++) {
            if (!(deltas & bit)) continue;
            if (i < in->ndeltas && in->delta[i].size) blit_apply_delta(work, in->delta[i].data, in->delta[i].size);
            deltas &= ~bit;
        }
        g = work;
    }
    uint32_t *px = malloc((size_t)w * (size_t)h * 4);
    if (!px) return false;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t e = g[y * 256 + x];
            px[y * w + x] = hr_from_xrgb(clut[e * 64], e ? 0xff : 0);
        }
    *tex = (HiresTexture){ w, h, px, NULL, true };
    return true;
}

HiresTexture *hires_sprite(const Style *s, int n, const SpriteInfo *in, const uint32_t *clut, uint32_t deltas,
                           int remap, const uint32_t *own_clut)
{
    if (!in || !clut || !ready(s)) return NULL;
    if (noverlays) {
        HiresAsset a = { s->number, HIRES_SPRITE, n, remap, clut, own_clut, in->data, in };
        HiresTexture *o = overlay_lookup(&a);
        if (o) return o;
    }
    uint32_t h = mix(mix(mix(0x7654321u, (uint32_t)n), (uint32_t)(clut - s->clut)), deltas);
    for (uint32_t i = 0;; i++) {
        SpriteEntry *e = &sprites[(h + i) & (SPRITE_SLOTS - 1)];
        if (!e->used) {
            if (nsprites >= SPRITE_MAX_HELD) {
                free_sprites();
                return hires_sprite(s, n, in, clut, deltas, remap, own_clut);
            }
            if (!convert_sprite(in, clut, deltas, &e->tex)) return NULL;
            e->used = true, e->n = n, e->clut = clut, e->deltas = deltas;
            nsprites++;
            return &e->tex;
        }
        if (e->n == n && e->clut == clut && e->deltas == deltas) return &e->tex;
    }
}
