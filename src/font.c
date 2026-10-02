/* .FON fonts (0x4304a0-0x430970), the frontend string renderers (0x42b800-0x42d1c0) and the HUD
   string renderers (0x4837a0-0x485910). */
#include "font.h"
#include "exe.h"
#include "text.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const Font *cur;                 /* 0x513228 */
static const uint32_t *front_pal;       /* 0x51156c */
static const uint32_t *hud_aux[8];      /* 0x5c2c4c */
static const uint32_t *hud_sprite0;     /* Tile_SelectSprite(0) */
HudFonts hud_fonts;

/* ---------------------------------------------------------------- loading */

/* Font_Load 0x4304d0. The original allocates the 0x80c-byte record, reads with fread: u8 count, u8
   height, then per glyph u8 width and width * height bytes; with a palette, 768 bytes of R, G, B follow
   and become 256 display-format colours (Font_Load converts them with the surface's channel shifts; the
   8-bit VESA path, palette >> 2, is dead in the Windows build). Every .FON file carries the palette;
   the HUD fonts are loaded without it. */
Font *font_load(const char *rel, uint16_t first, bool with_palette, char *err, size_t errcap)
{
    size_t n;
    uint8_t *d = vfs_read_all(rel, &n);
    if (!d) {
        snprintf(err, errcap, "cannot open '%s'", rel);
        return NULL;
    }
    Font *f = calloc(1, sizeof *f);
    size_t o = 2;
    if (!f || n < 2) goto short_file;
    f->count = d[0];
    f->height = d[1];
    f->first = first;
    for (int i = 0; i < f->count; i++) {
        if (o + 1 > n) goto short_file;
        f->glyph[i].w = d[o++];
        size_t sz = (size_t)f->glyph[i].w * f->height;
        if (o + sz > n) goto short_file;
        f->glyph[i].px = malloc(sz ? sz : 1);
        if (!f->glyph[i].px) goto short_file;
        memcpy(f->glyph[i].px, d + o, sz);
        o += sz;
    }
    if (with_palette) {
        if (o + 0x300 > n || !(f->pal = malloc(256 * sizeof *f->pal))) goto short_file;
        for (int i = 0; i < 256; i++) f->pal[i] = surface_rgb(d[o + 3 * i], d[o + 3 * i + 1], d[o + 3 * i + 2]);
    }
    free(d);
    return f;
short_file:
    snprintf(err, errcap, "cannot read data from '%s'", rel);
    free(d);
    font_free(f);
    return NULL;
}

/* Font_Free 0x430750 (the kanji branch is not ported). */
void font_free(Font *f)
{
    if (!f) return;
    for (int i = 0; i < 256; i++) free(f->glyph[i].px);
    free(f->pal);
    if (cur == f) cur = NULL;
    free(f);
}

/* Font_Select 0x4307c0: in Japanese mode (text_wide) a text font (first code 0x21) is replaced by the
   kanji font, with a per-font colour; that font (Font_InitKanji 0x4304a0, KANJI.IDX / KANJI.BIT) isn't
   shipped with the 2002 release and is not ported, so the font is used as is. */
void font_select(const Font *f) { cur = f; }
void font_select_raw(const Font *f) { cur = f; }
const Font *font_current(void) { return cur; }

/* Codes >= 0x80 go through the u16 table at 0x4b0980 (indexed by code; it lies right after the string
   "..\gtadata\cuttext.fon" 0x4b0974; Latin-1 letters map to the accented glyphs 0x80..0xac of the
   frontend and HUD fonts, the rest to 0). Read from the exe; 0 without it. */
static uint16_t glyph_remap(uint16_t c) { return exe_u16(0x4b0980 + 2u * c); }

static int glyph_index(const Font *f, unsigned code)
{
    int i = (int)code - (int)f->first;
    return i < 0 || i >= f->count ? 0 : i;
}

