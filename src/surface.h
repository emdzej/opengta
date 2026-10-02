/* A 32-bit drawing surface and the original's 8-bit -> display-pixel blitters (blit module 0x489470-,
   hand-written assembly in the original).

   Pixels: one uint32_t per pixel, bytes R, G, B, A in memory (0xAABBGGRR read as a little-endian word),
   which is what plat_present / gasm video_present take. stride is in pixels.

   The original draws into a DirectDraw surface of 8, 16 or 32 bits per pixel and converts every palette
   once into that surface's pixel format, a precision shift and a position shift per channel (globals
   0x775318/0x7750c8 red, 0x7752e0/0x775528 green, 0x775520/0x7752dc blue, set by Gfx_SetVideoMode
   0x414db0 from the DirectDraw masks). Our display format is fixed: precision 0, red at bit 0, green at
   8, blue at 16, and alpha 0xff (the original has no alpha; we set it so a frame is opaque). The
   frontend always ran at 640x480x16 (RGB555/565, modes 0x110/0x111), so on the original its colours
   are quantised to 5/6 bits per channel; ours are the full 8 bits of the data.

   Destinations are linear pixel offsets into the surface, as the original works with byte pointers into
   the frame buffer (row base + x * bytes per pixel). A write that would fall outside the buffer is
   dropped (the original would write wherever the pointer went); one past the end of a row lands on the
   next row exactly as in the original. */
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t *px;
    int w, h, stride;           /* stride in pixels */
} Surface;

/* An 8-bit colour converted to the display format (Style_ConvertPalettes 0x47cd10, Font_Load 0x4304d0,
   Gfx_LoadRawImage 0x42d5f0 all do `(c >> precision) << position` per channel). */
static inline uint32_t surface_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint32_t)r | (uint32_t)g << 8 | (uint32_t)b << 16 | 0xff000000u;
}
/* 0x00RRGGBB (style.h palettes) -> display format. */
static inline uint32_t surface_from_xrgb(uint32_t c)
{
    return surface_rgb((uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c);
}

static inline long surface_offset(const Surface *s, int x, int y) { return (long)y * s->stride + x; }

static inline void surface_put(Surface *s, long off, uint32_t c)
{
    if (off >= 0 && off < (long)s->stride * s->h) s->px[off] = c;
}

/* ---- the 32 bpp CLUT blitters. The original's CLUT pointer (0x78c10c) points into the paged style
   CLUT, where colour i of a palette is 256 bytes after colour i-1 (`clut[pix * 256]`); here a palette is
   256 consecutive display-format colours. Colour 0 is transparent in all of them. The 16 bpp twins
   (Blit_Tile16 0x489516, Blit_Sprite16 0x489683, Blit_SpriteClip16 0x4896d8) are not ported: our
   surface is always 32 bpp, which is the branch the dispatchers Blit_Tile 0x48974b, Blit_SpriteClip
   0x489789 and Blit_Sprite 0x4897d3 take when 0x50321c (bytes per pixel) is not 2. ---- */

/* Blit_Tile32 0x4894a1: a w x h block of an image 256 bytes wide (a style tile page) at (x, y). */
static inline void blit_tile32(Surface *s, int x, int y, const uint8_t *src, int w, int h,
                               const uint32_t *clut)
{
    for (int r = 0; r < h; r++, src += 256) {
        long d = surface_offset(s, x, y + r);
        for (int c = 0; c < w; c++)
            if (src[c]) surface_put(s, d + c, clut[src[c]]);
    }
}

/* Blit_Sprite32 0x4895bb: w x h packed bytes (stride w) at linear offset dst; pitch = surface stride
   (0x503220). The original's row loop runs `loop` with ECX = h and the column loop counts EBX down
   from w with a test after the decrement, so h or w of 0 would run 2^32 times: such calls do not
   happen in the original (callers skip zero-width glyphs) and draw nothing here. */
static inline void blit_sprite32(Surface *s, long dst, const uint8_t *src, int w, int h,
                                 const uint32_t *clut)
{
    if (w <= 0 || h <= 0) return;
    for (int r = 0; r < h; r++, dst += s->stride)
        for (int c = 0; c < w; c++, src++)
            if (*src) surface_put(s, dst + c, clut[*src]);
}

/* Blit_SpriteClip32 0x489610: as Blit_Sprite32 but skips clip_l columns on the left and clip_r on the
   right of each source row; the first visible column lands at dst. Nothing when w - (l + r) <= 0. */
static inline void blit_sprite_clip32(Surface *s, long dst, const uint8_t *src, int w, int h,
                                      int clip_l, int clip_r, const uint32_t *clut)
{
    int vis = w - (clip_l + clip_r);
    if (vis <= 0 || h <= 0) return;
    src += clip_l;
    for (int r = 0; r < h; r++, dst += s->stride, src += w)
        for (int c = 0; c < vis; c++)
            if (src[c]) surface_put(s, dst + c, clut[src[c]]);
}

/* Blit_ApplyDelta 0x48958a: patches a 256-byte-wide sprite page with a delta stream of
   {u16 skip, u8 n, n bytes} records until len bytes of stream are consumed. */
static inline void blit_apply_delta(uint8_t *dst, const uint8_t *delta, size_t len)
{
    const uint8_t *p = delta, *end = delta + len;
    while (p < end) {
        dst += (uint16_t)(p[0] | p[1] << 8);
        uint8_t n = p[2];
        p += 3;
        for (uint8_t i = 0; i < n; i++) *dst++ = *p++;
    }
}
