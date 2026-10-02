/* DMA's software rasteriser (Poly_*, 0x496cf0-0x49b778): 64x64 tiles on trapezoids with horizontal or
   vertical edges (block faces), general textured triangles/quads (slopes), the rotated-tile LRU cache,
   the blend table. Only the 32 bpp fillers are ported; the 16 bpp ones (Poly_FaceHoriz16 0x49b328,
   Poly_FaceVert16 0x49b778, Poly_SpanTex16 0x4994b9, Poly_SpanBlend15 0x4998ce, Poly_SpanBlend16
   0x499b39, Poly_RectFill16 0x49a2e8) are the same algorithms writing 15/16-bit pixels.

   Textures are addressed like the original: a tile is (page, u0, v0) and a texel is page[v << 8 | u]
   with 8-bit u and v, so steps that overshoot a tile read its neighbour in the page. A CLUT pointer
   points at colour 0 of a palette; colour t is 64 words further per t (the original puts t into byte 1
   of the pointer). See docs/render.md. */
#pragma once
#include <stdint.h>

/* Face word bits (the first argument of the drawers) */
enum {
    POLY_ROT_MASK = 0xc000,      /* rotation, 90 degree steps */
    POLY_TRANSPARENT = 0x80,     /* texel 0 not drawn (block "flat" bit); DrawQuad: blended */
    POLY_FLIP_V = 0x200000,      /* swap the two edges */
    POLY_MIRROR_U = 0x400000,    /* mirror u */
};

/* 0x78c10c: the CLUT of the next draw (set by the caller before every draw). */
extern const uint32_t *poly_clut;
/* Debug counter: lookups of the 63/len table outside its 4096 entries (the original reads the memory
   after it; the port uses 0). */
extern unsigned poly_recip_oob;

/* Rotated-tile cache hit and miss totals (0x78e8d8, 0x78e8d0). */
void poly_cache_stats(unsigned *hits, unsigned *misses);

/* Poly_Init 0x497c3b: texture base (tile page 0), rotated-tile cache of nslots 4 KB slots at cache_base
   starting at slot index first; also builds the 63/len table. */
void poly_init(uint8_t *tex_base, uint8_t *cache_base, unsigned nslots, unsigned first);
/* Poly_BuildBlendTable 0x497b16: tbl[a * 256 + b] = (int)(a * alpha + b * (1 - alpha)); the table is
   used by the blended span filler. */
void poly_build_blend_table(uint8_t *tbl, float alpha);
/* Poly_SetClip 0x497bab: inclusive clip rectangle. */
void poly_set_clip(int x0, int y0, int x1, int y1);
/* Poly_SetScreenRows 0x497bfa: the target surface, 32 bpp, pitch in bytes (also 0x503218). */
void poly_set_screen_rows(uint32_t *base, int pitch_bytes, int h);

/* Poly_DrawFaceHoriz 0x497035: tile on a trapezoid whose two edges are horizontal: edge a is
   x xl_a..xr_a at y y_a, edge b is xl_b..xr_b at y y_b. u runs along the edges, v from a to b; the face
   word's rotation, flip and mirror bits reorient the tile. Arguments are used as 16-bit values. */
void poly_draw_face_horiz(uint32_t face, int tile, int xl_a, int xr_a, int xl_b, int xr_b, int y_a, int y_b);
/* Poly_DrawFaceVert 0x497332: the same with vertical edges at x_a and x_b: edge a spans y yt_a..yb_a,
   edge b yt_b..yb_b. The tile is turned a further 90 degrees. */
void poly_draw_face_vert(uint32_t face, int tile, int yt_a, int yt_b, int yb_a, int yb_b, int x_a, int x_b);
/* Poly_DrawQuad 0x497710: textured quad (u3 < 0: triangle), u/v in texels of the tile (0..63). */
void poly_draw_quad(uint32_t face, int tile, int x0, int x1, int x2, int x3, int y0, int y1, int y2, int y3,
                    int u0, int u1, int u2, int u3, int v0, int v1, int v2, int v3);
