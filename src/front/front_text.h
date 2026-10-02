/* The frontend font set (Front_LoadFonts 0x42d2e0) and the frontend's text layout helpers (menu items,
   headings, credit lines: 0x42c030-0x42cf70). */
#pragma once
#include "font.h"

typedef struct {
    Font *cuttext;          /* 0x511578: CUTTEXT.FON, cutscene text, 140 glyphs from 0x21 */
    Font *mhead;            /* 0x511570: F_MHEAD.FON, headings and the main menu items */
    Font *mmiss;            /* 0x51157c: F_MMISS.FON, small text */
    Font *mtext;            /* 0x511568: F_MTEXT.FON, menu text */
    Font *key;              /* 0x511574: F_KEY.FON, the 13 frames of the selection marker (codes 1..13) */
    Font *city[4];          /* 0x511154..: F_CITY1..4.FON, the city logo animations (codes 1..) */
} FrontFonts;
extern FrontFonts front_fonts;

/* Front_LoadFonts 0x42d2e0: all with their palettes; F8_CITYn.FON instead of F_CITYn.FON is the dead
   8-bit path. False if one is missing. */
bool front_load_fonts(char *err, size_t errcap);
void front_free_fonts(void);                 /* Front_FreeFonts 0x42d410 */

/* Front_DrawMenuItem 0x42c340: item n (1-based) of a list in the heading font at x 196, rows
   mhead height apart from y 216; the selected item gets the animated key marker at (116, y - 16). */
void front_draw_menu_item(Surface *s, int item, int selected, const char *text);
/* Front_DrawMenuItemB 0x42c6b0 / C 0x42ca20: the same in the menu text font from y 224, marker at
   y - 24 (C selects the font without the Japanese substitution). Each variant has its own marker frame. */
void front_draw_menu_item_b(Surface *s, int item, int selected, const char *text);
void front_draw_menu_item_c(Surface *s, int item, int selected, const char *text);
/* Front_DrawTitle 0x42cd90 / Front_DrawTitleAlt 0x42cf70: centred on x 320 at row y, heading / menu
   text font. */
void front_draw_title(Surface *s, int y, const char *text);
void front_draw_title_alt(Surface *s, int y, const char *text);
/* Font_DrawCreditLine 0x42c030: a line of the credits scroll at (x, y), cut to the clip rectangle's
   rows (gfx_set_clip_rect). */
void font_draw_credit_line(Surface *s, const Font *f, int x, int y, const char *text);
