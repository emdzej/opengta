/* DMA's rasteriser, 32 bpp path (see poly.h). The original is one assembly-flavoured translation unit
   working on fixed globals; the port keeps them as file statics named after their role, with the
   addresses. The span loops reproduce the original's register arithmetic exactly: u and v are 8-bit
   integer parts in BL/BH stepped with add/adc from 16-bit fractions, and where the original adds a
   step whose low half carries junk (the integer parts) into a fraction register, the port does too. */
#include "poly.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

const uint32_t *poly_clut;                 /* 0x78c10c */
unsigned poly_recip_oob;

/* ---- module state ---- */
static uint8_t *tex_base;                  /* 0x78c110: tile page 0 */
static const uint8_t *blend;               /* 0x78c108: 64 KB blend table */
static int32_t recip[0x1000];              /* 0x788100: 0x3f0000 / i (63 / i in 16.16), [0] = 0 */
static uint32_t *rows[2048];               /* 0x503228: scanline pointers */
static int pitch_px;                       /* 0x503218 / 4 */
static int clip_x0, clip_x1, clip_y0, clip_y1;   /* 0x78e548, 0x78e54c, 0x78e550, 0x78e554 (shorts at 0x4b8850..) */

/* rotated-tile LRU cache: 16-byte entries at 0x78c740 {prev, next, page, u, v, tile} */
typedef struct { int prev, next; uint8_t *page; uint8_t u, v; uint16_t tile; } CacheSlot;
static CacheSlot slots[16];
static int nslots;                         /* 0x7880e8 (0 = no cache, rotations ignored) */
static int lru_head, lru_tail;             /* 0x78c720 (least recent), 0x78c114 (most recent) */
static int slot_of[0x200];                 /* 0x78c120: tile -> slot + 1 (0x180 entries in the original) */
static unsigned hits[0x200], misses[0x200], hit_total, miss_total;   /* 0x78df40, 0x78e900, 0x78e8d8, 0x78e8d0 */

/* Poly_SelectTile results */
static uint8_t *sel_page;                  /* 0x78e8dc */
static unsigned sel_u, sel_v;              /* 0x78e540, 0x78e544 */

/* face-mapper inputs */
static uint8_t *face_tex;                  /* 0x4b8c58 */
static uint32_t face_word;                 /* 0x4b8c80 */
static int32_t face_u0, face_v0;           /* 0x4b8c84, 0x4b8c88 (tile origin << 16) */
static int fx[6];                          /* 0x4b8c60.. (horiz: xl_a xr_a xl_b xr_b y_a y_b) */
static int fv[6];                          /* 0x4b8c60, 64, 70, 74, 78, 7c (vert: x_a x_b yt_a yt_b yb_a yb_b) */

void poly_cache_stats(unsigned *hit, unsigned *miss)
{
    *hit = hit_total;
    *miss = miss_total;
}

static inline int32_t recip_at(int i)
{
    if (i >= 0 && i < 0x1000) return recip[i];
    poly_recip_oob++;
    return 0;   /* TODO(0x788100): the original reads whatever follows / precedes the table */
}

static inline uint32_t ror16(uint32_t x) { return x >> 16 | x << 16; }

/* ---- setup ---- */

void poly_init(uint8_t *tbase, uint8_t *cache_base, unsigned n, unsigned first)
{
    tex_base = tbase;
    recip[0] = 0;
    for (int i = 1; i < 0x1000; i++) recip[i] = (int32_t)(0x3f0000LL / i);
    if (n > 16) n = 16;
    if (n) {
        unsigned c = first;
        for (unsigned i = 0; i < n; i++, c++) {
            slots[i].prev = (int)i - 1;
            slots[i].next = (int)i + 1;
            slots[i].tile = 0xffff;
            slots[i].u = (uint8_t)((c & 3) << 6);
            slots[i].v = (uint8_t)(((c >> 2) & 3) << 6);
            slots[i].page = cache_base + (c & ~15u) * 0x1000;
        }
        slots[n - 1].next = -1;
        lru_head = 0;
        lru_tail = (int)n - 1;
        memset(slot_of, 0, sizeof slot_of);
    }
    nslots = (int)n;
    memset(hits, 0, sizeof hits);
    memset(misses, 0, sizeof misses);
    hit_total = miss_total = 0;
}

