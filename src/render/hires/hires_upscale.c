/* Pixel-art upscalers for the original art (see hires_upscale.h). Integer arithmetic only, so every
   runner makes the same texels. Written from the algorithms' public descriptions:

   - Scale2x (Andrea Mazzoleni's AdvMAME2x, the same rule as EPX): each texel P becomes 2 x 2; a
     quarter takes the colour of its two outer neighbours when they are equal to each other and the
     other two neighbours are not equal to them, else P's colour. Only copies texels.
   - xBR (Hyllian), level 2, 2x: for each corner of a texel E, two weighted sums of colour distances
     over a 5 x 5 neighbourhood (minus its corners) tell whether an edge runs across the corner
     (between E's two neighbours toward it) rather than along it; if so the corner quarter is blended
     toward the closer of those two neighbours: half way, or three quarters (and the next quarter one
     quarter) when the edge is a shallow or a steep line (level 2's tests). Distances are in YUV, luma
     weighted most, plus the alpha difference. */
#include "hires_upscale.h"
#include "hires_tex.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static int kind = HIRES_UPSCALE_NONE, passes;
static const char *name = "none";

static const struct { const char *name; int kind, passes; } modes[] = {
    { "none", HIRES_UPSCALE_NONE, 0 },  { "scale2x", HIRES_UPSCALE_SCALE2X, 1 }, { "scale4x", HIRES_UPSCALE_SCALE2X, 2 },
    { "xbr", HIRES_UPSCALE_XBR, 1 },    { "xbr4", HIRES_UPSCALE_XBR, 2 },
};

bool hires_upscale_set(const char *s)
{
    hires_textures_reset();
    kind = HIRES_UPSCALE_NONE, passes = 0, name = "none";
    for (unsigned i = 0; i < sizeof modes / sizeof *modes; i++)
        if (s && !strcmp(s, modes[i].name)) {
            kind = modes[i].kind, passes = modes[i].passes, name = modes[i].name;
            return true;
        }
    return false;
}

const char *hires_upscale_name(void) { return name; }
int hires_upscale_factor(void) { return 1 << passes; }

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ---- Scale2x ---- */

static void scale2x(const uint32_t *s, int w, int h, uint32_t *o)
{
    const int ow = 2 * w;
    for (int y = 0; y < h; y++) {
        const uint32_t *up = s + clampi(y - 1, 0, h - 1) * w, *row = s + y * w, *dn = s + clampi(y + 1, 0, h - 1) * w;
        uint32_t *o0 = o + (size_t)2 * y * ow, *o1 = o0 + ow;
        for (int x = 0; x < w; x++) {
            const int xl = x > 0 ? x - 1 : 0, xr = x + 1 < w ? x + 1 : w - 1;
            const uint32_t A = up[x], B = row[xr], C = row[xl], D = dn[x], P = row[x];
            uint32_t e0 = P, e1 = P, e2 = P, e3 = P;
            if (A != D && C != B) {   /* the four rules share these two inequalities */
                if (C == A) e0 = A;
                if (A == B) e1 = B;
                if (D == C) e2 = C;
                if (B == D) e3 = D;
            }
            o0[2 * x] = e0, o0[2 * x + 1] = e1, o1[2 * x] = e2, o1[2 * x + 1] = e3;
        }
    }
}

/* ---- xBR level 2 ---- */

/* A texel in YUV (x 1000) and alpha, for the distances */
typedef struct { int y, u, v, a; } Yuv;

static Yuv yuv(uint32_t c)
{
    const int r = c & 0xff, g = c >> 8 & 0xff, b = c >> 16 & 0xff;
    return (Yuv){ 299 * r + 587 * g + 114 * b, -169 * r - 331 * g + 500 * b, 500 * r - 419 * g - 81 * b, (int)(c >> 24) * 1000 };
}

static inline int dist(const Yuv *p, const Yuv *q)
{
    return 48 * abs(p->y - q->y) + 7 * abs(p->u - q->u) + 6 * abs(p->v - q->v) + 48 * abs(p->a - q->a);
}

/* p moved toward q by k quarters; the colour weighted by alpha (a transparent texel's colour doesn't
   bleed into the visible one), straight if both are transparent */
static uint32_t mix(uint32_t p, uint32_t q, int k)
{
    const uint32_t wp = (uint32_t)(4 - k), wq = (uint32_t)k, ap = p >> 24, aq = q >> 24;
    const uint32_t ta = ap * wp + aq * wq;
    uint32_t out = (ta + 2) / 4 << 24;
    for (int sh = 0; sh < 24; sh += 8) {
        const uint32_t cp = p >> sh & 0xff, cq = q >> sh & 0xff;
        const uint32_t c = ta ? (cp * ap * wp + cq * aq * wq + ta / 2) / ta : (cp * wp + cq * wq + 2) / 4;
        out |= c << sh;
    }
    return out;
}

/* The 21 texels around E: offsets (dx, dy) in E's frame for the bottom-right corner. */
enum { A1, B1, C1, A0, A, B, C, C4, D0, D, E, F, F4, G0, G, H, I, I4, G5, H5, I5, NB };
static const signed char nb_off[NB][2] = {
    { -1, -2 }, { 0, -2 }, { 1, -2 }, { -2, -1 }, { -1, -1 }, { 0, -1 }, { 1, -1 }, { 2, -1 }, { -2, 0 }, { -1, 0 }, { 0, 0 },
    { 1, 0 },   { 2, 0 },  { -2, 1 }, { -1, 1 },  { 0, 1 },   { 1, 1 },  { 2, 1 },  { -1, 2 }, { 0, 2 },  { 1, 2 },
};
/* The four corners as rotations of the bottom-right one: (dx, dy) -> (r00 dx + r01 dy, r10 dx + r11 dy). */
static const signed char rot[4][4] = { { 1, 0, 0, 1 }, { 0, 1, -1, 0 }, { -1, 0, 0, -1 }, { 0, -1, 1, 0 } };

