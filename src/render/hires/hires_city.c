/* The hires city pass: Render_DrawCity 0x4389f0 walked again, block for block and sprite for sprite in
   the faithful renderer's order, with every corner projected twice: the faithful renderer's integer
   value (`f`, which every visibility test and choice uses, so the same faces are drawn in the same
   order) and a sub-pixel value at N times the resolution (`h`, which is drawn). The block drawers below
   follow city.c line by line (see the comments there for the original's addresses); only the drawing
   calls differ. Nothing here writes game or faithful-renderer state. */
#include "hires_internal.h"
#include "../../exe.h"
#include "../../game/gmath.h"
#include "../city.h"
#include "../poly.h"
#include "../sprite.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int32_t f, h; } HV;   /* faithful pixel, hires sub-pixel */

enum { GS = 192 };                     /* grid columns/rows held (the faithful grid has 65; see below) */
static HV gx[2][GS * GS], gy[2][GS * GS];
static int pu, pl;                     /* upper and lower plane (0x5c1c20, 0x5bfbe0) */
static const uint8_t *slope_table;     /* 0x4b0c88 */
static HrTarget T;
static int64_t K;                      /* sub-pixels per faithful pixel: N * HR_SUB */
static const Style *S;

#define G (&render_rects[6])
#define GI(x, y) ((x) * GS + (y))
#define UX(x, y) gx[pu][GI(x, y)]
#define UY(x, y) gy[pu][GI(x, y)]
#define LX(x, y) gx[pl][GI(x, y)]
#define LY(x, y) gy[pl][GI(x, y)]

static inline int32_t mul32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }

/* ---- projection ---- */

/* Render_ProjectLayer 0x43b620 (city.c project_layer) for the faithful values, and the exact projection
   centre + (p - camera) * scale / depth, times N in sub-pixels, for the hires ones. The faithful grid
   wraps rows past 64 into the next column (city.c); here every corner has its own entry. */
static void project(const Viewport *vp, int z, int plane)
{
    int32_t d = z * 0x40 + vp->height;
    int32_t s = (vp->scale << 16) / d;
    int32_t step = s * 0x40, ystep = vp->squash ? s * 0x140 / 6 : step;
    int32_t sx = mul32(mul32(G->left * 0x40 - vp->x, vp->scale) / d + vp->cx, 0x10000);
    int32_t sy0 = vp->squash ? mul32(mul32(G->top * 0x40 - vp->y, vp->scale) / d, 5) / 6 + vp->cy
                             : mul32(G->top * 0x40 - vp->y, vp->scale) / d + vp->cy;
    static int64_t hy[GS];
    for (int j = 0; j <= G->ny; j++) {
        int64_t v = (int64_t)((G->top + j) * 0x40 - vp->y) * vp->scale * K / d;
        hy[j] = (vp->squash ? v * 5 / 6 : v) + vp->cy * K;
    }
    for (int i = 0; i <= G->nx; i++) {
        int64_t hx = (int64_t)((G->left + i) * 0x40 - vp->x) * vp->scale * K / d + vp->cx * K;
        int32_t sy = mul32(sy0, 0x10000);
        for (int j = 0; j <= G->ny; j++) {
            gx[plane][GI(i, j)] = (HV){ sx >> 16, (int32_t)hx };
            gy[plane][GI(i, j)] = (HV){ sy >> 16, (int32_t)hy[j] };
            sy += ystep;
        }
        sx += step;
    }
}

/* Sprite_WorldToScreen 0x47bb10 (render_world_to_screen) and its exact counterpart. */
static void wts(int32_t x, int32_t y, int32_t z, HV *px, HV *py)
{
    int32_t fx, fy;
    render_world_to_screen(x, y, z, &fx, &fy);
    const int32_t d = (z >> 16) + render_cam.height;
    int64_t dx = (int32_t)((uint32_t)x - (uint32_t)render_cam.x * 0x10000u);
    int64_t dy = (int32_t)((uint32_t)y - (uint32_t)render_cam.y * 0x10000u);
    int64_t hx = d ? (dx * render_cam.scale * K / d) >> 16 : 0, hy = d ? (dy * render_cam.scale * K / d) >> 16 : 0;
    if (render_cam.squash) hy = hy * 5 / 6;
    *px = (HV){ fx, (int32_t)(hx + render_cam.cx * K) };
    *py = (HV){ fy, (int32_t)(hy + render_cam.cy * K) };
}

/* ---- the drawing calls ---- */

static HiresTexture *side_tex(int t, int dir) { return hires_tile(S, S->side_remap[t], S->side_clut[t][dir], dir, S->side_clut[t][0]); }
static HiresTexture *lid_tex(const MapBlock *b)
{
    int r = (b->ext & 0x18) >> 3;
    return hires_tile(S, S->lid_remap[b->lid], S->lid_clut[b->lid][r], r, S->lid_clut[b->lid][0]);
}

static void face_h(uint32_t face, HiresTexture *t, HV xl_a, HV xr_a, HV xl_b, HV xr_b, HV y_a, HV y_b)
{
    hr_face_horiz(&T, face, t, xl_a.h, xr_a.h, xl_b.h, xr_b.h, y_a.h, y_b.h);
}
static void face_v(uint32_t face, HiresTexture *t, HV yt_a, HV yt_b, HV yb_a, HV yb_b, HV x_a, HV x_b)
{
    hr_face_vert(&T, face, t, yt_a.h, yt_b.h, yb_a.h, yb_b.h, x_a.h, x_b.h);
}

