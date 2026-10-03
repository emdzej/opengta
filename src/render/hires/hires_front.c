/* The frontend at hires (see hires_front.h). */
#include "hires_front.h"
#include "../../front/images.h"
#include "skin_ui.h"
#include <stdlib.h>
#include <string.h>

enum { FW = 640, FH = 480, NMAX = 4 };

static int N;
static uint32_t *frame;   /* 640N x 480N */

/* ---- resampling ---- */

/* Catmull-Rom (a = -1/2) at x, 16.16 in and out */
static bool linear;   /* hires_ui_resample_linear: the tent instead */

static int64_t cubic(int64_t x)
{
    if (x < 0) x = -x;
    if (linear) return x < 0x10000 ? 0x10000 - x : 0;
    const int64_t x2 = x * x >> 16, x3 = x2 * x >> 16;
    if (x < 0x10000) return (3 * x3 - 5 * x2 + 2 * 0x10000) / 2;
    if (x < 0x20000) return (-x3 + 5 * x2 - 8 * x + 4 * 0x10000) / 2;
    return 0;
}

enum { WBITS = 12, WONE = 1 << WBITS };

/* Taps of every output position along one axis: first source index, count, weights summing to WONE. */
typedef struct { int *first, *count, *w, maxt; } Taps;

static bool taps_make(Taps *t, int s, int d)
{
    const int64_t f = (int64_t)s * 0x10000 / d > 0x10000 ? (int64_t)s * 0x10000 / d : 0x10000;   /* kernel scale */
    t->maxt = (int)(4 * f >> 16) + 2;
    t->first = malloc((size_t)d * sizeof(int));
    t->count = malloc((size_t)d * sizeof(int));
    t->w = malloc((size_t)d * (size_t)t->maxt * sizeof(int));
    if (!t->first || !t->count || !t->w) return false;
    for (int i = 0; i < d; i++) {
        const int64_t c = (int64_t)(2 * i + 1) * s * 0x10000 / (2 * d) - 0x8000;   /* source position, 16.16 */
        const int64_t lo = c - 2 * f, hi = c + 2 * f;
        int j0 = (int)((lo >> 16) + 1), j1 = (int)(hi >> 16);
        if (j1 - j0 + 1 > t->maxt) j1 = j0 + t->maxt - 1;
        int *w = t->w + (size_t)i * t->maxt, n = 0;
        int64_t sum = 0, raw[64 + 8];
        int64_t *r = t->maxt <= 72 ? raw : malloc((size_t)t->maxt * sizeof *r);
        if (!r) return false;
        for (int j = j0; j <= j1; j++, n++) sum += r[n] = cubic((((int64_t)j << 16) - c) * 0x10000 / f);
        int lead = 0;   /* taps of weight 0 at the ends (the tent's) dropped */
        while (n > 1 && r[lead] == 0) lead++, n--, j0++;
        while (n > 1 && r[lead + n - 1] == 0) n--;
        if (lead) memmove(r, r + lead, (size_t)n * sizeof *r);
        int acc = 0, big = 0;
        for (int k = 0; k < n; k++) {
            w[k] = sum ? (int)(r[k] * WONE / sum) : (k == 0 ? WONE : 0);
            acc += w[k];
            if (w[k] > w[big]) big = k;
        }
        w[big] += WONE - acc;   /* exactly WONE */
        if (r != raw) free(r);
        t->first[i] = j0, t->count[i] = n;
    }
    return true;
}

static void taps_free(Taps *t)
{
    free(t->first);
    free(t->count);
    free(t->w);
    memset(t, 0, sizeof *t);
}

static inline int clamp_idx(int j, int n) { return j < 0 ? 0 : j >= n ? n - 1 : j; }

