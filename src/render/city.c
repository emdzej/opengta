/* Render_DrawCity 0x4389f0 and the block drawers it reaches (see city.h, docs/render.md). The vertex
   grid and plane selectors are module statics like the original's globals. */
#include "city.h"
#include "../exe.h"
#include "poly.h"
#include <stddef.h>

RenderRect render_rects[7];                /* 0x5bfab8 */
RenderCamera render_cam;
bool render_draw_blocks = true;
bool render_draw_sprites = true;

/* 0x54f2a0: two planes of 65 x 65 projected corners {sx, sy}, [plane][x][y]. Render_ProjectLayer
   writes (nx + 1) x (ny + 1) of them, which the camera's limits keep inside a plane's 65 columns only
   approximately (ny > 64 spills into the next column, as in the original); the slack keeps extreme
   views inside the array (the original would run into the type cache that follows it). */
static int32_t grid[2 * RENDER_GRID * RENDER_GRID + RENDER_GRID * 192 + 256][2];
static int plane_upper, plane_lower;       /* 0x5c1c20, 0x5bfbe0 */
static const uint8_t *slope_table;         /* 0x4b0c88: {class, segments, segment} per slope 0..63 */

#define G (&render_rects[6])
#define IDX(p, x, y) ((((p) * RENDER_GRID) + (x)) * RENDER_GRID + (y))
#define UX(x, y) grid[IDX(plane_upper, x, y)][0]
#define UY(x, y) grid[IDX(plane_upper, x, y)][1]
#define LX(x, y) grid[IDX(plane_lower, x, y)][0]
#define LY(x, y) grid[IDX(plane_lower, x, y)][1]

static inline int32_t mul32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }

/* ---- camera / projection ---- */

void render_copy_camera(const Viewport *vp)
{
    render_cam.x = vp->x;
    render_cam.y = vp->y;
    render_cam.height = vp->height;
    render_cam.squash = vp->squash;
    render_cam.scale = vp->scale;
    render_cam.cx = vp->cx;
    render_cam.cy = vp->cy;
    render_cam.w = vp->w;
    render_cam.h = vp->h;
}

/* Render_ComputeVisibleRect 0x43b7e0: for depths 7 * 64 down to 64 (rects 6..0), how many blocks fit
   across the screen (+3, rounded up to even), centred on the camera's block. Rect 6 (the deepest, the
   widest) is the grid descriptor the others are relative to. */
void render_compute_visible_rect(const Viewport *vp)
{
    for (int k = 6; k >= 0; k--) {
        RenderRect *r = &render_rects[k];
        int32_t s = (vp->scale << 16) / (vp->height + (k + 1) * 0x40);
        int32_t sx = s * 0x40, sy = vp->squash ? s * 0x140 / 6 : sx;
        int32_t n = vp->w16 / sx;
        r->nx = (n + 3) % 2 ? n + 4 : n + 3;
        n = vp->h16 / sy;
        r->ny = (n + 3) % 2 ? n + 4 : n + 3;
        int32_t hx = r->nx / 2;
        r->left = vp->x / 64 - hx;
        r->top = vp->y / 64 - r->ny / 2;
        r->x0_rel = r->left - G->left;
        r->x_mid_rel = r->x0_rel + hx;
        r->x_sum = r->nx - 1 + r->x0_rel * 2;
        r->y0_rel = r->top - G->top;
        r->y_mid_rel = r->y0_rel + r->ny / 2;
        r->y_sum = r->ny - 1 + r->y0_rel * 2;
    }
}

/* Render_ProjectLayer 0x43b620: corners of the grid at depth height + 64 z. Columns step by
   (scale << 16) / depth * 64 in 16.16 from a rounded-down origin, so neighbouring corners are exactly
   one step apart. */
static void project_layer(const Viewport *vp, int z, int plane)
{
    int32_t d = z * 0x40 + vp->height;
    int32_t s = (vp->scale << 16) / d;
    int32_t step = s * 0x40, ystep = vp->squash ? s * 0x140 / 6 : step;
    int32_t sx = mul32(mul32(G->left * 0x40 - vp->x, vp->scale) / d + vp->cx, 0x10000);
    int32_t sy0 = vp->squash ? mul32(mul32(G->top * 0x40 - vp->y, vp->scale) / d, 5) / 6 + vp->cy
                             : mul32(G->top * 0x40 - vp->y, vp->scale) / d + vp->cy;
    const int maxi = (int)(sizeof grid / sizeof *grid);
    for (int i = 0; i <= G->nx; i++) {
        int32_t sy = mul32(sy0, 0x10000);
        for (int j = 0; j <= G->ny; j++) {
            int e = IDX(plane, i, j);
            if (e >= 0 && e < maxi) grid[e][0] = sx >> 16, grid[e][1] = sy >> 16;
            sy += ystep;
        }
        sx += step;
    }
}

