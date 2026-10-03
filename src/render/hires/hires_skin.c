/* Skins (see hires_skin.h). */
#include "hires_skin.h"
#include "../../vfs.h"
#include "hires_png.h"
#include "hires_tex.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

HiresFileReader hires_skin_reader = vfs_read_all;

enum { SKINS_MAX = 8, SLOTS = 4096 };
enum { V_BASE = -1, V_MASK = -2 };   /* cache variants besides the remaps: the plain image, the paint mask */

typedef struct {
    bool used;
    int style, kind, n, variant;
    const uint32_t *clut;            /* recoloured sprites: the CLUT they were made for */
    HiresTexture *tex;               /* NULL: no such file */
} Entry;

typedef struct {
    HiresSkinInfo info;
    Entry *e;
    int nentries;
} Skin;

static Skin skins[SKINS_MAX];
static int nskins, scale = 1, nloaded, nmissing;
static void (*logf_)(const char *);

static void say(const char *fmt, const char *a, const char *b)
{
    if (!logf_) return;
    char m[512];
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

static Entry *slot(Skin *k, int style, int kind, int n, int variant, const uint32_t *clut, bool *found)
{
    uint32_t hh = (uint32_t)(style * 7919 + kind * 104729 + n * 31 + variant * 1299709) ^ (uint32_t)(uintptr_t)clut;
    hh *= 0x9e3779b1u;
    for (uint32_t i = 0; i < SLOTS; i++) {
        Entry *e = &k->e[(hh + i) & (SLOTS - 1)];
        if (!e->used) { *found = false; return k->nentries < SLOTS * 3 / 4 ? e : NULL; }
        if (e->style == style && e->kind == kind && e->n == n && e->variant == variant && e->clut == clut) {
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
    Entry *e = slot(k, a->style, a->kind, a->n, variant, NULL, &found);
    if (found) return e->tex;
    char path[256];
    snprintf(path, sizeof path, "skins/%s/style%03d/%s/%d%s.png", k->info.name, a->style, FOLDER[a->kind], a->n, suffix);
    int w, h;
    uint32_t *px = load_png(path, &w, &h);
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
    if (e) *e = (Entry){ true, a->style, a->kind, a->n, variant, NULL, t }, k->nentries++;
    return t;
}

static unsigned lum(uint32_t c) { return ((c & 0xff) * 77 + (c >> 8 & 0xff) * 150 + (c >> 16 & 0xff) * 29) >> 8; }

/* The paint colour a remap gives the original sprite: the average remapped colour (and the average own
   colour) of the texels whose colour the remap changes. */
static bool paint_colours(const HiresAsset *a, unsigned own[3], unsigned rem[3])
{
    const SpriteInfo *in = a->info;
    unsigned long so[3] = { 0, 0, 0 }, sr[3] = { 0, 0, 0 }, n = 0;
    for (int y = 0; y < in->h; y++)
        for (int x = 0; x < in->w; x++) {
            uint8_t t = in->data[y * 256 + x];
            if (!t) continue;
            uint32_t o = a->own_clut[t * 64] & 0xffffff, r = a->clut[t * 64] & 0xffffff;
            if (o == r) continue;
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
    Entry *e = slot(k, a->style, a->kind, a->n, a->remap, a->clut, &found);
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
    if (e) *e = (Entry){ true, a->style, a->kind, a->n, a->remap, a->clut, t == base ? NULL : t }, k->nentries++;
    return t;
}

/* A tile variant without its own image (a lid remap, a side's direction): the plain image with each
   channel scaled like the variant's CLUT scales the original tile on average (the remaps are mostly
   shading: shadowed pavements, darker walls). */
static HiresTexture *shaded(Skin *k, const HiresAsset *a, HiresTexture *base)
{
    bool found;
    Entry *e = slot(k, a->style, a->kind, a->n, a->remap, a->clut, &found);
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
    if (e) *e = (Entry){ true, a->style, a->kind, a->n, a->remap, a->clut, t }, k->nentries++;
    return t ? t : base;
}

static HiresTexture *lookup(void *ctx, const HiresAsset *a)
{
    Skin *k = ctx;
    if (a->kind < 0 || a->kind >= HIRES_KINDS) return NULL;
    char sfx[16];
    snprintf(sfx, sizeof sfx, "_r%d", a->remap);
    if (a->kind != HIRES_SPRITE || a->remap != 0) {
        HiresTexture *v = file(k, a, a->remap, sfx);
        if (v) return v;
    }
    HiresTexture *b = file(k, a, V_BASE, "");
    if (b && a->kind != HIRES_SPRITE && a->remap != 0 && a->texels && a->clut && a->own_clut && a->clut != a->own_clut)
        return shaded(k, a, b);
    if (!b || a->kind != HIRES_SPRITE || a->remap == 0 || !a->info || !a->own_clut || !a->clut) return b;
    HiresTexture *m = file(k, a, V_MASK, "_mask");
    if (!m) return b;
    HiresTexture *r = recoloured(k, a, b, m);
    return r ? r : b;
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
        free(k->e);
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
        if (!(k->e = calloc(SLOTS, sizeof *k->e))) continue;
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