/* Poly_DrawQuad 0x497710 with the faithful drop tests (no height, Poly_Draw's empty extent, the winding
   that makes the faithful spans empty) taken on the faithful coordinates. u/v are texel indices of the
   tile 0..63 (the slices of the slope drawers); they become coordinates that put 0 and 63 on the tile's
   edges. 90/270 degree faces read the rotated tile. Transparent faces are blended (flags 6). */
static void quad(uint32_t face, HiresTexture *tex, HV x0, HV x1, HV x2, HV x3, HV y0, HV y1, HV y2, HV y3, int u0,
                 int u1, int u2, int u3, int v0, int v1, int v2, int v3)
{
    const int n = (int16_t)u3 < 0 ? 3 : 4;
    const HV X[4] = { x0, x1, x2, x3 }, Y[4] = { y0, y1, y2, y3 };
    const int U[4] = { u0, u1, u2, u3 }, V[4] = { v0, v1, v2, v3 };
    int32_t fx[4], fy[4], hx[4], hy[4], tu[4], tv[4];
    int32_t xmin = 0x7fff, xmax = -0x8000, ymin = 0x7fff, ymax = -0x8000;
    for (int i = 0; i < n; i++) {
        fx[i] = (int16_t)X[i].f, fy[i] = (int16_t)Y[i].f, hx[i] = X[i].h, hy[i] = Y[i].h;
        if (fx[i] < xmin) xmin = fx[i];
        if (fx[i] > xmax) xmax = fx[i];
        if (fy[i] < ymin) ymin = fy[i];
        if (fy[i] > ymax) ymax = fy[i];
    }
    if (!tex || ymin >= ymax || xmin >= xmax || !hr_winding_draws(n, fx, fy)) return;
    const bool rot = (face & 0xc000) == 0x4000 || (face & 0xc000) == 0xc000;
    const int64_t W = (int64_t)tex->w << 16, H = (int64_t)tex->h << 16;
    for (int i = 0; i < n; i++) {
        if (!rot) tu[i] = (int32_t)(U[i] * W / 63), tv[i] = (int32_t)(V[i] * H / 63);
        else tu[i] = (int32_t)(V[i] * W / 63), tv[i] = (int32_t)(H - U[i] * H / 63);
    }
    hr_polygon(&T, face & 0x80 ? HR_BLEND : HR_KEYED, tex, n, hx, hy, tu, tv);
}

/* ---- blocks (city.c) ---- */

static inline uint32_t ext_bit(const MapBlock *b, int bit, int shift) { return (uint32_t)(b->ext & bit) << shift; }
static inline uint32_t not_ext_bit(const MapBlock *b, int bit, int shift) { return (uint32_t)((uint8_t)~b->ext & bit) << shift; }

static void draw_lid(int x, int y, const MapBlock *b)
{
    face_h(b->type_map, lid_tex(b), UX(x, y), UX(x + 1, y), UX(x, y + 1), UX(x + 1, y + 1), UY(x, y), UY(x + 1, y + 1));
}

/* Render_DrawBlock 0x438d60 */
static void draw_block(int x, int y, const MapBlock *b)
{
    if (b->left && LX(x, y).f < UX(x, y).f)
        face_v(not_ext_bit(b, 0x40, 15) | 0xc000, side_tex(b->left, 2), UY(x, y), LY(x, y), UY(x, y + 1), LY(x, y + 1),
               UX(x, y), LX(x, y));
    if (b->right && UX(x + 1, y).f < LX(x + 1, y).f)
        face_v(ext_bit(b, 0x40, 15) | 0xc000, side_tex(b->right, 3), UY(x + 1, y), LY(x + 1, y), UY(x + 1, y + 1),
               LY(x + 1, y + 1), UX(x + 1, y), LX(x + 1, y));
    if (b->top && LY(x, y).f < UY(x, y).f)
        face_h(ext_bit(b, 0x20, 17) | 0x8000, side_tex(b->top, 0), LX(x, y), LX(x + 1, y), UX(x, y), UX(x + 1, y),
               LY(x, y), UY(x + 1, y));
    if (b->bottom && UY(x, y + 1).f < LY(x, y + 1).f)
        face_h(not_ext_bit(b, 0x20, 17) | 0x8000, side_tex(b->bottom, 1), LX(x, y + 1), LX(x + 1, y + 1), UX(x, y + 1),
               UX(x + 1, y + 1), LY(x, y + 1), UY(x + 1, y + 1));
    if (b->lid) draw_lid(x, y, b);
}

/* Render_DrawFlatSidesX 0x4392d0 */
static void draw_flat_sides_x(int x, int y, const MapBlock *b)
{
    int t = b->left, dir = 2;
    if (!t && !b->right) return;
    if (!(LX(x, y).f < UX(x, y).f && t && t != S->skip_side)) {
        t = b->right, dir = 3;
        if (!t || t == S->skip_side) return;
    }
    face_v(not_ext_bit(b, 0x40, 15) | 0xc080, side_tex(t, dir), UY(x, y), LY(x, y), UY(x, y + 1), LY(x, y + 1), UX(x, y),
           LX(x, y));
}

/* Render_DrawFlatSidesY 0x4393f0 */
static void draw_flat_sides_y(int x, int y, const MapBlock *b)
{
    int t = b->top, dir = 0;
    if (!t && !b->bottom) return;
    if (!(LY(x, y).f < UY(x, y).f && t && t != S->skip_side)) {
        t = b->bottom, dir = 1;
        if (!t || t == S->skip_side) return;
    }
    face_h(ext_bit(b, 0x20, 17) | 0x8080, side_tex(t, dir), LX(x, y), LX(x + 1, y), UX(x, y), UX(x + 1, y), LY(x, y),
           UY(x + 1, y));
}