void render_world_to_screen(int32_t x, int32_t y, int32_t z, int32_t *sx, int32_t *sy)
{
    int32_t d = (z >> 16) + render_cam.height;
    int32_t px = (int32_t)((uint32_t)x - (uint32_t)render_cam.x * 0x10000u) / d + 0x7f;
    *sx = (mul32(px, render_cam.scale) >> 16) + render_cam.cx;
    int32_t py = (int32_t)((uint32_t)y - (uint32_t)render_cam.y * 0x10000u) / d + 0x7f;
    if (render_cam.squash) *sy = (mul32(py, render_cam.scale) >> 16) * 5 / 6 + render_cam.cy;
    else *sy = (mul32(py, render_cam.scale) >> 16) + render_cam.cy;
}

/* ---- blocks ---- */

static inline uint32_t ext_bit(const MapBlock *b, int bit, int shift) { return (uint32_t)(b->ext & bit) << shift; }
static inline uint32_t not_ext_bit(const MapBlock *b, int bit, int shift) { return (uint32_t)((uint8_t)~b->ext & bit) << shift; }

static void draw_lid(const Style *s, int x, int y, const MapBlock *b)
{
    poly_clut = s->lid_clut[b->lid][(b->ext & 0x18) >> 3];
    poly_draw_face_horiz(b->type_map, s->lid_remap[b->lid], UX(x, y), UX(x + 1, y), UX(x, y + 1), UX(x + 1, y + 1),
                         UY(x, y), UY(x + 1, y + 1));
}

/* Render_DrawBlock 0x438d60: a side is drawn when the camera sees it, i.e. its edge on the upper plane
   projects outside its edge on the lower plane. Side CLUTs are per direction (top 0, bottom 1, left 2,
   right 3); vertical sides use rotation 270 (FaceVert makes it 0), horizontal ones 180; the flip bits
   come from ext (inverted for left and bottom). */
static void draw_block(const Style *s, int x, int y, const MapBlock *b)
{
    if (b->left && LX(x, y) < UX(x, y)) {
        poly_clut = s->side_clut[b->left][2];
        poly_draw_face_vert(not_ext_bit(b, 0x40, 15) | 0xc000, s->side_remap[b->left], UY(x, y), LY(x, y), UY(x, y + 1),
                            LY(x, y + 1), UX(x, y), LX(x, y));
    }
    if (b->right && UX(x + 1, y) < LX(x + 1, y)) {
        poly_clut = s->side_clut[b->right][3];
        poly_draw_face_vert(ext_bit(b, 0x40, 15) | 0xc000, s->side_remap[b->right], UY(x + 1, y), LY(x + 1, y),
                            UY(x + 1, y + 1), LY(x + 1, y + 1), UX(x + 1, y), LX(x + 1, y));
    }
    if (b->top && LY(x, y) < UY(x, y)) {
        poly_clut = s->side_clut[b->top][0];
        poly_draw_face_horiz(ext_bit(b, 0x20, 17) | 0x8000, s->side_remap[b->top], LX(x, y), LX(x + 1, y), UX(x, y),
                             UX(x + 1, y), LY(x, y), UY(x + 1, y));
    }
    if (b->bottom && UY(x, y + 1) < LY(x, y + 1)) {
        poly_clut = s->side_clut[b->bottom][1];
        poly_draw_face_horiz(not_ext_bit(b, 0x20, 17) | 0x8000, s->side_remap[b->bottom], LX(x, y + 1), LX(x + 1, y + 1),
                             UX(x, y + 1), UX(x + 1, y + 1), LY(x, y + 1), UY(x + 1, y + 1));
    }
    if (b->lid) draw_lid(s, x, y, b);
}

