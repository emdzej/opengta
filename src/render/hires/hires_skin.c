/* Skins (see hires_skin.h). */
#include "hires_skin.h"
#include "../../vfs.h"
#include "hires_png.h"
#include "hires_tex.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

HiresFileReader hires_skin_reader = vfs_read_all;

enum { SKINS_MAX = 8, SLOTS = 4096, PATH_SLOTS = 1024 };
/* cache variants besides the remaps: the plain image, the paint mask, a sprite with its deltas applied,
   delta overlay k (V_DELTA - k) */
enum { V_BASE = -1, V_MASK = -2, V_DAMAGED = -3, V_DELTA = -16 };

typedef struct {
    bool used;
    int style, kind, n, variant;
    const uint32_t *clut;            /* recoloured sprites: the CLUT they were made for */
    uint32_t deltas;                 /* V_DAMAGED: the delta mask */
    HiresTexture *tex;               /* NULL: no such file */
} Entry;

typedef struct {                     /* hires_skin_image's cache: images by path */
    char *path;
    int cap;
    HiresTexture *tex;               /* NULL: no such file */
} PathEntry;

typedef struct {
    HiresSkinInfo info;
    Entry *e;
    int nentries;
    PathEntry *pe;
    int npaths;
} Skin;

static Skin skins[SKINS_MAX];
static int nskins, scale = 1, nloaded, nmissing;
static void (*logf_)(const char *);

static void say(const char *fmt, const char *a, const char *b)
{
    if (!logf_) return;
    char m[640];   /* fits a 340-byte path and a 160-byte error */
    snprintf(m, sizeof m, fmt, a, b);
    logf_(m);
}

static const char *const FOLDER[HIRES_KINDS] = { "side", "lid", "aux", "sprite" };

/* ---- images ---- */

static HiresTexture *texture_of(uint32_t *px, int w, int h)
{
    HiresTexture *t = calloc(1, sizeof *t);
    if (!t) { free(px); return NULL; }
    *t = (HiresTexture){ w, h, px, NULL, true };
    return t;
}

/* Halve (2 x 2 boxes, colour weighted by alpha) while larger than cap texels on its longer side. */
static void shrink(uint32_t **ppx, int *pw, int *ph, int cap)
{
    while ((*pw > cap || *ph > cap) && *pw >= 2 && *ph >= 2) {
        const int w = *pw, h = *ph, nw = w / 2, nh = h / 2;
        uint32_t *s = *ppx, *d = malloc((size_t)nw * nh * 4);
        if (!d) return;
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++) {
                unsigned r = 0, g = 0, b = 0, a = 0;
                for (int j = 0; j < 2; j++)
                    for (int i = 0; i < 2; i++) {
                        uint32_t c = s[(size_t)(2 * y + j) * w + 2 * x + i], ca = c >> 24;
                        r += (c & 0xff) * ca, g += (c >> 8 & 0xff) * ca, b += (c >> 16 & 0xff) * ca, a += ca;
                    }
                d[(size_t)y * nw + x] = a ? (a / 4) << 24 | (b / a) << 16 | (g / a) << 8 | r / a : 0;
            }
        free(s);
        *ppx = d, *pw = nw, *ph = nh;
    }
}

static uint32_t *load_png(const char *path, int *w, int *h)
{
    size_t size;
    uint8_t *f = hires_skin_reader(path, &size);
    if (!f) return NULL;
    char err[160];
    uint32_t *px = hires_png_decode(f, size, w, h, err, sizeof err);
    free(f);
    if (!px) say("OpenGTA: skin: %s: %s", path, err);
    return px;
}

/* Per-resolution variants: "<base>@<k>x.png" is the image hand-made for hires=k. At hires=N the first
   that exists of: @Nx, the nearest larger level (@N+1x .. @4x, scaled down), the master "<base>.png" (any
   size, scaled), the nearest smaller level (@N-1x .. @1x, scaled up). `path` ends in ".png". */
