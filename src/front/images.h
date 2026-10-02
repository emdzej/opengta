/* Frontend images (0x42d490-0x42dc40): headerless .RAW pictures, the 640x480 backdrop made of an
   animated logo strip over a lower panel, cutscene stills and the player-select pictures. Layout:
   docs/text-fonts.md. */
#pragma once
#include "surface.h"
#include <stdbool.h>
#include <stdint.h>

/* {w, h, pixels} (12 bytes in the original); pixels in the display format, w * h of them. */
typedef struct {
    int w, h;
    uint32_t *px;
} Image;

/* An 8-bit .RAT picture (palette indices): the VESA-mode-0x101 path, dead in the Windows build. */
typedef struct {
    int w, h;
    uint8_t *px;
} Image8;

void gfx_set_clip_rect(int x0, int y0, int x1, int y1);   /* Gfx_SetClipRect 0x42d590 */
void gfx_get_clip_rect(int *x0, int *y0, int *x1, int *y1);
bool gfx_image_alloc(Image *im, int w, int h);           /* Gfx_ImageAlloc 0x42d5c0 */
void gfx_image_free(Image *im);                          /* Gfx_ImageFree 0x42d7e0 */
/* Gfx_LoadRawImage 0x42d5f0: "<name>.RAW", w * h R, G, B triples, into an allocated image (invert only
   applies to the 8-bit path). False if the file is missing or short (the original: fatal -202/-203). */
bool gfx_load_raw_image(Image *im, const char *name, bool invert);
/* The 8-bit branch of Gfx_LoadRawImage: "<name>.RAT", w * h indices, each replaced by 255 - v when
   invert is set. im->w / im->h set by the caller. */
bool gfx_load_rat_image(Image8 *im, const char *name, bool invert);
/* Not in the Windows exe: a 768-byte RGB palette (F_PAL.RAW for the F_*.RAT pictures, CUTn.ACT for the
   CUTn.RAT stills; DOS leftovers) to look at .RAT pictures. */
bool gfx_load_palette(const char *rel, uint32_t pal[256]);
/* Gfx_BlitImage 0x42d490: opaque copy at (x, y) inside the clip rectangle, with its quirks. */
void gfx_blit_image(Surface *s, int x, int y, const Image *im);

/* The frontend backdrop and stills (0x511930.. 0x511950). */
typedef struct {
    Image bg;               /* 0x511930: 640 x 480, cutscene still */
    Image upper;            /* 0x51193c: F_UPPER 640 x 168 */
    Image logo[8];          /* 0x5118a0: F_LOGO0..7 640 x 168 */
    Image lower[2];         /* 0x511908: F_LOWER0/1 640 x 312 */
    int logo_frame;         /* 0x511950 */
    bool clock_started;     /* bit 0 of 0x511898 */
    uint32_t logo_due;      /* 0x51189c: clock() of the next logo frame */
} FrontImages;
extern FrontImages front_images;

/* Front_LoadImages 0x42da60 (after Gfx_SetVideoMode(-1), the 640x480x16 frontend mode). */
bool front_load_images(void);
void front_free_images(void);                            /* Front_FreeImages 0x42dc40 */
/* Front_LoadCutsceneBg 0x42d810: "GTADATA/CUT0".."CUT5" into the 640x480 background. */
bool front_load_cutscene_bg(const char *name);
/* Front_ClearOrDrawBg 0x42d830: draw == 0 clears the surface, else copies the background. */
void front_clear_or_draw_bg(Surface *s, int draw);
/* Front_DrawBackground 0x42d8e0: rows 0-167 the logo (animate: frame advancing every 83 ms of
   clock_ms, the original's clock()) or F_UPPER, rows 168-479 F_LOWER<lower>. */
void front_draw_background(Surface *s, int animate, int lower, uint32_t clock_ms);

/* The player-select pictures, loaded once by Front_Enter 0x42b690 (flag 0x5110f8) and freed by
   Front_Leave 0x42b7a0. */
typedef struct {
    Image play[8];          /* 0x5106f0: F_PLAY1..8, 102 x 141 (path table 0x4a7390) */
    Image playn;            /* 0x510218: F_PLAYN 180 x 50 (name plate) */
    Image rstar;            /* 0x51027c: F_RSTAR 64 x 59 */
    Image rstarn;           /* 0x5106ac: F_RSTARN 64 x 59 */
    bool loaded;
} FrontPictures;
extern FrontPictures front_pictures;
bool front_load_pictures(void);
void front_free_pictures(void);