/* Render_DrawFlatSidesX 0x4392d0: a flat block has at most one visible side per axis, drawn on its
   left edge whichever tile it uses (left if the camera sees the left side, else right), transparent;
   the style's skip tile is never drawn. */
static void draw_flat_sides_x(const Style *s, int x, int y, const MapBlock *b)
{
    int t = b->left;
    if (!t && !b->right) return;
    if (LX(x, y) < UX(x, y) && t && t != s->skip_side) {
        poly_clut = s->side_clut[t][2];
    } else {
        t = b->right;
        if (!t || t == s->skip_side) return;
        poly_clut = s->side_clut[t][3];
    }
    poly_draw_face_vert(not_ext_bit(b, 0x40, 15) | 0xc080, s->side_remap[t], UY(x, y), LY(x, y), UY(x, y + 1),
                        LY(x, y + 1), UX(x, y), LX(x, y));
}

/* Render_DrawFlatSidesY 0x4393f0: the same on the top edge. */
static void draw_flat_sides_y(const Style *s, int x, int y, const MapBlock *b)
{
    int t = b->top;
    if (!t && !b->bottom) return;
    if (LY(x, y) < UY(x, y) && t && t != s->skip_side) {
        poly_clut = s->side_clut[t][0];
    } else {
        t = b->bottom;
        if (!t || t == s->skip_side) return;
        poly_clut = s->side_clut[t][1];
    }
    poly_draw_face_horiz(ext_bit(b, 0x20, 17) | 0x8080, s->side_remap[t], LX(x, y), LX(x + 1, y), UX(x, y),
                         UX(x + 1, y), LY(x, y), UY(x + 1, y));
}

/* Render_DrawFlatBlock 0x439180: sides in an order depending on where the camera is, then the lid
   (its face word has the flat bit, so it is transparent). */
static void draw_flat_block(const Style *s, int x, int y, const MapBlock *b, bool lid)
{
    if (LX(x, y) < UX(x, y) && UY(x, y) <= LY(x, y)) {
        draw_flat_sides_y(s, x, y, b);
        draw_flat_sides_x(s, x, y, b);
    } else {
        draw_flat_sides_x(s, x, y, b);
        draw_flat_sides_y(s, x, y, b);
    }
    if (b->lid && lid) draw_lid(s, x, y, b);
}

/* Render_DrawFlatAbove 0x439530 (NYC only): after a slope, the flat block above it is drawn again
   without its lid, with this layer's planes. */
static void draw_flat_above(const Map *m, const Style *s, int x, int y, int z)
{
    if (z <= 0) return;
    const MapBlock *a = map_get_block(m, G->left + x, G->top + y, z - 1);
    if (a && (a->type_map & 0x80)) draw_flat_block(s, x, y, a, false);
}

/* ---- slopes ---- */

/* A slope block is split into n segments along its slope; segment k (0 = highest) spans heights k / n
   to (k + 1) / n of the block below the layer's top. Corners at the layer planes come from the grid,
   the intermediate ones from Sprite_WorldToScreen. Sides are textured triangles/quads with the matching
   part of the tile; the sloped lid is a face trapezoid. */
typedef struct { int x, y, wx, wy; int32_t z0, z_hi, z_lo; int n, k, m; } Slope;

static Slope slope_at(int x, int y, int z, int n, int k)
{
    Slope q = { x, y, x + G->left, y + G->top, z * 0x400000, (k << 22) / n, ((k + 1) * 0x400000) / n, n, k, n - k };
    return q;
}
#define WTS(bx, by, zz, px, py) render_world_to_screen((bx) * 0x400000, (by) * 0x400000, (zz), (px), (py))