/* Font_GlyphWidth 0x430850: a space is as wide as 'n' (except in fonts whose first code is 1); codes
   outside the font are glyph 0. */
int font_glyph_width(uint16_t c)
{
    if (!cur) return 0;
    unsigned code = c;
    if (c < 0x80) {
        if (cur->first != 1 && c == 0x20) code = 'n';
    } else
        code = glyph_remap(c);
    return cur->glyph[glyph_index(cur, code)].w;
}

/* Font_GlyphPixels 0x4308d0 (no space substitution: callers don't draw spaces). */
const uint8_t *font_glyph_pixels(uint16_t c)
{
    if (!cur) return NULL;
    unsigned code = c > 0x7f ? glyph_remap(c) : c;
    return cur->glyph[glyph_index(cur, code)].px;
}

int font_height(void) { return cur ? cur->height : 0; }     /* Font_Height 0x430930 */
bool font_is_kanji(void) { return cur && cur->kanji == 1; } /* Font_IsKanji 0x430960 */

/* ---------------------------------------------------------------- frontend renderers */

void font_set_front_palette(const uint32_t *pal) { front_pal = pal; }

/* Blit_Copy8to16_640 0x4898ac / Blit_Copy8to16Clip_640 0x489904: the CLUT blitters with the frontend
   palette and a fixed pitch of 640 pixels (the frontend surface; here the surface stride). */
static void front_blit(Surface *s, long dst, const uint8_t *src, int w, int h)
{
    if (src && front_pal) blit_sprite32(s, dst, src, w, h, front_pal);
}
static void front_blit_clip(Surface *s, long dst, const uint8_t *src, int w, int h, int l, int r)
{
    if (src && front_pal) blit_sprite_clip32(s, dst, src, w, h, l, r, front_pal);
}

static void draw_string(Surface *s, const Font *f, int x, int y, const char *text, bool raw)
{
    long dst = surface_offset(s, x, y);
    front_pal = f->pal;
    if (raw) font_select_raw(f);
    else font_select(f);
    int h = font_height(), px = 0;
    while (*text) {
        uint16_t c = text_utf8_next(&text);
        int w = font_glyph_width(c);
        if (c != 0x20) front_blit(s, dst + px, font_glyph_pixels(c), w, h);
        px += w;
    }
}

/* Font_DrawString 0x42bad0 */
void font_draw_string(Surface *s, const Font *f, int x, int y, const char *text) { draw_string(s, f, x, y, text, false); }
/* Font_DrawStringAlt 0x42bc70 */
void font_draw_string_alt(Surface *s, const Font *f, int x, int y, const char *text) { draw_string(s, f, x, y, text, true); }

static int string_width(const char *text)
{
    int w = 0;
    while (*text) w += font_glyph_width(text_utf8_next(&text));
    return w;
}
/* Font_StringWidth 0x42c0e0 */
int font_string_width(const Font *f, const char *text) { font_select(f); return string_width(text); }
/* Font_StringWidthAlt 0x42c120 */
int font_string_width_alt(const Font *f, const char *text) { font_select_raw(f); return string_width(text); }

/* Font_DrawCentered 0x42c160 */
void font_draw_centered(Surface *s, const Font *f, const char *text, int x, int y, int box_w)
{
    int off = (box_w - font_string_width(f, text)) / 2;
    if (off < 0) off = 0;
    draw_string(s, f, x + off, y, text, false);
}

/* Font_DrawStringClipped 0x42be10. Walks to the glyph that contains pixel `skip`, draws its visible
   right part at dst, then whole glyphs while they fit in max_w, then the visible left part of the one
   that crosses max_w. Glyph rows start at row0 (the vertical clip of Font_DrawCreditLine). A negative
   skip makes the original step back before the start of the string; that draws nothing here. */
