/* skin_generate: a new skin whose tiles join seamlessly, drawn from scratch with the original as a
   reference only for *what is where* on each tile, in a flat vector look with a palette per city.

     skin_generate [--data DIR] [--out DIR] [--scale S] [--style N] [--materials K] [--generic]

   How it works, per style:
   1. Materials: every colour the tiles use (side, lid and aux tiles through their default CLUTs) is
      clustered into K materials (k-means on a colour histogram).
   2. What each tile is: the map says which ground type (road, pavement, field, water) lies on every lid;
      aux tiles inherit the lid whose animation shows them. Everything else is a building: walls (sides)
      and roofs (the other lids).
   3. Class maps, no colour value of the tile reaches the output, only which class is where:
      - ground: the tile blurred (the texture's grain gone) against the style's reference colour of each
        ground type it is used for, plus road markings (white on asphalt, ochre wherever there is road);
      - roofs: the materials' shade groups (one surface in light and shadow is one class), ragged
        patches absorbed;
      - walls: the materials as they are.
      Then lines (long, thin, straight regions: markings, joints, frames) become strokes, noise is
      absorbed, and on buildings box-like regions become rectangles (windows, panels).
   4. Drawing at S x 64 pixels: smooth coverage fields per class, flat colours: the ground classes in the
      city's palette (pavements with a tile-aligned slab grid), the materials graded towards it.
   5. Seams: tiles that join in the original have the same classes along the shared edge; the fields
      clamp at the border, the grid and the strokes are symmetric under the map's rotations and flips.
      The seam check prints the colour step across the neighbouring lids of the maps against the step
      inside them (about 1 or less: the boundaries can't be told).
   --generic: the material classes everywhere (no semantics, no palette).

   The output is generated from the structure of your own copy of the game: keep it for your own use,
   don't distribute it (skin.ini says so). */
#include "exe.h"
#include "game/carinfo.h"
#include "map.h"
#include "render/hires/hires_skin.h"
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

/* og_core's platform hooks (the game's saves; unused here) */
uint8_t *plat_load_user_file(const char *name, size_t *size) { (void)name, (void)size; return NULL; }
bool plat_save_user_file(const char *name, const void *data, size_t size) { (void)name, (void)data, (void)size; return true; }

enum { TILE = 64, MAXK = 64, TRANSPARENT = 255 };

