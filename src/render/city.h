/* The block city renderer (0x4389f0-0x43b7e0): per layer, from the lowest (z = 5) up, project the
   65 x 65 grid of block corners of the layer's top plane, then draw the layer's blocks from the edges
   of the visible rectangle inward (four mirrored blocks at a time), each block's visible faces
   (normal, flat or one of four slope classes). See docs/render.md. */
#pragma once
#include "../map.h"
#include "../style.h"
#include "camera.h"
#include <stdbool.h>
#include <stdint.h>

enum { RENDER_GRID = 65 };

/* Per-layer visible rectangle (10 ints at 0x5bfab8 + k * 0x28, k = 0..6; layer z uses k = z + 1, k = 6
   is also the grid descriptor the pointer at 0x4b0d10). Block coordinates; *_rel are relative to the grid
   origin (rect 6's left/top). */
typedef struct {
    int32_t y0_rel, y_mid_rel, x0_rel, x_mid_rel;  /* rows/columns [x0, x_mid) are walked ... */
    int32_t y_sum, x_sum;                          /* ... and mirrored: x' = x_sum - x */
    int32_t left, top;                             /* +0x18, +0x1c: world block of the rect's corner */
    int32_t nx, ny;                                /* +0x20, +0x24: size in blocks (even) */
} RenderRect;

/* The renderer's copy of the camera (Render_CopyCamera 0x43b780), used by Sprite_WorldToScreen. */
typedef struct {
    int32_t x, y, height;       /* 0x5c0c04, 0x5c0c08, 0x5c0c0c */
    uint8_t squash;             /* 0x5bfbdc */
    int32_t scale;              /* 0x5c1c10 */
    int32_t cx, cy;             /* 0x5bfbe4, 0x5bfbe8 */
    int32_t w, h;               /* 0x5c0c00, 0x5bfab0 */
} RenderCamera;

extern RenderRect render_rects[7];
extern RenderCamera render_cam;
extern bool render_draw_blocks;            /* 0x5031c8 */
extern bool render_draw_sprites;           /* 0x5031a4: Sprite_DrawLevel between the layers */

void render_compute_visible_rect(const Viewport *vp);   /* Render_ComputeVisibleRect 0x43b7e0 */
void render_copy_camera(const Viewport *vp);            /* Render_CopyCamera 0x43b780 */
/* Render_DrawCity 0x4389f0, into the surface set with poly_set_screen_rows. */
void render_draw_city(const Map *m, const Style *s, const Viewport *vp);
/* Sprite_WorldToScreen 0x47bb10: 16.16 world point to screen (its own rounding, unlike the grid). */
void render_world_to_screen(int32_t x, int32_t y, int32_t z, int32_t *sx, int32_t *sy);