void poly_build_blend_table(uint8_t *tbl, float alpha)
{
    for (int a = 0; a < 256; a++) {
        float fa = (float)a * alpha;
        for (int b = 0; b < 256; b++) {
            float fb = (float)((1.0 - alpha) * b);
            tbl[a * 256 + b] = (uint8_t)(int)(fb + fa);
        }
    }
    blend = tbl;
}

void poly_set_clip(int x0, int y0, int x1, int y1)
{
    clip_x0 = (int16_t)x0, clip_y0 = (int16_t)y0, clip_x1 = (int16_t)x1, clip_y1 = (int16_t)y1;
}

void poly_set_screen_rows(uint32_t *base, int pitch_bytes, int h)
{
    if (h > (int)(sizeof rows / sizeof *rows)) h = (int)(sizeof rows / sizeof *rows);
    for (int y = 0; y < h; y++) rows[y] = (uint32_t *)((uint8_t *)base + (size_t)y * pitch_bytes);
    pitch_px = pitch_bytes / 4;
}

/* ---- tiles ---- */

/* Poly_RotateTile90 0x49b044: dst[x * 256 + 63 - y] = src[y * 256 + x]. */
static void rotate_tile90(const uint8_t *src, uint8_t *dst)
{
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) dst[x * 256 + 63 - y] = src[y * 256 + x];
}

/* Poly_SelectTile 0x497dfc: page/u/v of tile t; for 90 and 270 degree faces the tile comes from the LRU
   cache of rotated copies (rotated on a miss). The cache only changes where an overshooting step lands
   (the neighbouring slot), so it is kept exactly. */
static void select_tile(unsigned t, uint32_t face)
{
    bool rot = (face & 0xc000) == 0x4000 || (face & 0xc000) == 0xc000;
    if (!nslots || !rot) {
        sel_page = tex_base + (t >> 4) * 0x10000;
        sel_u = (t & 3) << 6;
        sel_v = (t >> 2 & 3) << 6;
        return;
    }
    /* TODO(0x78c120): the original's table has 0x180 entries; ids 0x180/0x181 alias the LRU head and
       Poly state there. Lid ids stay below that in the shipped styles unless an animated lid shows an
       aux tile >= 0x180 and is rotated. */
    t &= 0x1ff;
    int e = slot_of[t] - 1;
    if (e < 0) {
        misses[t]++;
        miss_total++;
        e = lru_head;
        CacheSlot *s = &slots[e];
        if (s->tile != 0xffff) slot_of[s->tile] = 0;
        int nx = s->next;
        slots[nx].prev = s->prev;
        slots[lru_tail].next = e;
        lru_head = nx;
        s->next = -1;
        s->prev = lru_tail;
        s->tile = (uint16_t)t;
        lru_tail = e;
        slot_of[t] = e + 1;
        uint8_t *page = tex_base + (t >> 4) * 0x10000;
        const uint8_t *src = page + ((t >> 2) & 3) * 0x4000 + (t & 3) * 0x40;
        rotate_tile90(src, s->page + s->v * 0x100 + s->u);
    } else {
        CacheSlot *s = &slots[e];
        if (e != lru_tail) {
            slots[s->next].prev = s->prev;
            if (e == lru_head) lru_head = s->next;
            else slots[s->prev].next = s->next;
            slots[lru_tail].next = e;
            s->next = -1;
            s->prev = lru_tail;
            lru_tail = e;
        }
        hits[t]++;
        hit_total++;
    }
    sel_page = slots[e].page;
    sel_u = slots[e].u;
    sel_v = slots[e].v;
}

/* ---- trapezoid face mappers ---- */

