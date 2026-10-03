/* The hires 2D layer: display list, glyphs, pixel-art scaling (see hires_text.h). Integer arithmetic only. */
#include "hires_text.h"
#include "hires_front.h"
#include "hires_hud.h"
#include "skin_ui.h"
#include <stdlib.h>
#include <string.h>

bool hires_ui_recording;
int hires_ui_filter = HR_UI_SCALE;

bool hires_ui_set_filter(const char *name)
{
    static const char *const NAMES[] = { "nearest", "bilinear", "scale" };
    for (int i = 0; i < 3; i++)
        if (!strcmp(name, NAMES[i])) {
            if (hires_ui_filter != i) hires_ui_art_flush();
            hires_ui_filter = i;
            return true;
        }
    return false;
}

/* ---- the display list ---- */

enum { CMDS_MAX = 1 << 15 };
static HrUiCmd *cmds;
static int ncmds, capcmds;

static struct { const uint32_t *clut; int step; uint32_t h; } memo[16];
static int nmemo;

void hires_ui_begin(void)
{
    ncmds = 0;
    nmemo = 0;
    hires_ui_recording = true;
}

void hires_ui_end(void) { hires_ui_recording = false; }
int hires_ui_count(void) { return ncmds; }

HrUiCmd *hires_ui_push(int kind)
{
    if (ncmds == capcmds) {
        if (capcmds >= CMDS_MAX) return NULL;
        int nc = capcmds ? capcmds * 2 : 512;
        HrUiCmd *n = realloc(cmds, (size_t)nc * sizeof *n);
        if (!n) return NULL;
        cmds = n, capcmds = nc;
    }
    HrUiCmd *c = &cmds[ncmds++];
    memset(c, 0, sizeof *c);
    c->kind = (uint8_t)kind;
    return c;
}

uint32_t hires_ui_clut_hash(const uint32_t *clut, int step)
{
    for (int i = 0; i < nmemo; i++)
        if (memo[i].clut == clut && memo[i].step == step) return memo[i].h;
    uint32_t h = 2166136261u;
    for (int e = 0; e < 256; e++) h = (h ^ clut[e * step]) * 16777619u;
    if (nmemo < 16) memo[nmemo].clut = clut, memo[nmemo].step = step, memo[nmemo++].h = h;
    return h;
}

/* ---- compositing ---- */

/* d * (255 - a) / 255 + s, s premultiplied (hires_raster.c's `over`) */
static inline uint32_t over(uint32_t s, uint32_t d)
{
    uint32_t a = s >> 24;
    if (a == 0xff) return s;
    uint32_t k = 256 - a - (a >> 7);
    uint32_t rb = ((d & 0x00ff00ffu) * k >> 8 & 0x00ff00ffu) + (s & 0x00ff00ffu);
    uint32_t g = ((d & 0x0000ff00u) * k >> 8 & 0x0000ff00u) + (s & 0x0000ff00u);
    return 0xff000000u | rb | g;
}

void hires_ui_blit(const HrTarget *t, const uint32_t *pm, int pw, int sx, int sy, int sw, int sh, int dx, int dy)
{
    if (dx < 0) sx -= dx, sw += dx, dx = 0;
    if (dy < 0) sy -= dy, sh += dy, dy = 0;
    if (dx + sw > t->w) sw = t->w - dx;
    if (dy + sh > t->h) sh = t->h - dy;
    for (int r = 0; r < sh; r++) {
        const uint32_t *s = pm + (size_t)(sy + r) * pw + sx;
        uint32_t *d = t->px + (size_t)(dy + r) * t->pitch + dx;
        for (int c = 0; c < sw; c++) {
            uint32_t p = s[c];
            if (p >> 24) d[c] = over(p, d[c]);
        }
    }
}

void hires_ui_stretch(const HrTarget *t, HiresTexture *tex, int x0, int y0, int x1, int y1, int32_t su0, int32_t sv0,
                      int32_t su1, int32_t sv1)
{
    if (!tex || x1 <= x0 || y1 <= y0) return;
    /* top left, top right, bottom right, bottom left */
    const int32_t x[4] = { x0 * HR_SUB, x1 * HR_SUB, x1 * HR_SUB, x0 * HR_SUB };
    const int32_t y[4] = { y0 * HR_SUB, y0 * HR_SUB, y1 * HR_SUB, y1 * HR_SUB };
    const int32_t u[4] = { su0, su1, su1, su0 }, v[4] = { sv0, sv0, sv1, sv1 };
    hr_polygon(t, HR_KEYED, tex, 4, x, y, u, v);
}

