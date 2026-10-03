/* skin_generate: a new skin whose tiles join seamlessly, drawn from scratch with the original as a
   reference only for *what is where* on each tile (docs/howto/generate-a-skin.md).

     skin_generate [--data DIR] [--out DIR] [--scale S] [--style N] [--materials K]

   How it works, per style:
   1. Materials: every colour the tiles use (side, lid and aux tiles through their default CLUTs) is
      clustered into K materials (k-means on a colour histogram). A material is our own flat colour plus
      a texture strength taken from how much its colours vary.
   2. Class map: every tile pixel gets its material, or "transparent" (colour 0) — the layout of the
      tile: where the markings, curbs, windows and letters are. No colour value of the tile reaches the
      output, only which material is where.
   3. Drawing at S x 64 pixels: material boundaries come from a bilinear vote of the four nearest class
      cells (sharpened, anti-aliased), so contours are smooth instead of stair-stepped, and each
      material is filled with procedural noise of its own.
   4. Seams: tiles that join in the original have identical class maps along the shared edge (the
      originals join pixel for pixel), and the votes clamp at the tile border, so boundaries meet. The
      noise near every border is replaced by a band that depends only on the distance along the edge,
      folded so it reads the same from both ends: the same on all four edges and under every rotation
      and flip the map applies, so same-material neighbours meet without a seam.

   The output is generated from the structure of your own copy of the game: keep it for your own use,
   don't distribute it (skin.ini says so). */
#include "exe.h"
#include "map.h"
#include "render/sprite.h"
#include "style.h"
#include "vfs.h"
#include "vfs_host.h"
#include "../tests/png.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum { TILE = 64, MAXK = 64, TRANSPARENT = 255 };

static int scale = 4, nmat = 12, min_region = 40;
static bool flat = true;   /* the flat vector look (default); --textured: shading and grain */
static const char *out_dir = "out/skins/generated";

typedef struct { float r, g, b; double n; float var; } Material;

/* ---- deterministic noise ---- */

static uint32_t hash3(uint32_t x, uint32_t y, uint32_t z)
{
    uint32_t h = x * 0x8da6b343u ^ y * 0xd8163841u ^ z * 0xcb1ab31fu;
    h ^= h >> 13, h *= 0x5bd1e995u, h ^= h >> 15;
    return h;
}
static float hnoise(uint32_t x, uint32_t y, uint32_t z) { return (float)(hash3(x, y, z) & 0xffff) / 32767.5f - 1.0f; }

/* Smooth value noise at (x, y) in cells, seeded by material m. */
static float vnoise(float x, float y, uint32_t m)
{
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx), fy = fy * fy * (3 - 2 * fy);
    float a = hnoise((uint32_t)xi, (uint32_t)yi, m), b = hnoise((uint32_t)xi + 1, (uint32_t)yi, m);
    float c = hnoise((uint32_t)xi, (uint32_t)yi + 1, m), d = hnoise((uint32_t)xi + 1, (uint32_t)yi + 1, m);
    return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy;
}

/* The material's texture at output pixel (x, y) of a tile of side L pixels: interior noise blended into
   an edge band that depends only on the folded distance along the nearest edge (same on every edge,
   symmetric end to end), so any rotation or flip of a tile meets any other along its border. */
static float texture(int x, int y, int L, uint32_t m)
{
    float f = (float)TILE / 4.0f / (float)L;   /* noise cells per output pixel: 4 source pixels a cell */
    float inner = 0.65f * vnoise(x * f, y * f, m) + 0.35f * vnoise(x * f * 3.1f, y * f * 3.1f, m + 77);
    int dl = x, dr = L - 1 - x, dt = y, db = L - 1 - y;
    int d = dl, t = y;
    if (dr < d) d = dr, t = y;
    if (dt < d) d = dt, t = x;
    if (db < d) d = db, t = x;
    int band = L / 16;
    if (d >= band) return inner;
    int folded = t < L - 1 - t ? t : L - 1 - t;   /* the same from both ends of the edge */
    float edge = 0.65f * vnoise(folded * f, 0.5f, m + 991) + 0.35f * vnoise(folded * f * 3.1f, 0.5f, m + 1068);
    float w = (float)d / band;
    w = w * w * (3 - 2 * w);
    return edge + (inner - edge) * w;
}

/* ---- the reference: tiles as material maps ---- */

typedef struct {
    uint8_t cls[TILE * TILE];   /* material per pixel, TRANSPARENT for colour 0 */
    float shade[TILE * TILE];   /* light and shadow: the reference's lightness against its material's,
                                   heavily smoothed (only where it is lit or shaded survives) */
} ClassMap;

static float luma(float r, float g, float b) { return 0.299f * r + 0.587f * g + 0.114f * b; }

static void smooth1d(float *v, int n)
{
    float tmp[TILE];
    const int R = 6;
    for (int pass = 0; pass < 3; pass++) {
        for (int i = 0; i < n; i++) {
            float s = 0;
            for (int k = -R; k <= R; k++) { int j = i + k < 0 ? 0 : i + k > n - 1 ? n - 1 : i + k; s += v[j]; }
            tmp[i] = s / (2 * R + 1);
        }
        memcpy(v, tmp, sizeof(float) * (size_t)n);
    }
}

static void smooth(float *v);

/* The shading field: smoothed in 2D, then pinned near each edge to a profile smoothed only along that
   edge from the border pixels themselves. A tile's border pixels continue its neighbour's in the
   original, so both sides arrive at (nearly) the same shade where they meet. */