/* Poly_FaceHoriz32 0x49a78c: rows from edge a (y_a) to edge b (y_b) inclusive, left and right x stepped
   per row, v stepped 63/rows per row, u stepped 63/width along each row. Pixel counts: a row draws
   x_right - x_left + 1 pixels (one if the edges cross). */
static void face_horiz32(void)
{
    const int xl_a = fx[0], xr_a = fx[1], xl_b = fx[2], xr_b = fx[3], y_a = fx[4], y_b = fx[5];
    if (y_a < clip_y0 && y_b < clip_y0) return;
    if (y_a > clip_y1 && y_b > clip_y1) return;
    if (xl_a > clip_x1 && xl_b > clip_x1) return;
    if (xr_a < clip_x0 && xr_b < clip_x0) return;
    int y = y_a, d = y_b - y_a, dir;
    if (d > 0) dir = 1;
    else if (d == 0) dir = 0, d = 1;
    else dir = -1, d = -d;
    int32_t vstep = recip_at(d);
    int end = y_b + dir;
    uint32_t dxl = (uint32_t)((int32_t)((uint32_t)(xl_a - xl_b) << 16) / d);
    uint32_t dxr = (uint32_t)((int32_t)((uint32_t)(xr_b - xr_a) << 16) / d);
    uint32_t xl = ((uint32_t)xl_a << 16) + 0x8000, xr = ((uint32_t)xr_a << 16) + 0x8000;
    uint32_t v = 0x8000;
    if (y_b < clip_y0) end = clip_y0 + dir;
    if (y_b > clip_y1) end = clip_y1 + dir;
    if (y_a < clip_y0) {
        uint32_t n = (uint32_t)(clip_y0 - y_a);
        xl -= dxl * n, xr += dxr * n, v += (uint32_t)vstep * n, y = clip_y0;
    }
    if (y_a > clip_y1) {
        uint32_t n = (uint32_t)(y_a - clip_y1);
        xl -= dxl * n, xr += dxr * n, v += (uint32_t)vstep * n, y = clip_y1;
    }
    v += (uint32_t)face_v0;
    const uint8_t *page = face_tex;
    const uint32_t *clut = poly_clut;
    const bool transparent = face_word & POLY_TRANSPARENT;
    do {
        unsigned bh = v >> 16 & 0xff;
        uint32_t u = (uint32_t)face_u0 + 0x8000;
        int x0 = (int32_t)xl >> 16, x1 = (int32_t)xr >> 16;
        uint32_t step = (uint32_t)recip_at(x1 - x0);
        if (face_word & POLY_MIRROR_U) step = 0u - step, u += 0x3f0000;
        if (x0 <= clip_x1 && x1 >= clip_x0) {
            if (x0 < clip_x0) u += step * (uint32_t)(clip_x0 - x0), x0 = clip_x0;
            if (x1 > clip_x1) x1 = clip_x1;
            int n = x1 - x0;
            uint32_t *dst = rows[y] + x0;
            uint8_t bl = (uint8_t)(u >> 16);
            const uint32_t eax = ror16(step);
            uint32_t ecx = u << 16;
            do {
                uint8_t t = page[bh << 8 | bl];
                uint32_t s = ecx + eax;
                bl = (uint8_t)(bl + (eax & 0xff) + (s < ecx));
                ecx = s;
                if (!transparent || t) *dst = clut[t * 64];
                dst++;
            } while (n-- > 0);
        }
        y += dir;
        v += (uint32_t)vstep;
        xl -= dxl;
        xr += dxr;
    } while (y != end);
}

/* Poly_FaceVert32 0x49abdc: the same with columns: x from x_a to x_b, top and bottom y stepped per
   column, u along the column, v across. */