static uint32_t *load_png_level(const char *path, int *w, int *h)
{
    size_t n = strlen(path);
    if (n < 4 || strcmp(path + n - 4, ".png")) return load_png(path, w, h);
    char base[320], p[340];
    snprintf(base, sizeof base, "%.*s", (int)(n - 4), path);
    int order[9], no = 0;
    order[no++] = scale;
    for (int k = scale + 1; k <= 4; k++) order[no++] = k;
    order[no++] = 0;   /* the master */
    for (int k = scale - 1; k >= 1; k--) order[no++] = k;
    for (int i = 0; i < no; i++) {
        if (order[i]) snprintf(p, sizeof p, "%s@%dx.png", base, order[i]);
        else snprintf(p, sizeof p, "%s.png", base);
        size_t size;
        uint8_t *f = hires_skin_reader(p, &size);
        if (!f) continue;
        char err[160];
        uint32_t *px = hires_png_decode(f, size, w, h, err, sizeof err);
        free(f);
        if (px) return px;
        say("OpenGTA: skin: %s: %s", p, err);
    }
    return NULL;
}

static Entry *slot(Skin *k, int style, int kind, int n, int variant, const uint32_t *clut, uint32_t deltas, bool *found)
{
    uint32_t hh = (uint32_t)(style * 7919 + kind * 104729 + n * 31 + variant * 1299709) ^ (uint32_t)(uintptr_t)clut;
    hh = (hh ^ deltas * 0x85ebca6bu) * 0x9e3779b1u;
    for (uint32_t i = 0; i < SLOTS; i++) {
        Entry *e = &k->e[(hh + i) & (SLOTS - 1)];
        if (!e->used) { *found = false; return k->nentries < SLOTS * 3 / 4 ? e : NULL; }
        if (e->style == style && e->kind == kind && e->n == n && e->variant == variant && e->clut == clut &&
            e->deltas == deltas) {
            *found = true;
            return e;
        }
    }
    *found = false;
    return NULL;
}

/* skins/<name>/style<NNN>/<kind>/<n><suffix>.png, decoded and fitted to what scale n can show: tiles to
   128 n texels (a block is about 110 pixels at street level at 1x), sprites to 2 n times their size. */
static HiresTexture *file(Skin *k, const HiresAsset *a, int variant, const char *suffix)
{
    bool found;
    Entry *e = slot(k, a->style, a->kind, a->n, variant, NULL, 0, &found);
    if (found) return e->tex;
    char path[256];
    snprintf(path, sizeof path, "skins/%s/style%03d/%s/%d%s.png", k->info.name, a->style, FOLDER[a->kind], a->n, suffix);
    int w, h;
    uint32_t *px = load_png_level(path, &w, &h);
    HiresTexture *t = NULL;
    if (px) {
        int cap = 128 * scale;
        if (a->kind == HIRES_SPRITE && a->info) cap = 2 * scale * (a->info->w > a->info->h ? a->info->w : a->info->h);
        else if (w != h) say("OpenGTA: skin: %s: %s", path, "tiles should be square (drawn stretched)");
        shrink(&px, &w, &h, cap);
        t = texture_of(px, w, h);
    }
    if (t) nloaded++;
    else nmissing++;
    if (e) *e = (Entry){ true, a->style, a->kind, a->n, variant, NULL, 0, t }, k->nentries++;
    return t;
}

static unsigned lum(uint32_t c) { return ((c & 0xff) * 77 + (c >> 8 & 0xff) * 150 + (c >> 16 & 0xff) * 29) >> 8; }
static unsigned lum_xrgb(uint32_t c) { return ((c >> 16 & 0xff) * 77 + (c >> 8 & 0xff) * 150 + (c & 0xff) * 29) >> 8; }

/* Paint: a texel whose colour the remap changes by more than HIRES_PAINT_STEP (|dR| + |dG| + |dB|); the
   remaps also tint the glass and the dark parts slightly, which isn't paint. */
static bool is_paint(uint32_t o, uint32_t r)
{
    int d = 0;
    for (int k = 0; k < 24; k += 8) d += abs((int)(o >> k & 0xff) - (int)(r >> k & 0xff));
    return d > HIRES_PAINT_STEP;
}

/* The paint colour a remap gives the original sprite: the average remapped colour (and the average own
   colour) of its paint texels. */
