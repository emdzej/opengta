/* Frontend images (0x42d490-0x42dc40) and the picture loading of Front_Enter 0x42b690. */
#include "front/images.h"
#include "render/hires/hires_front.h"
#include "render/hires/skin_ui.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FrontImages front_images;
FrontPictures front_pictures;
static int clip_x0, clip_y0, clip_x1, clip_y1;   /* 0x511920 0x511924 0x511928 0x51192c */

enum { FRONT_W = 640, FRONT_H = 480, UPPER_H = 168, LOWER_H = 312 };

/* Gfx_SetClipRect 0x42d590 */
void gfx_set_clip_rect(int x0, int y0, int x1, int y1)
{
    clip_x0 = x0;
    clip_y0 = y0;
    clip_x1 = x1;
    clip_y1 = y1;
}

void gfx_get_clip_rect(int *x0, int *y0, int *x1, int *y1)
{
    *x0 = clip_x0;
    *y0 = clip_y0;
    *x1 = clip_x1;
    *y1 = clip_y1;
}

/* Gfx_ImageAlloc 0x42d5c0: w * h pixels of the frontend's bytes per pixel (0x511900). */
bool gfx_image_alloc(Image *im, int w, int h)
{
    im->w = w;
    im->h = h;
    im->px = calloc((size_t)w * h, sizeof *im->px);
    return im->px != NULL;
}

/* Gfx_ImageFree 0x42d7e0 */
void gfx_image_free(Image *im)
{
    im->w = im->h = 0;
    free(im->px);
    im->px = NULL;
}

/* Gfx_LoadRawImage 0x42d5f0, the 16 bpp branch (the one the Windows build takes: 0x511590 is 0x110 or
   0x111): "%s.raw" (0x4b098c) read with fgetc as R, G, B per pixel, each pixel converted to the
   display format. The size is the caller's (the file has no header). */
bool gfx_load_raw_image(Image *im, const char *name, bool invert)
{
    (void)invert;   /* the 16 bpp branch ignores it */
    char rel[64];
    snprintf(rel, sizeof rel, "%s.RAW", name);
    VfsFile *f = vfs_open(rel);
    if (!f) return false;
    size_t n = (size_t)im->w * im->h;
    uint8_t *rgb = malloc(n * 3 + 1);
    bool ok = rgb && im->px && vfs_read_at(f, 0, rgb, n * 3) == (int64_t)(n * 3);
    vfs_close(f);
    if (ok)
        for (size_t i = 0; i < n; i++) im->px[i] = surface_rgb(rgb[3 * i], rgb[3 * i + 1], rgb[3 * i + 2]);
    free(rgb);
    skin_ui_picture_loaded(im, name);   /* the port: the hires renderer's name for it, and its copies stale */
    hires_front_image_changed(im);
    return ok;
}

/* Gfx_LoadRawImage, the 8-bit branch (VESA mode 0x101, never set by Gfx_SetVideoMode 0x414db0 in this
   build): "%s.rat" (0x4b0994) read with one fread, then optionally every byte v -> 255 - v (only the
   cutscene stills ask for it, Front_LoadCutsceneBg). */
bool gfx_load_rat_image(Image8 *im, const char *name, bool invert)
{
    char rel[64];
    snprintf(rel, sizeof rel, "%s.RAT", name);
    VfsFile *f = vfs_open(rel);
    if (!f) return false;
    size_t n = (size_t)im->w * im->h;
    free(im->px);
    im->px = malloc(n + 1);
    bool ok = im->px && vfs_read_at(f, 0, im->px, n) == (int64_t)n;
    vfs_close(f);
    if (ok && invert)
        for (size_t i = 0; i < n; i++) im->px[i] = (uint8_t)(255 - im->px[i]);
    return ok;
}

bool gfx_load_palette(const char *rel, uint32_t pal[256])
{
    size_t n;
    uint8_t *d = vfs_read_all(rel, &n);
    if (!d || n < 768) {
        free(d);
        return false;
    }
    for (int i = 0; i < 256; i++) pal[i] = surface_rgb(d[3 * i], d[3 * i + 1], d[3 * i + 2]);
    free(d);
    return true;
}