static int scale = 4, nmat = 32, min_region = 40;
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
static float light_weight = 0.4f;
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
static float sigma = 2.0f, building_sigma = 0.9f, sprite_sigma = 1.3f, vehicle_sigma = 1.5f;
static int field_mirror;   /* > 0: the fields are made symmetric about x = (field_mirror - 1) / 2 (cars) */
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
    if (field_mirror > 0)
        for (int j = 0; j < np; j++)
            for (int y = 0; y < TILE; y++)
                for (int x = 0; x < field_mirror / 2; x++) {
                    int px = field_mirror - 1 - x;
                    if (px >= TILE) continue;
                    float *a = &field[j][y * TILE + x], *b = &field[j][y * TILE + px], m = (*a + *b) / 2;
                    *a = *b = m;
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

/* ---- the semantic look: what a tile is used for decides how it is drawn ----
   The maps say what every lid is: the block type of each block that shows it (water, road, pavement,
   field). A lid used (mostly) on the ground gets the ground classes: each pixel, blurred over 5 x 5 so the
   texture's grain is gone, goes to the nearest of the style's reference colours for asphalt, pavement,
   grass and water (the median colour of the lids of that type), and road markings are the light or
   yellow pixels on asphalt. Those classes are drawn in the city's own palette, pavements with a slab
   grid aligned to the tile (period 16, so tiles continue each other under every rotation and flip).
   Everything else keeps the material classes, with the colours graded towards the city's palette. */
enum { K_WATER = 1, K_ROAD = 2, K_PAVE = 3, K_FIELD = 4, NKIND = 8 };
enum { S_ASPHALT, S_PAVE, S_GRASS, S_WATER, S_WHITE, S_YELLOW, NSEM };
static bool semantic = true;   /* --generic: the material classes everywhere */

typedef struct {
    uint32_t sem[NSEM];   /* 0xRRGGBB */
    uint32_t slab;        /* the pavement's joints */
    float sat, tint, tr, tg, tb;   /* grading of the other materials: saturation, then a mix towards the tint */
} CityPalette;
/* Liberty City: cool slate and stone; San Andreas: warm sand under fog; Vice City: pastels and turquoise. */
static const CityPalette PALETTES[3] = {
    { { 0x4b5263, 0xcfc8b4, 0x6e9450, 0x3b6f9e, 0xeeeee6, 0xf0c03a }, 0xb3ab96, 0.95f, 0.14f, 140, 160, 195 },
    { { 0x58534e, 0xd9c7a2, 0x91a457, 0x4a8fae, 0xf3efe4, 0xf2b632 }, 0xbfad88, 1.00f, 0.14f, 205, 165, 125 },
    { { 0x4e5068, 0xecd3c6, 0x4fb36b, 0x2bb3c6, 0xfbf6ee, 0xffc93c }, 0xd8b8aa, 1.15f, 0.12f, 240, 160, 195 },
};

static void set_rgb(Material *m, uint32_t c) { m->r = (float)(c >> 16 & 0xff), m->g = (float)(c >> 8 & 0xff), m->b = (float)(c & 0xff); }

/* A material's colour graded towards the city: saturation scaled around its grey, then mixed with the tint. */
static void grade(Material *m, const CityPalette *p)
{
    float y = luma(m->r, m->g, m->b);
    float *c[3] = { &m->r, &m->g, &m->b }, t[3] = { p->tr, p->tg, p->tb };
    for (int i = 0; i < 3; i++) {
        float v = y + (*c[i] - y) * p->sat;
        v += (t[i] * y / 160.0f - v) * p->tint;   /* the tint at the material's own lightness */
        *c[i] = v < 0 ? 0 : v > 255 ? 255 : v;
    }
}

/* lid_kind[n]: the ground type lid n is mostly under (K_*), 0 if it isn't ground; aux_kind the same for
   the aux tiles, inherited from the lid whose animation shows them. */
static void usage_scan(const Style *s, int number, int8_t *lid_kind, int8_t *aux_kind, uint8_t *lid_allow, uint8_t *aux_allow)
{
    static const char *const MAPS[] = { "GTADATA/NYC.CMP", "GTADATA/SANB.CMP", "GTADATA/MIAMI.CMP" };
    static int use[256][NKIND];
    memset(use, 0, sizeof use);
    memset(lid_kind, 0, 256);
    memset(aux_kind, 0, 256);
    memset(lid_allow, 0, 256);
    memset(aux_allow, 0, 256);
    char err[256];
    Map *m = map_load(MAPS[number - 1], err, sizeof err);
    if (!m) { fprintf(stderr, "skin_generate: %s\n", err); return; }
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            for (int z = 0; z < MAP_Z; z++) {
                /* the ground type is the empty block's above the lid (docs/formats.md); a lid at the top of
                   its column (a roof, mostly) counts as none */
                const MapBlock *b = map_get_block(m, x, y, z), *a = z ? map_get_block(m, x, y, z - 1) : NULL;
                if (b && b->lid && b->lid < s->nlid) use[b->lid][a ? a->type_map >> 4 & 7 : 7]++;
            }
    map_free(m);
    for (int n = 0; n < s->nlid; n++) {
        int total = 0, ground = 0, best = 0;
        for (int k = 0; k < NKIND; k++) total += use[n][k];
        for (int k = K_WATER; k <= K_FIELD; k++) {
            ground += use[n][k];
            if (use[n][k] > use[n][best]) best = k;
        }
        if (!total || ground * 10 < total * 6) continue;
        lid_kind[n] = (int8_t)best;
        /* the ground classes a pixel of it may take: the types it is used for (8 % or more); road and
           pavement go together (kerbs) */
        for (int k = K_WATER; k <= K_FIELD; k++)
            if (k == best || use[n][k] * 100 >= total * 8) lid_allow[n] |= (uint8_t)(1u << k);
        if (lid_allow[n] & (1u << K_ROAD | 1u << K_PAVE)) lid_allow[n] |= 1u << K_ROAD | 1u << K_PAVE;
    }
    for (int i = 0; i < s->nanims; i++) {
        const uint8_t *d = s->anims[i].def;   /* block, which (0 side, 1 lid), speed, n, frames */
        if (d[1] != 1) continue;
        for (int f = 0; f < d[3]; f++)
            if (d[4 + f] < s->naux) aux_kind[d[4 + f]] = lid_kind[d[0]], aux_allow[d[4 + f]] = lid_allow[d[0]];
    }
}

/* The tile blurred over 5 x 5 (clamped at the border; transparent pixels left out). */
static void blur_tile(const Style *s, const uint32_t *clut, int t, float (*out)[3])
{
    static float raw[TILE * TILE][3];
    static bool tr[TILE * TILE];
    for (int v = 0; v < TILE; v++)
        for (int u = 0; u < TILE; u++) rgbf(tile_rgb(s, clut, t, u, v, &tr[v * TILE + u]), &raw[v * TILE + u][0], &raw[v * TILE + u][1], &raw[v * TILE + u][2]);
    for (int y = 0; y < TILE; y++)
        for (int x = 0; x < TILE; x++) {
            float a[3] = { 0, 0, 0 };
            int n = 0;
            for (int dy = -2; dy <= 2; dy++)
                for (int dx = -2; dx <= 2; dx++) {
                    int X = x + dx, Y = y + dy;
                    if (X < 0 || Y < 0 || X >= TILE || Y >= TILE || tr[Y * TILE + X]) continue;
                    for (int c = 0; c < 3; c++) a[c] += raw[Y * TILE + X][c];
                    n++;
                }
            for (int c = 0; c < 3; c++) out[y * TILE + x][c] = n ? a[c] / n : 0;
        }
}

/* The reference colour of each ground type: the per-channel median of the blurred pixels of its lids. */
static bool ground_refs(const Style *s, const int8_t *lid_kind, Material *ref)
{
    static uint32_t hist[NKIND][3][256];
    static float bl[TILE * TILE][3];
    memset(hist, 0, sizeof hist);
    for (int n = 0; n < s->nlid; n++) {
        if (!lid_kind[n]) continue;
        blur_tile(s, s->lid_clut[n][0], s->lid_base + n, bl);
        for (int i = 0; i < TILE * TILE; i++)
            for (int c = 0; c < 3; c++) hist[lid_kind[n]][c][(int)(bl[i][c] + 0.5f) & 255]++;
    }
    bool any = false;
    for (int k = 0; k < NKIND; k++) {
        float v[3] = { 0, 0, 0 };
        uint64_t total = 0;
        for (int i = 0; i < 256; i++) total += hist[k][0][i];
        ref[k].n = (double)total;
        if (!total) continue;
        any = true;
        for (int c = 0; c < 3; c++) {
            uint64_t acc = 0;
            int i = 0;
            while (i < 255 && (acc += hist[k][c][i]) * 2 < total) i++;
            v[c] = (float)i;
        }
        ref[k].r = v[0], ref[k].g = v[1], ref[k].b = v[2];
    }
    return any;
}

/* Distance to a ground reference: chroma fully, lightness at 0.3 (a shadowed pavement is still pavement). */
enum { GROUND_MATCH = 48 };
static float ground_dist(float r, float g, float b, const Material *m)
{
    float dr = r - m->r, dg = g - m->g, db = b - m->b;
    float dy = 0.299f * dr + 0.587f * dg + 0.114f * db;
    float dcb = -0.169f * dr - 0.331f * dg + 0.5f * db, dcr = 0.5f * dr - 0.419f * dg - 0.081f * db;
    return sqrtf(0.3f * dy * dy + 4 * (dcb * dcb + dcr * dcr));
}

/* The ground classes of one tile into cm (classes nmat + S_*); pixels far from every reference keep their
   material class. */
static void classify_ground(const Style *s, const uint32_t *clut, int t, uint8_t allow, const Material *ref, const Material *mat, ClassMap *cm)
{
    static float bl[TILE * TILE][3];
    blur_tile(s, clut, t, bl);
    static const int KIND_SEM[NKIND] = { -1, S_WATER, S_ASPHALT, S_PAVE, S_GRASS, -1, -1, -1 };
    float ya = luma(ref[K_ROAD].r, ref[K_ROAD].g, ref[K_ROAD].b);
    for (int v = 0; v < TILE; v++)
        for (int u = 0; u < TILE; u++) {
            int i = v * TILE + u;
            bool tr;
            float r, g, b;
            rgbf(tile_rgb(s, clut, t, u, v, &tr), &r, &g, &b);
            cm->shade[i] = 1;
            if (tr) { cm->cls[i] = TRANSPARENT; continue; }
            float best = 1e30f;
            int bk = -1;
            for (int k = K_WATER; k <= K_FIELD; k++) {
                if (!ref[k].n || !(allow & (1u << k))) continue;
                float d = ground_dist(bl[i][0], bl[i][1], bl[i][2], &ref[k]);
                if (d < best) best = d, bk = k;
            }
            int c;
            if (bk < 0 || best > GROUND_MATCH) c = classify(r, g, b, mat, nmat);
            else c = nmat + KIND_SEM[bk];
            /* markings: ochre paint (red clearly over green over blue; the beige pavements have red ~ green)
               wherever there may be road, white only on asphalt */
            if ((allow & 1u << K_ROAD) && r > 140 && r - g > 25 && g - b > 20) c = nmat + S_YELLOW;
            else if (bk == K_ROAD && best <= GROUND_MATCH) {
                float y = luma(r, g, b), mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b));
                if (y > ya + 80 && mx - mn < 70) c = nmat + S_WHITE;
            }
            cm->cls[i] = (uint8_t)c;
        }
}

/* The pavement's slab joints over the drawn tile: lines on the tile's own 16-pixel grid, borders included
   (half a joint on each side of a tile edge, so neighbours make a whole one). */