static bool paint_colours(const HiresAsset *a, unsigned own[3], unsigned rem[3])
{
    const SpriteInfo *in = a->info;
    unsigned long so[3] = { 0, 0, 0 }, sr[3] = { 0, 0, 0 }, n = 0;
    for (int y = 0; y < in->h; y++)
        for (int x = 0; x < in->w; x++) {
            uint8_t t = in->data[y * 256 + x];
            if (!t) continue;
            uint32_t o = a->own_clut[t * 64] & 0xffffff, r = a->clut[t * 64] & 0xffffff;
            if (!is_paint(o, r)) continue;
            for (int c = 0; c < 3; c++) so[c] += o >> (16 - 8 * c) & 0xff, sr[c] += r >> (16 - 8 * c) & 0xff;
            n++;
        }
    if (!n) return false;
    for (int c = 0; c < 3; c++) own[c] = (unsigned)(so[c] / n), rem[c] = (unsigned)(sr[c] / n);   /* R, G, B */
    return true;
}

/* sprite/<n>.png recoloured where sprite/<n>_mask.png is white: the remap's paint colour, shaded by the
   skin pixel's brightness relative to the original paint's. */
static HiresTexture *recoloured(Skin *k, const HiresAsset *a, HiresTexture *base, HiresTexture *mask)
{
    bool found;
    Entry *e = slot(k, a->style, a->kind, a->n, a->remap, a->clut, 0, &found);
    if (found) return e->tex ? e->tex : base;
    unsigned own[3], rem[3];
    HiresTexture *t = base;
    if (paint_colours(a, own, rem) && mask->w > 0 && mask->h > 0) {
        uint32_t *px = malloc((size_t)base->w * base->h * 4);
        if (px) {
            const unsigned lo = lum((uint32_t)own[2] << 16 | own[1] << 8 | own[0]) + 1;
            for (int y = 0; y < base->h; y++)
                for (int x = 0; x < base->w; x++) {
                    uint32_t c = base->rgba[y * base->w + x];
                    uint32_t m = lum(mask->rgba[(y * mask->h / base->h) * mask->w + x * mask->w / base->w]);
                    unsigned l = lum(c), out[3];
                    for (int ch = 0; ch < 3; ch++) {
                        unsigned src = c >> (8 * ch) & 0xff, tint = rem[ch] * l / lo;
                        if (tint > 255) tint = 255;
                        out[ch] = (src * (255 - m) + tint * m) / 255;
                    }
                    px[y * base->w + x] = (c & 0xff000000u) | out[2] << 16 | out[1] << 8 | out[0];
                }
            t = texture_of(px, base->w, base->h);
            if (!t) t = base;
        }
    }
    if (e) *e = (Entry){ true, a->style, a->kind, a->n, a->remap, a->clut, 0, t == base ? NULL : t }, k->nentries++;
    return t;
}

/* A tile variant without its own image (a lid remap, a side's direction): the plain image with each
   channel scaled like the variant's CLUT scales the original tile on average (the remaps are mostly
   shading: shadowed pavements, darker walls). */
static HiresTexture *shaded(Skin *k, const HiresAsset *a, HiresTexture *base)
{
    bool found;
    Entry *e = slot(k, a->style, a->kind, a->n, a->remap, a->clut, 0, &found);
    if (found) return e->tex ? e->tex : base;
    unsigned long so[3] = { 0, 0, 0 }, sv[3] = { 0, 0, 0 };
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            uint8_t t = a->texels[y * 256 + x];
            uint32_t o = a->own_clut[t * 64], v = a->clut[t * 64];
            for (int c = 0; c < 3; c++) so[c] += o >> (16 - 8 * c) & 0xff, sv[c] += v >> (16 - 8 * c) & 0xff;
        }
    unsigned ratio[3];   /* R, G, B in 1/256 */
    bool same = true;
    for (int c = 0; c < 3; c++) {
        ratio[c] = (unsigned)(so[c] ? sv[c] * 256 / so[c] : 256);
        if (ratio[c] > 1024) ratio[c] = 1024;
        same &= ratio[c] >= 254 && ratio[c] <= 258;
    }
    HiresTexture *t = NULL;
    if (!same) {
        uint32_t *px = malloc((size_t)base->w * base->h * 4);
        if (px) {
            for (int i = 0; i < base->w * base->h; i++) {
                uint32_t c = base->rgba[i], out = c & 0xff000000u;
                for (int ch = 0; ch < 3; ch++) {
                    unsigned v = (c >> (8 * ch) & 0xff) * ratio[ch] >> 8;
                    out |= (v > 255 ? 255 : v) << (8 * ch);
                }
                px[i] = out;
            }
            t = texture_of(px, base->w, base->h);
        }
    }
    if (e) *e = (Entry){ true, a->style, a->kind, a->n, a->remap, a->clut, 0, t }, k->nentries++;
    return t ? t : base;
}