static void smooth_shade(float *v)
{
    float edge[4][TILE];
    for (int i = 0; i < TILE; i++) {
        edge[0][i] = v[i * TILE];                  /* left */
        edge[1][i] = v[i * TILE + TILE - 1];       /* right */
        edge[2][i] = v[i];                         /* top */
        edge[3][i] = v[(TILE - 1) * TILE + i];     /* bottom */
    }
    for (int e = 0; e < 4; e++) smooth1d(edge[e], TILE);
    smooth(v);
    const int band = 10;
    for (int y = 0; y < TILE; y++)
        for (int x = 0; x < TILE; x++) {
            int d[4] = { x, TILE - 1 - x, y, TILE - 1 - y };
            int e = 0;
            for (int k = 1; k < 4; k++)
                if (d[k] < d[e]) e = k;
            if (d[e] >= band) continue;
            float w = (float)d[e] / band;
            w = w * w * (3 - 2 * w);
            float pv = edge[e][e < 2 ? y : x];
            v[y * TILE + x] = pv + (v[y * TILE + x] - pv) * w;
        }
}

/* A wide box blur (radius 6, three passes ~ Gaussian), clamped at the tile border like the votes. */
static void smooth(float *v)
{
    static float tmp[TILE * TILE];
    const int R = 6;
    for (int pass = 0; pass < 3; pass++) {
        for (int y = 0; y < TILE; y++)
            for (int x = 0; x < TILE; x++) {
                float s = 0;
                for (int k = -R; k <= R; k++) { int X = x + k < 0 ? 0 : x + k > TILE - 1 ? TILE - 1 : x + k; s += v[y * TILE + X]; }
                tmp[y * TILE + x] = s / (2 * R + 1);
            }
        for (int y = 0; y < TILE; y++)
            for (int x = 0; x < TILE; x++) {
                float s = 0;
                for (int k = -R; k <= R; k++) { int Y = y + k < 0 ? 0 : y + k > TILE - 1 ? TILE - 1 : y + k; s += tmp[Y * TILE + x]; }
                v[y * TILE + x] = s / (2 * R + 1);
            }
    }
}

static uint32_t tile_rgb(const Style *s, const uint32_t *clut, int t, int u, int v, bool *transparent)
{
    const uint8_t *page = s->buf + style_tile_page(t);
    int u0 = (t & 3) * 64, v0 = ((t >> 2) & 3) * 64;
    uint8_t e = page[(v0 + v) * 256 + u0 + u];
    *transparent = e == 0;
    return clut[e * 64] & 0xffffff;
}

static void rgbf(uint32_t c, float *r, float *g, float *b) { *r = (float)(c >> 16 & 0xff), *g = (float)(c >> 8 & 0xff), *b = (float)(c & 0xff); }

/* Colour distance for materials: chroma counts fully, lightness at light_weight, so the light and dark
   shades of one surface (streaks, shadows) are one material and only strong edges (joints, outlines)
   separate it. */
static float light_weight = 0.15f;
static float dist2(float r, float g, float b, const Material *m)
{
    float dr = r - m->r, dg = g - m->g, db = b - m->b;
    float dy = 0.299f * dr + 0.587f * dg + 0.114f * db;
    float dcb = -0.169f * dr - 0.331f * dg + 0.5f * db, dcr = 0.5f * dr - 0.419f * dg - 0.081f * db;
    return light_weight * dy * dy + dcb * dcb + dcr * dcr;
}

typedef struct { uint32_t c; uint32_t n; } Hist;
static int cmp_hist(const void *a, const void *b) { return (int)((const Hist *)a)->c - (int)((const Hist *)b)->c; }

/* k-means over the colour histogram (deterministic k-means++ seeding). */
static void cluster(Hist *h, int nh, Material *mat, int k)
{
    uint32_t seed = 12345;
    int first = 0;
    for (int i = 1; i < nh; i++)
        if (h[i].n > h[first].n) first = i;
    rgbf(h[first].c, &mat[0].r, &mat[0].g, &mat[0].b);
    float *best = malloc((size_t)nh * sizeof *best);
    for (int c = 1; c < k; c++) {
        double total = 0;
        for (int i = 0; i < nh; i++) {
            float r, g, b, d = 1e30f;
            rgbf(h[i].c, &r, &g, &b);
            for (int j = 0; j < c; j++) { float e = dist2(r, g, b, &mat[j]); if (e < d) d = e; }
            best[i] = d * h[i].n;
            total += best[i];
        }
        seed = seed * 1103515245u + 12345u;
        double pick = (double)(seed >> 8) / 16777216.0 * total, acc = 0;
        int sel = nh - 1;
        for (int i = 0; i < nh; i++) { acc += best[i]; if (acc >= pick) { sel = i; break; } }
        rgbf(h[sel].c, &mat[c].r, &mat[c].g, &mat[c].b);
    }
    for (int it = 0; it < 25; it++) {
        double sr[MAXK] = { 0 }, sg[MAXK] = { 0 }, sb[MAXK] = { 0 }, sn[MAXK] = { 0 };
        for (int i = 0; i < nh; i++) {
            float r, g, b, d = 1e30f;
            int bj = 0;
            rgbf(h[i].c, &r, &g, &b);
            for (int j = 0; j < k; j++) { float e = dist2(r, g, b, &mat[j]); if (e < d) d = e, bj = j; }
            sr[bj] += r * h[i].n, sg[bj] += g * h[i].n, sb[bj] += b * h[i].n, sn[bj] += h[i].n;
        }
        for (int j = 0; j < k; j++)
            if (sn[j] > 0) mat[j].r = (float)(sr[j] / sn[j]), mat[j].g = (float)(sg[j] / sn[j]), mat[j].b = (float)(sb[j] / sn[j]), mat[j].n = sn[j];
    }
    /* texture strength: how far the member colours spread in brightness */
    double sv[MAXK] = { 0 }, sn[MAXK] = { 0 };
    for (int i = 0; i < nh; i++) {
        float r, g, b, d = 1e30f;
        int bj = 0;
        rgbf(h[i].c, &r, &g, &b);
        for (int j = 0; j < k; j++) { float e = dist2(r, g, b, &mat[j]); if (e < d) d = e, bj = j; }
        float l = 0.299f * r + 0.587f * g + 0.114f * b, lm = 0.299f * mat[bj].r + 0.587f * mat[bj].g + 0.114f * mat[bj].b;
        sv[bj] += (double)(l - lm) * (l - lm) * h[i].n, sn[bj] += h[i].n;
    }
    for (int j = 0; j < k; j++) mat[j].var = sn[j] > 0 ? (float)sqrt(sv[j] / sn[j]) : 0;
    free(best);
}