static void draw_slabs(const ClassMap *cm, int pave, uint32_t colour, uint32_t *out, int L)
{
    const float period = 16, hw = 0.45f;   /* source pixels */
    float k = (float)L / TILE;
    float cr = (float)(colour >> 16 & 0xff), cg = (float)(colour >> 8 & 0xff), cb = (float)(colour & 0xff);
    for (int y = 0; y < L; y++)
        for (int x = 0; x < L; x++) {
            float sx = (x + 0.5f) / k, sy = (y + 0.5f) / k;
            int ux = (int)sx, uy = (int)sy;
            if (cm->cls[(uy < TILE ? uy : TILE - 1) * TILE + (ux < TILE ? ux : TILE - 1)] != pave) continue;
            float dx = fabsf(sx - period * floorf(sx / period + 0.5f)), dy = fabsf(sy - period * floorf(sy / period + 0.5f));
            float d = fminf(dx, dy) * k, cov = hw * k - d + 0.5f;
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            uint32_t o = out[y * L + x];
            float r = (float)(o & 0xff), g = (float)(o >> 8 & 0xff), b = (float)(o >> 16 & 0xff);
            r += (cr - r) * cov, g += (cg - g) * cov, b += (cb - b) * cov;
            out[y * L + x] = (o & 0xff000000u) | (uint32_t)(b + 0.5f) << 16 | (uint32_t)(g + 0.5f) << 8 | (uint32_t)(r + 0.5f);
        }
}

/* Building tiles as rectangles: every region inside the tile that is box-like (it fills 60 % of its
   bounding box or more) becomes its bounding box. Regions are painted largest box first, so a window's
   frame stays under its glass; regions touching the border keep their pixels (the neighbour's continuation
   of them is the same in the original), as do ragged ones. */
static void rectify(ClassMap *cm, bool absorb_ragged)
{
    typedef struct { int x0, y0, x1, y1, area, nm, first; uint8_t c; bool rect; } Region;
    static Region reg[TILE * TILE];
    static int16_t label[TILE * TILE];
    static int stack[TILE * TILE], order[TILE * TILE];
    static uint8_t out[TILE * TILE];
    int nr = 0;
    memset(label, -1, sizeof label);
    for (int start = 0; start < TILE * TILE; start++) {
        if (label[start] != -1) continue;
        uint8_t c = cm->cls[start];
        Region *r = &reg[nr];
        *r = (Region){ TILE, TILE, -1, -1, 0, 0, start, c, false };
        bool border = false;
        int sp = 0;
        stack[sp++] = start;
        label[start] = (int16_t)nr;
        while (sp) {
            int p = stack[--sp], x = p % TILE, y = p / TILE;
            r->nm++;
            if (x == 0 || y == 0 || x == TILE - 1 || y == TILE - 1) border = true;
            if (x < r->x0) r->x0 = x;
            if (x > r->x1) r->x1 = x;
            if (y < r->y0) r->y0 = y;
            if (y > r->y1) r->y1 = y;
            const int nb[4] = { x > 0 ? p - 1 : -1, x < TILE - 1 ? p + 1 : -1, y > 0 ? p - TILE : -1, y < TILE - 1 ? p + TILE : -1 };
            for (int k = 0; k < 4; k++)
                if (nb[k] >= 0 && label[nb[k]] == -1 && cm->cls[nb[k]] == c) label[nb[k]] = (int16_t)nr, stack[sp++] = nb[k];
        }
        r->area = (r->x1 - r->x0 + 1) * (r->y1 - r->y0 + 1);
        r->rect = !border && c != TRANSPARENT && r->nm >= 6 && r->nm * 10 >= r->area * 6;
        /* (roofs) a ragged patch inside the tile, of any size up to a quarter of it, is texture: it takes the
           class around it */
        if (absorb_ragged && !border && !r->rect && r->nm * 2 < r->area && r->nm < TILE * TILE / 4) {
            int count[256] = { 0 }, best = -1;
            for (int y = r->y0; y <= r->y1; y++)
                for (int x = r->x0; x <= r->x1; x++) {
                    if (label[y * TILE + x] != nr) continue;
                    const int nb[4] = { x > 0 ? -1 : 0, x < TILE - 1 ? 1 : 0, y > 0 ? -TILE : 0, y < TILE - 1 ? TILE : 0 };
                    for (int k = 0; k < 4; k++) {
                        int q = y * TILE + x + nb[k];
                        if (nb[k] && label[q] != nr && cm->cls[q] != TRANSPARENT && ++count[cm->cls[q]] > (best < 0 ? 0 : count[best])) best = cm->cls[q];
                    }
                }
            if (best >= 0) r->c = (uint8_t)best;
        }
        order[nr] = nr;
        nr++;
    }
    /* largest box first (insertion sort: few hundred regions) */
    for (int i = 1; i < nr; i++) {
        int o = order[i], j = i;
        while (j > 0 && reg[order[j - 1]].area < reg[o].area) order[j] = order[j - 1], j--;
        order[j] = o;
    }
    for (int i = 0; i < nr; i++) {
        const Region *r = &reg[order[i]];
        for (int y = r->y0; y <= r->y1; y++)
            for (int x = r->x0; x <= r->x1; x++)
                if (r->rect || label[y * TILE + x] == order[i]) out[y * TILE + x] = r->c;
    }
    memcpy(cm->cls, out, sizeof out);
}

/* Shade groups: materials that are one surface in light and shadow (close in chroma, lightness within
   dl) are one class, drawn in their mean colour. A material joins the first more used representative it
   is close to (no chaining: black, grey and white stay apart). The same for every tile, so neighbours
   agree. */
static void shade_groups(const Material *mat, uint8_t *group, float dl)
{
    int order[MAXK];
    for (int i = 0; i < nmat; i++) order[i] = i;
    for (int i = 1; i < nmat; i++) {
        int o = order[i], j = i;
        while (j > 0 && mat[order[j - 1]].n < mat[o].n) order[j] = order[j - 1], j--;
        order[j] = o;
    }
    int reps[MAXK], nr = 0;
    for (int i = 0; i < nmat; i++) {
        const Material *m = &mat[order[i]];
        int g = -1;
        for (int k = 0; k < nr && g < 0; k++) {
            const Material *r = &mat[reps[k]];
            float dr = m->r - r->r, dg = m->g - r->g, db = m->b - r->b;
            float dy = 0.299f * dr + 0.587f * dg + 0.114f * db;
            float dcb = -0.169f * dr - 0.331f * dg + 0.5f * db, dcr = 0.5f * dr - 0.419f * dg - 0.081f * db;
            if (fabsf(dy) < dl && dcb * dcb + dcr * dcr < 14 * 14) g = reps[k];
        }
        if (g < 0) reps[nr++] = g = order[i];
        group[order[i]] = (uint8_t)g;
    }
}

