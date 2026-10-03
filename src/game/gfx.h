/* The display modes (gfx module 0x414b10-0x415a00) as the game uses them: the mode list of the in-game
   video menu (F11, HUD_DrawVideoMenu), Gfx_SelectMode / Gfx_GetModeIndex. The original enumerates
   SciTech MGL's modes (Gfx_InitDrivers 0x415310: DirectDraw / packed drivers, 4:3 or 16:10 modes up
   to 1600 wide, in three columns by depth: 15, 16 and 32 bits, each sorted, named "w x h x bits"
   through an ostream) and switches the display with Gfx_SetVideoMode 0x414db0. gasm gives one
   640 x 480 x 32 surface: the port's list is that one mode, in the 32-bit column. See docs/hud.md. */
#pragma once
#include <stdint.h>

/* a mode record of the list 0x504cd0 (0x1c bytes in the original) */
typedef struct {
    char name[0x14];            /* +0x00 "640x480x32" */
    uint8_t column;             /* +0x14 0 15 bits, 1 16 bits, 2 32 bits */
    uint8_t pad[3];
    const void *mode;           /* +0x18 the MGL mode record (GfxMode) */
} GfxModeEntry;

/* an enumerated mode (Gfx_InsertModeSorted's 0x14-byte list nodes) */
typedef struct GfxMode {
    int32_t mgl_mode;           /* +0x00 the MGL mode number */
    int32_t w, h, bits;         /* +0x04 */
    const struct GfxMode *next; /* +0x10 */
} GfxMode;

/* Gfx_GetModeLists 0x414c20: the records, their count, the column count (3) and the modes per column */
void gfx_get_mode_lists(const void **modes, int *count, int *columns, const uint8_t **per_column);
int gfx_get_mode_index(void);                /* Gfx_GetModeIndex 0x414d30 (0x503224) */
/* Gfx_SelectMode 0x414cc0: mode i of the list (Gfx_SetVideoMode: the HUD's view size, the local
   player's viewport), then HUD_LoadFonts with res 2 for modes wider than 400 pixels, else 1 */
void gfx_select_mode(int i);