static void face_vert32(void)
{
    const int x_a = fv[0], x_b = fv[1], yt_a = fv[2], yt_b = fv[3], yb_a = fv[4], yb_b = fv[5];
    if (x_a < clip_x0 && x_b < clip_x0) return;
    if (x_a > clip_x1 && x_b > clip_x1) return;
    if (yt_a > clip_y1 && yt_b > clip_y1) return;
    if (yb_a < clip_y0 && yb_b < clip_y0) return;
    int x = x_a, d = x_b - x_a, dir;
    if (d > 0) dir = 1;
    else if (d == 0) dir = 0, d = 1;
    else dir = -1, d = -d;
    int32_t vstep = recip_at(d);
    int end = x_b + dir;
    uint32_t dyt = (uint32_t)((int32_t)((uint32_t)(yt_b - yt_a) << 16) / d);
    uint32_t dyb = (uint32_t)((int32_t)((uint32_t)(yb_a - yb_b) << 16) / d);
    uint32_t yt = ((uint32_t)yt_a << 16) + 0x8000, yb = ((uint32_t)yb_a << 16) + 0x8000;
    uint32_t v = 0x8000;
    if (x_b < clip_x0) end = clip_x0 + dir;
    if (x_b > clip_x1) end = clip_x1 + dir;
    if (x_a < clip_x0) {
        uint32_t n = (uint32_t)(clip_x0 - x_a);
        yt += dyt * n, yb -= dyb * n, v += (uint32_t)vstep * n, x = clip_x0;
    }
    if (x_a > clip_x1) {
        uint32_t n = (uint32_t)(x_a - clip_x1);
        yt += dyt * n, yb -= dyb * n, v += (uint32_t)vstep * n, x = clip_x1;
    }
    v += (uint32_t)face_v0;
    const uint8_t *page = face_tex;
    const uint32_t *clut = poly_clut;
    const bool transparent = face_word & POLY_TRANSPARENT;
    do {
        unsigned bh = v >> 16 & 0xff;
        uint32_t u = (uint32_t)face_u0 + 0x8000;
        int y0 = (int32_t)yt >> 16, y1 = (int32_t)yb >> 16;
        uint32_t step = (uint32_t)recip_at(y1 - y0);
        if (face_word & POLY_MIRROR_U) step = 0u - step, u += 0x3f0000;
        if (y0 <= clip_y1 && y1 >= clip_y0) {
            if (y0 < clip_y0) u += step * (uint32_t)(clip_y0 - y0), y0 = clip_y0;
            if (y1 > clip_y1) y1 = clip_y1;
            int n = y1 - y0;
            uint32_t *dst = rows[y0] + x;
            uint8_t bl = (uint8_t)(u >> 16);
            const uint32_t eax = ror16(step);
            uint32_t ecx = u << 16;
            do {
                uint8_t t = page[bh << 8 | bl];
                uint32_t s = ecx + eax;
                bl = (uint8_t)(bl + (eax & 0xff) + (s < ecx));
                ecx = s;
                if (!transparent || t) *dst = clut[t * 64];
                dst += pitch_px;
            } while (n-- > 0);
        }
        x += dir;
        v += (uint32_t)vstep;
        yt += dyt;
        yb -= dyb;
    } while (x != end);
}

static void face_begin(uint32_t face, int tile)
{
    select_tile((unsigned)tile, face);
    face_tex = sel_page;
    face_u0 = (int32_t)(sel_u << 16);
    face_v0 = (int32_t)(sel_v << 16);
}

/* Poly_DrawFaceHoriz 0x497035. 180 and 270 degrees mirror u and swap the edges unless flipped; 0 and 90
   degrees swap them if flipped. 90 and 270 degrees also take the tile from the rotation cache. */
void poly_draw_face_horiz(uint32_t face, int tile, int p3, int p4, int p5, int p6, int p7, int p8)
{
    face_begin(face, tile);
    face_word = face;
    bool swap;
    if ((face & 0xc000) >= 0x8000) face_word ^= POLY_MIRROR_U, swap = !(face & POLY_FLIP_V);
    else swap = face & POLY_FLIP_V;
    int16_t a[6] = { (int16_t)p3, (int16_t)p4, (int16_t)p5, (int16_t)p6, (int16_t)p7, (int16_t)p8 };
    if (!swap) fx[0] = a[0], fx[1] = a[1], fx[2] = a[2], fx[3] = a[3], fx[4] = a[4], fx[5] = a[5];
    else fx[0] = a[2], fx[1] = a[3], fx[2] = a[0], fx[3] = a[1], fx[4] = a[5], fx[5] = a[4];
    face_horiz32();   /* 16 bpp: Poly_FaceHoriz16 0x49b328 */
}

