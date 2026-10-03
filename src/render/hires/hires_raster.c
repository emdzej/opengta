/* The hires rasteriser (see hires_raster.h). Integer arithmetic only (deterministic on every runner):
   sub-pixel coordinates, 16.16 texel coordinates, bilinear weights of 8 bits. */
#include "hires_raster.h"
#include <stdlib.h>

bool hr_nearest;

/* ---- textures ---- */

const uint32_t *hr_premultiplied(HiresTexture *t)
{
    if (t->pm) return t->pm;
    size_t n = (size_t)t->w * (size_t)t->h;
    if (!(t->pm = malloc(n * 4))) return t->rgba;
    for (size_t i = 0; i < n; i++) {
        uint32_t c = t->rgba[i], a = c >> 24;
        if (a == 0xff) t->pm[i] = c;
        else if (a == 0) t->pm[i] = 0;
        else {
            uint32_t rb = ((c & 0x00ff00ffu) * a + 0x00800080u) >> 8 & 0x00ff00ffu;
            uint32_t g = ((c & 0x0000ff00u) * a + 0x00008000u) >> 8 & 0x0000ff00u;
            t->pm[i] = a << 24 | rb | g;
        }
    }
    return t->pm;
}

void hr_texture_free(HiresTexture *t)
{
    if (!t) return;
    free(t->pm);
    if (t->owned) free(t->rgba);
    t->pm = NULL;
    t->rgba = NULL;
}

/* ---- sampling ---- */

/* a + (b - a) * f / 256 on all four bytes (f = 0..255) */
static inline uint32_t lerp4(uint32_t a, uint32_t b, uint32_t f)
{
    const uint32_t g = 256 - f;
    uint32_t rb = ((a & 0x00ff00ffu) * g + (b & 0x00ff00ffu) * f) >> 8 & 0x00ff00ffu;
    uint32_t ag = ((a >> 8 & 0x00ff00ffu) * g + (b >> 8 & 0x00ff00ffu) * f) & 0xff00ff00u;
    return rb | ag;
}

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* Texel at (u, v), 16.16 texel coordinates already moved back by half a texel (so the integer part
   is the top-left texel of the 2 x 2 footprint). Clamped to the texture: no bleeding into whatever
   lies next to it in the original's 256 x 256 page. */
static inline uint32_t sample(const uint32_t *tx, int w, int h, int32_t u, int32_t v)
{
    if (hr_nearest) {
        int x = clampi((u + 0x8000) >> 16, 0, w - 1), y = clampi((v + 0x8000) >> 16, 0, h - 1);
        return tx[y * w + x];
    }
    int x0 = u >> 16, y0 = v >> 16;
    uint32_t fx = (uint32_t)(u >> 8) & 0xff, fy = (uint32_t)(v >> 8) & 0xff;
    int xa = clampi(x0, 0, w - 1), xb = clampi(x0 + 1, 0, w - 1);
    int ya = clampi(y0, 0, h - 1), yb = clampi(y0 + 1, 0, h - 1);
    const uint32_t *ra = tx + ya * w, *rb = tx + yb * w;
    return lerp4(lerp4(ra[xa], ra[xb], fx), lerp4(rb[xa], rb[xb], fx), fy);
}

/* dst * (255 - a) / 255 + s, s premultiplied */
static inline uint32_t over(uint32_t s, uint32_t d)
{
    uint32_t a = s >> 24;
    if (a == 0xff) return s;
    uint32_t k = 256 - a - (a >> 7);   /* about (255 - a) * 256 / 255 */
    uint32_t rb = ((d & 0x00ff00ffu) * k >> 8 & 0x00ff00ffu) + (s & 0x00ff00ffu);
    uint32_t g = ((d & 0x0000ff00u) * k >> 8 & 0x0000ff00u) + (s & 0x0000ff00u);
    return 0xff000000u | rb | g;
}