/* Render_DrawSlopeUp 0x4396b0: high at the top (north) edge. */
static void draw_slope_up(const Style *s, const MapBlock *b, Slope q)
{
    const int x = q.x, y = q.y;
    int32_t p5 = 0, p4 = 0, l30, l2c, l28, l20 = 0, l1c, l4, l10, l14, dummy;
    if (b->left) {
        if (q.k == 0) p5 = UX(x, y);
        else WTS(q.wx, q.wy, q.z0 + q.z_hi, &p5, &p4);
        l30 = LX(x, y);
        if (l30 < p5) {
            if (q.k == 0) p4 = UY(x, y);
            l20 = LY(x, y), l2c = LX(x, y + 1), l28 = LY(x, y + 1);
            poly_clut = s->side_clut[b->left][2];
            uint32_t face = ext_bit(b, 0x40, 15) | 0xc000;
            int u = q.m * 0x40 / q.n - 1;
            if (q.k == q.n - 1)
                poly_draw_quad(face, s->side_remap[b->left], p5, l30, l2c, -1, p4, l20, l28, -1, u, 0, 0, -1, 0, 0, 0x3f, -1);
            else {
                WTS(q.wx, q.wy + 1, q.z0 + q.z_lo, &l10, &l14);
                poly_draw_quad(face, s->side_remap[b->left], p5, l30, l2c, l10, p4, l20, l28, l14, u, 0, 0,
                               (q.m * 0x40 - 0x40) / q.n - 1, 0, 0, 0x3f, 0x3f);
            }
        }
    }
    if (b->right) {
        if (q.k == 0) p5 = UX(x + 1, y);
        else WTS(q.wx + 1, q.wy, q.z0 + q.z_hi, &p5, &p4);
        l30 = LX(x + 1, y);
        if (p5 < l30) {
            if (q.k == 0) p4 = UY(x + 1, y);
            l20 = LY(x + 1, y), l2c = LX(x + 1, y + 1), l28 = LY(x + 1, y + 1);
            poly_clut = s->side_clut[b->right][3];
            uint32_t face = ext_bit(b, 0x40, 15) | 0xc000;
            int u = q.m * 0x40 / q.n - 1;
            if (q.k == q.n - 1)
                poly_draw_quad(face, s->side_remap[b->right], l30, p5, l2c, -1, l20, p4, l28, -1, 0, u, 0, -1, 0, 0, 0x3f, -1);
            else {
                WTS(q.wx + 1, q.wy + 1, q.z0 + q.z_lo, &l10, &l14);
                poly_draw_quad(face, s->side_remap[b->right], l30, p5, l10, l2c, l20, p4, l14, l28, 0, u,
                               (q.m * 0x40 - 0x40) / q.n - 1, 0, 0, 0, 0x3f, 0x3f);
            }
        }
    }
    if (b->top && q.k == 0) {
        p4 = LY(x, y), l28 = UY(x, y);
        if (p4 < l28) {
            poly_clut = s->side_clut[b->top][0];
            poly_draw_face_horiz(ext_bit(b, 0x20, 17) | 0x8000, s->side_remap[b->top], LX(x, y), LX(x + 1, y), UX(x, y),
                                 UX(x + 1, y), p4, UY(x + 1, y));
        }
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
        poly_clut = s->lid_clut[b->lid][(b->ext & 0x18) >> 3];
        poly_draw_face_horiz(b->type_map, s->lid_remap[b->lid], p5, l30, l2c, l1c, p4, l4);
    }
}