/* one premultiplied pixel from channel sums of weight WONE (clamped: colours not above alpha) */
static inline uint32_t pack(int32_t r, int32_t g, int32_t b, int32_t a)
{
    a = (a + WONE / 2) >> WBITS, r = (r + WONE / 2) >> WBITS, g = (g + WONE / 2) >> WBITS, b = (b + WONE / 2) >> WBITS;
    a = a < 0 ? 0 : a > 255 ? 255 : a;
    r = r < 0 ? 0 : r > a ? a : r;
    g = g < 0 ? 0 : g > a ? a : g;
    b = b < 0 ? 0 : b > a ? a : b;
    return (uint32_t)a << 24 | (uint32_t)b << 16 | (uint32_t)g << 8 | (uint32_t)r;
}

static void filter(const uint32_t *src, int sstep, int slen, uint32_t *dst, int dstep, const Taps *t, int dlen)   /* one row */
{
    for (int i = 0; i < dlen; i++) {
        const int *w = t->w + (size_t)i * t->maxt;
        int32_t r = 0, g = 0, b = 0, a = 0;
        for (int k = 0; k < t->count[i]; k++) {
            const uint32_t p = src[(size_t)clamp_idx(t->first[i] + k, slen) * sstep];
            r += w[k] * (int32_t)(p & 0xff), g += w[k] * (int32_t)(p >> 8 & 0xff);
            b += w[k] * (int32_t)(p >> 16 & 0xff), a += w[k] * (int32_t)(p >> 24);
        }
        dst[(size_t)i * dstep] = pack(r, g, b, a);
    }
}

/* The tap tables of the last sizes asked for (the movie asks for the same every frame). */
static struct { int s, d; bool linear; Taps t; } tapcache[2];

static const Taps *taps_for(int slot, int s, int d)
{
    if (tapcache[slot].s != s || tapcache[slot].d != d || tapcache[slot].linear != linear || !tapcache[slot].t.w) {
        taps_free(&tapcache[slot].t);
        tapcache[slot].s = tapcache[slot].d = 0;
        if (!taps_make(&tapcache[slot].t, s, d)) { taps_free(&tapcache[slot].t); return NULL; }
        tapcache[slot].s = s, tapcache[slot].d = d, tapcache[slot].linear = linear;
    }
    return &tapcache[slot].t;
}

void hires_ui_resample(const uint32_t *src, int sw, int sh, uint32_t *dst, int dw, int dh)
{
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    uint32_t *tmp = malloc((size_t)dw * sh * 4);
    const Taps *tx = taps_for(0, sw, dw), *ty = taps_for(1, sh, dh);
    if (!tmp || !tx || !ty) { free(tmp); return; }
    for (int y = 0; y < sh; y++) filter(src + (size_t)y * sw, 1, sw, tmp + (size_t)y * dw, 1, tx, dw);
    /* vertically row by row (cache friendly): each output row from a few rows of tmp */
    int32_t *acc = malloc((size_t)dw * 4 * sizeof *acc);
    if (!acc) { free(tmp); return; }
    for (int y = 0; y < dh; y++) {
        memset(acc, 0, (size_t)dw * 4 * sizeof *acc);
        const int *w = ty->w + (size_t)y * ty->maxt;
        for (int k = 0; k < ty->count[y]; k++) {
            const uint32_t *r = tmp + (size_t)clamp_idx(ty->first[y] + k, sh) * dw;
            const int32_t wk = w[k];
            for (int x = 0; x < dw; x++) {
                const uint32_t p = r[x];
                int32_t *a = acc + 4 * x;
                a[0] += wk * (int32_t)(p & 0xff), a[1] += wk * (int32_t)(p >> 8 & 0xff);
                a[2] += wk * (int32_t)(p >> 16 & 0xff), a[3] += wk * (int32_t)(p >> 24);
            }
        }
        uint32_t *o = dst + (size_t)y * dw;
        for (int x = 0; x < dw; x++) o[x] = pack(acc[4 * x], acc[4 * x + 1], acc[4 * x + 2], acc[4 * x + 3]);
    }
    free(acc);
    free(tmp);
}

/* ---- the frame ---- */

