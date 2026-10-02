/* State of DMA's rasteriser shared by its two translation units (poly.c: tiles, faces, polygons;
   poly_sprite.c: sprites and rectangles). In the original these are plain globals of one module. */
#pragma once
#include <stdint.h>

enum { POLY_MAX_ROWS = 2048 };

extern uint32_t *poly_rows[POLY_MAX_ROWS];   /* 0x503228: scanline pointers */
extern int poly_nrows, poly_pitch_px;        /* rows set, 0x503218 / 4 */
extern int poly_clip_x0, poly_clip_x1, poly_clip_y0, poly_clip_y1;   /* 0x78e548.. */
extern const uint8_t *poly_blend;            /* 0x78c108: 64 KB blend table */

/* The tail of Poly_DrawQuad / Poly_DrawSprite: a 4-vertex textured polygon descriptor (flags 2 textured,
   6 transparent, 0x42 blended; page = the texture's 64 KB page; u, v wrap in the page), walk direction
   -1, extents, Poly_Draw 0x496cf0 unless it has no height. */
void poly_draw_polygon(uint16_t flags, const uint8_t *page, const int16_t x[4], const int16_t y[4], const uint8_t u[4],
                       const uint8_t v[4]);

/* the names the rasteriser's code uses */
#define rows poly_rows
#define nrows poly_nrows
#define pitch_px poly_pitch_px
#define clip_x0 poly_clip_x0
#define clip_x1 poly_clip_x1
#define clip_y0 poly_clip_y0
#define clip_y1 poly_clip_y1
#define blend poly_blend