/* Poly_DrawFaceVert 0x497332: rotation + 90 degrees, then the flip and mirror bits trade places (v runs
   across the columns), the mirror bit is inverted, and the edges are swapped as in the horizontal case. */
void poly_draw_face_vert(uint32_t face, int tile, int p3, int p4, int p5, int p6, int p7, int p8)
{
    face = (face & 0xffff3fffu) | (((face & 0xc000) + 0x4000) & 0xc000);
    face_begin(face, tile);
    uint32_t f = face & 0xff9fffffu;
    if (face & POLY_MIRROR_U) f |= POLY_FLIP_V;
    if (face & POLY_FLIP_V) f |= POLY_MIRROR_U;
    f ^= POLY_MIRROR_U;
    face_word = f;
    bool swap;
    if ((f & 0xc000) >= 0x8000) face_word ^= POLY_MIRROR_U, swap = !(f & POLY_FLIP_V);
    else swap = f & POLY_FLIP_V;
    int16_t a[6] = { (int16_t)p3, (int16_t)p4, (int16_t)p5, (int16_t)p6, (int16_t)p7, (int16_t)p8 };
    if (!swap) fv[0] = a[4], fv[1] = a[5], fv[2] = a[0], fv[3] = a[1], fv[4] = a[2], fv[5] = a[3];
    else fv[0] = a[5], fv[1] = a[4], fv[2] = a[1], fv[3] = a[0], fv[4] = a[3], fv[5] = a[2];
    face_vert32();    /* 16 bpp: Poly_FaceVert16 0x49b778 */
}

/* ---- general polygons ---- */

/* The polygon descriptor (0x78e8c0 points at it; built on Poly_DrawQuad's stack): flags +2, count +6,
   texture page +8, {x, y} shorts +0x10, {u, v} bytes +0x20. Only the textured set (flags & 0x22) is
   used by the game's callers (flags 2, 6, 0x42); the per-vertex shade (flag 1) and second uv set
   (0x10) of Poly_SetupEdges are not ported. */
static struct {
    uint16_t flags;
    int16_t n;
    const uint8_t *tex;
    int16_t x[4], y[4];
    uint8_t u[4], v[4];
} P;
static int16_t ymax_i, ymin_i, xmax_i, xmin_i;   /* 0x78c724, 0x78c104, 0x78e8e0, 0x78e8cc */
static int16_t edge_a, edge_b;                   /* 0x78e8d4 (walks backwards), 0x78e8e2 (forwards) */
static const int16_t edge_dir = -1;              /* 0x7880e4 (0xffff from Poly_DrawQuad) */

/* edge state: A = the span's right end, B = its left end */
static uint32_t ax, adx, au, adu, av, adv;
static int32_t a_next;   /* 0x4b8864 74 / 888c a4 / 8884 b4 / 897c */
static uint32_t bx, bdx, bu, bdu, bv, bdv;
static int32_t b_next;   /* 0x4b886c 7c / 889c ac / 8894 bc / 8978 */
static int32_t span_y, span_end;                    /* 0x4b8980, 0x4b8984 */

/* Poly_FindExtents 0x499fd2 (first index wins ties) */
static void find_extents(void)
{
    ymax_i = ymin_i = xmax_i = xmin_i = 0;
    int16_t ymax = P.y[0], ymin = P.y[0], xmax = P.x[0], xmin = P.x[0];
    for (int16_t i = 0; i < P.n; i++) {
        if (P.y[i] > ymax) ymax = P.y[i], ymax_i = i;
        if (P.y[i] < ymin) ymin = P.y[i], ymin_i = i;
        if (P.x[i] > xmax) xmax = P.x[i], xmax_i = i;
        if (P.x[i] < xmin) xmin = P.x[i], xmin_i = i;
    }
}