/* ---- sprites: vehicles (with their paint masks), objects, traffic lights ----
   The same flat look on the sprites the world shows. Peds stay the original's (their clothes are remaps
   of several colours each, which one paint mask can't carry), as do the HUD's arrows and digits.
   A sprite larger than 64 is reduced onto the 64 x 64 class map by majority (f x f texels per class
   pixel) and drawn back at up to 512 pixels. Car paint (the texels remap 1 recolours) gets two classes of
   its own, light and dark, in the sprite's own paint colours, and a paint mask drawn from the same shapes:
   the game recolours the masked pixels per car, shaded by their brightness against the paint's. */
enum { P_LIGHT = MAXK, P_DARK, SPRITE_OUT = 512, VEHICLE_COLOURS = 6 };

static bool sprite_wanted(int n, bool *vehicle)
{
    static const int VEHICLES[] = { SPRITE_GROUP_BOAT, SPRITE_GROUP_BUS, SPRITE_GROUP_CAR, SPRITE_GROUP_TANK, SPRITE_GROUP_TRAIN, SPRITE_GROUP_BIKE };
    static const int OTHERS[] = { SPRITE_GROUP_OBJECT, SPRITE_GROUP_TRAFFIC_LIGHTS, SPRITE_GROUP_TRDOORS, SPRITE_GROUP_BOX };
    for (size_t i = 0; i < sizeof VEHICLES / sizeof *VEHICLES; i++)
        if (n >= sprite_group_base(VEHICLES[i]) && n < sprite_group_base(VEHICLES[i]) + sprite_group_count(VEHICLES[i])) return *vehicle = true;
    for (size_t i = 0; i < sizeof OTHERS / sizeof *OTHERS; i++)
        if (n >= sprite_group_base(OTHERS[i]) && n < sprite_group_base(OTHERS[i]) + sprite_group_count(OTHERS[i])) return !(*vehicle = false);
    return false;
}

static bool write_image(int number, const char *name, const uint32_t *img, int L, int w, int h)
{
    static uint32_t crop[SPRITE_OUT * SPRITE_OUT];
    for (int y = 0; y < h; y++) memcpy(crop + y * w, img + y * L, (size_t)w * sizeof *img);
    char dir[1024], path[1100];
    snprintf(dir, sizeof dir, "%s/style%03d/sprite", out_dir, number);
    mkdirs(dir);
    snprintf(path, sizeof path, "%s/%s.png", dir, name);
    if (png_write(path, crop, w, h, PNG_RGBA)) return true;
    fprintf(stderr, "skin_generate: can't write %s\n", path);
    return false;
}

/* Paint: a texel at least 3 of the car's 12 remaps recolour (by more than HIRES_PAINT_STEP). */
static bool paint_texel(int clut, int palette, uint8_t e)
{
    if (!e || palette < 0) return false;
    const uint32_t *c0 = sprite_remap_clut(clut, 0, 0);
    int n = 0;
    for (int r = 1; r <= CAR_REMAPS; r++) {
        const uint32_t *c = sprite_remap_clut(clut, r, palette);
        int d = 0;
        for (int k = 0; k < 24; k += 8) d += abs((int)(c0[e * 64] >> k & 0xff) - (int)(c[e * 64] >> k & 0xff));
        n += d > HIRES_PAINT_STEP;
    }
    return n >= 3;
}

/* A 3 x 3 majority over the sprite's opaque pixels: a pixel whose class has no other pixel around it takes
   the most common class around it. The outline (transparency) stays. */
static void sprite_speckle(ClassMap *cm, int cw, int ch)
{
    static uint8_t out[TILE * TILE];
    memcpy(out, cm->cls, sizeof out);
    for (int y = 0; y < ch; y++)
        for (int x = 0; x < cw; x++) {
            uint8_t c = cm->cls[y * TILE + x];
            if (c == TRANSPARENT) continue;
            int count[256] = { 0 }, best = -1, same = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int X = x + dx, Y = y + dy;
                    if ((!dx && !dy) || X < 0 || Y < 0 || X >= cw || Y >= ch) continue;
                    uint8_t o = cm->cls[Y * TILE + X];
                    if (o == TRANSPARENT) continue;
                    if (o == c) same++;
                    else if (++count[o] > (best < 0 ? 0 : count[best])) best = o;
                }
            if (!same && best >= 0) out[y * TILE + x] = (uint8_t)best;
        }
    memcpy(cm->cls, out, sizeof out);
}

/* The vector outline: pixels within r of the silhouette (alpha) darkened, anti-aliased by how many of
   16 directions reach outside. */
static void outline(uint32_t *img, int L, int w, int h, float r)
{
    static uint8_t a[SPRITE_OUT * SPRITE_OUT];
    for (int i = 0; i < L * L; i++) a[i] = (uint8_t)(img[i] >> 24);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t c = img[y * L + x];
            if ((c >> 24) < 8) continue;
            int out = 0;
            for (int d = 0; d < 16; d++) {
                float ang = (float)d * 0.3926991f;
                int X = x + (int)lroundf(cosf(ang) * r), Y = y + (int)lroundf(sinf(ang) * r);
                out += X < 0 || Y < 0 || X >= w || Y >= h || a[Y * L + X] < 128;
            }
            if (!out) continue;
            float t = 1 - 0.45f * (out > 4 ? 1 : out / 4.0f);
            uint32_t R = (uint32_t)((c & 0xff) * t), G = (uint32_t)((c >> 8 & 0xff) * t), B = (uint32_t)((c >> 16 & 0xff) * t);
            img[y * L + x] = (c & 0xff000000u) | B << 16 | G << 8 | R;
        }
}

/* ---- vehicles as designed shapes ----
   Seen from above a vehicle is symmetric about its long axis, so its parts are profiles: for every row
   of the class map, a half-width around the centre line. The body is the silhouette's; the glass is the
   dark rows inside it, grouped into runs (windscreen, rear window); the roof is the paint between the first
   two runs, a shade lighter. Profiles are smoothed along the car, then drawn at the output resolution with
   the half-width interpolated between rows: clean curves instead of traced pixels. What is left (lights,
   stripes, light bars, signs) is drawn over from the symmetric class fields, regions of 6 class pixels or
   more only. White in the paint mask: the body minus the glass and the details (roof included, so it is
   recoloured with the car and stays lighter). */
enum { GLASS = MAXK - 1, P_HI = MAXK - 2 };

static void smooth_profile(float *p, int n, int passes)
{
    float t[TILE];
    for (int k = 0; k < passes; k++) {
        for (int i = 0; i < n; i++) {
            if (p[i] < 0) { t[i] = p[i]; continue; }
            float a = i > 0 && p[i - 1] >= 0 ? p[i - 1] : p[i], b = i < n - 1 && p[i + 1] >= 0 ? p[i + 1] : p[i];
            t[i] = (a + 2 * p[i] + b) / 4;
        }
        memcpy(p, t, sizeof(float) * (size_t)n);
    }
}