void font_draw_string_clipped(Surface *s, const char *text, long dst, int skip, int max_w, int h, int row0)
{
    const char *p = text;
    int acc = 0, x = 0;
    if (!*p || skip < 0) return;
    do {
        if (!*p) break;
        acc += font_glyph_width(text_utf8_next(&p));
    } while (acc <= skip);
    text_utf8_back(&p);
    uint16_t c = text_utf8_peek(p);
    int w = font_glyph_width(c);
    int cut = w - acc + skip;           /* hidden columns of this glyph */
    if (cut >= w) return;               /* the string ends before skip */
    if (cut != 0) {
        if (c != 0x20) {
            const uint8_t *g = font_glyph_pixels(c);
            if (g) front_blit_clip(s, dst, g + (long)w * row0, w, h, cut, 0);
        }
        x = w - cut;
        text_utf8_skip(&p);
    }
    while (*p) {
        c = text_utf8_next(&p);
        w = font_glyph_width(c);
        const uint8_t *g = font_glyph_pixels(c);
        if (x + w > max_w) {
            int clip_r = w - max_w + x;
            if (clip_r != w && c != 0x20 && g) front_blit_clip(s, dst + x, g + (long)w * row0, w, h, 0, clip_r);
            return;
        }
        if (c != 0x20 && g) front_blit(s, dst + x, g + (long)w * row0, w, h);
        x += w;
    }
}

/* Text_WordWrap 0x42b800. Copies text code by code into the static buffer 0x511168, accumulating the
   line width with the current font ('n' for a space). '\n' and '@' (written as '\n') start a new line.
   When a code makes the line wider than width: if the line had a space, that space becomes '\n' and
   the copy resumes after it (the rest of the line is copied again); otherwise '\n' is inserted before
   the code, which starts the new line. The byte count that guards the buffer (fatal -289 "String is too
   long for text wrap" beyond 0x3ff) counts every byte written, re-copied ones included; here the copy
   stops there. */
static char wrap_buf[0x400 + 8];

static int utf8_len(uint8_t b) { return b < 0x80 ? 1 : (b & 0x20) ? 3 : 2; }

const char *text_word_wrap(const char *text, int width, int *lines)
{
    const uint8_t *in = (const uint8_t *)text, *sp_in = NULL;
    uint8_t *out = (uint8_t *)wrap_buf, *sp_out = NULL;
    int x = 0, used = 0;
    *lines = 1;
    for (;;) {
        uint16_t c = text_utf8_peek((const char *)in);
        if (c == 0) {
            *out = 0;
            return wrap_buf;
        }
        if (in[0] & 0x80) {
            if (in[0] & 0x20) out[2] = in[2];
            out[1] = in[1];
        }
        out[0] = in[0];
        if (c == '\n' || c == '@') {
            if (c == '@') *out = '\n';
            x = 0;
            sp_in = NULL;
            sp_out = NULL;
            ++*lines;
        } else if (c == ' ') {
            x += font_glyph_width('n');
            sp_in = in;
            sp_out = out;
        } else
            x += font_glyph_width(c);
        if (width < x) {
            x = 0;
            if (!sp_in || !sp_out) {
                out[0] = '\n';
                used++;
                x = font_glyph_width(text_utf8_peek((const char *)in));
                if (in[0] & 0x80) {
                    if (in[0] & 0x20) out[3] = in[2];
                    out[2] = in[1];
                }
                out[1] = in[0];
                out++;
            } else {
                *sp_out = '\n';
                in = sp_in;
                out = sp_out;
                sp_in = NULL;
                sp_out = NULL;
            }
            ++*lines;
        }
        in += utf8_len(in[0]);
        int l = utf8_len(out[0]);
        out += l;
        used += l;
        if (used > 0x3ff) {
            wrap_buf[0x3ff] = 0;
            return wrap_buf;
        }
    }
}