/* Vectorising a class map: every connected region of one material is either
   - a line: long, thin and straight (joints, markings, curbs, window frames) - taken out of the map and
     kept as a segment (ends within 2 pixels of the border are snapped onto it, so the neighbour's
     continuation of the same line meets it), drawn later as an anti-aliased stroke;
   - noise: small, or mid-sized and ragged (speckle, cracks) - absorbed by the material around it;
   - a region: drawn as a flat shape.
   Regions touching the border are never absorbed, so a neighbour's matching region isn't removed on one
   side only. */
typedef struct { float x0, y0, x1, y1, w; uint8_t cls; } Line;
enum { MAX_LINES = 512 };
typedef struct { Line l[MAX_LINES]; int n; } Lines;

static void absorb_into(ClassMap *cm, const int *members, int nm, uint8_t c)
{
    int count[256] = { 0 }, best = -1;
    for (int i = 0; i < nm; i++) {
        int p = members[i], x = p % TILE, y = p / TILE;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int X = x + dx, Y = y + dy;
                if (X < 0 || Y < 0 || X >= TILE || Y >= TILE) continue;
                uint8_t o = cm->cls[Y * TILE + X];
                if (o != c && o != TRANSPARENT) count[o]++;
            }
    }
    for (int j = 0; j < 256; j++)
        if (count[j] && (best < 0 || count[j] > count[best])) best = j;
    if (best >= 0)
        for (int i = 0; i < nm; i++) cm->cls[members[i]] = (uint8_t)best;
}

static float snap(float v)   /* an end near the border goes onto it (pixel centres: -0.5 .. 63.5) */
{
    if (v < 2.0f) return -0.5f;
    if (v > TILE - 3.0f) return TILE - 0.5f;
    return v;
}

/* Collinear pieces of one line (same material, direction, offset within 1.5 pixels, gaps up to 8) become
   one stroke; short pieces that don't reach the border are dropped. */
static bool on_border(float x, float y) { return x <= -0.4f || y <= -0.4f || x >= TILE - 0.6f || y >= TILE - 0.6f; }

static void merge_lines(Lines *ls)
{
    bool merged = true;
    while (merged) {
        merged = false;
        for (int i = 0; i < ls->n && !merged; i++)
            for (int j = i + 1; j < ls->n && !merged; j++) {
                Line *a = &ls->l[i], *b = &ls->l[j];
                if (a->cls != b->cls) continue;
                float dx = a->x1 - a->x0, dy = a->y1 - a->y0, la = sqrtf(dx * dx + dy * dy);
                float ex = b->x1 - b->x0, ey = b->y1 - b->y0, lb = sqrtf(ex * ex + ey * ey);
                if (la < 1e-3f || lb < 1e-3f) continue;
                float ux = dx / la, uy = dy / la;
                if (fabsf(fabsf(ux * ex / lb + uy * ey / lb) - 1) > 0.02f) continue;   /* not parallel */
                float off = fabsf((b->x0 - a->x0) * -uy + (b->y0 - a->y0) * ux);
                if (off > 1.5f) continue;
                float t0 = 0, t1 = la, s0 = (b->x0 - a->x0) * ux + (b->y0 - a->y0) * uy, s1 = (b->x1 - a->x0) * ux + (b->y1 - a->y0) * uy;
                if (s0 > s1) { float t = s0; s0 = s1; s1 = t; }
                if (s0 > t1 + 8 || s1 < t0 - 8) continue;   /* too far apart along the line */
                float lo = s0 < t0 ? s0 : t0, hi = s1 > t1 ? s1 : t1;
                float ox = a->x0, oy = a->y0;
                a->x0 = ox + ux * lo, a->y0 = oy + uy * lo, a->x1 = ox + ux * hi, a->y1 = oy + uy * hi;
                a->w = (a->w * la + b->w * lb) / (la + lb);
                ls->l[j] = ls->l[--ls->n];
                merged = true;
            }
    }
    for (int i = 0; i < ls->n;) {
        Line *l = &ls->l[i];
        float len = hypotf(l->x1 - l->x0, l->y1 - l->y0);
        if (len < 10 && !on_border(l->x0, l->y0) && !on_border(l->x1, l->y1)) ls->l[i] = ls->l[--ls->n];
        else i++;
    }
}