/* coverage of |x - cx| < hw(y) at class-map position (sx, sy), anti-aliased over one output pixel (1/q) */
static float profile_cover(const float *hw, int n, float sx, float sy, float cx, int q)
{
    int y0 = (int)floorf(sy);
    float fy = sy - y0;
    float a = y0 >= 0 && y0 < n ? hw[y0] : -1, b = y0 + 1 >= 0 && y0 + 1 < n ? hw[y0 + 1] : -1;
    float h = a < 0 && b < 0 ? -1 : a < 0 ? b - (1 - fy) * (b + 1) * 2 : b < 0 ? a - fy * (a + 1) * 2 : a + (b - a) * fy;
    float c = (h + 0.5f - fabsf(sx - cx)) * q;
    return c < 0 ? 0 : c > 1 ? 1 : c;
}

static uint32_t rgba_of(const Material *m, float a)
{
    return (uint32_t)(a * 255 + 0.5f) << 24 | (uint32_t)(m->b + 0.5f) << 16 | (uint32_t)(m->g + 0.5f) << 8 | (uint32_t)(m->r + 0.5f);
}

static void blend_over(uint32_t *d, const Material *m, float a)
{
    if (a <= 0) return;
    uint32_t o = *d;
    float da = (float)(o >> 24) / 255, oa = a + da * (1 - a);
    float r = ((float)(o & 0xff) * da * (1 - a) + m->r * a) / oa, g = ((float)(o >> 8 & 0xff) * da * (1 - a) + m->g * a) / oa,
          b = ((float)(o >> 16 & 0xff) * da * (1 - a) + m->b * a) / oa;
    Material t = { r, g, b, 0, 0 };
    *d = rgba_of(&t, oa);
}

/* cm: P_LIGHT paint, GLASS, 0..kv-1 the vehicle's other colours; colours in vm (P_LIGHT the paint). */
static void draw_vehicle(const ClassMap *cm, int cw, int ch, const Material *vm, int q, int L, uint32_t *img, uint32_t *mask)
{
    float cx = (field_mirror - 1) / 2.0f, body[TILE], glass[TILE];
    int run_of[TILE];
    for (int y = 0; y < ch; y++) {
        int l = -1, r = -1, gl = -1, gr = -1, ng = 0;
        for (int x = 0; x < cw; x++) {
            uint8_t c = cm->cls[y * TILE + x];
            if (c == TRANSPARENT) continue;
            if (l < 0) l = x;
            r = x;
            if (c == GLASS) { if (gl < 0) gl = x; gr = x; ng++; }
        }
        body[y] = l < 0 ? -1 : ((cx - l) + (r - cx)) / 2;
        /* a glass row: glass over a third of the body's width */
        glass[y] = l >= 0 && ng * 3 >= (r - l + 1) ? ((cx - gl) + (gr - cx)) / 2 : -1;
    }
    /* glass runs (rows of glass with gaps of at most one row), short ones dropped */
    int nruns = 0, rs[8], re[8];
    for (int y = 0; y < ch;) {
        if (glass[y] < 0) { y++; continue; }
        int a = y;
        while (y < ch && (glass[y] >= 0 || (y + 1 < ch && glass[y + 1] >= 0))) {
            if (glass[y] < 0) glass[y] = (glass[y - 1] + glass[y + 1]) / 2;
            y++;
        }
        if (y - a >= 3 && nruns < 8) rs[nruns] = a, re[nruns] = y - 1, nruns++;
        else for (int k = a; k < y; k++) glass[k] = -1;
    }
    for (int y = 0; y < ch; y++) run_of[y] = -1;
    for (int r = 0; r < nruns; r++)
        for (int y = rs[r]; y <= re[r]; y++) run_of[y] = r;
    smooth_profile(body, ch, 2);
    /* each run on its own (they don't blend into each other) */
    float gr[8][TILE];
    for (int r = 0; r < nruns; r++) {
        for (int y = 0; y < ch; y++) gr[r][y] = run_of[y] == r ? glass[y] : -1;
        smooth_profile(gr[r], ch, 2);
        for (int y = 0; y < ch; y++)
            if (gr[r][y] > body[y] - 1.5f && gr[r][y] >= 0) gr[r][y] = body[y] - 1.5f;   /* a frame of paint around the glass */
    }
    /* roof: the paint between the first two runs, inset from the body's sides */
    float roof[TILE];
    for (int y = 0; y < ch; y++) roof[y] = nruns >= 2 && y > re[0] && y < rs[1] && body[y] >= 0 ? body[y] - 2.0f : -1;
    Material paint = vm[P_LIGHT], light = paint, glass_c = vm[GLASS];
    light.r = fminf(255, paint.r * 1.15f + 12), light.g = fminf(255, paint.g * 1.15f + 12), light.b = fminf(255, paint.b * 1.15f + 12);
    /* details: the other colours from the symmetric fields, small regions out */
    /* paint tones first (they stay paint in the mask), then the other colours */
    static uint32_t pdet[SPRITE_OUT * SPRITE_OUT], det[SPRITE_OUT * SPRITE_OUT];
    for (int layer = 0; layer < 2; layer++) {
    ClassMap dm;
    for (int i = 0; i < TILE * TILE; i++) {
        uint8_t c = cm->cls[i];
        bool tone = c == P_DARK || c == P_HI;
        dm.cls[i] = layer == 0 ? (tone ? c : TRANSPARENT) : (c < P_HI ? c : TRANSPARENT);
        dm.shade[i] = 1;
    }
    {
        static int16_t label[TILE * TILE];
        static int stack[TILE * TILE], members[TILE * TILE];
        memset(label, -1, sizeof label);
        for (int st = 0; st < TILE * TILE; st++) {
            if (label[st] != -1 || dm.cls[st] == TRANSPARENT) continue;
            uint8_t c = dm.cls[st];
            int sp = 0, nm = 0;
            stack[sp++] = st, label[st] = 1;
            while (sp) {
                int p = stack[--sp], x = p % TILE, y = p / TILE;
                members[nm++] = p;
                const int nb[4] = { x > 0 ? p - 1 : -1, x < TILE - 1 ? p + 1 : -1, y > 0 ? p - TILE : -1, y < TILE - 1 ? p + TILE : -1 };
                for (int k = 0; k < 4; k++)
                    if (nb[k] >= 0 && label[nb[k]] == -1 && dm.cls[nb[k]] == c) label[nb[k]] = 1, stack[sp++] = nb[k];
            }
            if (nm < 6)
                for (int i = 0; i < nm; i++) dm.cls[members[i]] = TRANSPARENT;
        }
    }
    float sg = sigma;
    sigma = 1.0f;
    draw_fields(&dm, vm, layer == 0 ? pdet : det, L);
    sigma = sg;
    }
    int ow = cw * q, oh = ch * q;
    for (int Y = 0; Y < oh; Y++)
        for (int X = 0; X < ow; X++) {
            float sx = (X + 0.5f) / q - 0.5f, sy = (Y + 0.5f) / q - 0.5f;
            float b = profile_cover(body, ch, sx, sy, cx, q);
            uint32_t *d = &img[Y * L + X];
            *d = 0;
            if (b <= 0) { mask[Y * L + X] = 0xff000000u; continue; }
            *d = rgba_of(&paint, b);
            float rf = profile_cover(roof, ch, sx, sy, cx, q);
            blend_over(d, &light, rf * b);
            uint32_t pc = pdet[Y * L + X];
            Material pmc = { (float)(pc & 0xff), (float)(pc >> 8 & 0xff), (float)(pc >> 16 & 0xff), 0, 0 };
            blend_over(d, &pmc, (float)(pc >> 24) / 255 * b);
            float g = 0;
            for (int r = 0; r < nruns; r++) g = fmaxf(g, profile_cover(gr[r], ch, sx, sy, cx, q));
            blend_over(d, &glass_c, g * b);
            uint32_t dc = det[Y * L + X];
            float da = (float)(dc >> 24) / 255 * b;
            Material dmc = { (float)(dc & 0xff), (float)(dc >> 8 & 0xff), (float)(dc >> 16 & 0xff), 0, 0 };
            blend_over(d, &dmc, da);
            float pm = b * (1 - g) * (1 - da);
            uint32_t v = (uint32_t)(pm * 255 + 0.5f);
            mask[Y * L + X] = 0xff000000u | v << 16 | v << 8 | v;
        }
}

