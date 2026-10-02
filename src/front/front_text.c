/* Frontend fonts and text layout helpers (0x42c030-0x42d470). */
#include "front/front_text.h"
#include "front/images.h"
#include <stdio.h>

FrontFonts front_fonts;
static int marker_a, marker_b, marker_c;    /* 0x511584 0x511588 0x51158c */

/* Front_LoadFonts 0x42d2e0 ("..\gtadata\cuttext.fon" 0x4b0974 .. "f_city4.fon" 0x4b0854). In Japanese
   mode the kanji font is set up first (not ported). */
bool front_load_fonts(char *err, size_t errcap)
{
    FrontFonts *ff = &front_fonts;
    if (!(ff->cuttext = font_load("GTADATA/CUTTEXT.FON", 0x21, true, err, errcap)) ||
        !(ff->mhead = font_load("GTADATA/F_MHEAD.FON", 0x21, true, err, errcap)) ||
        !(ff->mmiss = font_load("GTADATA/F_MMISS.FON", 0x21, true, err, errcap)) ||
        !(ff->mtext = font_load("GTADATA/F_MTEXT.FON", 0x21, true, err, errcap)) ||
        !(ff->key = font_load("GTADATA/F_KEY.FON", 1, true, err, errcap)))
        return false;
    for (int i = 0; i < 4; i++) {
        char rel[32];
        snprintf(rel, sizeof rel, "GTADATA/F_CITY%d.FON", i + 1);
        if (!(ff->city[i] = font_load(rel, 1, true, err, errcap))) return false;
    }
    return true;
}

/* Front_FreeFonts 0x42d410 */
void front_free_fonts(void)
{
    FrontFonts *ff = &front_fonts;
    Font **all[] = {&ff->cuttext, &ff->mhead, &ff->mmiss, &ff->mtext, &ff->key,
                    &ff->city[0], &ff->city[1], &ff->city[2], &ff->city[3]};
    for (size_t i = 0; i < sizeof all / sizeof *all; i++) {
        font_free(*all[i]);
        *all[i] = NULL;
    }
}

/* The three menu-item variants inline Font_DrawString with fixed positions; the marker is a one-code
   string (frame + 1) in the key font, whose frame steps 0..12 on every call for the selected item. */
static void menu_item(Surface *s, const Font *f, bool raw, int top, int marker_dy, int *marker,
                      int item, int selected, const char *text)
{
    int y = f->height * (item - 1) + top;
    if (raw) font_draw_string_alt(s, f, 0xc4, y, text);
    else font_draw_string(s, f, 0xc4, y, text);
    if (item == selected) {
        if (++*marker > 0xc) *marker = 0;
        char m[2] = {(char)(*marker + 1), 0};
        if (front_fonts.key) font_draw_string(s, front_fonts.key, 0x74, y - marker_dy, m);
    }
}

void front_draw_menu_item(Surface *s, int item, int selected, const char *text)
{
    if (front_fonts.mhead) menu_item(s, front_fonts.mhead, false, 0xd8, 0x10, &marker_a, item, selected, text);
}
void front_draw_menu_item_b(Surface *s, int item, int selected, const char *text)
{
    if (front_fonts.mtext) menu_item(s, front_fonts.mtext, false, 0xe0, 0x18, &marker_b, item, selected, text);
}
void front_draw_menu_item_c(Surface *s, int item, int selected, const char *text)
{
    if (front_fonts.mtext) menu_item(s, front_fonts.mtext, true, 0xe0, 0x18, &marker_c, item, selected, text);
}

/* x = 320 - width / 2 (integer division of the width, as the original) */
void front_draw_title(Surface *s, int y, const char *text)
{
    const Font *f = front_fonts.mhead;
    if (f) font_draw_string(s, f, 0x140 - font_string_width(f, text) / 2, y, text);
}
void front_draw_title_alt(Surface *s, int y, const char *text)
{
    const Font *f = front_fonts.mtext;
    if (f) font_draw_string(s, f, 0x140 - font_string_width(f, text) / 2, y, text);
}

/* Font_DrawCreditLine 0x42c030: drawn if any of its rows is inside clip_y0..clip_y1; rows below
   clip_y1 are cut, rows above clip_y0 skipped (the line then starts at clip_y0). The horizontal window
   is the pitch (640) from x. */
void font_draw_credit_line(Surface *s, const Font *f, int x, int y, const char *text)
{
    int cx0, cy0, cx1, cy1;
    gfx_get_clip_rect(&cx0, &cy0, &cx1, &cy1);
    font_select_raw(f);
    font_set_front_palette(f->pal);
    int h = font_height(), row0 = 0;
    if (!(cy0 < h + y && y <= cy1)) return;
    if (cy1 + 1 < h + y) h = cy1 - y + 1;
    if (y < cy0) {
        row0 = cy0 - y;
        h -= row0;
        y = cy0;
    }
    font_draw_string_clipped(s, text, surface_offset(s, x, y), 0, s->stride, h, row0);
}