/* Render_DrawFlatBlock 0x439180 */
static void draw_flat_block(int x, int y, const MapBlock *b, bool lid)
{
    if (LX(x, y).f < UX(x, y).f && UY(x, y).f <= LY(x, y).f) {
        draw_flat_sides_y(x, y, b);
        draw_flat_sides_x(x, y, b);
    } else {
        draw_flat_sides_x(x, y, b);
        draw_flat_sides_y(x, y, b);
    }
    if (b->lid && lid) draw_lid(x, y, b);
}

/* Render_DrawFlatAbove 0x439530 (NYC only) */
static void draw_flat_above(const Map *m, int x, int y, int z)
{
    if (z <= 0) return;
    const MapBlock *a = map_get_block(m, G->left + x, G->top + y, z - 1);
    if (a && (a->type_map & 0x80)) draw_flat_block(x, y, a, false);
}

/* ---- slopes (city.c's drawers, same variables) ---- */

typedef struct { int x, y, wx, wy; int32_t z0, z_hi, z_lo; int n, k, m; } Slope;

static Slope slope_at(int x, int y, int z, int n, int k)
{
    Slope q = { x, y, x + G->left, y + G->top, z * 0x400000, (k << 22) / n, ((k + 1) * 0x400000) / n, n, k, n - k };
    return q;
}
#define WTS(bx, by, zz, px, py) wts((bx) * 0x400000, (by) * 0x400000, (zz), (px), (py))

/* Render_DrawSlopeUp 0x4396b0 */
static void draw_slope_up(const MapBlock *b, Slope q)
{
    const int x = q.x, y = q.y;
    HV p5 = { 0, 0 }, p4 = { 0, 0 }, l30, l2c, l28, l20, l1c, l4, l10, l14, dummy;
    if (b->left) {
        if (q.k == 0) p5 = UX(x, y);
        else WTS(q.wx, q.wy, q.z0 + q.z_hi, &p5, &p4);
        l30 = LX(x, y);
        if (l30.f < p5.f) {
            if (q.k == 0) p4 = UY(x, y);
            l20 = LY(x, y), l2c = LX(x, y + 1), l28 = LY(x, y + 1);
            HiresTexture *t = side_tex(b->left, 2);
            uint32_t face = ext_bit(b, 0x40, 15) | 0xc000;
            int u = q.m * 0x40 / q.n - 1;
            if (q.k == q.n - 1) quad(face, t, p5, l30, l2c, p5, p4, l20, l28, p4, u, 0, 0, -1, 0, 0, 0x3f, -1);
            else {
                WTS(q.wx, q.wy + 1, q.z0 + q.z_lo, &l10, &l14);
                quad(face, t, p5, l30, l2c, l10, p4, l20, l28, l14, u, 0, 0, (q.m * 0x40 - 0x40) / q.n - 1, 0, 0, 0x3f, 0x3f);
            }
        }
    }
    if (b->right) {
        if (q.k == 0) p5 = UX(x + 1, y);
        else WTS(q.wx + 1, q.wy, q.z0 + q.z_hi, &p5, &p4);
        l30 = LX(x + 1, y);
        if (p5.f < l30.f) {
            if (q.k == 0) p4 = UY(x + 1, y);
            l20 = LY(x + 1, y), l2c = LX(x + 1, y + 1), l28 = LY(x + 1, y + 1);
            HiresTexture *t = side_tex(b->right, 3);
            uint32_t face = ext_bit(b, 0x40, 15) | 0xc000;
            int u = q.m * 0x40 / q.n - 1;
            if (q.k == q.n - 1) quad(face, t, l30, p5, l2c, l2c, l20, p4, l28, l28, 0, u, 0, -1, 0, 0, 0x3f, -1);
            else {
                WTS(q.wx + 1, q.wy + 1, q.z0 + q.z_lo, &l10, &l14);
                quad(face, t, l30, p5, l10, l2c, l20, p4, l14, l28, 0, u, (q.m * 0x40 - 0x40) / q.n - 1, 0, 0, 0, 0x3f, 0x3f);
            }
        }
    }
    if (b->top && q.k == 0) {
        p4 = LY(x, y), l28 = UY(x, y);
        if (p4.f < l28.f)
            face_h(ext_bit(b, 0x20, 17) | 0x8000, side_tex(b->top, 0), LX(x, y), LX(x + 1, y), UX(x, y), UX(x + 1, y), p4,
                   UY(x + 1, y));
    }
    if (b->lid) {
        if (q.k == 0) p5 = UX(x, y), p4 = UY(x, y), l30 = UX(x + 1, y);
        else {
            WTS(q.wx, q.wy, q.z0 + q.z_hi, &p5, &p4);
            WTS(q.wx + 1, q.wy, q.z0 + q.z_hi, &l30, &dummy);
        }
        if (q.k == q.n - 1) l2c = LX(x, y + 1), l1c = LX(x + 1, y + 1), l4 = LY(x + 1, y + 1);
        else {
            WTS(q.wx, q.wy + 1, q.z0 + q.z_lo, &l2c, &dummy);
            WTS(q.wx + 1, q.wy + 1, q.z0 + q.z_lo, &l1c, &l4);
        }
        face_h(b->type_map, lid_tex(b), p5, l30, l2c, l1c, p4, l4);
    }
}