/* Font_DrawWrappedLines 0x42d1c0: draws the wrapped text, lines `height` rows apart. */
static void draw_wrapped_lines(Surface *s, const char *text, const Font *f, long dst, int width)
{
    front_pal = f->pal;
    font_select(f);
    int h = font_height(), lines, x = 0;
    long row = 0;
    const char *p = text_word_wrap(text, width, &lines);
    while (*p) {
        uint16_t c = text_utf8_next(&p);
        int w = font_glyph_width(c);
        if (c == '\n') {
            x = 0;
            row += (long)s->stride * h;
        } else if (c == 0x20)
            x += font_glyph_width('n');
        else {
            front_blit(s, dst + x + row, font_glyph_pixels(c), w, h);
            x += w;
        }
    }
}

/* Font_DrawWrapped 0x42d150 */
void font_draw_wrapped(Surface *s, const Font *f, int x, int y, int width, const char *text)
{
    draw_wrapped_lines(s, text, f, surface_offset(s, x, y), width);
}

/* ---------------------------------------------------------------- HUD fonts */

static bool load_hud(Font **f, const char *fmt, int res, uint16_t first, char *err, size_t errcap)
{
    char rel[40];
    snprintf(rel, sizeof rel, fmt, res);
    return (*f = font_load(rel, first, false, err, errcap)) != NULL;
}

void font_hud_free(void)
{
    Font **all[] = {&hud_fonts.big, &hud_fonts.sub, &hud_fonts.street, &hud_fonts.pager,
                    &hud_fonts.expscor, &hud_fonts.score, &hud_fonts.missmul};
    for (size_t i = 0; i < sizeof all / sizeof *all; i++) {
        font_free(*all[i]);
        *all[i] = NULL;
    }
    hud_fonts.res = 0;
}

/* HUD_LoadFonts 0x481030, the fonts: "..\gtadata\big%d.fon" 0x4b370c, sub 0x4b36f4, street 0x4b36dc,
   pager 0x4b36c4 (first code 0x21), expscor 0x4b36a8, score 0x4b3690, missmul 0x4b3674 (first 0, the
   digit fonts), all without palette. The pager font is left selected. */
bool font_hud_load(int res, char *err, size_t errcap)
{
    if (res != 1 && res != 2) {
        snprintf(err, errcap, "invalid case %d", res);   /* fatal -74 */
        return false;
    }
    if (res == hud_fonts.res) return true;
    font_hud_free();
    if (!load_hud(&hud_fonts.big, "GTADATA/BIG%d.FON", res, 0x21, err, errcap) ||
        !load_hud(&hud_fonts.sub, "GTADATA/SUB%d.FON", res, 0x21, err, errcap) ||
        !load_hud(&hud_fonts.street, "GTADATA/STREET%d.FON", res, 0x21, err, errcap) ||
        !load_hud(&hud_fonts.pager, "GTADATA/PAGER%d.FON", res, 0x21, err, errcap))
        return false;
    hud_fonts.res = res;
    font_select(hud_fonts.pager);
    int n = font_glyph_width('n');
    hud_fonts.pager_cols = n ? res * 0x3a / n : 0;
    if (!load_hud(&hud_fonts.expscor, "GTADATA/EXPSCOR%d.FON", res, 0, err, errcap) ||
        !load_hud(&hud_fonts.score, "GTADATA/SCORE%d.FON", res, 0, err, errcap) ||
        !load_hud(&hud_fonts.missmul, "GTADATA/MISSMUL%d.FON", res, 0, err, errcap))
        return false;
    return true;
}

/* ---------------------------------------------------------------- HUD renderers */

void font_set_hud_cluts(const uint32_t *const aux[8], const uint32_t *sprite0)
{
    for (int i = 0; i < 8; i++) hud_aux[i] = aux ? aux[i] : NULL;
    hud_sprite0 = sprite0;
}

static void hud_blit(Surface *s, long dst, uint16_t c, int w, int h, const uint32_t *clut)
{
    const uint8_t *g = font_glyph_pixels(c);
    if (g && clut) blit_sprite32(s, dst, g, w, h, clut);
}

