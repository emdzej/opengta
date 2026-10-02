/* Poly_DrawSprite 0x49787c, Poly_DrawSpriteBlend 0x4979c9, Poly_DrawRect 0x49806a / Poly_RectFill32
   0x49a539 (see poly_sprite.h, docs/sprites.md). */
#include "poly_sprite.h"
#include "poly.h"
#include "poly_internal.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- sprites ---- */

/* Poly_DrawSprite 0x49787c / Poly_DrawSpriteBlend 0x4979c9: the descriptor on the stack gets flags 2
   (textured, texel 0 skipped) or 0x42 (blended), 4 vertices, the graphic's page (pointer & ~0xffff)
   and per vertex the u/v bytes from the pointer's low bytes: (u0 + w - 1, v0), (u0, v0),
   (u0, v0 + h - 1), (u0 + w - 1, v0 + h - 1), each wrapping in 8 bits. Then the same tail as
   Poly_DrawQuad: walk direction -1, extents, nothing if the quad has no height. */
static void draw_sprite(uint16_t flags, const uint8_t *tex, int x0, int x1, int x2, int x3, int y0, int y1, int y2,
                        int y3, int w, int h)
{
    uintptr_t a = (uintptr_t)tex;
    uint8_t u0 = (uint8_t)a, v0 = (uint8_t)(a >> 8);
    const int16_t x[4] = { (int16_t)x0, (int16_t)x1, (int16_t)x2, (int16_t)x3 };
    const int16_t y[4] = { (int16_t)y0, (int16_t)y1, (int16_t)y2, (int16_t)y3 };
    const uint8_t u[4] = { (uint8_t)(u0 + w - 1), u0, u0, (uint8_t)(u0 + w - 1) };
    const uint8_t v[4] = { v0, v0, (uint8_t)(v0 + h - 1), (uint8_t)(v0 + h - 1) };
    poly_draw_polygon(flags, (const uint8_t *)(a & ~(uintptr_t)0xffff), x, y, u, v);
}

void poly_draw_sprite(const uint8_t *tex, int x0, int x1, int x2, int x3, int y0, int y1, int y2, int y3, int w, int h)
{
    draw_sprite(2, tex, x0, x1, x2, x3, y0, y1, y2, y3, w, h);
}

void poly_draw_sprite_blend(const uint8_t *tex, int x0, int x1, int x2, int x3, int y0, int y1, int y2, int y3,
                            int w, int h)
{
    draw_sprite(0x42, tex, x0, x1, x2, x3, y0, y1, y2, y3, w, h);
}

/* ---- scaled rectangles ---- */

static inline uint32_t hi16(int v) { return (uint32_t)(uint16_t)v << 16; }

/* Poly_RectFill32 0x49a539. Texel driven: every texel of a row is written to one pixel, x advancing
   by dx = (x1 - x0) / (umax + 1) per texel, so a rectangle wider than its image leaves gaps and a
   narrower one overwrites pixels. Every row restarts at the integer part of the left x (the 0x8000
   rounding and the clip's fraction are dropped). Rows step by dy = (y1 - y0) / vmax (not vmax + 1) and
   run while y <= min(y1, clip y1) + 0.5 after a first test with <. The clips count texels one by one,
   the right clip from 1 up, so a clipped row may draw one more texel than the image has. */
static void rect_fill32(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t umax, uint8_t vmax, const uint8_t *tex)
{
    const int32_t W = umax + 1, Hm = vmax;
    uint32_t x = hi16(x0) + 0x8000;
    const int32_t dx = (int32_t)hi16(x1 - x0) / W;
    if (Hm == 0) return;   /* #DE in the original */
    const int32_t dy = (int32_t)hi16(y1 - y0) / Hm;
    const int16_t ye = y1 > (int16_t)clip_y1 ? (int16_t)clip_y1 : y1;
    const int32_t yend = (int32_t)(hi16(ye) + 0x8000);
    int32_t count = W;
    if (x1 > (int16_t)clip_x1) {
        int32_t n = 1, c = W;
        const int32_t lim = (int32_t)hi16(clip_x1 + 1);
        uint32_t e = x;
        do {
            e += (uint32_t)dx;
            if ((int32_t)e >= lim) break;
            n++;
        } while (--c);
        count = n;
    }
    int32_t skip = 0;
    if (x0 < (int16_t)clip_x0) {
        int32_t n = 1, c = W;
        const int32_t lim = (int32_t)hi16(clip_x0);
        uint32_t e = x;
        do {
            e += (uint32_t)dx;
            if ((int32_t)e >= lim) break;
            n++;
        } while (--c);
        count -= n;
        x = e;
        skip = n;
    }
    uint32_t y = hi16(y0) + 0x8000;
    if (y0 < (int16_t)clip_y0) {
        int32_t c = Hm + 1;
        const int32_t lim = (int32_t)hi16(clip_y0);
        do {
            skip += W;
            y += (uint32_t)dy;
            if ((int32_t)y >= lim) break;
        } while (--c);
    }
    const int32_t stride_skip = W - count;
    const uint8_t *src = tex + skip;
    const uint32_t xi = x >> 16;
    if ((int32_t)y >= yend || count <= 0) return;
    const uint32_t *clut = poly_clut;
    do {
        uint32_t row = y >> 16, esi = xi << 16;
        uint32_t *dst = row < (uint32_t)nrows ? rows[row] : NULL;
        int32_t n = count;
        do {
            uint8_t t = *src++;
            uint32_t px = esi >> 16;
            if (t && dst && px < (uint32_t)pitch_px) dst[px] = clut[t * 64];   /* the original writes anywhere */
            esi += (uint32_t)dx;
        } while (--n > 0);
        src += stride_skip;
        y += (uint32_t)dy;
    } while ((int32_t)y <= yend);
}

/* Poly_DrawRect 0x49806a: descriptor {tex +8, x0 +0x10, y0 +0x12, x1 +0x14, y1 +0x16, uv (0, 0) and
   (umax, vmax) at +0x20}; drawn unless it misses the clip rectangle (16-bit compares). */
void poly_draw_rect(int x0, int x1, int y0, int y1, int umax, int vmax, const uint8_t *tex)
{
    int16_t sx0 = (int16_t)x0, sx1 = (int16_t)x1, sy0 = (int16_t)y0, sy1 = (int16_t)y1;
    if ((int16_t)clip_x0 <= sx1 && sx0 <= (int16_t)clip_x1 && (int16_t)clip_y0 <= sy1 && sy0 <= (int16_t)clip_y1)
        rect_fill32(sx0, sy0, sx1, sy1, (uint8_t)umax, (uint8_t)vmax, tex);   /* 16 bpp: Poly_RectFill16 0x49a2e8 */
}

void poly_sprite_blit_tile(int x, int y, const uint8_t *src, int w, int h, const uint32_t *clut)
{
    for (int r = 0; r < h; r++, src += 256) {
        if (y + r < 0 || y + r >= nrows) continue;
        uint32_t *dst = rows[y + r];
        for (int c = 0; c < w; c++)
            if (src[c] && x + c >= 0 && x + c < pitch_px) dst[x + c] = clut[src[c] * 64];
    }
}
