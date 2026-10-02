/* State shared by the HUD's translation units (hud.c, pager.c). In the original these are globals of
   one module; the addresses are given with each. */
#pragma once
#include "../render/sprite.h"
#include "../surface.h"
#include <stdbool.h>
#include <stdint.h>

enum {
    PAGER_SLOTS = 16,          /* 0x77f280, 0x4e0 bytes each */
    PAGER_TEXT = 0x4b4,
    PAGER_MAX_LEN = 0x4b0,     /* longest pager text (fatal -0x82 past it for new messages) */
};

/* pager slot states (+0x4d0) */
enum { PAGER_SHOWING = 0, PAGER_FREE = 1, PAGER_CHAT_LOCAL = 3, PAGER_CHAT_REMOTE = 4, PAGER_COUNTDOWN = 5 };

typedef struct {
    char text[PAGER_TEXT];     /* +0x000: pager_cols spaces, then the message (UTF-8, mapped) */
    int32_t len;               /* +0x4b4 */
    int32_t width;             /* +0x4b8: pixels: where scrolling ends */
    int32_t scroll;            /* +0x4bc: pixels scrolled */
    int32_t chat_max;          /* +0x4c0: chat line: longest length */
    int32_t chat_min;          /* +0x4c4: chat line: shortest length (the prefix) */
    uint32_t seq;              /* +0x4c8: order of arrival */
    int32_t chat_to;           /* +0x4cc */
    int32_t state;             /* +0x4d0 */
    int32_t cd_id;             /* +0x4d4: countdown id */
    int16_t cd_value;          /* +0x4d8: countdown seconds */
    int8_t cd_tick;            /* +0x4da: frames to the next second (25) */
    int16_t cd_max;            /* +0x4dc: countdown: scrolling stops here */
} PagerSlot;

typedef struct {
    PagerSlot slot[PAGER_SLOTS];
    int cur;                   /* 0x78425c shown slot (-1 none) */
    int last_shown;            /* 0x784764 for Pager_Resume (-1 none) */
    int last_added;            /* 0x78473c (-1 none) */
    uint32_t seq;              /* 0x7850fc */
    bool visible;              /* 0x77e7a5 (HUD_IsPagerBusy) */
    bool update;               /* 0x7850f4 Pager_Update runs */
    bool blink_on;             /* 0x77e7ac the light blinks */
    bool light;                /* 0x77ec74 the light is lit */
    int blink;                 /* 0x7850f0 frames to the next blink */
    int height;                /* 0x784768 pager sprite height (set when drawn) */
    int speed_sub;             /* 0x4b3610 subtitle duration divisor 0, 1, 2, 4 (initial value from the exe) */
    int speed_px;              /* 0x4b3614 pixels scrolled per frame 1, 1, 2, 3 */
} Pager;
extern Pager g_pager;

/* The frame's drawing state, set up by hud_draw. */
typedef struct {
    Surface s;                 /* the back buffer (0x503228 rows, pitch 0x503220), w = view width */
    int view_w, view_h;        /* 0x5c0c00, 0x5bfab0 (the render camera's) */
    int res;                   /* 0x785160: 1 or 2 */
} HudFrame;
extern HudFrame g_hud_frame;
extern int g_hud_screen_w, g_hud_screen_h;   /* 0x785170, 0x785174 (HUD_SetViewSize) */

/* HUD sprite n: arrow group base 0x774efa + 0x785120 (0x18 at res 2) + 0x785124 (1 at res 2) + n */
const SpriteInfo *hud_sprite(int n);
/* Tile_SelectAux 0x437730: the style's font palette n (both the rasteriser's paged CLUT, for
   Poly_DrawRect, and the linear copy font.c's HUD renderers take); returns the linear one. */
const uint32_t *hud_select_aux(int n);
_Noreturn void hud_fatal(int code, int line, int arg);
const char *hud_text(const char *key);       /* Text_Get 0x47d9a0 (fatal -0x55 if missing) */
int hud_player_colour(int n);                 /* Player_GetColourIndex 0x461710: table 0x4b21dc */

void pager_update(void);                      /* Pager_Update 0x486430 */
void hud_draw_pager(Surface *s);              /* HUD_DrawPager 0x485c30 */
void pager_hud_init(void);                    /* the pager part of HUD_Init (same as Pager_Reset) */
