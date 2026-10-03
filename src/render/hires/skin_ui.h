/* Skins for fonts and frontend pictures (not part of the original; docs/skins.md, "Fonts and pictures"):
   an extension of the skin stack (hires_skin.h, whose hires_skin_image finds a file in the loaded skins)
   for the hires 2D layer (hires_text.h):

     skins/<name>/font/<FONT>/<code>.png      glyph <code> of GTADATA/<FONT>.FON (BIG2, SUB2, F_MHEAD, ...)
     skins/<name>/pictures/<NAME>.png         the frontend picture GTADATA/<NAME>.RAW (F_UPPER, CUT3, ...)

   A glyph image is stretched over the glyph's box (its width x the font's height, n x at hires=n); a grey
   one is tinted with the palette the original draws that glyph with. A picture is stretched over the
   picture (n x) with a bicubic filter, opaque. */
#pragma once
#include "../../font.h"
#include "hires_raster.h"
#include <stdint.h>

/* The names the art is looked up by, registered when the original loads it (Font_Load, Gfx_LoadRawImage):
   the file's base name without folder and extension, upper case. Cheap, always on (also without hires). */
void skin_ui_font_loaded(const Font *f, const char *rel);
void skin_ui_forget_font(const Font *f);
const char *skin_ui_font_name(const Font *f);             /* NULL if unknown */
void skin_ui_picture_loaded(const void *image, const char *rel);
const char *skin_ui_picture_name(const void *image);

/* Glyph `code` (the font's own code: first + index) of font f at n x: (gw n) x (gh n) premultiplied
   0xAABBGGRR words, made from the skin's image (tinted by clut if grey), cached; NULL if no skin has it.
   px/clut/step/xrgb/chash: the original glyph and the palette it is drawn with (hires_ui_art). */
const uint32_t *skin_ui_glyph(const Font *f, int code, const uint8_t *px, int gw, int gh, const uint32_t *clut, int step,
                              bool xrgb, uint32_t chash, int n);
/* The picture `name` of the skins at (w n) x (h n), opaque 0xAABBGGRR, cached; NULL if no skin has it. */
const uint32_t *skin_ui_picture(const char *name, int w, int h, int n);
/* Forget what was made (the skins changed). */
void skin_ui_flush(void);