/* Render_DrawSlopeDown 0x439e10 */
static void draw_slope_down(const MapBlock *b, Slope q)
{
    const int x = q.x, y = q.y;
    HV p5 = { 0, 0 }, p4 = { 0, 0 }, l30, l2c, l28 = { 0, 0 }, l24, l20 = { 0, 0 }, l18, l8, lc, dummy;
    if (b->left) {
        if (q.k == 0) l28 = UX(x, y);
        else WTS(q.wx, q.wy, q.z0 + q.z_hi, &l28, &l20);
        l30 = LX(x, y);
        if (l30.f < l28.f) {
            if (q.k == 0) p5 = UX(x, y + 1), p4 = UY(x, y + 1);
            else WTS(q.wx, q.wy + 1, q.z0 + q.z_hi, &p5, &p4);
            l2c = LX(x, y + 1), l18 = LY(x, y), l24 = LY(x, y + 1);
            HiresTexture *t = side_tex(b->left, 2);
            uint32_t face = ext_bit(b, 0x40, 15) | 0xc000;
            int u = q.m * 0x40 / q.n - 1;
            if (q.k == q.n - 1) quad(face, t, l30, l2c, p5, p5, l18, l24, p4, p4, 0, 0, u, -1, 0, 0x3f, 0x3f, -1);
            else {
                WTS(q.wx, q.wy, q.z0 + q.z_lo, &l8, &lc);
                quad(face, t, l8, l30, l2c, p5, lc, l18, l24, p4, (q.m * 0x40 - 0x40) / q.n - 1, 0, 0, u, 0, 0, 0x3f, 0x3f);
            }
        }
    }
    if (b->right) {
        if (q.k == 0) l28 = UX(x + 1, y);
        else WTS(q.wx + 1, q.wy, q.z0 + q.z_hi, &l28, &l20);
        l30 = LX(x + 1, y);
        if (l28.f < l30.f) {
            if (q.k == 0) p5 = UX(x + 1, y + 1), p4 = UY(x + 1, y + 1);
            else WTS(q.wx + 1, q.wy + 1, q.z0 + q.z_hi, &p5, &p4);
            l2c = LX(x + 1, y + 1), l18 = LY(x + 1, y), l24 = LY(x + 1, y + 1);
            HiresTexture *t = side_tex(b->right, 3);
            uint32_t face = ext_bit(b, 0x40, 15) | 0xc000;
            int u = q.m * 0x40 / q.n - 1;
            if (q.k == q.n - 1) quad(face, t, l30, p5, l2c, l2c, l18, p4, l24, l24, 0, u, 0, -1, 0, 0x3f, 0x3f, -1);
            else {
                WTS(q.wx + 1, q.wy, q.z0 + q.z_lo, &l8, &lc);
                quad(face, t, l30, l8, p5, l2c, l18, lc, p4, l24, 0, (q.m * 0x40 - 0x40) / q.n - 1, u, 0, 0, 0, 0x3f, 0x3f);
            }
        }
    }
    if (b->bottom && q.k == 0) {
        l20 = LY(x, y + 1), l24 = UY(x, y + 1);
        if (l24.f < l20.f)
            face_h(not_ext_bit(b, 0x20, 17) | 0x8000, side_tex(b->bottom, 1), LX(x, y + 1), LX(x + 1, y + 1),
                   UX(x, y + 1), UX(x + 1, y + 1), l20, UY(x + 1, y + 1));
    }
    if (b->lid) {
        if (q.k == 0) l28 = UX(x, y + 1), l20 = UY(x, y + 1), l30 = UX(x + 1, y + 1);
        else {
            WTS(q.wx, q.wy + 1, q.z0 + q.z_hi, &l28, &l20);
            WTS(q.wx + 1, q.wy + 1, q.z0 + q.z_hi, &l30, &dummy);
        }
        if (q.k == q.n - 1) l2c = LX(x, y), p5 = LX(x + 1, y), p4 = LY(x + 1, y);
        else {
            WTS(q.wx, q.wy, q.z0 + q.z_lo, &l2c, &dummy);
            WTS(q.wx + 1, q.wy, q.z0 + q.z_lo, &p5, &p4);
        }
        face_h(b->type_map | POLY_FLIP_V, lid_tex(b), l28, l30, l2c, p5, l20, p4);
    }
}