/* ---- damage and door deltas on skin sprites ---- */

/* Delta k of the original, at the sprite's own size: kind[] says what the delta does to each texel it
   writes (the stream is Blit_ApplyDelta 0x48958a's: skip from the end of the previous run, then n texels,
   256-byte rows):
   - D_SHADE: recolours a texel of the graphic (dents, scratches, smashed glass): val[] = the brightness
     change, new / old luminance in 1/256 (at most 2x), which shades the skin pixel, keeping its colours;
   - D_PASTE: draws where the graphic was transparent (an open door outside the outline): val[] = the
     delta's colour through clut, pasted;
   - D_CUT: makes a texel transparent: the skin image is cut there.
   False if the delta writes nothing inside the graphic. */
enum { D_NONE, D_SHADE, D_PASTE, D_CUT };

static bool delta_texels(const SpriteInfo *in, int k, const uint32_t *clut, uint8_t *kind, uint32_t *val)
{
    if (k >= in->ndeltas || !in->delta[k].size) return false;
    const uint8_t *p = in->delta[k].data, *end = p + in->delta[k].size;
    size_t at = 0;
    bool any = false;
    while (p + 3 <= end) {
        at += (uint16_t)(p[0] | p[1] << 8);
        unsigned n = p[2];
        p += 3;
        for (unsigned i = 0; i < n && p < end; i++, at++, p++) {
            const size_t y = at / 256, x = at % 256;
            if (y >= in->h || x >= in->w) continue;   /* (outside the graphic: the original writes it anyway) */
            const size_t j = y * in->w + x;
            const uint8_t was = in->data[y * 256 + x], now = *p;
            if (!now) kind[j] = D_CUT;
            else if (!was) kind[j] = D_PASTE, val[j] = hr_from_xrgb(clut[now * 64], 0xff);
            else {
                const uint32_t lo = lum_xrgb(clut[was * 64]) + 4, ln = lum_xrgb(clut[now * 64]) + 4;
                uint32_t r = ln * 256 / lo;
                kind[j] = D_SHADE, val[j] = r > 512 ? 512 : r;
            }
            any = true;
        }
    }
    return any;
}

/* The delta (sw x sh texels, kind / val) stretched over dst (w x h, straight alpha), each kind's
   coverage bilinear, so the original's damage lands where it is on the original graphic, softened at the
   skin's resolution. */
