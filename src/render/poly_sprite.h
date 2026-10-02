/* The rasteriser's sprite entry points, 32 bpp (Poly_DrawSprite 0x49787c, Poly_DrawSpriteBlend 0x4979c9,
   Poly_DrawRect 0x49806a with its filler Poly_RectFill32 0x49a539). See docs/sprites.md.

   A sprite graphic is addressed the way the original passes it: one pointer whose low 16 bits are the
   texel's place in a 64 KB page of 256-byte rows (u = bits 0-7, v = bits 8-15) and whose high bits are
   the page. Sprite pages (the style's sprite graphics and the delta composite buffers) are 64 KB
   aligned, as in the original, so the pointer arithmetic is the same.

   The polygon filler, scanline table, clip rectangle, blend table and CLUT are poly.c's
   (poly_internal.h): set them with poly_set_screen_rows / poly_set_clip / poly_build_blend_table. */
#pragma once
#include <stdint.h>


/* Poly_DrawSprite 0x49787c: the w x h graphic at tex on the quad (x0,y0)..(x3,y3), vertex 0 taking the
   graphic's top right texel, 1 the top left, 2 the bottom left, 3 the bottom right. Texel 0 is not
   drawn. Coordinates are used as 16-bit values. */
void poly_draw_sprite(const uint8_t *tex, int x0, int x1, int x2, int x3, int y0, int y1, int y2, int y3, int w,
                      int h);
/* Poly_DrawSpriteBlend 0x4979c9: the same, each pixel averaged with the screen (blend table). */
void poly_draw_sprite_blend(const uint8_t *tex, int x0, int x1, int x2, int x3, int y0, int y1, int y2, int y3,
                            int w, int h);
/* Poly_DrawRect 0x49806a: a packed (stride umax + 1) image of (umax + 1) x (vmax + 1) texels scaled onto
   the screen rectangle x0..x1, y0..y1 one pixel per texel (Poly_RectFill32 0x49a539), texel 0 not
   drawn. Rejected if it misses the clip rectangle. vmax = 0 divides by zero in the original. */
void poly_draw_rect(int x0, int x1, int y0, int y1, int umax, int vmax, const uint8_t *tex);
/* Blit_Tile32 0x4894a1: a w x h block of a 256-byte-wide page at (x, y) through a style CLUT (colour t
   64 words after colour 0), texel 0 transparent. Unclipped in the original; writes off the target are
   dropped here. */
void poly_sprite_blit_tile(int x, int y, const uint8_t *src, int w, int h, const uint32_t *clut);