/* ---- pixel art scaling ---- */


/* display format colour (R, G, B bytes, 0xAABBGGRR) of index e; 0 is transparent */
static bool cx;   /* the palette being converted is 0x00RRGGBB */
static inline uint32_t colour(const uint32_t *clut, int step, uint8_t e)
{
    if (!e) return 0;
    return cx ? hr_from_xrgb(clut[e * step], 0xff) : clut[e * step] | 0xff000000u;
}

/* Scale2x (EPX) of a w x h index image into (2w) x (2h); neighbours outside are the edge pixel. */
static void scale2x(const uint8_t *s, int w, int h, uint8_t *d)
{
    const int dw = 2 * w;
    for (int y = 0; y < h; y++) {
        const uint8_t *r = s + y * w, *up = y ? r - w : r, *dn = y + 1 < h ? r + w : r;
        uint8_t *o = d + (size_t)2 * y * dw;
        for (int x = 0; x < w; x++) {
            const int xl = x ? x - 1 : x, xr = x + 1 < w ? x + 1 : x;
            const uint8_t E = r[x], B = up[x], H = dn[x], D = r[xl], F = r[xr];
            uint8_t e0 = E, e1 = E, e2 = E, e3 = E;
            if (B != H && D != F) {
                e0 = D == B ? D : E;
                e1 = B == F ? F : E;
                e2 = D == H ? D : E;
                e3 = H == F ? F : E;
            }
            o[2 * x] = e0, o[2 * x + 1] = e1, o[dw + 2 * x] = e2, o[dw + 2 * x + 1] = e3;
        }
    }
}

/* Scale3x into (3w) x (3h) */
static void scale3x(const uint8_t *s, int w, int h, uint8_t *d)
{
    const int dw = 3 * w;
    for (int y = 0; y < h; y++) {
        const uint8_t *r = s + y * w, *up = y ? r - w : r, *dn = y + 1 < h ? r + w : r;
        uint8_t *o = d + (size_t)3 * y * dw;
        for (int x = 0; x < w; x++) {
            const int xl = x ? x - 1 : x, xr = x + 1 < w ? x + 1 : x;
            const uint8_t A = up[xl], B = up[x], C = up[xr], D = r[xl], E = r[x], F = r[xr], G = dn[xl], H = dn[x],
                          I = dn[xr];
            uint8_t e[9] = { E, E, E, E, E, E, E, E, E };
            if (B != H && D != F) {
                e[0] = D == B ? D : E;
                e[1] = (D == B && E != C) || (B == F && E != A) ? B : E;
                e[2] = B == F ? F : E;
                e[3] = (D == B && E != G) || (D == H && E != A) ? D : E;
                e[5] = (B == F && E != I) || (H == F && E != C) ? F : E;
                e[6] = D == H ? D : E;
                e[7] = (D == H && E != I) || (H == F && E != G) ? H : E;
                e[8] = H == F ? F : E;
            }
            for (int j = 0; j < 3; j++)
                for (int i = 0; i < 3; i++) o[j * dw + 3 * x + i] = e[j * 3 + i];
        }
    }
}

/* The index image scaled n x by the Scale family (n = 1..4) into out ((w n) x (h n)); false: no memory. */
static bool scale_indices(const uint8_t *src, int w, int h, int n, uint8_t *out)
{
    if (n == 1) { memcpy(out, src, (size_t)w * h); return true; }
    if (n == 2) { scale2x(src, w, h, out); return true; }
    if (n == 3) { scale3x(src, w, h, out); return true; }
    uint8_t *mid = malloc((size_t)w * h * 4);
    if (!mid) return false;
    scale2x(src, w, h, mid);
    scale2x(mid, 2 * w, 2 * h, out);
    free(mid);
    return true;
}

static inline uint32_t lerp4(uint32_t a, uint32_t b, uint32_t f)
{
    const uint32_t arb = a & 0x00ff00ffu, aag = a >> 8 & 0x00ff00ffu;
    const uint32_t rb = (arb + (((b & 0x00ff00ffu) - arb) * f >> 8)) & 0x00ff00ffu;
    const uint32_t ag = ((aag << 8) + ((b >> 8 & 0x00ff00ffu) - aag) * f) & 0xff00ff00u;
    return rb | ag;
}