/* the blend table's (texel + screen) / 2 where the texel covers the pixel (a / 255 of it) */
static inline uint32_t blend50(uint32_t s, uint32_t d)
{
    uint32_t a = s >> 24;
    uint32_t ha = a >> 1;
    uint32_t k = 256 - ha - (ha >> 7);
    uint32_t rb = ((d & 0x00ff00ffu) * k >> 8 & 0x00ff00ffu) + ((s & 0x00fe00feu) >> 1);
    uint32_t g = ((d & 0x0000ff00u) * k >> 8 & 0x0000ff00u) + ((s & 0x0000fe00u) >> 1);
    return 0xff000000u | rb | g;
}

/* Bilinear without clamping, for spans whose every footprint lies inside the texture. */
static inline uint32_t sample_inside(const uint32_t *tx, int w, int32_t u, int32_t v)
{
    const uint32_t *r = tx + (v >> 16) * w + (u >> 16);
    uint32_t fx = (uint32_t)(u >> 8) & 0xff, fy = (uint32_t)(v >> 8) & 0xff;
    return lerp4(lerp4(r[0], r[1], fx), lerp4(r[w], r[w + 1], fx), fy);
}

#define SPAN_LOOP(SAMPLE)                                                                                        \
    do {                                                                                                         \
        if (mode == HR_OPAQUE)                                                                                   \
            for (; n > 0; n--, dst += stride, u += du, v += dv) *dst = SAMPLE | 0xff000000u;                     \
        else if (mode == HR_KEYED)                                                                               \
            for (; n > 0; n--, dst += stride, u += du, v += dv) {                                                \
                uint32_t s = SAMPLE;                                                                             \
                if (s >> 24) *dst = over(s, *dst);                                                               \
            }                                                                                                    \
        else                                                                                                     \
            for (; n > 0; n--, dst += stride, u += du, v += dv) {                                                \
                uint32_t s = SAMPLE;                                                                             \
                if (s >> 24) *dst = blend50(s, *dst);                                                            \
            }                                                                                                    \
    } while (0)

/* n pixels from dst (step `stride` words), texture coordinates (u, v) + i (du, dv) */
static void span(uint32_t *dst, int n, int stride, int mode, HiresTexture *tex, int32_t u, int32_t v, int32_t du,
                 int32_t dv)
{
    if (n <= 0) return;
    const int w = tex->w, h = tex->h;
    const uint32_t *tx = mode == HR_OPAQUE ? tex->rgba : hr_premultiplied(tex);
    u -= 0x8000, v -= 0x8000;
    if (hr_nearest) {
        SPAN_LOOP(sample(tx, w, h, u, v));
        return;
    }
    /* the footprint of both ends inside the texture (coordinates are linear along the span) */
    const int64_t ue = (int64_t)u + (int64_t)du * (n - 1), ve = (int64_t)v + (int64_t)dv * (n - 1);
    const int64_t umax = (int64_t)(w - 1) << 16, vmax = (int64_t)(h - 1) << 16;
    if (u >= 0 && v >= 0 && ue >= 0 && ve >= 0 && u < umax && v < vmax && ue < umax && ve < vmax)
        SPAN_LOOP(sample_inside(tx, w, u, v));
    else
        SPAN_LOOP(sample(tx, w, h, u, v));
}

/* ---- trapezoids ---- */

static inline int64_t ceil_div(int64_t a, int64_t b)   /* b > 0 */
{
    return a >= 0 ? (a + b - 1) / b : -((-a) / b);
}
/* first pixel whose centre is at or after sub-pixel coordinate p */
static inline int64_t first_px(int64_t p) { return ceil_div(p - HR_SUB / 2, HR_SUB); }

/* texture coordinate = c0 + cs * s + ct * t (16.16 texels), s along the edges, t from edge a to b */
typedef struct { int64_t u0, us, ut, v0, vs, vt; } UvMap;

/* The orientation the faithful mappers end up with: u = s (mirrored: 1 - s), v = t; a tile taken from the
   rotation cache is the original turned 90 degrees (Poly_RotateTile90 0x49b044: dst[x][63 - y] =
   src[y][x]), i.e. texel (u, v) of the copy is texel (v, 1 - u) of the tile. */
