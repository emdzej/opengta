/* Skins for fonts and frontend pictures (see skin_ui.h): the name registry and the lookups, on the skin
   stack's hires_skin_image (hires_skin.h). */
#include "skin_ui.h"
#include "hires_front.h"
#include "hires_skin.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- names ---- */

enum { NAMES = 64 };
typedef struct { const void *key; char name[24]; } Name;
static Name fonts[NAMES], pictures[NAMES];

/* "GTADATA/F_MHEAD.FON" -> "F_MHEAD" */
static void base_name(const char *rel, char *out, size_t cap)
{
    const char *b = rel;
    for (const char *p = rel; *p; p++)
        if (*p == '/' || *p == '\\') b = p + 1;
    size_t n = 0;
    for (; b[n] && b[n] != '.' && n + 1 < cap; n++) out[n] = (char)toupper((unsigned char)b[n]);
    out[n] = 0;
}

static void name_set(Name *t, const void *key, const char *rel)
{
    Name *slot = NULL;
    for (int i = 0; i < NAMES; i++) {
        if (t[i].key == key) { slot = &t[i]; break; }
        if (!t[i].key && !slot) slot = &t[i];
    }
    if (!slot) return;
    slot->key = key;
    base_name(rel, slot->name, sizeof slot->name);
}

static const char *name_get(const Name *t, const void *key)
{
    if (!key) return NULL;
    for (int i = 0; i < NAMES; i++)
        if (t[i].key == key) return t[i].name;
    return NULL;
}

void skin_ui_font_loaded(const Font *f, const char *rel) { if (f && rel) name_set(fonts, f, rel); }
void skin_ui_picture_loaded(const void *image, const char *rel) { if (image && rel) name_set(pictures, image, rel); }
const char *skin_ui_font_name(const Font *f) { return name_get(fonts, f); }
const char *skin_ui_picture_name(const void *image) { return name_get(pictures, image); }

/* ---- what was made from skin images: by path, size and tint ---- */

enum { SLOTS = 2048, HELD = 1536 };
typedef struct {
    char path[64];
    int w, h;
    uint32_t tint;     /* the palette hash a grey glyph was tinted for (0: not tinted) */
    uint32_t *px;      /* NULL: no skin has it */
    bool used, grey;   /* grey: a grey glyph image (tinted per palette) */
} Made;
static Made *made;
static int nmade;
static char signature[8 * 66];   /* the skins the cache was made from */

void skin_ui_flush(void)
{
    if (made)
        for (int i = 0; i < SLOTS; i++) free(made[i].px), made[i] = (Made){ 0 };
    nmade = 0;
}

void skin_ui_forget_font(const Font *f)
{
    for (int i = 0; i < NAMES; i++)
        if (fonts[i].key == f) fonts[i] = (Name){ 0 };
}

/* False when no skin is loaded. The cache starts over when the loaded skins changed. */
static bool sync(void)
{
    char sig[sizeof signature];
    size_t o = 0;
    sig[0] = 0;
    for (int i = 0; i < hires_skins_count(); i++) {
        const HiresSkinInfo *k = hires_skin_info(i);
        if (k) o += (size_t)snprintf(sig + o, sizeof sig - o, "%s,", k->name);
        if (o >= sizeof sig) break;
    }
    if (strcmp(sig, signature)) {
        skin_ui_flush();
        memcpy(signature, sig, sizeof sig);
    }
    return sig[0] != 0;
}

static Made *slot(const char *path, int w, int h, uint32_t tint, bool *found)
{
    if (!made && !(made = calloc(SLOTS, sizeof *made))) return NULL;
    uint32_t k = 2166136261u ^ (uint32_t)(w * 7919 + h) ^ tint;
    for (const char *p = path; *p; p++) k = (k ^ (uint8_t)*p) * 16777619u;
    for (uint32_t i = 0; i < SLOTS; i++) {
        Made *e = &made[(k + i) & (SLOTS - 1)];
        if (!e->used) {
            *found = false;
            if (nmade >= HELD) {   /* full: start over */
                skin_ui_flush();
                return slot(path, w, h, tint, found);
            }
            return e;
        }
        if (e->w == w && e->h == h && e->tint == tint && !strcmp(e->path, path)) { *found = true; return e; }
    }
    *found = false;
    return NULL;
}

/* The skin image at path, resampled to w x h (premultiplied). */
static uint32_t *resampled(HiresTexture *t, int w, int h)
{
    const uint32_t *pm = hr_premultiplied(t);
    uint32_t *px = malloc((size_t)w * h * 4);
    if (px) hires_ui_resample(pm, t->w, t->h, px, w, h);
    return px;
}