/* Bilinear n x of premultiplied pixels (texel centres on (i + 1/2) n, clamped at the edges). */
static void bilinear(const uint32_t *s, int w, int h, int n, uint32_t *d)
{
    const int W = w * n, H = h * n;
    for (int Y = 0; Y < H; Y++) {
        int v = (2 * Y + 1) * 128 / n - 128;   /* 1/256 texel */
        int y0 = v < 0 ? 0 : v >> 8, y1 = y0 + 1 < h ? y0 + 1 : h - 1;
        uint32_t fy = v < 0 ? 0 : (uint32_t)v & 0xff;
        for (int X = 0; X < W; X++) {
            int u = (2 * X + 1) * 128 / n - 128;
            int x0 = u < 0 ? 0 : u >> 8, x1 = x0 + 1 < w ? x0 + 1 : w - 1;
            uint32_t fx = u < 0 ? 0 : (uint32_t)u & 0xff;
            const uint32_t *ra = s + y0 * w, *rb = s + y1 * w;
            d[(size_t)Y * W + X] = lerp4(lerp4(ra[x0], ra[x1], fx), lerp4(rb[x0], rb[x1], fx), fy);
        }
    }
}

/* ---- the art cache ---- */

enum { ART_SLOTS = 4096, ART_MAX = 3072 };
typedef struct {
    const uint8_t *src;
    int stride, w, h, n, filter;
    bool xrgb;
    uint32_t chash;
    uint32_t *px;
} ArtEntry;
static ArtEntry *art;
static int nart;

void hires_ui_art_flush(void)
{
    if (art)
        for (int i = 0; i < ART_SLOTS; i++) free(art[i].px), art[i] = (ArtEntry){ 0 };
    nart = 0;
}

static uint32_t *make_art(const uint8_t *src, int stride, int w, int h, const uint32_t *clut, int step, int n)
{
    const size_t W = (size_t)w * n, H = (size_t)h * n;
    uint32_t *out = malloc(W * H * 4);
    if (!out) return NULL;
    if (hires_ui_filter == HR_UI_SCALE) {
        uint8_t *idx = malloc((size_t)w * h), *big = malloc(W * H);
        bool ok = idx && big;
        if (ok) {
            for (int y = 0; y < h; y++) memcpy(idx + (size_t)y * w, src + (size_t)y * stride, (size_t)w);
            ok = scale_indices(idx, w, h, n, big);
        }
        if (ok)
            for (size_t i = 0; i < W * H; i++) out[i] = colour(clut, step, big[i]);
        free(idx);
        free(big);
        if (!ok) { free(out); return NULL; }
        return out;
    }
    if (hires_ui_filter == HR_UI_BILINEAR) {
        uint32_t *one = malloc((size_t)w * h * 4);
        if (!one) { free(out); return NULL; }
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) one[y * w + x] = colour(clut, step, src[(size_t)y * stride + x]);
        bilinear(one, w, h, n, out);
        free(one);
        return out;
    }
    for (size_t Y = 0; Y < H; Y++)
        for (size_t X = 0; X < W; X++) out[Y * W + X] = colour(clut, step, src[(Y / n) * stride + X / n]);
    return out;
}

const uint32_t *hires_ui_art(const uint8_t *src, int stride, int w, int h, const uint32_t *clut, int step, bool xrgb,
                             uint32_t chash, int n)
{
    if (!src || !clut || w <= 0 || h <= 0 || n < 1) return NULL;
    if (!art && !(art = calloc(ART_SLOTS, sizeof *art))) return NULL;
    uint32_t k = (uint32_t)(uintptr_t)src * 0x9e3779b1u ^ chash ^ (uint32_t)(w * 131 + h * 7 + n * 1031);
    k ^= k >> 15;
    for (uint32_t i = 0; i < ART_SLOTS; i++) {
        ArtEntry *e = &art[(k + i) & (ART_SLOTS - 1)];
        if (!e->px) {
            if (nart >= ART_MAX) {   /* full: start over */
                hires_ui_art_flush();
                return hires_ui_art(src, stride, w, h, clut, step, xrgb, chash, n);
            }
            cx = xrgb;
            uint32_t *px = make_art(src, stride, w, h, clut, step, n);
            if (!px) return NULL;
            *e = (ArtEntry){ src, stride, w, h, n, hires_ui_filter, xrgb, chash, px };
            nart++;
            return px;
        }
        if (e->src == src && e->stride == stride && e->w == w && e->h == h && e->n == n && e->chash == chash &&
            e->filter == hires_ui_filter && e->xrgb == xrgb)
            return e->px;
    }
    return NULL;
}

void hires_ui_font_freed(const Font *f)
{
    hires_ui_art_flush();   /* its glyphs' memory may come back as another font's */
    skin_ui_forget_font(f);
}

/* ---- glyphs ---- */

static int glyph_code(const Font *f, const uint8_t *px)
{
    for (int i = 0; i < f->count; i++)
        if (f->glyph[i].px == px) return f->first + i;
    return -1;
}