static int16_t wrap(int16_t i)
{
    if (i < 0) return (int16_t)(P.n - 1);
    if (i >= P.n) return 0;
    return i;
}

static int32_t diff16(int a, int b) { return (int32_t)(int16_t)(a - b); }
/* (a << 16) / dy with the original's 32-bit shift */
static uint32_t edge_step(int32_t diff, int32_t dy) { return (uint32_t)((int32_t)((uint32_t)diff << 16) / dy); }

/* Poly_StepLeftEdge 0x498540 (edge A to its next vertex; x restarts at x << 16 + 0x7fff, u and v keep
   accumulating) */
static void step_a(void)
{
    int16_t s = wrap((int16_t)(edge_a + edge_dir)), b = edge_a;
    ax = ((uint32_t)P.x[b] << 16) + 0x7fff;
    int32_t dy = diff16(P.y[s], P.y[b]);
    if (dy == 0) adx = adu = adv = 0;
    else {
        adx = edge_step(diff16(P.x[s], P.x[b]), dy);
        adu = edge_step(diff16(P.u[s], P.u[b]), dy);
        adv = edge_step(diff16(P.v[s], P.v[b]), dy);
    }
    edge_a = s;
    a_next = P.y[s];
}

/* Poly_StepRightEdge 0x4986a9 (edge B; x restarts at x << 16) */
static void step_b(void)
{
    int16_t s = wrap((int16_t)(edge_b - edge_dir)), b = edge_b;
    bx = (uint32_t)P.x[b] << 16;
    int32_t dy = diff16(P.y[s], P.y[b]);
    if (dy == 0) bdx = bdu = bdv = 0;
    else {
        bdx = edge_step(diff16(P.x[s], P.x[b]), dy);
        bdu = edge_step(diff16(P.u[s], P.u[b]), dy);
        bdv = edge_step(diff16(P.v[s], P.v[b]), dy);
    }
    edge_b = s;
    b_next = P.y[s];
}

static void advance_edges(void)
{
    ax += adx, bx += bdx, au += adu, av += adv, bu += bdu, bv += bdv;
}

/* Poly_SetupEdges 0x498800: both edges from the top vertices, then rows above the clip are stepped
   through one by one (with edge changes); span_end is the bottom vertex row, or clip y1 + 1. */
static void setup_edges(void)
{
    int16_t b = edge_a, s = wrap((int16_t)(b + edge_dir));
    int32_t dy = diff16(P.y[s], P.y[b]);
    ax = ((uint32_t)P.x[b] << 16) + 0x8000;
    adx = adu = adv = 0;
    if (dy) adx = edge_step(diff16(P.x[s], P.x[b]), dy);
    au = ((uint32_t)P.u[b] << 16) + 0x8000;
    av = ((uint32_t)P.v[b] << 16) + 0x8000;
    if (dy) {
        adu = edge_step(diff16(P.u[s], P.u[b]), dy);
        adv = edge_step(diff16(P.v[s], P.v[b]), dy);
    }
    edge_a = s;
    a_next = P.y[s];

    b = edge_b, s = wrap((int16_t)(b - edge_dir));
    dy = diff16(P.y[s], P.y[b]);
    bx = (uint32_t)P.x[b] << 16;
    bdx = bdu = bdv = 0;
    if (dy) bdx = edge_step(diff16(P.x[s], P.x[b]), dy);
    bu = ((uint32_t)P.u[b] << 16) + 0x8000;
    bv = ((uint32_t)P.v[b] << 16) + 0x8000;
    if (dy) {
        bdu = edge_step(diff16(P.u[s], P.u[b]), dy);
        bdv = edge_step(diff16(P.v[s], P.v[b]), dy);
    }
    edge_b = s;
    b_next = P.y[s];

    span_y = P.y[ymin_i];
    if (span_y < clip_y0) {
        span_end = clip_y0;
        do {
            advance_edges();
            span_y++;
            if (span_y == a_next) step_a();
            if (span_y == b_next) step_b();
        } while (span_y < span_end);
    }
    span_end = P.y[ymax_i];
    if ((int16_t)span_end > clip_y1) span_end = clip_y1 + 1;
}