/* Render_DrawSlopeLeft 0x43a620 */
static void draw_slope_left(const MapBlock *b, Slope q)
{
    const int x = q.x, y = q.y;
    HV p5 = { 0, 0 }, p4, l34, l30, l2c, l28 = { 0, 0 }, l8, l18, l1c, dummy;
    if (b->left && q.k == 0) {
        l30 = UX(x, y), l34 = LX(x, y);
        if (l34.f < l30.f)
            face_v(not_ext_bit(b, 0x40, 15) | 0xc000, side_tex(b->left, 2), UY(x, y), LY(x, y), UY(x, y + 1),
                   LY(x, y + 1), l30, l34);
    }
    if (b->top) {
        p4 = LY(x, y);
        if (q.k == 0) p5 = UY(x, y);
        else WTS(q.wx, q.wy, q.z0 + q.z_hi, &l28, &p5);
        if (p4.f < p5.f) {
            if (q.k == 0) l28 = UX(x, y);
            l34 = LX(x + 1, y), l30 = LX(x, y), l2c = LY(x + 1, y);
            HiresTexture *t = side_tex(b->top, 0);
            uint32_t face = ext_bit(b, 0x20, 17) | 0x8000;
            int v = q.m * 0x40 / q.n - 1;
            if (q.k == q.n - 1)
                quad(face, t, l30, l28, l34, l34, p4, p5, l2c, l2c, 0, 0, 0x3f, -1, 0x3f, 0x40 - v, 0x3f, -1);
            else {
                WTS(q.wx + 1, q.wy, q.z0 + q.z_lo, &l18, &l1c);
                quad(face, t, l30, l28, l18, l34, p4, p5, l1c, l2c, 0, 0, 0x3f, 0x3f, 0x3f, 0x40 - v,
                     (q.m * -0x40 + 0x40) / q.n + 0x41, 0x3f);
            }
        }
    }
    if (b->bottom) {
        p4 = LY(x, y + 1);
        if (q.k == 0) p5 = UY(x, y + 1);
        else WTS(q.wx, q.wy + 1, q.z0 + q.z_hi, &l28, &p5);
        if (p5.f < p4.f) {
            if (q.k == 0) l28 = UX(x, y + 1);
            l30 = LX(x, y + 1), l34 = LX(x + 1, y + 1), l2c = LY(x + 1, y + 1);
            HiresTexture *t = side_tex(b->bottom, 1);
            uint32_t face = not_ext_bit(b, 0x20, 17) | 0x8000;
            int v = q.m * 0x40 / q.n - 1;
            if (q.k == q.n - 1)
                quad(face, t, l30, l34, l28, l28, p4, l2c, p5, p5, 0, 0x3f, 0, -1, 0x3f, 0x3f, 0x40 - v, -1);
            else {
                WTS(q.wx + 1, q.wy + 1, q.z0 + q.z_lo, &l18, &l1c);
                quad(face, t, l30, l34, l18, l28, p4, l2c, l1c, p5, 0, 0x3f, 0x3f, 0, 0x3f, 0x3f,
                     (q.m * -0x40 + 0x40) / q.n + 0x41, 0x40 - v);
            }
        }
    }
    if (b->lid) {
        if (q.k == 0) l30 = UX(x, y), p4 = UY(x, y), l8 = UY(x, y + 1);
        else {
            WTS(q.wx, q.wy, q.z0 + q.z_hi, &l30, &p4);
            WTS(q.wx, q.wy + 1, q.z0 + q.z_hi, &dummy, &l8);
        }
        if (q.k == q.n - 1) l34 = LX(x + 1, y), l2c = LY(x + 1, y), p5 = LY(x + 1, y + 1);
        else {
            WTS(q.wx + 1, q.wy, q.z0 + q.z_lo, &l34, &l2c);
            WTS(q.wx + 1, q.wy + 1, q.z0 + q.z_lo, &l28, &p5);
        }
        face_v(b->type_map, lid_tex(b), p4, l2c, l8, p5, l30, l34);
    }
}

/* Render_DrawSlopeRight 0x43adf0 */
static void draw_slope_right(const MapBlock *b, Slope q)
{
    const int x = q.x, y = q.y;
    HV p4 = { 0, 0 }, l30, l2c, l28 = { 0, 0 }, l24, l20 = { 0, 0 }, l1c, lc, l10, dummy;
    if (b->right && q.k == 0) {
        l1c = UX(x + 1, y), l2c = LX(x + 1, y);
        if (l1c.f < l2c.f)
            face_v(ext_bit(b, 0x40, 15) | 0xc000, side_tex(b->right, 3), UY(x + 1, y), LY(x + 1, y), UY(x + 1, y + 1),
                   LY(x + 1, y + 1), l1c, l2c);
    }
    if (b->top) {
        l30 = LY(x, y);
        if (q.k == 0) l28 = UY(x, y);
        else WTS(q.wx, q.wy, q.z0 + q.z_hi, &dummy, &l28);
        if (l30.f < l28.f) {
            if (q.k == 0) l20 = UX(x + 1, y), p4 = UY(x + 1, y);
            else WTS(q.wx + 1, q.wy, q.z0 + q.z_hi, &l20, &p4);
            l2c = LX(x + 1, y), l1c = LX(x, y), l24 = LY(x + 1, y);
            HiresTexture *t = side_tex(b->top, 0);
            uint32_t face = ext_bit(b, 0x20, 17) | 0x8000;
            int v = q.m * 0x40 / q.n - 1;
            if (q.k == q.n - 1)
                quad(face, t, l2c, l1c, l20, l20, l24, l30, p4, p4, 0x3f, 0, 0x3f, -1, 0x3f, 0x3f, 0x40 - v, -1);
            else {
                WTS(q.wx, q.wy, q.z0 + q.z_lo, &lc, &l10);
                quad(face, t, l2c, l1c, lc, l20, l24, l30, l10, p4, 0x3f, 0, 0, 0x3f, 0x3f, 0x3f,
                     (q.m * -0x40 + 0x40) / q.n + 0x41, 0x40 - v);
            }
        }
    }
    if (b->bottom) {
        l30 = LY(x, y + 1);
        if (q.k == 0) l28 = UY(x, y + 1);
        else WTS(q.wx, q.wy + 1, q.z0 + q.z_hi, &dummy, &l28);
        if (l28.f < l30.f) {
            if (q.k == 0) l20 = UX(x + 1, y + 1), p4 = UY(x + 1, y + 1);
            else WTS(q.wx + 1, q.wy + 1, q.z0 + q.z_hi, &l20, &p4);
            l1c = LX(x, y + 1), l2c = LX(x + 1, y + 1), l24 = LY(x + 1, y + 1);
            HiresTexture *t = side_tex(b->bottom, 1);
            uint32_t face = not_ext_bit(b, 0x20, 17) | 0x8000;
            int v = q.m * 0x40 / q.n - 1;
            if (q.k == q.n - 1)
                quad(face, t, l1c, l2c, l20, l20, l30, l24, p4, p4, 0, 0x3f, 0x3f, -1, 0x3f, 0x3f, 0x40 - v, -1);
            else {
                WTS(q.wx, q.wy + 1, q.z0 + q.z_lo, &lc, &l10);
                quad(face, t, l1c, l2c, l20, lc, l30, l24, p4, l10, 0, 0x3f, 0x3f, 0, 0x3f, 0x3f, 0x40 - v,
                     (q.m * -0x40 + 0x40) / q.n + 0x41);
            }
        }
    }
    if (b->lid) {
        if (q.k == 0) l1c = UX(x + 1, y), l30 = UY(x + 1, y), p4 = UY(x + 1, y + 1);
        else {
            WTS(q.wx + 1, q.wy, q.z0 + q.z_hi, &l1c, &l30);
            WTS(q.wx + 1, q.wy + 1, q.z0 + q.z_hi, &l20, &p4);
        }
        if (q.k == q.n - 1) l2c = LX(x, y), l24 = LY(x, y), l28 = LY(x, y + 1);
        else {
            WTS(q.wx, q.wy, q.z0 + q.z_lo, &l2c, &l24);
            WTS(q.wx, q.wy + 1, q.z0 + q.z_lo, &dummy, &l28);
        }
        face_v(b->type_map | POLY_MIRROR_U, lid_tex(b), l30, l24, p4, l28, l1c, l2c);
    }
}