bool hires_front_init(int n)
{
    hires_front_flush();
    free(frame);
    frame = NULL;
    N = 0;
    if (n == 0) return true;
    if (n < 1 || n > NMAX) return false;
    if (!(frame = malloc((size_t)FW * n * FH * n * 4))) return false;
    for (size_t i = 0; i < (size_t)FW * n * FH * n; i++) frame[i] = 0xff000000u;
    N = n;
    return true;
}

bool hires_front_active(void) { return frame != NULL; }

void hires_front_begin(void)
{
    if (frame) hires_ui_begin();
}

const uint32_t *hires_front_end(int *w, int *h)
{
    hires_ui_end();
    if (!frame) return NULL;
    HrTarget t = { frame, FW * N, FH * N, FW * N };
    hires_ui_replay(&t, N);
    *w = FW * N, *h = FH * N;
    return frame;
}

/* ---- pictures ---- */

enum { PICS = 48 };
typedef struct {
    const Image *im;
    const uint32_t *src;   /* the image's pixels it was made from */
    int n;
    uint32_t *px;          /* (w n) x (h n) */
    bool owned;            /* else the skin layer's */
} Pic;
static Pic pics[PICS];

static void pic_drop(Pic *p)
{
    if (p->owned) free(p->px);
    memset(p, 0, sizeof *p);
}

void hires_front_flush(void)
{
    for (int i = 0; i < PICS; i++) pic_drop(&pics[i]);
}

void hires_front_image_changed(const void *image)
{
    for (int i = 0; i < PICS; i++)
        if (pics[i].im == image) pic_drop(&pics[i]);
}

/* The picture at n x: the skins' pictures/<NAME>.png, or the original with the bicubic filter. */
static const uint32_t *picture(const Image *im, int n)
{
    if (!im->px || im->w <= 0 || im->h <= 0) return NULL;
    Pic *free_slot = NULL;
    for (int i = 0; i < PICS; i++) {
        Pic *p = &pics[i];
        if (p->im == im && p->src == im->px && p->n == n) return p->px;
        if (!p->px && !free_slot) free_slot = p;
    }
    if (!free_slot) {   /* full: start over */
        hires_front_flush();
        free_slot = &pics[0];
    }
    const char *name = skin_ui_picture_name(im);
    const uint32_t *k = name ? skin_ui_picture(name, im->w, im->h, n) : NULL;
    if (k) {
        *free_slot = (Pic){ im, im->px, n, (uint32_t *)k, false };
        return k;
    }
    uint32_t *px = malloc((size_t)im->w * n * im->h * n * 4);
    if (!px) return NULL;
    if (n == 1) memcpy(px, im->px, (size_t)im->w * im->h * 4);
    else hires_ui_resample(im->px, im->w, im->h, px, im->w * n, im->h * n);
    *free_slot = (Pic){ im, im->px, n, px, true };
    return px;
}

/* ---- hooks ---- */

void hires_front_image(const void *image, int x, int y, int sx, int sy, int w, int h, int clip_y0, int clip_y1)
{
    if (!image || w <= 0 || h <= 0) return;
    HrUiCmd *c = hires_ui_push(HRC_IMAGE);
    if (!c) return;
    c->x = x, c->y = y, c->w = w, c->h = h;
    c->u.im.im = image, c->u.im.sx = sx, c->u.im.sy = sy, c->u.im.clip_y0 = clip_y0, c->u.im.clip_y1 = clip_y1;
}

void hires_front_fill(int x, int y, int w, int h, uint32_t colour)
{
    HrUiCmd *c = hires_ui_push(HRC_FILL);
    if (!c) return;
    c->x = x, c->y = y, c->w = w, c->h = h;
    c->u.colour = colour;
}

static struct {
    const uint8_t *video;
    int stride, w, h, top;
    uint32_t pal[256];
    bool shown;
    uint32_t *rgb, *big;   /* the frame in colours, and filtered */
    int bw, bh;
} mv;