static UvMap uv_map(const HiresTexture *tex, bool mirror, bool rot)
{
    int64_t W = (int64_t)tex->w << 16, H = (int64_t)tex->h << 16;
    /* un = a0 + as s, vn = t */
    int64_t a0 = mirror ? 1 : 0, as = mirror ? -1 : 1;
    UvMap m;
    if (!rot) m = (UvMap){ a0 * W, as * W, 0, 0, 0, H };
    else m = (UvMap){ 0, 0, W, (1 - a0) * H, -as * H, 0 };
    return m;
}

/* One trapezoid: `rows` run along the axis from edge a (at coordinate pa, spanning lo_a..hi_a across)
   to edge b. For FaceHoriz the rows are screen rows; for FaceVert screen columns (transposed). */
static void trapezoid(const HrTarget *T, bool vertical, int mode, HiresTexture *tex, const UvMap *m, int64_t pa,
                      int64_t lo_a, int64_t hi_a, int64_t pb, int64_t lo_b, int64_t hi_b)
{
    int64_t d = pb - pa;
    if (d == 0) return;
    const int64_t pmin = d > 0 ? pa : pb, pmax = d > 0 ? pb : pa;
    const int along = vertical ? T->w : T->h, across = vertical ? T->h : T->w;
    int64_t r0 = first_px(pmin), r1 = first_px(pmax);
    if (r0 < 0) r0 = 0;
    if (r1 > along) r1 = along;
    for (int64_t r = r0; r < r1; r++) {
        int64_t c = r * HR_SUB + HR_SUB / 2, nt = c - pa;
        int64_t lo = lo_a + (lo_b - lo_a) * nt / d, hi = hi_a + (hi_b - hi_a) * nt / d;
        int64_t dx = hi - lo;
        if (dx <= 0) continue;
        int64_t c0 = first_px(lo), c1 = first_px(hi);
        if (c0 < 0) c0 = 0;
        if (c1 > across) c1 = across;
        if (c1 <= c0) continue;
        int64_t ub = m->u0 + m->ut * nt / d, vb = m->v0 + m->vt * nt / d;
        int64_t xs = c0 * HR_SUB + HR_SUB / 2 - lo;
        int32_t u = (int32_t)(ub + m->us * xs / dx), v = (int32_t)(vb + m->vs * xs / dx);
        int32_t du = (int32_t)(m->us * HR_SUB / dx), dv = (int32_t)(m->vs * HR_SUB / dx);
        uint32_t *dst = vertical ? T->px + c0 * T->pitch + r : T->px + r * T->pitch + c0;
        span(dst, (int)(c1 - c0), vertical ? T->pitch : 1, mode, tex, u, v, du, dv);
    }
}

/* Poly_DrawFaceHoriz 0x497035's orientation: rotation 180/270 toggles the mirror bit and swaps the edges
   unless the flip bit is set; 0/90 swap them if it is set; 90/270 use the rotated tile. */
void hr_face_horiz(const HrTarget *T, uint32_t face, HiresTexture *tex, int32_t xl_a, int32_t xr_a, int32_t xl_b,
                   int32_t xr_b, int32_t y_a, int32_t y_b)
{
    if (!tex) return;
    const bool rot = (face & 0xc000) == 0x4000 || (face & 0xc000) == 0xc000;
    uint32_t fw = face;
    bool swap;
    if ((face & 0xc000) >= 0x8000) fw ^= 0x400000, swap = !(face & 0x200000);
    else swap = face & 0x200000;
    if (swap) {
        int32_t t;
        t = xl_a, xl_a = xl_b, xl_b = t;
        t = xr_a, xr_a = xr_b, xr_b = t;
        t = y_a, y_a = y_b, y_b = t;
    }
    UvMap m = uv_map(tex, fw & 0x400000, rot);
    trapezoid(T, false, fw & 0x80 ? HR_KEYED : HR_OPAQUE, tex, &m, y_a, xl_a, xr_a, y_b, xl_b, xr_b);
}

/* Poly_DrawFaceVert 0x497332's: rotation + 90 degrees, flip and mirror exchanged, mirror inverted, then as
   FaceHoriz with columns. */