/* Render_DrawSlopeDown 0x439e10: high at the bottom (south) edge. */
static void draw_slope_down(const Style *s, const MapBlock *b, Slope q)
{
    const int x = q.x, y = q.y;
    int32_t p5 = 0, p4 = 0, l30, l2c, l28 = 0, l24, l20 = 0, l18, l8, lc, dummy;
    if (b->left) {
        if (q.k == 0) l28 = UX(x, y);
        else WTS(q.wx, q.wy, q.z0 + q.z_hi, &l28, &l20);
        l30 = LX(x, y);
        if (l30 < l28) {
            if (q.k == 0) p5 = UX(x, y + 1), p4 = UY(x, y + 1);
            else WTS(q.wx, q.wy + 1, q.z0 + q.z_hi, &p5, &p4);
            l2c = LX(x, y + 1), l18 = LY(x, y), l24 = LY(x, y + 1);
            poly_clut = s->side_clut[b->left][2];
            uint32_t face = ext_bit(b, 0x40, 15) | 0xc000;
            int u = q.m * 0x40 / q.n - 1;
            if (q.k == q.n - 1)
                poly_draw_quad(face, s->side_remap[b->left], l30, l2c, p5, -1, l18, l24, p4, -1, 0, 0, u, -1, 0, 0x3f, 0x3f, -1);
            else {
                WTS(q.wx, q.wy, q.z0 + q.z_lo, &l8, &lc);
                poly_draw_quad(face, s->side_remap[b->left], l8, l30, l2c, p5, lc, l18, l24, p4,
                               (q.m * 0x40 - 0x40) / q.n - 1, 0, 0, u, 0, 0, 0x3f, 0x3f);
            }
        }
    }
    if (b->right) {
        if (q.k == 0) l28 = UX(x + 1, y);
        else WTS(q.wx + 1, q.wy, q.z0 + q.z_hi, &l28, &l20);
        l30 = LX(x + 1, y);
        if (l28 < l30) {
            if (q.k == 0) p5 = UX(x + 1, y + 1), p4 = UY(x + 1, y + 1);
            else WTS(q.wx + 1, q.wy + 1, q.z0 + q.z_hi, &p5, &p4);
            l2c = LX(x + 1, y + 1), l18 = LY(x + 1, y), l24 = LY(x + 1, y + 1);
            poly_clut = s->side_clut[b->right][3];
            uint32_t face = ext_bit(b, 0x40, 15) | 0xc000;
            int u = q.m * 0x40 / q.n - 1;
            if (q.k == q.n - 1)
                poly_draw_quad(face, s->side_remap[b->right], l30, p5, l2c, -1, l18, p4, l24, -1, 0, u, 0, -1, 0, 0x3f, 0x3f, -1);
            else {
                WTS(q.wx + 1, q.wy, q.z0 + q.z_lo, &l8, &lc);
                poly_draw_quad(face, s->side_remap[b->right], l30, l8, p5, l2c, l18, lc, p4, l24, 0,
                               (q.m * 0x40 - 0x40) / q.n - 1, u, 0, 0, 0, 0x3f, 0x3f);
            }
        }
    }
    if (b->bottom && q.k == 0) {
        l20 = LY(x, y + 1), l24 = UY(x, y + 1);
        if (l24 < l20) {
            poly_clut = s->side_clut[b->bottom][1];
            poly_draw_face_horiz(not_ext_bit(b, 0x20, 17) | 0x8000, s->side_remap[b->bottom], LX(x, y + 1), LX(x + 1, y + 1),
                                 UX(x, y + 1), UX(x + 1, y + 1), l20, UY(x + 1, y + 1));
        }
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
        poly_clut = s->lid_clut[b->lid][(b->ext & 0x18) >> 3];
        poly_draw_face_horiz(b->type_map | POLY_FLIP_V, s->lid_remap[b->lid], l28, l30, l2c, p5, l20, p4);
    }
}