/* The span prologue shared by both fillers: x range [left, right) clipped, u/v at left and per-pixel
   steps. False if the row draws nothing. */
static bool span_row(int *left, int *count, uint32_t *u, uint32_t *v, uint32_t *du, uint32_t *dv)
{
    int r = (int32_t)ax >> 16, l = (int32_t)bx >> 16, w = r - l;
    if (w <= 0) return false;
    *u = bu, *v = bv;
    *du = (uint32_t)((int32_t)(au - bu) / w);
    *dv = (uint32_t)((int32_t)(av - bv) / w);
    if ((int16_t)r > clip_x1) {
        if ((int16_t)l > clip_x1) return false;
        r = clip_x1 + 1;
    }
    if ((int16_t)l < clip_x0) {
        if ((int16_t)r < clip_x0) return false;
        uint32_t k = (uint32_t)(clip_x0 - l);
        *u += *du * k, *v += *dv * k;
        l = clip_x0;
    }
    if (r - l <= 0) return false;
    *left = l, *count = r - l;
    return true;
}

static bool span_next_row(void)
{
    advance_edges();
    span_y++;
    if (span_y >= span_end) return false;
    if (span_y == a_next) step_a();
    if (span_y == b_next) step_b();
    return true;
}

/* Poly_SpanTex32 0x4996c4: texel 0 is not drawn. The v register also collects the integer parts of the
   steps in its low half (ECX = dv << 16 | dv_int << 8 | du_int), whose carries nudge the fraction. */
static void span_tex32(void)
{
    setup_edges();
    if (span_y >= span_end) return;
    const uint32_t *clut = poly_clut;
    do {
        int left, count;
        uint32_t u, v, du, dv;
        if (!span_row(&left, &count, &u, &v, &du, &dv)) continue;
        uint32_t *dst = rows[span_y] + left;
        const uint32_t ecx = dv << 16 | (dv >> 16 & 0xff) << 8 | (du >> 16 & 0xff);
        const uint32_t esi = du << 16;
        const uint8_t cl = (uint8_t)ecx, ch = (uint8_t)(ecx >> 8);
        uint8_t bl = (uint8_t)(u >> 16), bh = (uint8_t)(v >> 16);
        uint32_t eax = u << 16, ebp = v << 16;
        for (uint16_t n = (uint16_t)count; n; n--) {
            uint8_t t = P.tex[bh << 8 | bl];
            uint32_t s = eax + esi;
            bl = (uint8_t)(bl + cl + (s < eax));
            eax = s;
            s = ebp + ecx;
            bh = (uint8_t)(bh + ch + (s < ebp));
            ebp = s;
            if (t) *dst = clut[t * 64];
            dst++;
        }
    } while (span_next_row());
}

/* Poly_SpanBlend32 0x499da4: each channel becomes blend[clut][screen] (the 50% table), texel 0 not
   drawn, byte 3 cleared. Quirk: the u/v fractions are read from the unclipped edge values (dwords at
   0x4b889a / 0x4b8892 straddle the left edge's u and v), only the integer parts honour the left clip. */