/* Gfx_BlitImage 0x42d490. Drawn only if x < clip_x1 and x + w >= clip_x0. Quirks kept:
   - left of clip_x0, the visible part (columns clip_x0 - x ..) goes to column 0 of the row, not to
     clip_x0, and the right edge is then not clipped (the frontend's clip_x0 is always 0, so both agree);
   - otherwise the copy stops at column clip_x1 - 1 when x + w >= clip_x1: with the usual clip of
     0..0x27f an image reaching the right edge loses column 639;
   - rows are drawn for clip_y0 <= row < clip_y1 (row 479 is never drawn with 0..0x1df). */
void gfx_blit_image(Surface *s, int x, int y, const Image *im)
{
    if (!im->px || !(x < clip_x1 && clip_x0 <= im->w + x)) return;
    long dst;
    int src, n;
    if (x < clip_x0) {
        dst = surface_offset(s, 0, y);
        src = clip_x0 - x;
        n = im->w - clip_x0 + x;
    } else {
        n = im->w;
        if (clip_x1 <= im->w + x) n = clip_x1 - x;
        dst = surface_offset(s, x, y);
        src = 0;
    }
    if (hires_ui_recording)   /* the port: the hires renderer's copy of the frontend */
        hires_front_image(im, x < clip_x0 ? 0 : x, y, src, 0, n, im->h, clip_y0, clip_y1);
    for (int row = y; row < im->h + y; row++, dst += s->stride, src += im->w)
        if (clip_y0 <= row && row < clip_y1)
            for (int i = 0; i < n; i++) surface_put(s, dst + i, im->px[src + i]);
}

/* ---------------------------------------------------------------- the backdrop */

static bool load_raw(Image *im, int w, int h, const char *name)
{
    return gfx_image_alloc(im, w, h) && gfx_load_raw_image(im, name, false);
}

/* Front_LoadImages 0x42da60: clip rectangle 0,0..0x27f,0x1df; the 640x480 background is allocated
   (filled later by Front_LoadCutsceneBg), then F_UPPER and F_LOGO0..7 (640 x 168) and F_LOWER0/1
   (640 x 312) are loaded ("..\gtadata\f_upper" 0x4b0a64 .. "..\gtadata\f_lower1" 0x4b099c). */
bool front_load_images(void)
{
    FrontImages *fi = &front_images;
    gfx_set_clip_rect(0, 0, 0x27f, 0x1df);
    bool ok = gfx_image_alloc(&fi->bg, FRONT_W, FRONT_H);
    ok = ok && load_raw(&fi->upper, FRONT_W, UPPER_H, "GTADATA/F_UPPER");
    for (int i = 0; i < 8 && ok; i++) {
        char name[32];
        snprintf(name, sizeof name, "GTADATA/F_LOGO%d", i);
        ok = load_raw(&fi->logo[i], FRONT_W, UPPER_H, name);
    }
    ok = ok && load_raw(&fi->lower[0], FRONT_W, LOWER_H, "GTADATA/F_LOWER0");
    ok = ok && load_raw(&fi->lower[1], FRONT_W, LOWER_H, "GTADATA/F_LOWER1");
    return ok;
}

/* Front_FreeImages 0x42dc40 (it also resets the video mode id 0x511590). */
void front_free_images(void)
{
    FrontImages *fi = &front_images;
    gfx_image_free(&fi->bg);
    gfx_image_free(&fi->upper);
    for (int i = 0; i < 8; i++) gfx_image_free(&fi->logo[i]);
    for (int i = 0; i < 2; i++) gfx_image_free(&fi->lower[i]);
}

/* Front_LoadCutsceneBg 0x42d810 (names from the table 0x4af524: "..\gtadata\cut0".."cut5"). */
bool front_load_cutscene_bg(const char *name)
{
    FrontImages *fi = &front_images;
    if (!fi->bg.px && !gfx_image_alloc(&fi->bg, FRONT_W, FRONT_H)) return false;
    return gfx_load_raw_image(&fi->bg, name, true);
}

/* Copies one 640-pixel frontend row (the original's `rep movsd` of bpp * 160 dwords). */
static void copy_row(Surface *s, int y, const uint32_t *src)
{
    long d = surface_offset(s, 0, y);
    for (int i = 0; i < FRONT_W; i++) surface_put(s, d + i, src ? src[i] : 0);
}