/* HUD_DrawText 0x483df0: zero-width glyphs and spaces are skipped. */
void hud_draw_text(Surface *s, const char *text, long dst, bool alt)
{
    int h = font_height(), x = 0;
    const uint32_t *clut = hud_aux[alt ? 1 : 0];   /* Tile_SelectAux 0x437730 */
    while (*text) {
        uint16_t c = text_utf8_next(&text);
        int w = font_glyph_width(c);
        if (c != 0x20 && w != 0) hud_blit(s, dst + x, c, w, h, clut);
        x += w;
    }
}

/* HUD_DrawCenteredLine 0x4837a0: centred on the view width (0x5c0c00), the surface width here. (The
   kanji colour override is not ported.) */
void hud_draw_centered_line(Surface *s, const char *text, int y)
{
    if (!hud_fonts.sub) return;
    int off = (s->w - font_string_width(hud_fonts.sub, text)) / 2;
    long base = surface_offset(s, 0, y) + off;
    font_select(hud_fonts.sub);
    int h = font_height(), x = 0;
    const uint32_t *clut = hud_aux[0];
    while (*text) {
        uint16_t c = text_utf8_next(&text);
        int w = font_glyph_width(c);
        if (c != 0x20 && w != 0) hud_blit(s, base + x, c, w, h, clut);
        x += w;
    }
}

/* HUD_DrawTextClipped 0x4854a0: the algorithm of Font_DrawStringClipped (above) with the HUD's palettes
   and blitters (Blit_SpriteClip 0x489789 / Blit_Sprite 0x4897d3), skipping zero-width glyphs. */
void hud_draw_text_clipped(Surface *s, const char *text, long dst, int skip, int max_w)
{
    const Font *f = font_current();
    const uint32_t *clut;
    if (f == hud_fonts.street || f == hud_fonts.sub) clut = hud_aux[0];   /* or the kanji font */
    else if (f == hud_fonts.pager) clut = hud_aux[3];
    else clut = hud_sprite0;
    int h = font_height();
    const char *p = text;
    int acc = 0, x = 0;
    if (!*p || skip < 0 || !clut) return;
    do {
        if (!*p) break;
        acc += font_glyph_width(text_utf8_next(&p));
    } while (acc <= skip);
    text_utf8_back(&p);
    uint16_t c = text_utf8_peek(p);
    int w = font_glyph_width(c);
    int cut = w - acc + skip;
    if (cut >= w) return;
    if (cut != 0) {
        const uint8_t *g = font_glyph_pixels(c);
        if (c != 0x20 && w != 0 && g) blit_sprite_clip32(s, dst, g, w, h, cut, 0, clut);
        x = w - cut;
        text_utf8_skip(&p);
    }
    while (*p) {
        c = text_utf8_next(&p);
        w = font_glyph_width(c);
        const uint8_t *g = font_glyph_pixels(c);
        if (max_w < w + x) {
            int clip_r = w - max_w + x;
            if (clip_r != w && c != 0x20 && w != 0 && g) blit_sprite_clip32(s, dst + x, g, w, h, 0, clip_r, clut);
            return;
        }
        if (c != 0x20 && w != 0 && g) blit_sprite32(s, dst + x, g, w, h, clut);
        x += w;
    }
}

/* HUD_DrawTextMultiline 0x485910. A zero-width glyph doesn't advance (the others do after drawing). */
void hud_draw_text_multiline(Surface *s, const char *text, const Font *f, long dst, int colour)
{
    font_select(f);
    int h = font_height(), x = 0;
    const uint32_t *clut = hud_aux[colour != 0];
    long line = dst;
    while (*text) {
        uint16_t c = text_utf8_next(&text);
        if (c == '\n') {
            x = 0;
            line += (long)(h - 1) * s->stride;
        } else if (c == 0x20)
            x += font_glyph_width(0x20);
        else {
            int w = font_glyph_width(c);
            if (w != 0) {
                hud_blit(s, line + x, c, w, h, clut);
                x += w;
            }
        }
    }
}