static void xbr2x(const uint32_t *s, int w, int h, uint32_t *o)
{
    /* the texels and their YUV with a border of 2 (clamped), so a neighbour is an offset */
    const int pw = w + 4, ph = h + 4;
    uint32_t *c = malloc((size_t)pw * ph * 4);
    Yuv *Y = malloc((size_t)pw * ph * sizeof *Y);
    if (!c || !Y) {   /* no memory for them: plain doubling */
        free(c), free(Y);
        for (int y = 0; y < 2 * h; y++)
            for (int x = 0; x < 2 * w; x++) o[(size_t)y * 2 * w + x] = s[(y >> 1) * w + (x >> 1)];
        return;
    }
    for (int y = 0; y < ph; y++)
        for (int x = 0; x < pw; x++) {
            const uint32_t t = s[clampi(y - 2, 0, h - 1) * w + clampi(x - 2, 0, w - 1)];
            c[y * pw + x] = t, Y[y * pw + x] = yuv(t);
        }
    ptrdiff_t off[4][NB];
    for (int r = 0; r < 4; r++)
        for (int k = 0; k < NB; k++) {
            const signed char *m = rot[r];
            const int dx = nb_off[k][0], dy = nb_off[k][1];
            off[r][k] = (ptrdiff_t)(m[2] * dx + m[3] * dy) * pw + (m[0] * dx + m[1] * dy);
        }
    const int ow = 2 * w;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const ptrdiff_t at = (ptrdiff_t)(y + 2) * pw + x + 2;
            const uint32_t e = c[at];
            uint32_t q[4] = { e, e, e, e };   /* quarters: top left, top right, bottom left, bottom right */
            for (int r = 0; r < 4; r++) {
                const ptrdiff_t *f = off[r];
                const uint32_t *C_ = c + at;
                const Yuv *P = Y + at;
                if (e == C_[f[F]] || e == C_[f[H]]) continue;
#define DI(a, b) dist(&P[f[a]], &P[f[b]])
                const int wd1 = DI(E, C) + DI(E, G) + DI(I, F4) + DI(I, H5) + 4 * DI(H, F);
                const int wd2 = DI(H, D) + DI(H, I5) + DI(F, I4) + DI(F, B) + 4 * DI(E, I);
                if (wd1 >= wd2) continue;
                const uint32_t px = DI(E, F) <= DI(E, H) ? C_[f[F]] : C_[f[H]];
                const int fg = DI(F, G), hc = DI(H, C);
#undef DI
                const bool left = 2 * fg <= hc && e != C_[f[G]] && C_[f[D]] != C_[f[G]];
                const bool up = fg >= 2 * hc && e != C_[f[C]] && C_[f[B]] != C_[f[C]];
                /* quarter at the signs (sx, sy) of this frame, in the texel's frame */
                const signed char *m = rot[r];
#define QUARTER(sx, sy) q[((m[2] * (sx) + m[3] * (sy)) > 0) * 2 + ((m[0] * (sx) + m[1] * (sy)) > 0)]
                if (left || up) {
                    QUARTER(1, 1) = mix(QUARTER(1, 1), px, 3);
                    if (left) QUARTER(-1, 1) = mix(QUARTER(-1, 1), px, 1);
                    if (up) QUARTER(1, -1) = mix(QUARTER(1, -1), px, 1);
                } else
                    QUARTER(1, 1) = mix(QUARTER(1, 1), px, 2);
#undef QUARTER
            }
            uint32_t *d = o + (size_t)2 * y * ow + 2 * x;
            d[0] = q[0], d[1] = q[1], d[ow] = q[2], d[ow + 1] = q[3];
        }
    free(c);
    free(Y);
}

/* ---- the hook ---- */

uint32_t *hires_upscale(const uint32_t *src, int w, int h, int k, int n)
{
    if (w <= 0 || h <= 0 || n < 0) return NULL;
    uint32_t *cur = malloc((size_t)w * h * 4);
    if (!cur) return NULL;
    memcpy(cur, src, (size_t)w * h * 4);
    for (int i = 0; i < n && k != HIRES_UPSCALE_NONE; i++) {
        uint32_t *o = malloc((size_t)w * h * 16);
        if (!o) { free(cur); return NULL; }
        if (k == HIRES_UPSCALE_SCALE2X) scale2x(cur, w, h, o);
        else xbr2x(cur, w, h, o);
        free(cur);
        cur = o, w *= 2, h *= 2;
    }
    return cur;
}

void hires_upscale_texture(HiresTexture *t)
{
    if (kind == HIRES_UPSCALE_NONE || !t || !t->rgba) return;
    uint32_t *px = hires_upscale(t->rgba, t->w, t->h, kind, passes);
    if (!px) return;
    if (t->owned) free(t->rgba);
    free(t->pm);
    *t = (HiresTexture){ t->w << passes, t->h << passes, px, NULL, true };
}