/* A 5 x 5 majority filter (two passes) on the regions once the lines are out: straight, quiet outlines. */
static void mode_filter(ClassMap *cm)
{
    static uint8_t tmp[TILE * TILE];
    for (int pass = 0; pass < 2; pass++) {
        for (int y = 0; y < TILE; y++)
            for (int x = 0; x < TILE; x++) {
                int count[256] = { 0 }, best = cm->cls[y * TILE + x];
                for (int dy = -2; dy <= 2; dy++)
                    for (int dx = -2; dx <= 2; dx++) {
                        int X = x + dx < 0 ? 0 : x + dx > TILE - 1 ? TILE - 1 : x + dx;
                        int Y = y + dy < 0 ? 0 : y + dy > TILE - 1 ? TILE - 1 : y + dy;
                        int c = cm->cls[Y * TILE + X];
                        if (++count[c] > count[best]) best = c;
                    }
                tmp[y * TILE + x] = (uint8_t)best;
            }
        memcpy(cm->cls, tmp, sizeof tmp);
    }
}

/* Regions on a coarser grid (grid x grid cells, majority): outlines in steps of `grid` pixels, which the
   vote then draws as straight edges and 45-degree corners. */
static int grid = 1;   /* --grid N: regions on an N-pixel grid first (blockier) */
static void coarsen(ClassMap *cm)
{
    for (int cy = 0; cy < TILE; cy += grid)
        for (int cx = 0; cx < TILE; cx += grid) {
            int count[256] = { 0 }, best = cm->cls[cy * TILE + cx];
            for (int y = cy; y < cy + grid && y < TILE; y++)
                for (int x = cx; x < cx + grid && x < TILE; x++)
                    if (++count[cm->cls[y * TILE + x]] > count[best]) best = cm->cls[y * TILE + x];
            for (int y = cy; y < cy + grid && y < TILE; y++)
                for (int x = cx; x < cx + grid && x < TILE; x++) cm->cls[y * TILE + x] = (uint8_t)best;
        }
}

static void vectorise(ClassMap *cm, Lines *lines)
{
    static int16_t label[TILE * TILE];
    static int stack[TILE * TILE], members[TILE * TILE];
    lines->n = 0;
    for (int pass = 0; pass < 3; pass++) {
        memset(label, -1, sizeof label);
        bool changed = false;
        for (int start = 0; start < TILE * TILE; start++) {
            if (label[start] != -1) continue;
            uint8_t c = cm->cls[start];
            int sp = 0, nm = 0, bx0 = TILE, by0 = TILE, bx1 = -1, by1 = -1;
            bool border = false;
            double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
            stack[sp++] = start;
            label[start] = 1;
            while (sp) {
                int p = stack[--sp], x = p % TILE, y = p / TILE;
                members[nm++] = p;
                if (x == 0 || y == 0 || x == TILE - 1 || y == TILE - 1) border = true;
                if (x < bx0) bx0 = x;
                if (x > bx1) bx1 = x;
                if (y < by0) by0 = y;
                if (y > by1) by1 = y;
                sx += x, sy += y, sxx += (double)x * x, syy += (double)y * y, sxy += (double)x * y;
                const int nb[4] = { x > 0 ? p - 1 : -1, x < TILE - 1 ? p + 1 : -1, y > 0 ? p - TILE : -1, y < TILE - 1 ? p + TILE : -1 };
                for (int k = 0; k < 4; k++)
                    if (nb[k] >= 0 && label[nb[k]] == -1 && cm->cls[nb[k]] == c) label[nb[k]] = 1, stack[sp++] = nb[k];
            }
            if (c == TRANSPARENT) continue;
            /* principal axis */
            double mx = sx / nm, my = sy / nm, cxx = sxx / nm - mx * mx, cyy = syy / nm - my * my, cxy = sxy / nm - mx * my;
            double tr = cxx + cyy, det = cxx * cyy - cxy * cxy, disc = sqrt(tr * tr / 4 - det > 0 ? tr * tr / 4 - det : 0);
            double l1 = tr / 2 + disc;
            double ax = cxy, ay = l1 - cxx;
            if (fabs(ax) + fabs(ay) < 1e-9) ax = cxx >= cyy ? 1 : 0, ay = cxx >= cyy ? 0 : 1;
            double al = sqrt(ax * ax + ay * ay);
            ax /= al, ay /= al;
            double tmin = 1e9, tmax = -1e9;
            for (int i = 0; i < nm; i++) {
                double t = (members[i] % TILE - mx) * ax + (members[i] / TILE - my) * ay;
                if (t < tmin) tmin = t;
                if (t > tmax) tmax = t;
            }
            double length = tmax - tmin + 1, width = nm / length;
            int bw = bx1 - bx0 + 1, bh = by1 - by0 + 1;
            bool is_line = pass == 0 && length >= 6 && width <= 3.2 && length >= 4 * width && nm < TILE * TILE / 4;
            if (is_line) {
                /* angles snap to multiples of 45 degrees: the extent is measured again along the snapped axis */
                double ang = atan2(ay, ax), step = M_PI / 4;
                ang = floor(ang / step + 0.5) * step;
                ax = cos(ang), ay = sin(ang);
                if (fabs(ax) < 1e-6) ax = 0;
                if (fabs(ay) < 1e-6) ay = 0;
                tmin = 1e9, tmax = -1e9;
                for (int i = 0; i < nm; i++) {
                    double t = (members[i] % TILE - mx) * ax + (members[i] / TILE - my) * ay;
                    if (t < tmin) tmin = t;
                    if (t > tmax) tmax = t;
                }
            }
            if (is_line && lines->n < MAX_LINES) {
                Line *l = &lines->l[lines->n++];
                l->x0 = (float)(mx + ax * tmin), l->y0 = (float)(my + ay * tmin);
                l->x1 = (float)(mx + ax * tmax), l->y1 = (float)(my + ay * tmax);
                /* axis-aligned lines stay exactly axis-aligned */
                if (fabs(ay) < 0.08) l->y0 = l->y1 = (float)my;
                if (fabs(ax) < 0.08) l->x0 = l->x1 = (float)mx;
                l->x0 = snap(l->x0), l->y0 = snap(l->y0), l->x1 = snap(l->x1), l->y1 = snap(l->y1);
                l->w = (float)(width < 1 ? 1 : width);
                l->cls = c;
                absorb_into(cm, members, nm, c);
                changed = true;
                continue;
            }
            if (border) continue;
            bool ragged = nm < 160 && nm < 0.35 * bw * bh;
            if (nm < min_region || ragged) {
                absorb_into(cm, members, nm, c);
                changed = true;
            }
        }
        if (!changed) break;
    }
    merge_lines(lines);
    mode_filter(cm);
    if (grid > 1) coarsen(cm);
}