static bool generate_sprites(const Style *s, int number, const CityPalette *pal)
{
    int ns = sprite_count();
    /* each vehicle sprite's remap 1: the palette of the first car record drawn with it */
    static int car_pal[0x1000];
    memset(car_pal, -1, sizeof car_pal);
    car_info_setup(s);
    for (int i = car_info_count() - 1; i >= 0; i--) {
        int n = carinfo_s16(car_info_record(i), 6);
        if (n >= 0 && n < 0x1000) car_pal[n] = sprite_car_palette(i);
    }
    /* the materials of the sprites (paint left out) */
    Hist *h = NULL;
    size_t nh = 0, cap = 0;
    for (int n = 0; n < ns; n++) {
        bool veh;
        const SpriteInfo *in = sprite_get_info(n);
        if (!in || !in->w || !in->h || !in->data || !sprite_wanted(n, &veh)) continue;
        const uint32_t *c0 = sprite_remap_clut(in->clut, 0, 0);
        int cp = veh ? car_pal[n] : -1;
        for (int v = 0; v < in->h; v++)
            for (int u = 0; u < in->w; u++) {
                uint8_t e = in->data[v * 256 + u];
                if (!e || paint_texel(in->clut, cp, e)) continue;
                if (nh == cap) h = realloc(h, (cap = cap ? cap * 2 : 65536) * sizeof *h);
                h[nh++] = (Hist){ c0[e * 64] & 0xffffff, 1 };
            }
    }
    qsort(h, nh, sizeof *h, cmp_hist);
    int nu = 0;
    for (size_t i = 0; i < nh; i++)
        if (nu && h[nu - 1].c == h[i].c) h[nu - 1].n++;
        else h[nu++] = h[i];
    const int k = 32;
    Material mat[MAXK] = { 0 }, dmat[MAXK + 2];
    cluster(h, nu, mat, k);
    free(h);
    int saved = nmat, written = 0, masks = 0;
    nmat = k;
    /* shade groups as on the roofs (one surface in light and shadow), drawn in their mean colour */
    uint8_t group[MAXK];
    shade_groups(mat, group, 24);   /* tighter than the roofs': chrome, glass and body stay apart */
    {
        double sr[MAXK] = { 0 }, sg[MAXK] = { 0 }, sb[MAXK] = { 0 }, sn[MAXK] = { 0 };
        for (int j = 0; j < k; j++) {
            int g = group[j];
            sr[g] += mat[j].r * mat[j].n, sg[g] += mat[j].g * mat[j].n, sb[g] += mat[j].b * mat[j].n, sn[g] += mat[j].n;
        }
        for (int j = 0; j < k; j++) {
            dmat[j] = mat[j];
            if (sn[j] > 0) dmat[j].r = (float)(sr[j] / sn[j]), dmat[j].g = (float)(sg[j] / sn[j]), dmat[j].b = (float)(sb[j] / sn[j]);
            grade(&dmat[j], pal);
        }
    }
    static uint32_t img[SPRITE_OUT * SPRITE_OUT], mimg[SPRITE_OUT * SPRITE_OUT];
    static uint8_t paint[256 * 256];
    static float rgb[256 * 256][3];
    for (int n = 0; n < ns; n++) {
        bool veh;
        const SpriteInfo *in = sprite_get_info(n);
        if (!in || !in->w || !in->h || !in->data || !sprite_wanted(n, &veh)) continue;
        const uint32_t *c0 = sprite_remap_clut(in->clut, 0, 0);
        int cp = veh ? car_pal[n] : -1;
        bool pt[256];
        for (int e = 0; e < 256; e++) pt[e] = paint_texel(in->clut, cp, (uint8_t)e);
        /* texel classes: 0..k-1 materials, P_* paint, TRANSPARENT */
        int npaint = 0;
        double pl = 0;
        for (int v = 0; v < in->h; v++)
            for (int u = 0; u < in->w; u++) {
                uint8_t e = in->data[v * 256 + u];
                uint32_t c = c0[e * 64];
                float *q = rgb[v * 256 + u];
                q[0] = (float)(c >> 16 & 0xff), q[1] = (float)(c >> 8 & 0xff), q[2] = (float)(c & 0xff);
                paint[v * 256 + u] = pt[e];
                if (paint[v * 256 + u]) npaint++, pl += luma(q[0], q[1], q[2]);
            }
        float paint_mid = npaint ? (float)(pl / npaint) : 0;
        /* a vehicle's own few colours besides its paint (glass, trim, chrome, lights): k-means on its texels,
           lightness counting fully */
        Material vm[MAXK + 2];
        int kv = 0;
        if (veh) {
            static Hist vh[256 * 256];
            int nvh = 0;
            for (int v = 0; v < in->h; v++)
                for (int u = 0; u < in->w; u++)
                    if (in->data[v * 256 + u] && !paint[v * 256 + u]) vh[nvh++] = (Hist){ c0[in->data[v * 256 + u] * 64] & 0xffffff, 1 };
            qsort(vh, (size_t)nvh, sizeof *vh, cmp_hist);
            int nvu = 0;
            for (int i = 0; i < nvh; i++)
                if (nvu && vh[nvu - 1].c == vh[i].c) vh[nvu - 1].n++;
                else vh[nvu++] = vh[i];
            kv = nvu < VEHICLE_COLOURS ? nvu : VEHICLE_COLOURS;
            float lw = light_weight;
            light_weight = 1;
            memset(vm, 0, sizeof vm);
            if (kv) cluster(vh, nvu, vm, kv);
            light_weight = lw;
            for (int j = 0; j < kv; j++) grade(&vm[j], pal);
        }
        /* onto the class map: f x f texels per class pixel, majority */
        int f = 1;
        while (in->w > TILE * f || in->h > TILE * f) f++;
        int cw = (in->w + f - 1) / f, ch = (in->h + f - 1) / f;
        ClassMap cm;
        double ps[3][4] = { { 0 } };
        static const int TONE[3] = { P_LIGHT, P_DARK, P_HI };
        for (int i = 0; i < TILE * TILE; i++) cm.cls[i] = TRANSPARENT, cm.shade[i] = 1;
        for (int y = 0; y < ch; y++)
            for (int x = 0; x < cw; x++) {
                int count[256] = { 0 }, best = TRANSPARENT;
                for (int v = y * f; v < (y + 1) * f && v < in->h; v++)
                    for (int u = x * f; u < (x + 1) * f && u < in->w; u++) {
                        const float *q = rgb[v * 256 + u];
                        int c;
                        if (!in->data[v * 256 + u]) c = TRANSPARENT;
                        else if (paint[v * 256 + u]) {
                            /* paint in three tones (mid, dark, light): the engine keeps a skin pixel's brightness
                               against the paint's when it recolours, so windscreens and panels stay readable */
                            float l = luma(q[0], q[1], q[2]);
                            int pc = l < 0.7f * paint_mid ? 1 : l > 1.3f * paint_mid ? 2 : 0;
                            c = TONE[pc];
                            ps[pc][0] += q[0], ps[pc][1] += q[1], ps[pc][2] += q[2], ps[pc][3]++;
                        } else if (veh) {
                            float lw = light_weight;
                            light_weight = 1;
                            c = kv ? classify(q[0], q[1], q[2], vm, kv) : TRANSPARENT;
                            light_weight = lw;
                        } else c = group[classify(q[0], q[1], q[2], mat, k)];
                        if (++count[c] > count[best]) best = c;
                    }
                cm.cls[y * TILE + x] = (uint8_t)best;
            }
        /* the paint drawn in its own colours (the mask's brightness reference is the original paint) */
        Material *draw_mat = veh ? vm : dmat;
        for (int pc = 0; pc < 3; pc++)
            if (ps[pc][3] > 0) draw_mat[TONE[pc]].r = (float)(ps[pc][0] / ps[pc][3]), draw_mat[TONE[pc]].g = (float)(ps[pc][1] / ps[pc][3]), draw_mat[TONE[pc]].b = (float)(ps[pc][2] / ps[pc][3]);
        /* speckle out (3 x 3 majority, transparency kept); objects then as boxes; vehicles as smooth shapes,
           made symmetric when the original mostly is (it faces up: left and right mirror each other) */
        static Lines lines;
        lines.n = 0;
        if (cw * ch >= 400) {
            sprite_speckle(&cm, cw, ch);
            if (!veh) rectify(&cm, true);
        }
        field_mirror = 0;
        if (veh) {
            /* the axis: the silhouette's centre line (to the half pixel), averaged over its rows */
            double sum = 0;
            int rows = 0;
            for (int y = 0; y < ch; y++) {
                int l = -1, r = -1;
                for (int x = 0; x < cw; x++)
                    if (cm.cls[y * TILE + x] != TRANSPARENT) { if (l < 0) l = x; r = x; }
                if (l >= 0) sum += l + r, rows++;
            }
            int m2 = rows ? (int)lround(sum / rows) : cw - 1;   /* twice the axis */
            int same = 0, opaque = 0;
            for (int y = 0; y < ch; y++)
                for (int x = 0; x < cw; x++) {
                    int px = m2 - x;
                    bool a = cm.cls[y * TILE + x] != TRANSPARENT, b = px >= 0 && px < cw && cm.cls[y * TILE + px] != TRANSPARENT;
                    if (!a && !b) continue;
                    opaque++, same += a == b;
                }
            if (opaque && same * 10 >= opaque * 9) field_mirror = m2 + 1;
            if (getenv("SKIN_GENERATE_DEBUG")) printf("  sprite %d: %dx%d symmetric %d%% paint %d colours %d\n", n, cw, ch, opaque ? same * 100 / opaque : 0, npaint, kv);
        }
        /* drawn at q output pixels per class pixel */
        int q = SPRITE_OUT / (cw > ch ? cw : ch);
        if (q > 2 * scale * f) q = 2 * scale * f;
        if (q < 1) q = 1;
        int L = TILE * q;
        if (L > SPRITE_OUT) {   /* only the sprite's corner of the map is kept: draw that part */
            q = SPRITE_OUT / TILE;
            L = SPRITE_OUT;
        }
        float sg = sigma;
        int ow = cw * q, oh = ch * q;
        char name[64];
        bool designed = veh && field_mirror;
        if (designed && !npaint) {
            /* a vehicle that keeps its colours (police, ambulance, taxi): its most common light colour is the body */
            int count[MAXK] = { 0 }, best = -1;
            for (int i = 0; i < TILE * TILE; i++)
                if (cm.cls[i] < kv && luma(vm[cm.cls[i]].r, vm[cm.cls[i]].g, vm[cm.cls[i]].b) >= 80 && ++count[cm.cls[i]] > (best < 0 ? 0 : count[best])) best = cm.cls[i];
            if (best < 0) designed = false;
            else {
                vm[P_LIGHT] = vm[best];
                for (int i = 0; i < TILE * TILE; i++)
                    if (cm.cls[i] == best) cm.cls[i] = P_LIGHT;
            }
        }
        if (designed) {
            /* the glass: the vehicle's dark colours inside the silhouette */
            double gs[4] = { 0 };
            for (int y = 0; y < ch; y++)
                for (int x = 0; x < cw; x++) {
                    uint8_t *c = &cm.cls[y * TILE + x];
                    if (*c >= kv) continue;
                    bool edge = false;
                    for (int d = 0; d < 4 && !edge; d++) {
                        int X = x + (d == 0) - (d == 1), Y = y + (d == 2) - (d == 3);
                        edge = X < 0 || Y < 0 || X >= cw || Y >= ch || cm.cls[Y * TILE + X] == TRANSPARENT;
                    }
                    if (edge || luma(vm[*c].r, vm[*c].g, vm[*c].b) >= 80) continue;
                    gs[0] += vm[*c].r, gs[1] += vm[*c].g, gs[2] += vm[*c].b, gs[3]++;
                    *c = GLASS;
                }
            if (gs[3] > 0) vm[GLASS].r = (float)(gs[0] / gs[3]), vm[GLASS].g = (float)(gs[1] / gs[3]), vm[GLASS].b = (float)(gs[2] / gs[3]);
            draw_vehicle(&cm, cw, ch, vm, q, L, img, mimg);
            outline(img, L, ow, oh, 0.9f * q);
        } else {
            sigma = veh ? vehicle_sigma : cw * ch >= 400 ? sprite_sigma : 0.7f;
            draw_fields(&cm, draw_mat, img, L);
            draw_lines(&lines, draw_mat, img, L);
            if (veh) outline(img, L, ow, oh, 0.9f * q);
        }
        snprintf(name, sizeof name, "%d", n);
        if (!write_image(number, name, img, L, ow, oh)) return false;
        written++;
        if (designed && npaint) {
            snprintf(name, sizeof name, "%d_mask", n);
            if (!write_image(number, name, mimg, L, ow, oh)) return false;
            masks++;
        } else if (!designed && npaint) {
            Material mm[MAXK + 2];
            for (int j = 0; j < MAXK + 2; j++) set_rgb(&mm[j], j >= P_LIGHT || j == P_HI ? 0xffffff : 0);
            draw_fields(&cm, mm, mimg, L);
            draw_lines(&lines, mm, mimg, L);
            for (int i = 0; i < L * L; i++) {
                uint32_t v = (mimg[i] & 0xff) * (mimg[i] >> 24) / 255;   /* opaque grey: white = paint */
                mimg[i] = 0xff000000u | v << 16 | v << 8 | v;
            }
            snprintf(name, sizeof name, "%d_mask", n);
            if (!write_image(number, name, mimg, L, ow, oh)) return false;
            masks++;
        }
        sigma = sg;
        field_mirror = 0;
    }
    nmat = saved;
    printf("style %03d: %d sprites (%d paint masks), %d sprite materials\n", number, written, masks, k);
    return true;
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
    /* the drawing colours: the materials graded towards the city, then the semantic classes */
    const CityPalette *pal = &PALETTES[number - 1];
    uint8_t group[MAXK];
    shade_groups(mat, group, 45);
    Material dmat[MAXK + NSEM];
    for (int j = 0; j < nmat; j++) dmat[j] = mat[j];
    if (semantic) {
        /* a shade group is drawn in its members' mean colour */
        double sr[MAXK] = { 0 }, sg[MAXK] = { 0 }, sb[MAXK] = { 0 }, sn[MAXK] = { 0 };
        for (int j = 0; j < nmat; j++) {
            int g = group[j];
            sr[g] += mat[j].r * mat[j].n, sg[g] += mat[j].g * mat[j].n, sb[g] += mat[j].b * mat[j].n, sn[g] += mat[j].n;
        }
        for (int j = 0; j < nmat; j++)
            if (group[j] == j && sn[j] > 0) dmat[j].r = (float)(sr[j] / sn[j]), dmat[j].g = (float)(sg[j] / sn[j]), dmat[j].b = (float)(sb[j] / sn[j]);
    }
    if (semantic)
        for (int j = 0; j < nmat; j++) grade(&dmat[j], pal);
    for (int j = 0; j < NSEM; j++) set_rgb(&dmat[nmat + j], pal->sem[j]);
    int8_t lid_kind[256], aux_kind[256];
    uint8_t lid_allow[256], aux_allow[256];
    Material ref[NKIND] = { 0 };
    bool ground = false;
    if (semantic) {
        usage_scan(s, number, lid_kind, aux_kind, lid_allow, aux_allow);
        ground = ground_refs(s, lid_kind, ref);
        int nl = 0;
        for (int i = 0; i < s->nlid; i++) nl += lid_kind[i] != 0;
        printf("style %03d: %d ground lids; asphalt %02x%02x%02x pavement %02x%02x%02x grass %02x%02x%02x water %02x%02x%02x\n", number, nl,
               (int)ref[K_ROAD].r, (int)ref[K_ROAD].g, (int)ref[K_ROAD].b, (int)ref[K_PAVE].r, (int)ref[K_PAVE].g, (int)ref[K_PAVE].b,
               (int)ref[K_FIELD].r, (int)ref[K_FIELD].g, (int)ref[K_FIELD].b, (int)ref[K_WATER].r, (int)ref[K_WATER].g, (int)ref[K_WATER].b);
    }
    /* 2-3. class maps and drawing */
    int L = TILE * scale;
    uint32_t *img = malloc((size_t)L * L * sizeof *img);
    uint32_t **lid_img = calloc((size_t)s->nlid, sizeof *lid_img);
    ClassMap cm;
    for (int i = 0; i < nt; i++) {
        int kind = !semantic || !ground ? 0 : !strcmp(tiles[i].kind, "lid") ? lid_kind[tiles[i].n] : !strcmp(tiles[i].kind, "aux") ? aux_kind[tiles[i].n] : 0;
        if (kind) classify_ground(s, tiles[i].clut, tiles[i].t, tiles[i].kind[0] == 'l' ? lid_allow[tiles[i].n] : aux_allow[tiles[i].n], ref, mat, &cm);
        /* roofs (the other lids): their materials' shade groups, so the texture's light and dark patches are
           one surface */
        bool roof = semantic && !kind && tiles[i].kind[0] == 'l';
        if (!kind) for (int v = 0; v < TILE; v++)
            for (int u = 0; u < TILE; u++) {
                bool tr;
                float r, g, b;
                rgbf(tile_rgb(s, tiles[i].clut, tiles[i].t, u, v, &tr), &r, &g, &b);
                int c = tr ? TRANSPARENT : classify(r, g, b, mat, nmat);
                if (roof && c != TRANSPARENT) c = group[c];
                cm.cls[v * TILE + u] = (uint8_t)c;
                float ml = c == TRANSPARENT ? 1 : luma(mat[c].r, mat[c].g, mat[c].b);
                cm.shade[v * TILE + u] = c == TRANSPARENT ? 1 : (luma(r, g, b) + 8) / (ml + 8);
            }
        smooth_shade(cm.shade);
        static Lines lines;
        vectorise(&cm, &lines);
        float sg = sigma;
        if (semantic && !kind) rectify(&cm, roof), sigma = building_sigma;   /* crisp boxes */
        if (flat) draw_fields(&cm, dmat, img, L);
        else draw(&cm, dmat, img, L);
        sigma = sg;
        if (kind) {
            draw_slabs(&cm, nmat + S_PAVE, pal->slab, img, L);
            /* on the ground only the markings are strokes (the rest are the texture's joints and cracks) */
            for (int j = 0; j < lines.n;)
                if (lines.l[j].cls != nmat + S_WHITE && lines.l[j].cls != nmat + S_YELLOW) lines.l[j] = lines.l[--lines.n];
                else j++;
        } else if (semantic) {
            /* on buildings only long horizontal and vertical strokes (diagonals and short ones are shading and
               grain) */
            for (int j = 0; j < lines.n;) {
                const Line *l = &lines.l[j];
                bool axis = l->x0 == l->x1 || l->y0 == l->y1, edge = on_border(l->x0, l->y0) || on_border(l->x1, l->y1);
                float len = hypotf(l->x1 - l->x0, l->y1 - l->y0);
                if (!axis || len < (roof ? 32 : 16) || (len < 24 && !edge)) lines.l[j] = lines.l[--lines.n];
                else j++;
            }
        }
        draw_lines(&lines, dmat, img, L);
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
    if (semantic && !generate_sprites(s, number, pal)) return false;
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
        else if (!strcmp(argv[i], "--generic")) semantic = false;
        else if (!strcmp(argv[i], "--grid") && i + 1 < argc) grid = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--sigma") && i + 1 < argc) sigma = (float)atof(argv[++i]);
        else {
            fprintf(stderr, "usage: skin_generate [--data DIR] [--out DIR] [--scale S] [--style N] [--materials K] [--min-region N] [--generic]\n"
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