/* Render_DrawSlope 0x4395d0 */
static void draw_slope(int x, int y, int z, const MapBlock *b)
{
    if (!slope_table) return;
    const uint8_t *e = slope_table + ((b->type_map >> 8) & 0x3f) * 3;
    int n = (int8_t)e[1], k = (int8_t)e[2];
    if (n == 0) return;   /* (classes 1-4 always have segments) */
    switch (e[0]) {
    case 1: draw_slope_up(b, slope_at(x, y, z, n, k)); break;
    case 2: draw_slope_down(b, slope_at(x, y, z, n, k)); break;
    case 3: draw_slope_left(b, slope_at(x, y, z, n, k)); break;
    case 4: draw_slope_right(b, slope_at(x, y, z, n, k)); break;
    }
}

static void draw_at(const Map *m, int x, int y, int z)
{
    if (x < 0 || y < 0 || x + 1 > G->nx || y + 1 > G->ny) return;   /* (the faithful grid has no bounds) */
    const MapBlock *b = map_get_block(m, G->left + x, G->top + y, z);
    if (!b) return;
    if ((b->type_map & 0x3f00) == 0) {
        if (b->type_map & 0x80) draw_flat_block(x, y, b, true);
        else draw_block(x, y, b);
    } else {
        draw_slope(x, y, z, b);
        if (S->number == 1) draw_flat_above(m, x, y, z);
    }
}

/* ---- sprites (sprite.c's Sprite_DrawCached 0x47c130 and the draw tree callback 0x47c050) ---- */

/* Sprite_DrawCached: the same corners (rotated integer offsets), each projected; the faithful
   renderer's culling, palette and raw-or-composite choice; the graphic's corner texels' centres on the
   corners, as Poly_DrawSprite 0x49787c puts them. */
static void draw_sprite(const Sprite *sp)
{
    const SpriteInfo *in = sp->info;
    if (!in) return;
    int32_t c[8];
    sprite_get_corners(in, sp->angle, c);
    const uint32_t X = (uint32_t)sp->x & 0xffff0000u, Y = (uint32_t)sp->y & 0xffff0000u;
    const int32_t z = (int32_t)((uint32_t)(int16_t)(sp->z >> 16) << 16);
    HV px[4], py[4];
    for (int i = 0; i < 4; i++) wts((int32_t)(X + (uint32_t)c[2 * i]), (int32_t)(Y - (uint32_t)c[2 * i + 1]), z, &px[i], &py[i]);
    int32_t xmax = px[0].f, xmin = px[0].f, ymax = py[0].f, ymin = py[0].f;
    for (int i = 1; i < 4; i++) {
        if (px[i].f > xmax) xmax = px[i].f;
        if (px[i].f < xmin) xmin = px[i].f;
        if (py[i].f > ymax) ymax = py[i].f;
        if (py[i].f < ymin) ymin = py[i].f;
    }
    if (xmax < 0 || ymax < 0 || xmin >= render_cam.w || ymin >= render_cam.h) return;
    /* Poly_Draw's drops: no height, no width (16-bit coordinates) */
    if ((int16_t)xmin >= (int16_t)xmax || (int16_t)ymin >= (int16_t)ymax) return;
    int32_t dy = py[3].f - py[0].f, dx = px[3].f - px[0].f;
    if (dy < 0) dy = -dy;
    if (dx < 0) dx = -dx;
    const uint32_t *clut = sprite_remap_clut(in->clut, sp->remap, sp->palette);
    HiresTexture *t = hires_sprite(S, sp->frame, in, clut, dy + dx < 10 ? 0 : sp->deltas, sp->remap,
                                   sprite_remap_clut(in->clut, 0, 0));
    if (!t) return;
    /* vertex order of Poly_DrawSprite: top right, top left, bottom left, bottom right (corners 1, 0, 2, 3) */
    static const int order[4] = { 1, 0, 2, 3 };
    int32_t fx[4], fy[4], hx[4], hy[4], tu[4], tv[4];
    const int64_t W = (int64_t)t->w << 16, H = (int64_t)t->h << 16;
    for (int i = 0; i < 4; i++) {
        int k = order[i];
        fx[i] = (int16_t)px[k].f, fy[i] = (int16_t)py[k].f, hx[i] = px[k].h, hy[i] = py[k].h;
        bool right = k == 1 || k == 3, bottom = k >= 2;
        tu[i] = (int32_t)((right ? (2 * in->w - 1) : 1) * W / (2 * in->w));
        tv[i] = (int32_t)((bottom ? (2 * in->h - 1) : 1) * H / (2 * in->h));
    }
    if (!hr_winding_draws(4, fx, fy)) return;
    hr_polygon(&T, sp->blend && sprite_blend_option ? HR_BLEND : HR_KEYED, t, 4, hx, hy, tu, tv);
}