/* Lines drawn as anti-aliased strokes over the regions (L x L, 0xAABBGGRR). */
static void draw_lines(const Lines *lines, const Material *mat, uint32_t *out, int L)
{
    float k = (float)L / TILE;
    for (int i = 0; i < lines->n; i++) {
        const Line *l = &lines->l[i];
        float x0 = (l->x0 + 0.5f) * k, y0 = (l->y0 + 0.5f) * k, x1 = (l->x1 + 0.5f) * k, y1 = (l->y1 + 0.5f) * k;
        float hw = l->w * k * 0.5f;
        int X0 = (int)floorf(fminf(x0, x1) - hw - 1), X1 = (int)ceilf(fmaxf(x0, x1) + hw + 1);
        int Y0 = (int)floorf(fminf(y0, y1) - hw - 1), Y1 = (int)ceilf(fmaxf(y0, y1) + hw + 1);
        float dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
        const Material *m = &mat[l->cls];
        for (int y = Y0 < 0 ? 0 : Y0; y <= Y1 && y < L; y++)
            for (int x = X0 < 0 ? 0 : X0; x <= X1 && x < L; x++) {
                float px = x + 0.5f, py = y + 0.5f, t = len2 > 0 ? ((px - x0) * dx + (py - y0) * dy) / len2 : 0;
                t = t < 0 ? 0 : t > 1 ? 1 : t;
                float ex = px - (x0 + t * dx), ey = py - (y0 + t * dy), d = sqrtf(ex * ex + ey * ey);
                float cov = hw - d + 0.5f;
                if (cov <= 0) continue;
                if (cov > 1) cov = 1;
                uint32_t o = out[y * L + x];
                float r = (float)(o & 0xff), g = (float)(o >> 8 & 0xff), b = (float)(o >> 16 & 0xff), a = (float)(o >> 24);
                r += (m->r - r) * cov, g += (m->g - g) * cov, b += (m->b - b) * cov, a += (255 - a) * cov;
                out[y * L + x] = (uint32_t)(a + 0.5f) << 24 | (uint32_t)(b + 0.5f) << 16 | (uint32_t)(g + 0.5f) << 8 | (uint32_t)(r + 0.5f);
            }
    }
}

/* ---- drawing ---- */

static int classify(float r, float g, float b, const Material *mat, int k)
{
    float d = 1e30f;
    int bj = 0;
    for (int j = 0; j < k; j++) { float e = dist2(r, g, b, &mat[j]); if (e < d) d = e, bj = j; }
    return bj;
}

/* Tile class map -> L x L RGBA (0xAABBGGRR). */
/* The flat look: a smooth coverage field per material (its mask blurred with a Gaussian of sigma
   source pixels, clamped at the border), sampled at full output resolution; each pixel takes the
   strongest material, anti-aliased against the runner-up over about one output pixel. Contours become
   smooth curves at the output resolution, small wiggles below the blur disappear, corners round
   slightly. */