/* Front_ClearOrDrawBg 0x42d830: every row of the surface (0x785174), 640 pixels each. */
void front_clear_or_draw_bg(Surface *s, int draw)
{
    const Image *bg = &front_images.bg;
    if (hires_ui_recording) {   /* the port */
        if (draw == 0 || !bg->px) hires_front_fill(0, 0, FRONT_W, s->h, 0);
        else hires_front_image(bg, 0, 0, 0, 0, FRONT_W, s->h < bg->h ? s->h : bg->h, 0, s->h);
    }
    for (int y = 0; y < s->h; y++)
        copy_row(s, y, draw == 0 || !bg->px ? NULL : bg->px + (size_t)y * FRONT_W);
}

/* Front_DrawBackground 0x42d8e0. The first call starts the logo clock (clock() + 0x53, i.e. 83 ms at
   CLOCKS_PER_SEC 1000); with animate the current logo frame is drawn and, once the clock has passed
   the due time, the frame advances (0..7) and the next one is due 83 ms later. Without animate F_UPPER
   is drawn and the clock is left alone. */
void front_draw_background(Surface *s, int animate, int lower, uint32_t clock_ms)
{
    FrontImages *fi = &front_images;
    if (!fi->clock_started) {
        fi->clock_started = true;
        fi->logo_due = clock_ms + 0x53;
    }
    const Image *top = animate == 0 ? &fi->upper : &fi->logo[fi->logo_frame];
    const Image *bot = &fi->lower[lower & 1];   /* callers pass 0 or 1 (unchecked in the original) */
    if (hires_ui_recording) {   /* the port */
        if (top->px) hires_front_image(top, 0, 0, 0, 0, FRONT_W, UPPER_H, 0, FRONT_H);
        else hires_front_fill(0, 0, FRONT_W, UPPER_H, 0);
        if (bot->px) hires_front_image(bot, 0, UPPER_H, 0, 0, FRONT_W, LOWER_H, 0, FRONT_H);
        else hires_front_fill(0, UPPER_H, FRONT_W, LOWER_H, 0);
    }    for (int y = 0; y < UPPER_H; y++) copy_row(s, y, top->px ? top->px + (size_t)y * FRONT_W : NULL);
    if (animate != 0 && (int32_t)fi->logo_due < (int32_t)clock_ms) {
        if (++fi->logo_frame > 7) fi->logo_frame = 0;
        fi->logo_due = clock_ms + 0x53;
    }
    for (int y = UPPER_H; y < FRONT_H; y++)
        copy_row(s, y, bot->px ? bot->px + (size_t)(y - UPPER_H) * FRONT_W : NULL);
}

/* ---------------------------------------------------------------- player-select pictures */

/* Front_Enter 0x42b690, the pictures: once (flag 0x5110f8), F_PLAY1..8 at 102 x 141 (0x66 x 0x8d),
   F_PLAYN 180 x 50 (0xb4 x 0x32), F_RSTAR and F_RSTARN 64 x 59 (0x40 x 0x3b). */
bool front_load_pictures(void)
{
    FrontPictures *fp = &front_pictures;
    if (fp->loaded) return true;
    bool ok = true;
    for (int i = 0; i < 8 && ok; i++) {
        char name[32];
        snprintf(name, sizeof name, "GTADATA/F_PLAY%d", i + 1);
        ok = load_raw(&fp->play[i], 0x66, 0x8d, name);
    }
    ok = ok && load_raw(&fp->playn, 0xb4, 0x32, "GTADATA/F_PLAYN");
    ok = ok && load_raw(&fp->rstar, 0x40, 0x3b, "GTADATA/F_RSTAR");
    ok = ok && load_raw(&fp->rstarn, 0x40, 0x3b, "GTADATA/F_RSTARN");
    fp->loaded = true;
    return ok;
}

/* Front_Leave 0x42b7a0, the pictures. */
void front_free_pictures(void)
{
    FrontPictures *fp = &front_pictures;
    if (!fp->loaded) return;
    for (int i = 0; i < 8; i++) gfx_image_free(&fp->play[i]);
    gfx_image_free(&fp->playn);
    gfx_image_free(&fp->rstar);
    gfx_image_free(&fp->rstarn);
    fp->loaded = false;
}