/* 0x47c050: the sprite, then its attached chain placed relative to it (on a copy: the faithful callback
   moves the child and puts it back). */
static void draw_item(void *item)
{
    const Sprite *sp = item;
    draw_sprite(sp);
    for (const Sprite *c = sp->next; c; c = c->next) {
        Sprite t = *c;
        int32_t rx = c->x, ry = c->y;
        if (sp->angle != 0) {
            const int a = sp->angle & 0x3ff;
            const int32_t s = math_sin(a), co = math_cos(a), xi = c->x >> 16, yi = c->y >> 16;
            rx = mul32(co, xi) - mul32(s, yi);
            ry = mul32(co, yi) + mul32(s, xi);
        }
        t.x = (int32_t)((uint32_t)sp->x + (uint32_t)rx);
        t.y = (int32_t)((uint32_t)sp->y - (uint32_t)ry);
        t.z = (int32_t)((uint32_t)sp->z - (uint32_t)c->z);
        t.angle = (int16_t)((sp->angle + c->angle) & 0x3ff);
        draw_sprite(&t);
    }
}

/* ---- cast shadows (an addition: --param shadows=1) ----
   The sun is a direction; a lid point is in shadow when the ray from it towards the sun meets a block.
   Per ground cell a 16 x 16 mask (2 x 2 samples a texel) is marched once over the map's block heights and
   cached (the city doesn't move); it is drawn over the lid as a dark, semi-transparent texture right after
   its layer and before the layer above, so the walls drawn later cover it where they should. */
bool hires_shadows = false;
float hires_sun_x = -0.8f, hires_sun_y = -0.55f;   /* towards the sun, blocks per block of height */
float hires_shadow_strength = 0.42f;
uint32_t hires_shadow_tint = 0x00301810;           /* 0x00BBGGRR of the shade (a cool dark) */

enum { SH_RES = 16 };
static HiresTexture *const SH_NONE = (HiresTexture *)1;
static HiresTexture **sh_cache;                    /* [z][y][x] */
static const Map *sh_map;

static bool solid(const Map *m, int x, int y, int z)
{
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H || z < 0 || z >= MAP_Z) return false;
    const MapBlock *b = map_get_block(m, x, y, z);
    return b && (b->lid || b->left || b->right || b->top || b->bottom);
}

static bool in_shadow(const Map *m, float px, float py, float h0)
{
    for (float t = 0.02f; h0 + t < MAP_Z; t += 0.1f) {
        float x = px + hires_sun_x * t, y = py + hires_sun_y * t, h = h0 + t;
        int L = MAP_Z - 1 - (int)floorf(h);   /* layer z spans heights [5 - z, 6 - z) */
        if (solid(m, (int)floorf(x), (int)floorf(y), L)) return true;
    }
    return false;
}

static HiresTexture *shadow_mask(const Map *m, int x, int y, int z)
{
    if (sh_map != m) {
        /* each city its own sun: Liberty City a low winter sun, San Andreas the afternoon, Vice City a high
           tropical sun with short, light shadows */
        static const float SUN[3][3] = { { -0.8f, -0.55f, 0.42f }, { -0.55f, -0.4f, 0.38f }, { -0.32f, -0.22f, 0.3f } };
        if (S && S->number >= 1 && S->number <= 3)
            hires_sun_x = SUN[S->number - 1][0], hires_sun_y = SUN[S->number - 1][1], hires_shadow_strength = SUN[S->number - 1][2];
        if (sh_cache)
            for (int i = 0; i < MAP_Z * MAP_W * MAP_H; i++)
                if (sh_cache[i] && sh_cache[i] != SH_NONE) hr_texture_free(sh_cache[i]);
        free(sh_cache);
        sh_cache = calloc((size_t)MAP_Z * MAP_W * MAP_H, sizeof *sh_cache);
        sh_map = m;
    }
    if (!sh_cache) return NULL;
    HiresTexture **slot = &sh_cache[(z * MAP_H + y) * MAP_W + x];
    if (*slot) return *slot == SH_NONE ? NULL : *slot;
    uint32_t *px = malloc(SH_RES * SH_RES * sizeof *px);
    int any = 0;
    float h0 = (float)(MAP_Z - z);   /* the lid of layer z */
    for (int j = 0; j < SH_RES; j++)
        for (int i = 0; i < SH_RES; i++) {
            int n = 0;
            for (int k = 0; k < 4; k++)
                n += in_shadow(m, x + (i + 0.25f + 0.5f * (k & 1)) / SH_RES, y + (j + 0.25f + 0.5f * (k >> 1)) / SH_RES, h0);
            uint32_t a = (uint32_t)(n * 255 * hires_shadow_strength / 4 + 0.5f);
            any |= n;
            px[j * SH_RES + i] = a << 24 | hires_shadow_tint;
        }
    if (!any) { free(px); *slot = SH_NONE; return NULL; }
    HiresTexture *t = calloc(1, sizeof *t);
    *t = (HiresTexture){ SH_RES, SH_RES, px, NULL, true };
    return *slot = t;
}