static float sigma = 2.0f;
static void draw_fields(const ClassMap *cm, const Material *mat, uint32_t *out, int L)
{
    static float field[MAXK + 1][TILE * TILE], tmp[TILE * TILE];
    int present[MAXK + 1], np = 0;
    bool has[256] = { 0 };
    for (int i = 0; i < TILE * TILE; i++) has[cm->cls[i]] = true;
    for (int c = 0; c < 256; c++)
        if (has[c] && np < MAXK + 1) present[np++] = c;
    int R = (int)ceilf(sigma * 3);
    float kern[32], ks = 0;
    for (int k = -R; k <= R; k++) ks += kern[k + R] = expf(-(float)(k * k) / (2 * sigma * sigma));
    for (int k = 0; k <= 2 * R; k++) kern[k] /= ks;
    for (int j = 0; j < np; j++) {
        float *f = field[j];
        for (int i = 0; i < TILE * TILE; i++) f[i] = cm->cls[i] == present[j];
        for (int y = 0; y < TILE; y++)
            for (int x = 0; x < TILE; x++) {
                float v = 0;
                for (int k = -R; k <= R; k++) { int X = x + k < 0 ? 0 : x + k > TILE - 1 ? TILE - 1 : x + k; v += kern[k + R] * f[y * TILE + X]; }
                tmp[y * TILE + x] = v;
            }
        for (int y = 0; y < TILE; y++)
            for (int x = 0; x < TILE; x++) {
                float v = 0;
                for (int k = -R; k <= R; k++) { int Y = y + k < 0 ? 0 : y + k > TILE - 1 ? TILE - 1 : y + k; v += kern[k + R] * tmp[Y * TILE + x]; }
                f[y * TILE + x] = v;
            }
    }
    float gain = (float)L / TILE * sigma * 1.25f;
    for (int y = 0; y < L; y++)
        for (int x = 0; x < L; x++) {
            float sx = (x + 0.5f) * TILE / L - 0.5f, sy = (y + 0.5f) * TILE / L - 0.5f;
            sx = sx < 0 ? 0 : sx > TILE - 1 ? TILE - 1 : sx;
            sy = sy < 0 ? 0 : sy > TILE - 1 ? TILE - 1 : sy;
            int x0 = (int)sx, y0 = (int)sy, x1 = x0 < TILE - 1 ? x0 + 1 : x0, y1 = y0 < TILE - 1 ? y0 + 1 : y0;
            float fx = sx - x0, fy = sy - y0;
            float f1 = -1, f2 = -1;
            int c1 = 0, c2 = 0;
            for (int j = 0; j < np; j++) {
                const float *f = field[j];
                float v = (f[y0 * TILE + x0] * (1 - fx) + f[y0 * TILE + x1] * fx) * (1 - fy) +
                          (f[y1 * TILE + x0] * (1 - fx) + f[y1 * TILE + x1] * fx) * fy;
                if (v > f1) f2 = f1, c2 = c1, f1 = v, c1 = present[j];
                else if (v > f2) f2 = v, c2 = present[j];
            }
            float t = 0.5f + (f1 - (f2 < 0 ? 0 : f2)) * gain;
            if (f2 < 0) t = 1;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            float r1 = 0, g1 = 0, b1 = 0, a1 = 0, r2 = 0, g2 = 0, b2 = 0, a2 = 0;
            if (c1 != TRANSPARENT) r1 = mat[c1].r, g1 = mat[c1].g, b1 = mat[c1].b, a1 = 255;
            if (c2 != TRANSPARENT) r2 = mat[c2].r, g2 = mat[c2].g, b2 = mat[c2].b, a2 = 255;
            if (c1 == TRANSPARENT) r1 = r2, g1 = g2, b1 = b2;   /* colour of an edge into transparency */
            if (c2 == TRANSPARENT) r2 = r1, g2 = g1, b2 = b1;
            float r = r2 + (r1 - r2) * t, g = g2 + (g1 - g2) * t, b = b2 + (b1 - b2) * t, a = a2 + (a1 - a2) * t;
            out[y * L + x] = (uint32_t)(a + 0.5f) << 24 | (uint32_t)(b + 0.5f) << 16 | (uint32_t)(g + 0.5f) << 8 | (uint32_t)(r + 0.5f);
        }
}

static void draw(const ClassMap *cm, const Material *mat, uint32_t *out, int L)
{
    for (int y = 0; y < L; y++)
        for (int x = 0; x < L; x++) {
            float sx = (x + 0.5f) * TILE / L - 0.5f, sy = (y + 0.5f) * TILE / L - 0.5f;
            int x0 = (int)floorf(sx), y0 = (int)floorf(sy);
            float fx = sx - x0, fy = sy - y0;
            /* a Gaussian vote (sigma 0.9 source pixels) of the 4 x 4 nearest class cells, clamped at the
               border: smooth curves instead of pixel outlines */
            int cls[16];
            float w[16];
            for (int k = 0; k < 16; k++) {
                int dx = (k & 3) - 1, dy = (k >> 2) - 1;
                int cx = x0 + dx, cy = y0 + dy;
                float ddx = (float)dx - fx, ddy = (float)dy - fy;
                cx = cx < 0 ? 0 : cx > TILE - 1 ? TILE - 1 : cx;
                cy = cy < 0 ? 0 : cy > TILE - 1 ? TILE - 1 : cy;
                cls[k] = cm->cls[cy * TILE + cx];
                /* a separable tent (square support): straight edges and square corners stay square, steps
                   along a diagonal become 45-degree edges */
                float tx = 1.0f - fabsf(ddx) / (0.75f * grid + 0.5f), ty = 1.0f - fabsf(ddy) / (0.75f * grid + 0.5f);
                w[k] = (tx > 0 ? tx : 0) * (ty > 0 ? ty : 0);
            }
            /* merge the votes per class, sharpen them (anti-aliased but crisp contours) */
            int uc[16], nu = 0;
            float uw[16];
            for (int k = 0; k < 16; k++) {
                int j = 0;
                while (j < nu && uc[j] != cls[k]) j++;
                if (j == nu) uc[nu] = cls[k], uw[nu++] = 0;
                uw[j] += w[k];
            }
            float tot = 0;
            for (int j = 0; j < nu; j++) {
                float a = uw[j] * uw[j];
                a = a * a;
                uw[j] = a * a;   /* ^8: crisp, still anti-aliased */
                tot += uw[j];
            }
            float sh = 0;
            for (int k = 0; k < 4; k++) {
                int cx = x0 + (k & 1), cy = y0 + (k >> 1);
                cx = cx < 0 ? 0 : cx > TILE - 1 ? TILE - 1 : cx;
                cy = cy < 0 ? 0 : cy > TILE - 1 ? TILE - 1 : cy;
                sh += ((k & 1) ? fx : 1 - fx) * ((k >> 1) ? fy : 1 - fy) * cm->shade[cy * TILE + cx];
            }
            if (flat) sh = 1;
            if (sh < 0.45f) sh = 0.45f;
            if (sh > 1.6f) sh = 1.6f;
            float r = 0, g = 0, b = 0, alpha = 0;
            for (int j = 0; j < nu; j++) {
                float wj = uw[j] / tot;
                if (uc[j] == TRANSPARENT) continue;
                const Material *m = &mat[uc[j]];
                float amp = 0.06f + m->var / 255.0f * 0.8f;   /* our grain, scaled by how varied the material is */
                if (amp > 0.22f) amp = 0.22f;
                float t = flat ? 1 : sh * (1 + amp * texture(x, y, L, (uint32_t)uc[j] * 7919u + 1));
                r += wj * m->r * t, g += wj * m->g * t, b += wj * m->b * t, alpha += wj;
            }
            if (alpha > 0) r /= alpha, g /= alpha, b /= alpha;
            uint32_t R = r > 255 ? 255 : (uint32_t)r, G = g > 255 ? 255 : (uint32_t)g, B = b > 255 ? 255 : (uint32_t)b;
            uint32_t A = (uint32_t)(alpha * 255 + 0.5f);
            out[y * L + x] = A << 24 | B << 16 | G << 8 | R;
        }
}

