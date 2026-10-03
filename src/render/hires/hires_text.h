/* The hires renderer's 2D layer (not part of the original; docs/hires.md, "HUD and menus"): the HUD and the
   frontend drawn again at 640N x 480N. The faithful HUD / frontend code runs unchanged and draws its
   640 x 480 frame as always; while hires_ui_recording is set, a hook next to each of its drawing calls
   (font.c's glyph blitters, the HUD's sprites, arrows and score digits, the frontend's pictures and the
   intro movie) also records what was drawn, where and with which palette. After the faithful pass the list
   is replayed at N times the resolution: positions are the faithful ones times N, glyphs and HUD sprites
   are scaled by the UI filter (or replaced by skin art: skin_ui.h), pictures with a bicubic filter.
   Nothing here writes game, HUD or frontend state.

   This file: the display list, the glyph commands and the pixel-art scaler. hires_hud.c and
   hires_front.c add their own commands and replay them. */
#pragma once
#include "../../font.h"
#include "../../surface.h"
#include "hires_raster.h"
#include <stdbool.h>
#include <stdint.h>

/* Set while a hires HUD or frontend frame is being recorded: the hooks test it and do nothing else when it
   is clear, so without hires nothing changes. */
extern bool hires_ui_recording;

/* How the 8-bit pixel art (glyphs, HUD sprites) is scaled up to N x: nearest neighbour, bilinear (soft
   edges blended with what is below), or the Scale2x / Scale3x family (EPX: edges along equal colours
   rounded, the colours themselves untouched; 4x is Scale2x twice). Default HR_UI_SCALE (docs/hires.md
   has the comparison). */
enum { HR_UI_NEAREST, HR_UI_BILINEAR, HR_UI_SCALE };
extern int hires_ui_filter;
/* "nearest", "bilinear" or "scale"; false (and unchanged) for anything else. */
bool hires_ui_set_filter(const char *name);

/* ---- the display list ---- */

enum {
    HRC_GLYPH,      /* a glyph (or part of one) 1:1 at the faithful position */
    HRC_GLYPH_RECT, /* a glyph stretched over a rectangle (the score popups, Poly_DrawRect) */
    HRC_SPRITE,     /* a HUD sprite unrotated (Sprite_DrawScreen) */
    HRC_WORLD,      /* a HUD sprite in the world (Sprite_Draw: the arrows, the roof marker) */
    HRC_IMAGE,      /* part of a frontend picture */
    HRC_FILL,       /* a rectangle of one colour */
    HRC_MOVIE,      /* the intro movie's frame */
};

typedef struct HrUiCmd {
    uint8_t kind;
    int x, y, w, h;                /* faithful pixels: the destination (GLYPH_RECT: x0, y0, x1 exclusive, y1 exclusive) */
    union {
        struct {
            const Font *font;
            const uint8_t *px;     /* the glyph's first row */
            int gw, gh;            /* the whole glyph */
            int sx, sy;            /* first column and row drawn */
            const uint32_t *clut;  /* colour e = clut[e * step] */
            int step;
            bool xrgb;             /* clut words 0x00RRGGBB (style palettes), else the display format */
            uint32_t chash;
        } g;
        struct {
            const void *info;      /* SpriteInfo */
            int n;                 /* sprite number */
            const uint32_t *clut;  /* paged (step 64) */
            uint32_t chash;
        } s;
        struct {                   /* the Sprite fields Sprite_Draw reads */
            int32_t x, y, z;
            int16_t angle, palette;
            uint16_t frame;
            uint8_t remap, blend;
            uint32_t deltas;
            const void *info;
        } wd;
        struct {
            const void *im;        /* Image */
            int sx, sy;            /* source position */
            int clip_y0, clip_y1;  /* rows drawn: clip_y0 <= y < clip_y1 */
        } im;
        uint32_t colour;           /* FILL: display format */
    } u;
} HrUiCmd;

/* A new frame: the list cleared, recording on. */
void hires_ui_begin(void);
/* Recording off (the list is kept for hires_ui_replay). */
void hires_ui_end(void);
/* Adds a command (NULL if the list is full: the command is dropped). */
HrUiCmd *hires_ui_push(int kind);
/* Every recorded command, in order, into t at n times the faithful resolution. */
void hires_ui_replay(const HrTarget *t, int n);
int hires_ui_count(void);

/* ---- hooks (only when hires_ui_recording) ---- */

/* A glyph blit of font f (the current font): rows row0 .. row0 + h - 1 of the glyph whose pixels start at
   glyph (w wide, the font's height tall), columns clip_l .. w - clip_r - 1, the first one at linear offset
   dst of s; colour e is clut[e * step], 0x00RRGGBB words if xrgb (the HUD: style palettes), else the
   display format (the frontend: font palettes). */
void hires_ui_glyph(const Surface *s, long dst, const Font *f, const uint8_t *glyph, int w, int row0, int h,
                    int clip_l, int clip_r, const uint32_t *clut, int step, bool xrgb);
/* A glyph stretched over x0..x1, y0..y1 (Poly_DrawRect 0x49806a's rectangle: texels 0..umax across x0 to
   x1, rows 0..vmax from y0 to y1 inclusive); clut a linear style palette (0x00RRGGBB). */
void hires_ui_glyph_rect(const Font *f, const uint8_t *glyph, int x0, int x1, int y0, int y1, const uint32_t *clut);
/* Font_Free: forget what was made from f's glyphs. */
void hires_ui_font_freed(const Font *f);

/* ---- helpers shared with hires_hud.c / hires_front.c ---- */

/* A hash of the 256 colours clut[e * step] (memoised per pointer within a frame). */
uint32_t hires_ui_clut_hash(const uint32_t *clut, int step);
/* The 8-bit art src (w x h, rows `stride` bytes apart, 0 transparent) through clut (xrgb: 0x00RRGGBB words,
   else the display format), scaled n x by the UI filter: (w n) x (h n) premultiplied 0xAABBGGRR words,
   cached (NULL: out of memory). */
const uint32_t *hires_ui_art(const uint8_t *src, int stride, int w, int h, const uint32_t *clut, int step, bool xrgb,
                             uint32_t chash, int n);
/* Forget every scaled piece of art (a style or font change). */
void hires_ui_art_flush(void);
/* Premultiplied pixels pm (pw wide) 1:1 over t: the sw x sh block at (sx, sy) to (dx, dy), clipped. */
void hires_ui_blit(const HrTarget *t, const uint32_t *pm, int pw, int sx, int sy, int sw, int sh, int dx, int dy);
/* A texture stretched (bilinear, keyed) over the hires-pixel rectangle x0..x1 x y0..y1 (exclusive), the
   part sx0..sx1 x sy0..sy1 of it (16.16 texels). */
void hires_ui_stretch(const HrTarget *t, HiresTexture *tex, int x0, int y0, int x1, int y1, int32_t su0, int32_t sv0,
                      int32_t su1, int32_t sv1);