static void stamp_delta(uint32_t *dst, int w, int h, const uint8_t *kind, const uint32_t *val, int sw, int sh)
{
    for (int y = 0; y < h; y++) {
        /* source position in 1/256 texel, texel centres at i + 0.5 */
        int fy = (int)(((2 * y + 1) * (int64_t)sh * 128) / h) - 128;
        if (fy < 0) fy = 0;
        int y0 = fy >> 8, y1 = y0 + 1 < sh ? y0 + 1 : y0, wy = fy & 255;
        for (int x = 0; x < w; x++) {
            int fx = (int)(((2 * x + 1) * (int64_t)sw * 128) / w) - 128;
            if (fx < 0) fx = 0;
            int x0 = fx >> 8, x1 = x0 + 1 < sw ? x0 + 1 : x0, wx = fx & 255;
            const int idx[4] = { y0 * sw + x0, y0 * sw + x1, y1 * sw + x0, y1 * sw + x1 };
            const uint32_t wt[4] = { (uint32_t)((256 - wx) * (256 - wy)), (uint32_t)(wx * (256 - wy)),
                                     (uint32_t)((256 - wx) * wy), (uint32_t)(wx * wy) };   /* sum 65536 */
            uint32_t cshade = 0, cpaste = 0, ccut = 0;
            uint64_t ratio = 0, pc[3] = { 0, 0, 0 };
            for (int i = 0; i < 4; i++) {
                const int j = idx[i];
                if (kind[j] == D_SHADE) cshade += wt[i], ratio += (uint64_t)wt[i] * val[j];
                else if (kind[j] == D_PASTE) {
                    cpaste += wt[i];
                    for (int ch = 0; ch < 3; ch++) pc[ch] += (uint64_t)wt[i] * (val[j] >> (8 * ch) & 0xff);
                } else if (kind[j] == D_CUT) ccut += wt[i];
            }
            if (!(cshade | cpaste | ccut)) continue;
            /* in 1/65536: the skin pixel keeps (1 - all), is shaded by cshade, pasted over by cpaste (opaque) */
            const uint32_t d = dst[(size_t)y * w + x], da = d >> 24;
            const uint64_t keep = 65536 - cshade - cpaste - ccut;
            const uint64_t r = cshade ? ratio / cshade : 256;   /* the mean brightness change, 1/256 */
            const uint64_t oa = (da * (keep + cshade) + 255 * (uint64_t)cpaste) >> 16;   /* 0..255 */
            if (!oa) { dst[(size_t)y * w + x] = 0; continue; }
            uint32_t out = (uint32_t)oa << 24;
            for (int ch = 0; ch < 3; ch++) {
                const uint64_t sc = d >> (8 * ch) & 0xff;
                uint64_t shaded_c = sc * r >> 8;
                if (shaded_c > 255) shaded_c = 255;
                /* premultiplied, x 65536 */
                const uint64_t prem = sc * da * keep + shaded_c * da * cshade + pc[ch] * 255;
                const uint64_t v = prem / (oa * 65536);
                out |= (uint32_t)(v > 255 ? 255 : v) << (8 * ch);
            }
            dst[(size_t)y * w + x] = out;
        }
    }
}

/* sprite/<n>_delta<k>.png (any size, stretched over the image; straight alpha) laid over dst. */
static void stamp_image(uint32_t *dst, int w, int h, const HiresTexture *o)
{
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t s = o->rgba[(size_t)(y * o->h / h) * o->w + x * o->w / w], sa = s >> 24;
            if (!sa) continue;
            uint32_t d = dst[(size_t)y * w + x], da = d >> 24;
            uint32_t oa = sa + da * (255 - sa) / 255;
            uint32_t out = oa << 24;
            for (int ch = 0; ch < 3; ch++) {
                uint32_t v = ((s >> (8 * ch) & 0xff) * sa * 255 + (d >> (8 * ch) & 0xff) * da * (255 - sa)) / (oa * 255);
                out |= (v > 255 ? 255 : v) << (8 * ch);
            }
            dst[(size_t)y * w + x] = out;
        }
}

/* The skin sprite base with the asset's delta mask applied, lowest delta first (as the original
   composites them, Sprite_GetComposite 0x4143a0): each delta k from sprite/<n>_delta<k>.png if the skin
   has it, else derived from the original delta (delta_texels: the brightness changes it makes to the
   original shade the skin's colours; texels it draws outside the original outline are pasted through
   the asset's CLUT). Cached per (sprite, CLUT, mask); base itself without deltas, or
   when the cache is full. */
static HiresTexture *damaged(Skin *k, const HiresAsset *a, HiresTexture *base)
{
    const SpriteInfo *in = a->info;
    bool found;
    Entry *e = slot(k, a->style, a->kind, a->n, V_DAMAGED, a->clut, a->deltas, &found);
    if (found) return e->tex ? e->tex : base;
    if (!e) return base;   /* cache full: undamaged rather than a texture per frame */
    uint32_t *px = malloc((size_t)base->w * base->h * 4);
    uint8_t *cov = malloc((size_t)in->w * in->h);      /* D_* per texel */
    uint32_t *col = malloc((size_t)in->w * in->h * 4);
    HiresTexture *t = NULL;
    bool changed = false;
    if (px && cov && col) {
        memcpy(px, base->rgba, (size_t)base->w * base->h * 4);
        for (int d = 0; d < SPRITE_DELTAS_MAX && d < in->ndeltas; d++) {
            if (!(a->deltas >> d & 1) || !in->delta[d].size) continue;
            char sfx[16];
            snprintf(sfx, sizeof sfx, "_delta%d", d);
            HiresTexture *o = file(k, a, V_DELTA - d, sfx);
            if (o) {
                stamp_image(px, base->w, base->h, o);
                changed = true;
                continue;
            }
            memset(cov, 0, (size_t)in->w * in->h);
            if (delta_texels(in, d, a->clut, cov, col)) {
                stamp_delta(px, base->w, base->h, cov, col, in->w, in->h);
                changed = true;
            }
        }
        if (changed) t = texture_of(px, base->w, base->h), px = NULL;
    }
    free(px), free(cov), free(col);
    *e = (Entry){ true, a->style, a->kind, a->n, V_DAMAGED, a->clut, a->deltas, t };
    k->nentries++;
    return t ? t : base;
}