static void draw_shadows(const Map *m, const RenderRect *r, int z)
{
    for (int y = r->y0_rel; y <= r->y_sum - r->y0_rel; y++)
        for (int x = r->x0_rel; x <= r->x_sum - r->x0_rel; x++) {
            if (x < 0 || y < 0 || x + 1 > G->nx || y + 1 > G->ny) continue;
            int wx = G->left + x, wy = G->top + y;
            if (wx < 0 || wy < 0 || wx >= MAP_W || wy >= MAP_H) continue;
            const MapBlock *b = map_get_block(m, wx, wy, z);
            if (!b || !b->lid || b->type_map & 0x3f00) continue;     /* no lid, or a slope */
            if (z > 0 && solid(m, wx, wy, z - 1)) continue;         /* covered by the block above */
            HiresTexture *t = shadow_mask(m, wx, wy, z);
            if (t) face_h(0x80, t, UX(x, y), UX(x + 1, y), UX(x, y + 1), UX(x + 1, y + 1), UY(x, y), UY(x + 1, y + 1));
        }
}

/* ---- the city's colour grade (an addition, --param grade=1) ----
   Each city its own vibe, applied to the drawn world before the HUD: Liberty City a cool steel blue,
   San Andreas warm sandy light, Vice City saturated pastels. Per channel: contrast around the middle,
   gain, lift; then saturation against the luma. */
bool hires_grade = false;
typedef struct { float lift[3], gain[3], contrast, sat; } Grade;
static const Grade GRADES[3] = {
    { { -2, 2, 14 }, { 0.96f, 1.00f, 1.06f }, 1.06f, 0.82f },   /* style 1: Liberty City */
    { { 8, 4, -4 }, { 1.08f, 1.02f, 0.88f }, 1.08f, 1.02f },    /* style 2: San Andreas */
    { { 4, 2, 6 }, { 1.06f, 0.98f, 1.05f }, 1.02f, 1.38f },     /* style 3: Vice City */
};

static void grade_frame(const HrTarget *t, int style)
{
    if (style < 1 || style > 3) return;
    const Grade *g = &GRADES[style - 1];
    uint8_t lut[3][256];
    for (int c = 0; c < 3; c++)
        for (int v = 0; v < 256; v++) {
            float f = ((v / 255.0f - 0.5f) * g->contrast + 0.5f) * 255.0f * g->gain[c] + g->lift[c];
            lut[c][v] = (uint8_t)(f < 0 ? 0 : f > 255 ? 255 : f + 0.5f);
        }
    for (int y = 0; y < t->h; y++) {
        uint32_t *row = t->px + (size_t)y * t->pitch;
        for (int x = 0; x < t->w; x++) {
            uint32_t p = row[x];
            float r = lut[0][p & 0xff], gr = lut[1][p >> 8 & 0xff], b = lut[2][p >> 16 & 0xff];
            float l = 0.299f * r + 0.587f * gr + 0.114f * b;
            r = l + (r - l) * g->sat, gr = l + (gr - l) * g->sat, b = l + (b - l) * g->sat;
            uint32_t R = r < 0 ? 0 : r > 255 ? 255 : (uint32_t)r, G2 = gr < 0 ? 0 : gr > 255 ? 255 : (uint32_t)gr,
                     B = b < 0 ? 0 : b > 255 ? 255 : (uint32_t)b;
            row[x] = (p & 0xff000000u) | B << 16 | G2 << 8 | R;
        }
    }
}

/* ---- the city ---- */

void hires_city_draw(const Map *m, const Style *s, const Viewport *vp, const HrTarget *target, int n)
{
    if (!slope_table) slope_table = exe_data(0x4b0c88, 64 * 3);
    if (G->nx + 1 > GS || G->ny + 1 > GS || G->nx < 0 || G->ny < 0) return;
    T = *target;
    K = (int64_t)n * HR_SUB;
    S = s;
    project(vp, 6, 1);
    pu = 1;
    for (int z = 5; z >= 0; z--) {
        pl = pu != 0;
        pu = pu == 0;
        project(vp, z, pu);
        if (render_draw_sprites) sprite_walk_level(z, draw_item);
        if (!render_draw_blocks) continue;
        const RenderRect *r = &render_rects[z + 1];
        for (int y = r->y0_rel; y < r->y_mid_rel; y++)
            for (int x = r->x0_rel; x < r->x_mid_rel; x++) {
                draw_at(m, x, y, z);
                draw_at(m, r->x_sum - x, y, z);
                draw_at(m, x, r->y_sum - y, z);
                draw_at(m, r->x_sum - x, r->y_sum - y, z);
            }
        if (hires_shadows) draw_shadows(m, r, z);
    }
    if (hires_grade) grade_frame(&T, s->number);
}