/* Render_DrawSlopeLeft 0x43a620: high at the left (west) edge; the lid is a vertical-edge face. */
static void draw_slope_left(const Style *s, const MapBlock *b, Slope q)
{
    const int x = q.x, y = q.y;
    int32_t p5 = 0, p4, l34, l30, l2c, l28 = 0, l8, l18, l1c, dummy;
    if (b->left && q.k == 0) {
        l30 = UX(x, y), l34 = LX(x, y);
        if (l34 < l30) {
            poly_clut = s->side_clut[b->left][2];
            poly_draw_face_vert(not_ext_bit(b, 0x40, 15) | 0xc000, s->side_remap[b->left], UY(x, y), LY(x, y),
                                UY(x, y + 1), LY(x, y + 1), l30, l34);
        }
    }
    if (b->top) {
        p4 = LY(x, y);
        if (q.k == 0) p5 = UY(x, y);
        else WTS(q.wx, q.wy, q.z0 + q.z_hi, &l28, &p5);
        if (p4 < p5) {
            if (q.k == 0) l28 = UX(x, y);
            l34 = LX(x + 1, y), l30 = LX(x, y), l2c = LY(x + 1, y);
            poly_clut = s->side_clut[b->top][0];
            uint32_t face = ext_bit(b, 0x20, 17) | 0x8000;
            int v = q.m * 0x40 / q.n - 1;
            if (q.k == q.n - 1)
                poly_draw_quad(face, s->side_remap[b->top], l30, l28, l34, -1, p4, p5, l2c, -1, 0, 0, 0x3f, -1, 0x3f,
                               0x40 - v, 0x3f, -1);
            else {
                WTS(q.wx + 1, q.wy, q.z0 + q.z_lo, &l18, &l1c);
                poly_draw_quad(face, s->side_remap[b->top], l30, l28, l18, l34, p4, p5, l1c, l2c, 0, 0, 0x3f, 0x3f, 0x3f,
                               0x40 - v, (q.m * -0x40 + 0x40) / q.n + 0x41, 0x3f);
            }
        }
    }
    if (b->bottom) {
        p4 = LY(x, y + 1);
        if (q.k == 0) p5 = UY(x, y + 1);
        else WTS(q.wx, q.wy + 1, q.z0 + q.z_hi, &l28, &p5);
        if (p5 < p4) {
            if (q.k == 0) l28 = UX(x, y + 1);
            l30 = LX(x, y + 1), l34 = LX(x + 1, y + 1), l2c = LY(x + 1, y + 1);
            poly_clut = s->side_clut[b->bottom][1];
            uint32_t face = not_ext_bit(b, 0x20, 17) | 0x8000;
            int v = q.m * 0x40 / q.n - 1;
            if (q.k == q.n - 1)
                poly_draw_quad(face, s->side_remap[b->bottom], l30, l34, l28, -1, p4, l2c, p5, -1, 0, 0x3f, 0, -1, 0x3f,
                               0x3f, 0x40 - v, -1);
            else {
                WTS(q.wx + 1, q.wy + 1, q.z0 + q.z_lo, &l18, &l1c);
                poly_draw_quad(face, s->side_remap[b->bottom], l30, l34, l18, l28, p4, l2c, l1c, p5, 0, 0x3f, 0x3f, 0, 0x3f,
                               0x3f, (q.m * -0x40 + 0x40) / q.n + 0x41, 0x40 - v);
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
        poly_clut = s->lid_clut[b->lid][(b->ext & 0x18) >> 3];
        poly_draw_face_vert(b->type_map, s->lid_remap[b->lid], p4, l2c, l8, p5, l30, l34);
    }
}

/* Render_DrawSlopeRight 0x43adf0: high at the right (east) edge. */
static void draw_slope_right(const Style *s, const MapBlock *b, Slope q)
{
    const int x = q.x, y = q.y;
    int32_t p4 = 0, l30, l2c, l28 = 0, l24, l20 = 0, l1c, lc, l10, dummy;
    if (b->right && q.k == 0) {
        l1c = UX(x + 1, y), l2c = LX(x + 1, y);
        if (l1c < l2c) {
            poly_clut = s->side_clut[b->right][3];
            poly_draw_face_vert(ext_bit(b, 0x40, 15) | 0xc000, s->side_remap[b->right], UY(x + 1, y), LY(x + 1, y),
                                UY(x + 1, y + 1), LY(x + 1, y + 1), l1c, l2c);
        }
    }
    if (b->top) {
        l30 = LY(x, y);
        if (q.k == 0) l28 = UY(x, y);
        else WTS(q.wx, q.wy, q.z0 + q.z_hi, &dummy, &l28);
        if (l30 < l28) {
            if (q.k == 0) l20 = UX(x + 1, y), p4 = UY(x + 1, y);
            else WTS(q.wx + 1, q.wy, q.z0 + q.z_hi, &l20, &p4);
            l2c = LX(x + 1, y), l1c = LX(x, y), l24 = LY(x + 1, y);
            poly_clut = s->side_clut[b->top][0];
            uint32_t face = ext_bit(b, 0x20, 17) | 0x8000;
            int v = q.m * 0x40 / q.n - 1;
            if (q.k == q.n - 1)
                poly_draw_quad(face, s->side_remap[b->top], l2c, l1c, l20, -1, l24, l30, p4, -1, 0x3f, 0, 0x3f, -1, 0x3f,
                               0x3f, 0x40 - v, -1);
            else {
                WTS(q.wx, q.wy, q.z0 + q.z_lo, &lc, &l10);
                poly_draw_quad(face, s->side_remap[b->top], l2c, l1c, lc, l20, l24, l30, l10, p4, 0x3f, 0, 0, 0x3f, 0x3f,
                               0x3f, (q.m * -0x40 + 0x40) / q.n + 0x41, 0x40 - v);
            }
        }
    }
    if (b->bottom) {
        l30 = LY(x, y + 1);
        if (q.k == 0) l28 = UY(x, y + 1);
        else WTS(q.wx, q.wy + 1, q.z0 + q.z_hi, &dummy, &l28);
        if (l28 < l30) {
            if (q.k == 0) l20 = UX(x + 1, y + 1), p4 = UY(x + 1, y + 1);
            else WTS(q.wx + 1, q.wy + 1, q.z0 + q.z_hi, &l20, &p4);
            l1c = LX(x, y + 1), l2c = LX(x + 1, y + 1), l24 = LY(x + 1, y + 1);
            poly_clut = s->side_clut[b->bottom][1];
            uint32_t face = not_ext_bit(b, 0x20, 17) | 0x8000;
            int v = q.m * 0x40 / q.n - 1;
            if (q.k == q.n - 1)
                poly_draw_quad(face, s->side_remap[b->bottom], l1c, l2c, l20, -1, l30, l24, p4, -1, 0, 0x3f, 0x3f, -1, 0x3f,
                               0x3f, 0x40 - v, -1);
            else {
                WTS(q.wx, q.wy + 1, q.z0 + q.z_lo, &lc, &l10);
                poly_draw_quad(face, s->side_remap[b->bottom], l1c, l2c, l20, lc, l30, l24, p4, l10, 0, 0x3f, 0x3f, 0, 0x3f,
                               0x3f, 0x40 - v, (q.m * -0x40 + 0x40) / q.n + 0x41);
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
        poly_clut = s->lid_clut[b->lid][(b->ext & 0x18) >> 3];
        poly_draw_face_vert(b->type_map | POLY_MIRROR_U, s->lid_remap[b->lid], l30, l24, p4, l28, l1c, l2c);
    }
}

/* Render_DrawSlope 0x4395d0: class (1 up, 2 down, 3 left, 4 right), segments and segment from the table
   at 0x4b0c88; slopes 45..63 index past its end into other data (classes there other than 1..4 draw
   nothing). */
static void draw_slope(const Style *s, int x, int y, int z, const MapBlock *b)
{
    if (!slope_table) return;
    const uint8_t *e = slope_table + ((b->type_map >> 8) & 0x3f) * 3;
    int n = (int8_t)e[1], k = (int8_t)e[2];
    switch (e[0]) {
    case 1: draw_slope_up(s, b, slope_at(x, y, z, n, k)); break;
    case 2: draw_slope_down(s, b, slope_at(x, y, z, n, k)); break;
    case 3: draw_slope_left(s, b, slope_at(x, y, z, n, k)); break;
    case 4: draw_slope_right(s, b, slope_at(x, y, z, n, k)); break;
    }
}

/* ---- the city ---- */

static void draw_at(const Map *m, const Style *s, int x, int y, int z)
{
    const MapBlock *b = map_get_block(m, G->left + x, G->top + y, z);
    if (!b) return;
    if ((b->type_map & 0x3f00) == 0) {
        if (b->type_map & 0x80) draw_flat_block(s, x, y, b, true);
        else draw_block(s, x, y, b);
    } else {
        draw_slope(s, x, y, z, b);
        if (s->number == 1) draw_flat_above(m, s, x, y, z);
    }
}

/* Render_DrawCity 0x4389f0: layers from the lowest (z = 5) to the highest (z = 0); for each, project
   its top plane (the previous one is its bottom), draw the layer's sprites (Sprite_DrawLevel 0x47c030,
   not ported yet), then its blocks. Rows go from the rect's top and bottom edges toward the middle and
   columns from both sides inward, so nearer (more central) blocks overdraw farther ones. Nothing clears
   the screen: pixels no block covers keep the previous frame. */
void render_draw_city(const Map *m, const Style *s, const Viewport *vp)
{
    if (!slope_table) slope_table = exe_data(0x4b0c88, 64 * 3);
    project_layer(vp, 6, 1);
    plane_upper = 1;
    for (int z = 5; z >= 0; z--) {
        plane_lower = plane_upper != 0;
        plane_upper = plane_upper == 0;
        project_layer(vp, z, plane_upper);
        /* if (render_draw_sprites) Sprite_DrawLevel(z);   TODO(0x47c030): sprites */
        if (!render_draw_blocks) continue;
        const RenderRect *r = &render_rects[z + 1];
        for (int y = r->y0_rel; y < r->y_mid_rel; y++)
            for (int x = r->x0_rel; x < r->x_mid_rel; x++) {
                draw_at(m, s, x, y, z);
                draw_at(m, s, r->x_sum - x, y, z);
                draw_at(m, s, x, r->y_sum - y, z);
                draw_at(m, s, r->x_sum - x, r->y_sum - y, z);
            }
    }
}
