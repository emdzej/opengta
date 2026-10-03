/* The frontend at hires (not part of the original; docs/hires.md, "HUD and menus"): with hires=N the menus,
   the cutscene stills and the intro movie are presented at 640N x 480N. The frontend runs and draws its
   640 x 480 screen as always; its picture, glyph and movie draws are recorded (hires_text.h) and replayed
   here: pictures scaled with a bicubic filter (cached per picture), glyphs at N x by the UI filter, the
   movie's frame filtered each frame. Skins can replace fonts and pictures (skin_ui.h). */
#pragma once
#include "../../surface.h"
#include "hires_text.h"
#include <stdbool.h>
#include <stdint.h>

/* n = 1..HIRES_MAX: the frontend drawn at n x (n = 1 only makes sense with skins); 0: off, freed. False
   if n is out of range or memory is short (then it is off). */
bool hires_front_init(int n);
bool hires_front_active(void);
/* Before front_frame: start recording. */
void hires_front_begin(void);
/* After it: the recorded frame drawn into the hires frontend frame, which is returned (640n x 480n,
   R, G, B, A bytes). */
const uint32_t *hires_front_end(int *w, int *h);
/* Forget the scaled pictures (memory: the game is about to run). */
void hires_front_flush(void);

/* ---- hooks (only when hires_ui_recording) ---- */

/* Gfx_BlitImage / the backdrop's row copies: the w x h block at (sx, sy) of image (an Image) to (x, y),
   rows clip_y0 <= y < clip_y1 only. */
void hires_front_image(const void *image, int x, int y, int sx, int sy, int w, int h, int clip_y0, int clip_y1);
/* A rectangle of one colour (display format). */
void hires_front_fill(int x, int y, int w, int h, uint32_t colour);
/* The movie's frame: w x h indices (rows stride apart) shown 2 x 2 from row `top` (the rest colour 0 of
   pal); shown = false: everything colour 0. */
void hires_front_movie(const uint8_t *video, int stride, int w, int h, int top, const uint32_t pal[256], bool shown);
/* Gfx_LoadRawImage wrote new pixels into image: its scaled copies are stale. */
void hires_front_image_changed(const void *image);

/* Replays the frontend commands (called by hires_ui_replay). */
void hires_front_replay(const HrTarget *t, const HrUiCmd *c, int n);

/* Separable bicubic (Catmull-Rom, widened when shrinking) resampling of premultiplied 0xAABBGGRR pixels,
   edges clamped; integer arithmetic. */
void hires_ui_resample(const uint32_t *src, int sw, int sh, uint32_t *dst, int dw, int dh);
