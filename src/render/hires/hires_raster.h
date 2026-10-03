/* The high-resolution renderer's rasteriser (not a port: an addition, see docs/hires.md). True colour
   in the presentation byte order (R, G, B, A in memory: a word reads 0xAABBGGRR), sub-pixel geometry
   (coordinates in 1/256 of a hires pixel), bilinear texture sampling clamped to the texture's edges.

   The primitives mirror the faithful rasteriser's (poly.c) so the same calls can be replayed:
   - hr_face_horiz / hr_face_vert: a tile on a trapezoid with two horizontal (vertical) edges, the
     texture mapped the way Poly_FaceHoriz32 0x49a78c / Poly_FaceVert32 0x49abdc map it (u along the
     edges, v from edge a to edge b, linear in screen space), oriented by the face word's rotation,
     flip and mirror bits exactly as Poly_DrawFaceHoriz 0x497035 / Poly_DrawFaceVert 0x497332 decide;
   - hr_polygon: a convex 3- or 4-vertex textured polygon with per-vertex texture coordinates
     interpolated along the edges and across each row (Poly_Draw 0x496cf0's scheme), used for the
     slope sides (Poly_DrawQuad 0x497710) and the sprites (Poly_DrawSprite 0x49787c).
   Pixels are covered when their centre is inside the shape (top-left rule), so neighbouring faces
   meet without gaps or overlaps (the faithful rasteriser draws edges inclusive). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { HR_SUB_BITS = 8, HR_SUB = 1 << HR_SUB_BITS };   /* sub-pixel precision of screen coordinates */

/* A texture: w x h texels, 0xAABBGGRR. Alpha 0 is the original's transparent texel 0 (its colour is
   still the CLUT's colour 0, which opaque faces draw). `pm` is the premultiplied copy the keyed and
   blended draws sample, made on first use. */
typedef struct HiresTexture {
    int w, h;
    uint32_t *rgba;
    uint32_t *pm;
    bool owned;                 /* rgba is freed with the texture */
} HiresTexture;

/* The draw target: 0xAABBGGRR words, alpha always written as 0xff. */
typedef struct { uint32_t *px; int w, h, pitch; } HrTarget;

enum {
    HR_OPAQUE,                  /* every texel drawn (block faces without the flat bit) */
    HR_KEYED,                   /* transparent texels skipped (alpha blended at their edges) */
    HR_BLEND,                   /* keyed, and the rest averaged with the screen (50 %, the blend table) */
};

extern bool hr_nearest;         /* debug: nearest-neighbour sampling instead of bilinear */

const uint32_t *hr_premultiplied(HiresTexture *t);
void hr_texture_free(HiresTexture *t);

/* Face trapezoids. Coordinates in sub-pixels (HR_SUB per hires pixel); `face` is the face word of the
   faithful call (rotation 0xc000, transparent 0x80, flip 0x200000, mirror 0x400000) and the argument
   order is that of poly_draw_face_horiz / poly_draw_face_vert. */
void hr_face_horiz(const HrTarget *t, uint32_t face, HiresTexture *tex, int32_t xl_a, int32_t xr_a, int32_t xl_b,
                   int32_t xr_b, int32_t y_a, int32_t y_b);
void hr_face_vert(const HrTarget *t, uint32_t face, HiresTexture *tex, int32_t yt_a, int32_t yt_b, int32_t yb_a,
                  int32_t yb_b, int32_t x_a, int32_t x_b);

/* A convex polygon of n (3 or 4) vertices: x, y in sub-pixels, u, v in 16.16 texels of tex (texel i
   covers [i, i + 1)). The caller decides whether it is drawn at all (hr_winding_draws on the faithful
   coordinates: the faithful filler draws only one winding). */
void hr_polygon(const HrTarget *t, int mode, HiresTexture *tex, int n, const int32_t x[], const int32_t y[],
                const int32_t u[], const int32_t v[]);
/* The faithful filler's draw test: rows span from edge B (walking forward from the top) to edge A
   (backward), so only one winding draws: twice the signed area (y down) < 0. */
static inline bool hr_winding_draws(int n, const int32_t x[], const int32_t y[])
{
    int64_t a = 0;
    for (int i = 0; i < n; i++) {
        int j = i + 1 == n ? 0 : i + 1;
        a += (int64_t)x[i] * y[j] - (int64_t)x[j] * y[i];
    }
    return a < 0;
}

/* CLUT word (0x00RRGGBB, the 32 bpp DirectDraw format) to 0xAABBGGRR. */
static inline uint32_t hr_from_xrgb(uint32_t c, uint32_t a)
{
    return a << 24 | (c & 0xff) << 16 | (c & 0xff00) | (c >> 16 & 0xff);
}