static void span_blend32(void)
{
    setup_edges();
    if (span_y >= span_end) return;
    const uint32_t *clut = poly_clut;
    do {
        int left, count;
        uint32_t u, v, du, dv;
        uint32_t eu = bu, ev = bv;   /* before span_row's clip adjustment */
        if (!span_row(&left, &count, &u, &v, &du, &dv)) continue;
        uint32_t *dst = rows[span_y] + left;
        uint8_t bl = (uint8_t)(u >> 16), bh = (uint8_t)(v >> 16);
        uint32_t esi = eu << 16, ebp = ev << 16;
        const uint32_t su = du << 16, sv = dv << 16;
        const uint8_t iu = (uint8_t)(du >> 16), iv = (uint8_t)(dv >> 16);
        for (int n = count; n; n--) {
            uint8_t t = P.tex[bh << 8 | bl];
            uint32_t s = esi + su;
            bl = (uint8_t)(bl + iu + (s < esi));
            esi = s;
            s = ebp + sv;
            bh = (uint8_t)(bh + iv + (s < ebp));
            ebp = s;
            if (t) {
                uint32_t sc = *dst, c = clut[t * 64];
                *dst = (uint32_t)blend[(c & 0xff) << 8 | (sc & 0xff)] |
                       (uint32_t)blend[(c >> 8 & 0xff) << 8 | (sc >> 8 & 0xff)] << 8 |
                       (uint32_t)blend[(c >> 16 & 0xff) << 8 | (sc >> 16 & 0xff)] << 16;
            }
            dst++;
        }
    } while (span_next_row());
}

/* Poly_Draw 0x496cf0: start vertices of both edges (the ends of a flat top), reject polygons outside
   the clip or without extent, fill. */
static void poly_draw(void)
{
    find_extents();
    int16_t top = ymin_i, i;
    i = (int16_t)(top + 1);
    if (i >= P.n) i = (int16_t)(i - P.n);
    while (P.y[i] == P.y[top]) {
        i++;
        if (i >= P.n) i = (int16_t)(i - P.n);
    }
    i--;
    if (i < 0) i = (int16_t)(P.n + i);
    edge_b = i;
    i = (int16_t)(top - 1);
    if (i < 0) i = (int16_t)(P.n + i);
    while (P.y[i] == P.y[top]) {
        i--;
        if (i < 0) i = (int16_t)(P.n + i);
    }
    i++;
    if (i >= P.n) i = (int16_t)(i - P.n);
    edge_a = i;
    if (clip_x0 <= P.x[xmax_i] && P.x[xmin_i] <= clip_x1 && clip_y0 <= P.y[ymax_i] && P.y[ymin_i] <= clip_y1 &&
        P.x[xmin_i] < P.x[xmax_i] && P.y[ymin_i] < P.y[ymax_i]) {
        if (P.flags == 2) span_tex32();   /* 16 bpp: Poly_SpanTex16 0x4994b9 */
        else span_blend32();              /* 15/16 bpp: Poly_SpanBlend15 0x4998ce / Poly_SpanBlend16 0x499b39 */
    }
}

void poly_draw_quad(uint32_t face, int tile, int x0, int x1, int x2, int x3, int y0, int y1, int y2, int y3,
                    int u0, int u1, int u2, int u3, int v0, int v1, int v2, int v3)
{
    select_tile((unsigned)tile, face);
    P.flags = face & POLY_TRANSPARENT ? 6 : 2;
    P.tex = sel_page;
    P.x[0] = (int16_t)x0, P.y[0] = (int16_t)y0;
    P.x[1] = (int16_t)x1, P.y[1] = (int16_t)y1;
    P.x[2] = (int16_t)x2, P.y[2] = (int16_t)y2;
    P.x[3] = (int16_t)x3, P.y[3] = (int16_t)y3;
    P.n = (int16_t)u3 < 0 ? 3 : 4;
    P.u[0] = (uint8_t)(u0 + sel_u), P.v[0] = (uint8_t)(v0 + sel_v);
    P.u[1] = (uint8_t)(u1 + sel_u), P.v[1] = (uint8_t)(v1 + sel_v);
    P.u[2] = (uint8_t)(u2 + sel_u), P.v[2] = (uint8_t)(v2 + sel_v);
    P.u[3] = (uint8_t)(u3 + sel_u), P.v[3] = (uint8_t)(v3 + sel_v);
    find_extents();
    if (P.y[ymax_i] != P.y[ymin_i]) poly_draw();
}
