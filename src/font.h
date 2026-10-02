/* Bitmap fonts (.FON, font module 0x4304a0-0x430970) and the string renderers: the frontend's
   (0x42b800-0x42d1c0, drawing with the font's own palette) and the HUD's (0x4837a0-0x485910, drawing
   with a palette of the city's style). Layout and behaviour: docs/text-fonts.md.

   Strings are UTF-8 (text.h). Like the original, the glyph functions work on a current font
   (font_select, 0x513228). */
#pragma once
#include "surface.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The in-memory font (0x80c bytes in the original). */
typedef struct {
    uint8_t count, height;      /* +0, +1 */
    uint16_t first;             /* +2: code of glyph 0 (0x21 text fonts, 1 for icon fonts, 0 for digits) */
    int kanji;                  /* +4: 1 = the Japanese pseudo font (not ported) */
    uint32_t *pal;              /* +8: 256 colours (display format) or NULL (HUD fonts: style palette) */
    struct {
        uint8_t w;              /* +0xc + 8 i */
        uint8_t *px;            /* +0x10 + 8 i: w * height bytes, row by row, 0 = transparent */
    } glyph[256];
} Font;

/* Font_Load 0x4304d0: count, height, glyphs; with_palette reads the 768-byte RGB palette after them and
   converts it to the display format. NULL on a missing or short file (the original: fatal -202/-203). */
Font *font_load(const char *rel, uint16_t first, bool with_palette, char *err, size_t errcap);
void font_free(Font *f);                      /* Font_Free 0x430750 */

void font_select(const Font *f);              /* Font_Select 0x4307c0 */
void font_select_raw(const Font *f);          /* Font_SelectRaw 0x430840 */
const Font *font_current(void);               /* Font_GetCurrent 0x430970 */
int font_glyph_width(uint16_t c);             /* Font_GlyphWidth 0x430850 */
const uint8_t *font_glyph_pixels(uint16_t c); /* Font_GlyphPixels 0x4308d0 */
int font_height(void);                        /* Font_Height 0x430930 */
bool font_is_kanji(void);                     /* Font_IsKanji 0x430960 */

/* ---- frontend renderers. They draw with the font's palette (the original's 0x51156c) through the
   640-wide frontend blitters Blit_Copy8to16_640 0x4898ac / Blit_Copy8to16Clip_640 0x489904 (here
   into the 32 bpp surface; the 8-bit Blit_Copy8_640 0x489814 path for VESA mode 0x101 is dead in the
   Windows build and not ported). Spaces are not drawn but advance by the width of 'n'. */

/* Font_DrawString 0x42bad0 / Font_DrawStringAlt 0x42bc70 (no Japanese font substitution) */
void font_draw_string(Surface *s, const Font *f, int x, int y, const char *text);
void font_draw_string_alt(Surface *s, const Font *f, int x, int y, const char *text);
/* Font_StringWidth 0x42c0e0 / Font_StringWidthAlt 0x42c120 (they select the font) */
int font_string_width(const Font *f, const char *text);
int font_string_width_alt(const Font *f, const char *text);
/* Font_DrawCentered 0x42c160: centred in a box of width box_w starting at x (never left of x). */
void font_draw_centered(Surface *s, const Font *f, const char *text, int x, int y, int box_w);
/* Font_DrawStringClipped 0x42be10, with the current font and palette: the part of the string between
   pixel `skip` and `skip + max_w`, drawn at dst (a linear offset); only glyph rows row0 .. row0 + h - 1. */
void font_draw_string_clipped(Surface *s, const char *text, long dst, int skip, int max_w, int h, int row0);
/* Text_WordWrap 0x42b800 with the current font: a copy of text with '\n' inserted so that no line is
   wider than width ('@' is a line break too), in a static 0x400-byte buffer; *lines = line count. */
const char *text_word_wrap(const char *text, int width, int *lines);
/* Font_DrawWrapped 0x42d150 -> Font_DrawWrappedLines 0x42d1c0 */
void font_draw_wrapped(Surface *s, const Font *f, int x, int y, int width, const char *text);

/* The frontend palette of the renderers above (0x51156c), set from the font by each of them. */
void font_set_front_palette(const uint32_t *pal);

/* ---- HUD fonts and renderers (draw with style palettes and the CLUT blitters of surface.h). */

typedef struct {
    Font *big, *sub, *street, *pager;   /* 0x785158 0x785150 0x785128 0x785130: first code 0x21 */
    Font *expscor, *score, *missmul;    /* 0x785138 0x785140 0x785148: first code 0 */
    int res;                            /* 0x785160: 1 or 2 (BIG1.FON / BIG2.FON ...) */
    int pager_cols;                     /* 0x784860: res * 58 / width of 'n' in the pager font */
} HudFonts;
extern HudFonts hud_fonts;
/* The font half of HUD_LoadFonts 0x481030 (the HUD sprite setup that follows is the HUD's). Reloads
   only when the resolution changes. False if a file is missing (the original: fatal). */
bool font_hud_load(int res, char *err, size_t errcap);
void font_hud_free(void);                     /* HUD_FreeFonts 0x4832c0 (fonts only) */

/* The palettes the HUD renderers select (the original's current CLUT 0x78c10c): aux[0..7] is the table
   of Tile_BuildAuxTable 0x4376f0 (0x5c2c4c: the style's font palettes, logical palettes font_base + n,
   font_base = (tileclut + spriteclut + newcarclut size) / 1024, 0x7752f8) and sprite0 the palette of
   Tile_SelectSprite(0) 0x4385b0. Set by whoever owns the style; display-format colours. */
void font_set_hud_cluts(const uint32_t *const aux[8], const uint32_t *sprite0);

/* HUD_DrawText 0x483df0: current font, palette aux[alt != 0], at linear offset dst. */
void hud_draw_text(Surface *s, const char *text, long dst, bool alt);
/* HUD_DrawCenteredLine 0x4837a0: the sub font centred on the surface width at row y, palette aux[0]. */
void hud_draw_centered_line(Surface *s, const char *text, int y);
/* HUD_DrawTextClipped 0x4854a0: the pixels skip .. skip + max_w of the string (pager scrolling). The
   palette follows the current font: street/sub aux[0], pager aux[3], others sprite0. */
void hud_draw_text_clipped(Surface *s, const char *text, long dst, int skip, int max_w);
/* HUD_DrawTextMultiline 0x485910: '\n' starts a new line height - 1 rows lower; palette aux[colour != 0]. */
void hud_draw_text_multiline(Surface *s, const char *text, const Font *f, long dst, int colour);