static bool mkdirs(const char *path)
{
    char p[1024];
    snprintf(p, sizeof p, "%s", path);
    for (char *s = p + 1; *s; s++)
        if (*s == '/') { *s = 0; mkdir(p, 0755); *s = '/'; }
    struct stat st;
    return mkdir(p, 0755) == 0 || (stat(p, &st) == 0 && S_ISDIR(st.st_mode));
}

/* ---- the seam check ---- */

static int px_diff(uint32_t a, uint32_t b)
{
    int d = 0;
    for (int k = 0; k < 24; k += 8) d += abs((int)(a >> k & 0xff) - (int)(b >> k & 0xff));
    return d;
}

/* The original lid n as L x L (nearest), 0xAABBGGRR like the generated ones, for comparison. */
static void original_lid(const Style *s, int n, uint32_t *out, int L)
{
    for (int y = 0; y < L; y++)
        for (int x = 0; x < L; x++) {
            bool tr;
            uint32_t c = tile_rgb(s, s->lid_clut[n][0], s->lid_base + n, x * TILE / L, y * TILE / L, &tr);
            out[y * L + x] = 0xff000000u | (c & 0xff) << 16 | (c & 0xff00) | (c >> 16 & 0xff);
        }
}

/* For every pair of neighbouring top lids in the style's maps (same layer, not rotated or flipped, not
   sloped): the colour step across the shared edge against the step over the same distance (one source
   pixel) just inside the tiles. Around 1 = the boundary can't be told from the inside; the original's own
   value is printed next to it for comparison. */
static void seam_check(const Style *s, int number, uint32_t **gen, int L)
{
    static const char *const MAPS[] = { "GTADATA/NYC.CMP", "GTADATA/SANB.CMP", "GTADATA/MIAMI.CMP" };
    char err[256];
    Map *m = map_load(MAPS[number - 1], err, sizeof err);
    if (!m) { fprintf(stderr, "skin_generate: seam check: %s\n", err); return; }
    uint32_t *oa = malloc((size_t)L * L * 4), *ob = malloc((size_t)L * L * 4);
    double gs = 0, gi = 0, os = 0, oi = 0;
    long pairs = 0;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            for (int dir = 0; dir < 2; dir++) {
                int X = x + (dir == 0), Y = y + (dir == 1);
                if (X >= MAP_W || Y >= MAP_H) continue;
                int z;
                const MapBlock *a = NULL, *b = NULL;
                for (z = 0; z < MAP_Z; z++)
                    if ((a = map_get_block(m, x, y, z)) && a->lid) break;
                if (z == MAP_Z || !(b = map_get_block(m, X, Y, z)) || !b->lid) continue;
                if ((a->type_map | b->type_map) & 0xff00 || (a->ext | b->ext) & 0x60) continue;   /* rotated, sloped, flipped */
                if (a->lid >= s->nlid || b->lid >= s->nlid) continue;
                const uint32_t *ga = gen[a->lid], *gb = gen[b->lid];
                if (!ga || !gb) continue;
                original_lid(s, a->lid, oa, L);
                original_lid(s, b->lid, ob, L);
                for (int k = 0; k < L; k++) {
                    /* across the edge, and one step inside each tile */
                    /* one source pixel apart (scale output pixels) on both sides of the comparison */
                    int ia1 = dir == 0 ? k * L + L - 1 : (L - 1) * L + k, ia2 = dir == 0 ? k * L + L - 1 - scale : (L - 1 - scale) * L + k;
                    int ib1 = dir == 0 ? k * L : k, ib2 = dir == 0 ? k * L + scale : scale * L + k;
                    gs += px_diff(ga[ia1], gb[ib1]), gi += 0.5 * (px_diff(ga[ia1], ga[ia2]) + px_diff(gb[ib1], gb[ib2]));
                    os += px_diff(oa[ia1], ob[ib1]), oi += 0.5 * (px_diff(oa[ia1], oa[ia2]) + px_diff(ob[ib1], ob[ib2]));
                }
                pairs++;
            }
    if (pairs)
        printf("style %03d seams: %ld neighbouring lids; edge step / inside step = %.2f generated, %.2f original\n",
               number, pairs, gi > 0 ? gs / gi : 0, oi > 0 ? os / oi : 0);
    free(oa);
    free(ob);
    map_free(m);
}