void hires_ui_glyph(const Surface *s, long dst, const Font *f, const uint8_t *glyph, int w, int row0, int h,
                    int clip_l, int clip_r, const uint32_t *clut, int step, bool xrgb)
{
    if (!f || !glyph || !clut || w <= 0 || h <= 0 || dst < 0 || s->stride <= 0) return;
    int vis = w - clip_l - clip_r;
    if (vis <= 0) return;
    HrUiCmd *c = hires_ui_push(HRC_GLYPH);
    if (!c) return;
    c->x = (int)(dst % s->stride), c->y = (int)(dst / s->stride), c->w = vis, c->h = h;
    c->u.g.font = f, c->u.g.px = glyph, c->u.g.gw = w, c->u.g.gh = f->height;
    c->u.g.sx = clip_l, c->u.g.sy = row0;
    c->u.g.clut = clut, c->u.g.step = step, c->u.g.xrgb = xrgb, c->u.g.chash = hires_ui_clut_hash(clut, step);
}

void hires_ui_glyph_rect(const Font *f, const uint8_t *glyph, int x0, int x1, int y0, int y1, const uint32_t *clut)
{
    if (!f || !glyph || !clut) return;
    HrUiCmd *c = hires_ui_push(HRC_GLYPH_RECT);
    if (!c) return;
    c->x = x0, c->y = y0, c->w = x1, c->h = y1 + 1;
    c->u.g.font = f, c->u.g.px = glyph, c->u.g.gh = f->height, c->u.g.clut = clut, c->u.g.step = 1;
    c->u.g.xrgb = true;
    c->u.g.gw = 0;
    for (int i = 0; i < f->count; i++)
        if (f->glyph[i].px == glyph) c->u.g.gw = f->glyph[i].w;
    c->u.g.chash = hires_ui_clut_hash(clut, 1);
}

/* The glyph at n x: the skin's (font/<FONT>/<code>.png) or the original scaled by the UI filter. */
static const uint32_t *glyph_art(const HrUiCmd *c, int n)
{
    const int code = glyph_code(c->u.g.font, c->u.g.px);
    if (code >= 0) {
        const uint32_t *k = skin_ui_glyph(c->u.g.font, code, c->u.g.px, c->u.g.gw, c->u.g.gh, c->u.g.clut,
                                          c->u.g.step, c->u.g.xrgb, c->u.g.chash, n);
        if (k) return k;
    }
    return hires_ui_art(c->u.g.px, c->u.g.gw, c->u.g.gw, c->u.g.gh, c->u.g.clut, c->u.g.step, c->u.g.xrgb,
                        c->u.g.chash, n);
}

static void replay_glyph(const HrTarget *t, const HrUiCmd *c, int n)
{
    if (c->u.g.gw <= 0 || c->u.g.gh <= 0) return;
    const uint32_t *pm = glyph_art(c, n);
    if (!pm) return;
    /* the faithful blitter's rows past the end of a surface row wrap onto the next; here they are clipped */
    hires_ui_blit(t, pm, c->u.g.gw * n, c->u.g.sx * n, c->u.g.sy * n, c->w * n, c->h * n, c->x * n, c->y * n);
}

/* Poly_DrawRect's texel mapping: umax + 1 texels from x0 over x1 - x0 pixels, vmax + 1 rows from y0 to y1
   inclusive. Drawn unless it misses the screen (the faithful 16-bit clip test). */
static void replay_glyph_rect(const HrTarget *t, const HrUiCmd *c, int n)
{
    if (c->u.g.gw <= 0 || c->u.g.gh <= 0) return;
    const uint32_t *pm = glyph_art(c, n);
    if (!pm) return;
    HiresTexture tex = { c->u.g.gw * n, c->u.g.gh * n, (uint32_t *)pm, (uint32_t *)pm, false };
    hires_ui_stretch(t, &tex, c->x * n, c->y * n, c->w * n, c->h * n, 0, 0, tex.w << 16, tex.h << 16);
}

void hires_ui_replay(const HrTarget *t, int n)
{
    for (int i = 0; i < ncmds; i++) {
        const HrUiCmd *c = &cmds[i];
        switch (c->kind) {
        case HRC_GLYPH: replay_glyph(t, c, n); break;
        case HRC_GLYPH_RECT: replay_glyph_rect(t, c, n); break;
        case HRC_SPRITE:
        case HRC_WORLD: hires_hud_replay(t, c, n); break;
        default: hires_front_replay(t, c, n); break;
        }
    }
}