void hires_front_movie(const uint8_t *video, int stride, int w, int h, int top, const uint32_t pal[256], bool shown)
{
    HrUiCmd *c = hires_ui_push(HRC_MOVIE);
    if (!c) return;
    mv.video = video, mv.stride = stride, mv.w = w, mv.h = h, mv.top = top, mv.shown = shown && video && w > 0 && h > 0;
    memcpy(mv.pal, pal, sizeof mv.pal);
}

static void fill(const HrTarget *t, int x0, int y0, int x1, int y1, uint32_t c)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > t->w) x1 = t->w;
    if (y1 > t->h) y1 = t->h;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) t->px[(size_t)y * t->pitch + x] = c;
}

/* Copy (opaque) the block sw x sh at (sx, sy) of px (pw wide) to (dx, dy), clipped to t. */
static void copy(const HrTarget *t, const uint32_t *px, int pw, int sx, int sy, int sw, int sh, int dx, int dy)
{
    if (dx < 0) sx -= dx, sw += dx, dx = 0;
    if (dy < 0) sy -= dy, sh += dy, dy = 0;
    if (dx + sw > t->w) sw = t->w - dx;
    if (dy + sh > t->h) sh = t->h - dy;
    if (sw <= 0) return;
    for (int r = 0; r < sh; r++)
        memcpy(t->px + (size_t)(dy + r) * t->pitch + dx, px + (size_t)(sy + r) * pw + sx, (size_t)sw * 4);
}

static void replay_movie(const HrTarget *t, int n)
{
    const uint32_t bg = mv.pal[0] | 0xff000000u;
    if (!mv.shown) { fill(t, 0, 0, t->w, t->h, bg); return; }
    const int W = mv.w * 2 * n, H = mv.h * 2 * n, y0 = mv.top * n;
    if (mv.bw != W || mv.bh != H || !mv.big) {
        free(mv.rgb);
        free(mv.big);
        mv.rgb = malloc((size_t)mv.w * mv.h * 4);
        mv.big = malloc((size_t)W * H * 4);
        mv.bw = W, mv.bh = H;
        if (!mv.rgb || !mv.big) {
            free(mv.rgb), free(mv.big);
            mv.rgb = mv.big = NULL;
            return;
        }
    }
    for (int y = 0; y < mv.h; y++)
        for (int x = 0; x < mv.w; x++) mv.rgb[y * mv.w + x] = mv.pal[mv.video[(size_t)y * mv.stride + x]] | 0xff000000u;
    linear = n >= 3;   /* bicubic costs too much per frame at 3x and 4x in wasm: bilinear there */
    hires_ui_resample(mv.rgb, mv.w, mv.h, mv.big, W, H);
    linear = false;
    fill(t, 0, 0, t->w, y0, bg);                    /* above, below and right of the movie: colour 0 */
    fill(t, 0, y0 + H, t->w, t->h, bg);
    fill(t, W, y0, t->w, y0 + H, bg);
    copy(t, mv.big, W, 0, 0, W, H, 0, y0);
}

void hires_front_replay(const HrTarget *t, const HrUiCmd *c, int n)
{
    switch (c->kind) {
    case HRC_IMAGE: {
        const Image *im = c->u.im.im;
        const uint32_t *px = picture(im, n);
        if (!px) return;
        int ya = c->y > c->u.im.clip_y0 ? c->y : c->u.im.clip_y0;
        int yb = c->y + c->h < c->u.im.clip_y1 ? c->y + c->h : c->u.im.clip_y1;
        if (yb <= ya) return;
        copy(t, px, im->w * n, c->u.im.sx * n, (c->u.im.sy + ya - c->y) * n, c->w * n, (yb - ya) * n, c->x * n, ya * n);
        break;
    }
    case HRC_FILL: fill(t, c->x * n, c->y * n, (c->x + c->w) * n, (c->y + c->h) * n, c->u.colour); break;
    case HRC_MOVIE: replay_movie(t, n); break;
    default: break;
    }
}