static bool generate_style(int number)
{
    char err[256];
    Style *s = style_load(number, err, sizeof err);
    if (!s) { fprintf(stderr, "skin_generate: %s\n", err); return false; }
    style_convert_palettes(s, &PIXFMT_32);
    int nt = s->nside + s->nlid + s->naux;
    /* every tile with its default CLUT: side direction 0, lid remap 0, aux as a side frame */
    typedef struct { const char *kind; int n, t; const uint32_t *clut; } TileRef;
    TileRef *tiles = malloc((size_t)nt * sizeof *tiles);
    int k = 0;
    for (int i = 0; i < s->nside; i++) tiles[k++] = (TileRef){ "side", i, s->side_base + i, s->side_clut[i][0] };
    for (int i = 0; i < s->nlid; i++) tiles[k++] = (TileRef){ "lid", i, s->lid_base + i, s->lid_clut[i][0] };
    for (int i = 0; i < s->naux; i++) tiles[k++] = (TileRef){ "aux", i, s->aux_base + i, s->aux_side_clut[i][0] };
    /* 1. the colour histogram */
    Hist *h = malloc((size_t)nt * TILE * TILE * sizeof *h);
    int nh = 0;
    for (int i = 0; i < nt; i++)
        for (int v = 0; v < TILE; v++)
            for (int u = 0; u < TILE; u++) {
                bool tr;
                uint32_t c = tile_rgb(s, tiles[i].clut, tiles[i].t, u, v, &tr);
                if (!tr) h[nh++] = (Hist){ c, 1 };
            }
    qsort(h, (size_t)nh, sizeof *h, cmp_hist);
    int nu = 0;
    for (int i = 0; i < nh; i++)
        if (nu && h[nu - 1].c == h[i].c) h[nu - 1].n++;
        else h[nu++] = h[i];
    Material mat[MAXK] = { 0 };
    cluster(h, nu, mat, nmat);
    printf("style %03d: %d tiles, %d colours -> %d materials\n", number, nt, nu, nmat);
    /* 2-3. class maps and drawing */
    int L = TILE * scale;
    uint32_t *img = malloc((size_t)L * L * sizeof *img);
    uint32_t **lid_img = calloc((size_t)s->nlid, sizeof *lid_img);
    ClassMap cm;
    for (int i = 0; i < nt; i++) {
        for (int v = 0; v < TILE; v++)
            for (int u = 0; u < TILE; u++) {
                bool tr;
                float r, g, b;
                rgbf(tile_rgb(s, tiles[i].clut, tiles[i].t, u, v, &tr), &r, &g, &b);
                int c = tr ? TRANSPARENT : classify(r, g, b, mat, nmat);
                cm.cls[v * TILE + u] = (uint8_t)c;
                float ml = c == TRANSPARENT ? 1 : luma(mat[c].r, mat[c].g, mat[c].b);
                cm.shade[v * TILE + u] = c == TRANSPARENT ? 1 : (luma(r, g, b) + 8) / (ml + 8);
            }
        smooth_shade(cm.shade);
        static Lines lines;
        vectorise(&cm, &lines);
        if (flat) draw_fields(&cm, mat, img, L);
        else draw(&cm, mat, img, L);
        draw_lines(&lines, mat, img, L);
        char dir[1024], path[1100];
        snprintf(dir, sizeof dir, "%s/style%03d/%s", out_dir, number, tiles[i].kind);
        mkdirs(dir);
        snprintf(path, sizeof path, "%s/%d.png", dir, tiles[i].n);
        if (!png_write(path, img, L, L, PNG_RGBA)) { fprintf(stderr, "skin_generate: can't write %s\n", path); return false; }
        if (!strcmp(tiles[i].kind, "lid")) {
            lid_img[tiles[i].n] = malloc((size_t)L * L * sizeof *img);
            memcpy(lid_img[tiles[i].n], img, (size_t)L * L * sizeof *img);
        }
    }
    seam_check(s, number, lid_img, L);
    for (int i = 0; i < s->nlid; i++) free(lid_img[i]);
    free(lid_img);
    free(img);
    free(h);
    free(tiles);
    style_free(s);
    return true;
}

int main(int argc, char **argv)
{
    const char *data = NULL;
    int only = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc) data = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out_dir = argv[++i];
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--style") && i + 1 < argc) only = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--materials") && i + 1 < argc) nmat = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--min-region") && i + 1 < argc) min_region = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--light-weight") && i + 1 < argc) light_weight = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--textured")) flat = false;
        else if (!strcmp(argv[i], "--grid") && i + 1 < argc) grid = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--sigma") && i + 1 < argc) sigma = (float)atof(argv[++i]);
        else {
            fprintf(stderr, "usage: skin_generate [--data DIR] [--out DIR] [--scale S] [--style N] [--materials K] [--min-region N]\n"
                            "Draws a new skin from scratch, using your own copy of the game only as a reference for\n"
                            "what is where on each tile. Keep the result for your own use: don't distribute it.\n");
            return 2;
        }
    }
    if (scale < 1 || scale > 8 || nmat < 2 || nmat > MAXK) { fprintf(stderr, "skin_generate: bad --scale or --materials\n"); return 2; }
    if (!(data ? vfs_mount_path(data) : vfs_mount_default())) { fprintf(stderr, "skin_generate: no game data\n"); return 1; }
    char err[256];
    if (!exe_init(err, sizeof err)) { fprintf(stderr, "skin_generate: %s\n", err); return 1; }
    mkdirs(out_dir);
    char ini[1100];
    snprintf(ini, sizeof ini, "%s/skin.ini", out_dir);
    FILE *f = fopen(ini, "w");
    if (f) {
        fprintf(f, "; Generated by tools/skin-generate.c from the structure of your own copy of the game (what is\n"
                   "; where on each tile, not its pixels). For your own use: don't distribute it.\n"
                   "name = Generated\nauthor = skin_generate\nscale = %d\n", scale);
        fclose(f);
    }
    for (int n = 1; n <= 3; n++)
        if (!only || only == n)
            if (!generate_style(n)) return 1;
    return 0;
}