/* The skin's image for a sprite without deltas: sprite/<n>_r<r>.png, else the plain image, recoloured
   through sprite/<n>_mask.png for a remap. */
static HiresTexture *sprite_image(Skin *k, const HiresAsset *a, const char *sfx)
{
    if (a->remap != 0) {
        HiresTexture *v = file(k, a, a->remap, sfx);
        if (v) return v;
    }
    HiresTexture *b = file(k, a, V_BASE, "");
    if (!b || a->remap == 0 || !a->info || !a->own_clut || !a->clut) return b;
    HiresTexture *m = file(k, a, V_MASK, "_mask");
    if (!m) return b;
    HiresTexture *r = recoloured(k, a, b, m);
    return r ? r : b;
}

static HiresTexture *lookup(void *ctx, const HiresAsset *a)
{
    Skin *k = ctx;
    if (a->kind < 0 || a->kind >= HIRES_KINDS) return NULL;
    char sfx[16];
    snprintf(sfx, sizeof sfx, "_r%d", a->remap);
    if (a->kind == HIRES_SPRITE) {
        HiresTexture *t = sprite_image(k, a, sfx);
        if (t && a->deltas && a->info && a->clut) return damaged(k, a, t);
        return t;
    }
    HiresTexture *v = file(k, a, a->remap, sfx);
    if (v) return v;
    HiresTexture *b = file(k, a, V_BASE, "");
    if (b && a->remap != 0 && a->texels && a->clut && a->own_clut && a->clut != a->own_clut) return shaded(k, a, b);
    return b;
}

/* ---- any image by path (hires_skin_image) ---- */