void hr_face_vert(const HrTarget *T, uint32_t face, HiresTexture *tex, int32_t yt_a, int32_t yt_b, int32_t yb_a,
                  int32_t yb_b, int32_t x_a, int32_t x_b)
{
    if (!tex) return;
    face = (face & 0xffff3fffu) | (((face & 0xc000) + 0x4000) & 0xc000);
    const bool rot = (face & 0xc000) == 0x4000 || (face & 0xc000) == 0xc000;
    uint32_t f = face & 0xff9fffffu;
    if (face & 0x400000) f |= 0x200000;
    if (face & 0x200000) f |= 0x400000;
    f ^= 0x400000;
    uint32_t fw = f;
    bool swap;
    if ((f & 0xc000) >= 0x8000) fw ^= 0x400000, swap = !(f & 0x200000);
    else swap = f & 0x200000;
    if (swap) {
        int32_t t;
        t = x_a, x_a = x_b, x_b = t;
        t = yt_a, yt_a = yt_b, yt_b = t;
        t = yb_a, yb_a = yb_b, yb_b = t;
    }
    UvMap m = uv_map(tex, fw & 0x400000, rot);
    trapezoid(T, true, fw & 0x80 ? HR_KEYED : HR_OPAQUE, tex, &m, x_a, yt_a, yb_a, x_b, yt_b, yb_b);
}

/* ---- polygons ---- */

void hr_polygon(const HrTarget *T, int mode, HiresTexture *tex, int n, const int32_t x[], const int32_t y[],
                const int32_t u[], const int32_t v[])
{
    if (!tex || n < 3) return;
    int64_t ymin = y[0], ymax = y[0], xmin = x[0], xmax = x[0];
    for (int i = 1; i < n; i++) {
        if (y[i] < ymin) ymin = y[i];
        if (y[i] > ymax) ymax = y[i];
        if (x[i] < xmin) xmin = x[i];
        if (x[i] > xmax) xmax = x[i];
    }
    if (xmax < 0 || ymax < 0 || xmin >= (int64_t)T->w * HR_SUB || ymin >= (int64_t)T->h * HR_SUB) return;
    int64_t r0 = first_px(ymin), r1 = first_px(ymax);
    if (r0 < 0) r0 = 0;
    if (r1 > T->h) r1 = T->h;
    for (int64_t r = r0; r < r1; r++) {
        const int64_t c = r * HR_SUB + HR_SUB / 2;
        int64_t xl = INT64_MAX, xr = INT64_MIN, ul = 0, vl = 0, ur = 0, vr = 0;
        for (int i = 0; i < n; i++) {
            int j = i + 1 == n ? 0 : i + 1;
            int64_t ya = y[i], yb = y[j];
            if (ya == yb) continue;
            if (c < (ya < yb ? ya : yb) || c >= (ya < yb ? yb : ya)) continue;
            int64_t dy = yb - ya, k = c - ya;
            int64_t xc = x[i] + ((int64_t)x[j] - x[i]) * k / dy;
            int64_t uc = u[i] + ((int64_t)u[j] - u[i]) * k / dy, vc = v[i] + ((int64_t)v[j] - v[i]) * k / dy;
            if (xc < xl) xl = xc, ul = uc, vl = vc;
            if (xc > xr) xr = xc, ur = uc, vr = vc;
        }
        int64_t dx = xr - xl;
        if (dx <= 0) continue;
        int64_t c0 = first_px(xl), c1 = first_px(xr);
        if (c0 < 0) c0 = 0;
        if (c1 > T->w) c1 = T->w;
        if (c1 <= c0) continue;
        int64_t xs = c0 * HR_SUB + HR_SUB / 2 - xl;
        int32_t uu = (int32_t)(ul + (ur - ul) * xs / dx), vv = (int32_t)(vl + (vr - vl) * xs / dx);
        int32_t du = (int32_t)((ur - ul) * HR_SUB / dx), dv = (int32_t)((vr - vl) * HR_SUB / dx);
        span(T->px + r * T->pitch + c0, (int)(c1 - c0), 1, mode, tex, uu, vv, du, dv);
    }
}