static bool is_grey(const HiresTexture *t)
{
    for (int i = 0; i < t->w * t->h; i++) {
        uint32_t c = t->rgba[i];
        if (!(c >> 24)) continue;
        if ((c & 0xff) != (c >> 8 & 0xff) || (c & 0xff) != (c >> 16 & 0xff)) return false;
    }
    return true;
}

/* A grey glyph image tinted: grey g -> g x (the original glyph's average colour in this palette) / (the
   image's average grey), per channel, clamped. */
static void tint(uint32_t *px, int n, const uint8_t *g, int gw, int gh, const uint32_t *clut, int step, bool xrgb)
{
    uint64_t sum[3] = { 0, 0, 0 }, cnt = 0, grey = 0, gcnt = 0;
    for (int i = 0; i < gw * gh; i++)
        if (g[i]) {
            uint32_t c = clut[g[i] * step];
            if (xrgb) c = hr_from_xrgb(c, 0xff);
            sum[0] += c & 0xff, sum[1] += c >> 8 & 0xff, sum[2] += c >> 16 & 0xff, cnt++;
        }
    for (int i = 0; i < n; i++) {
        uint32_t a = px[i] >> 24;
        if (a) grey += (px[i] & 0xff) * 255 / a, gcnt++;   /* un-premultiplied */
    }
    if (!cnt || !gcnt || !grey) return;
    const uint64_t mg = grey / gcnt ? grey / gcnt : 1;
    for (int i = 0; i < n; i++) {
        uint32_t p = px[i], a = p >> 24, out = a << 24;
        for (int ch = 0; ch < 3; ch++) {
            uint64_t v = (p >> (8 * ch) & 0xff) * (sum[ch] / cnt) / mg;
            if (v > a) v = a;
            out |= (uint32_t)v << (8 * ch);
        }
        px[i] = out;
    }
}

const uint32_t *skin_ui_glyph(const Font *f, int code, const uint8_t *px, int gw, int gh, const uint32_t *clut, int step,
                              bool xrgb, uint32_t chash, int n)
{
    if (!sync()) return NULL;
    const char *name = skin_ui_font_name(f);
    if (!name) return NULL;
    char path[64];
    snprintf(path, sizeof path, "font/%s/%d.png", name, code);
    const int w = gw * n, h = gh * n;
    bool found;
    /* first the untinted entry: it says whether the image exists and whether it is grey */
    Made *e = slot(path, w, h, 0, &found);
    if (!e) return NULL;
    if (!found) {
        HiresTexture *t = hires_skin_image(path, 4 * (w > h ? w : h), NULL);
        *e = (Made){ .w = w, .h = h, .used = true };
        snprintf(e->path, sizeof e->path, "%s", path);
        nmade++;
        if (!t) return NULL;
        e->px = resampled(t, w, h);
        e->tint = 0;
        e->grey = e->px && is_grey(t);
    }
    if (!e->px) return NULL;
    if (!e->grey) return e->px;
    Made *g = slot(path, w, h, chash | 1, &found);
    if (!g) return e->px;
    if (!found) {
        *g = (Made){ .w = w, .h = h, .tint = chash | 1, .used = true };
        snprintf(g->path, sizeof g->path, "%s", path);
        nmade++;
        if ((g->px = malloc((size_t)w * h * 4))) {
            memcpy(g->px, e->px, (size_t)w * h * 4);
            tint(g->px, w * h, px, gw, gh, clut, step, xrgb);
        }
    }
    return g->px ? g->px : e->px;
}

const uint32_t *skin_ui_picture(const char *name, int w, int h, int n)
{
    if (!sync() || !name) return NULL;
    char path[64];
    snprintf(path, sizeof path, "pictures/%s.png", name);
    const int W = w * n, H = h * n;
    bool found;
    Made *e = slot(path, W, H, 0, &found);
    if (!e) return NULL;
    if (found) return e->px;
    *e = (Made){ .w = W, .h = H, .used = true };
    snprintf(e->path, sizeof e->path, "%s", path);
    nmade++;
    HiresTexture *t = hires_skin_image(path, 4 * (W > H ? W : H), NULL);
    if (!t) return NULL;
    if ((e->px = resampled(t, W, H)))   /* opaque: what is transparent in the image is black */
        for (int i = 0; i < W * H; i++) e->px[i] |= 0xff000000u;
    return e->px;
}