static uint32_t path_hash(const char *p, int cap)
{
    uint32_t h = 2166136261u ^ (uint32_t)cap;
    for (; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    return h;
}

/* skins/<name>/<rel> of skin k: decoded once, kept (missing ones too). *have = false when the cache is
   full and the image was not kept (the caller owns it then). */
static HiresTexture *path_image(Skin *k, const char *rel, int cap, bool *have)
{
    *have = true;
    const uint32_t h = path_hash(rel, cap);
    PathEntry *e = NULL;
    for (uint32_t i = 0; i < PATH_SLOTS; i++) {
        PathEntry *q = &k->pe[(h + i) & (PATH_SLOTS - 1)];
        if (!q->path) { e = k->npaths < PATH_SLOTS * 3 / 4 ? q : NULL; break; }
        if (q->cap == cap && !strcmp(q->path, rel)) return q->tex;
    }
    char path[320];
    snprintf(path, sizeof path, "skins/%s/%s", k->info.name, rel);
    int w, h2;
    uint32_t *px = load_png_level(path, &w, &h2);
    HiresTexture *t = NULL;
    if (px) {
        if (cap > 0) shrink(&px, &w, &h2, cap);
        t = texture_of(px, w, h2);
    }
    if (t) nloaded++;
    else nmissing++;
    char *copy = e ? malloc(strlen(rel) + 1) : NULL;
    if (copy) {
        strcpy(copy, rel);
        *e = (PathEntry){ copy, cap, t };
        k->npaths++;
    } else {
        *have = false;
    }
    return t;
}

HiresTexture *hires_skin_image(const char *rel, int cap, int *which)
{
    if (which) *which = -1;
    if (!rel || !*rel || strstr(rel, "..") || rel[0] == '/') return NULL;
    for (int i = nskins - 1; i >= 0; i--) {
        bool kept;
        HiresTexture *t = path_image(&skins[i], rel, cap, &kept);
        if (t && !kept) {   /* cache full: hand it back rather than leak it (rare: 768 paths per skin) */
            hr_texture_free(t), free(t);
            t = NULL;
        }
        if (t) {
            if (which) *which = i;
            return t;
        }
    }
    return NULL;
}

/* ---- skin.ini ---- */

static void trim(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0;
    size_t i = 0;
    while (s[i] == ' ' || s[i] == '\t') i++;
    memmove(s, s + i, n - i + 1);
}

static bool read_ini(Skin *k)
{
    char path[160];
    snprintf(path, sizeof path, "skins/%s/skin.ini", k->info.name);
    size_t size;
    uint8_t *f = hires_skin_reader(path, &size);
    if (!f) return false;
    memcpy(k->info.title, k->info.name, sizeof k->info.name);   /* (title is the larger) */
    k->info.scale = 1;
    char line[256];
    for (size_t p = 0; p < size;) {
        size_t q = p;
        while (q < size && f[q] != '\n') q++;
        size_t n = q - p < sizeof line - 1 ? q - p : sizeof line - 1;
        memcpy(line, f + p, n);
        line[n] = 0;
        p = q + 1;
        char *eq = strchr(line, '=');
        if (line[0] == '#' || line[0] == ';' || line[0] == '[' || !eq) continue;
        *eq = 0;
        char *key = line, *val = eq + 1;
        trim(key), trim(val);
        if (!strcmp(key, "name")) snprintf(k->info.title, sizeof k->info.title, "%s", val);
        else if (!strcmp(key, "author")) snprintf(k->info.author, sizeof k->info.author, "%s", val);
        else if (!strcmp(key, "scale")) k->info.scale = atoi(val);
    }
    free(f);
    return true;
}

/* ---- the stack ---- */

void hires_skins_free(void)
{
    for (int i = 0; i < nskins; i++) {
        Skin *k = &skins[i];
        for (int j = 0; k->e && j < SLOTS; j++)
            if (k->e[j].used && k->e[j].tex) hr_texture_free(k->e[j].tex), free(k->e[j].tex);
        for (int j = 0; k->pe && j < PATH_SLOTS; j++)
            if (k->pe[j].path) {
                free(k->pe[j].path);
                if (k->pe[j].tex) hr_texture_free(k->pe[j].tex), free(k->pe[j].tex);
            }
        free(k->e);
        free(k->pe);
        memset(k, 0, sizeof *k);
    }
    nskins = nloaded = nmissing = 0;
    hires_overlay_clear();
}

int hires_skins_load(const char *list, int n, void (*log)(const char *msg))
{
    hires_skins_free();
    logf_ = log;
    scale = n < 1 ? 1 : n;
    for (const char *p = list; p && *p;) {
        const char *c = strchr(p, ',');
        size_t len = c ? (size_t)(c - p) : strlen(p);
        char name[64];
        snprintf(name, sizeof name, "%.*s", (int)(len < sizeof name - 1 ? len : sizeof name - 1), p);
        trim(name);
        p = c ? c + 1 : NULL;
        if (!name[0] || nskins >= SKINS_MAX) continue;
        if (strchr(name, '/') || strchr(name, '\\') || strstr(name, "..")) {
            say("OpenGTA: skin %s: %s", name, "a skin name is a folder name under skins/");
            continue;
        }
        Skin *k = &skins[nskins];
        memset(k, 0, sizeof *k);
        snprintf(k->info.name, sizeof k->info.name, "%s", name);
        if (!read_ini(k)) {
            say("OpenGTA: skin %s: no skins/%s/skin.ini", name, name);
            continue;
        }
        if (!(k->e = calloc(SLOTS, sizeof *k->e)) || !(k->pe = calloc(PATH_SLOTS, sizeof *k->pe))) {
            free(k->e), free(k->pe);
            continue;
        }
        nskins++;
        hires_overlay_push(lookup, k);
        char m[300];
        snprintf(m, sizeof m, "OpenGTA: skin %s: \"%s\" by %s (made for hires=%d)", k->info.name, k->info.title,
                 k->info.author[0] ? k->info.author : "?", k->info.scale);
        if (log) log(m);
    }
    return nskins;
}

int hires_skins_count(void) { return nskins; }
const HiresSkinInfo *hires_skin_info(int i) { return i >= 0 && i < nskins ? &skins[i].info : NULL; }

void hires_skin_stats(int *loaded, int *missing)
{
    *loaded = nloaded;
    *missing = nmissing;
}
