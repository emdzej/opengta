/* The display modes of the game (gfx.h). */
#include "gfx.h"
#include "../hud/hud.h"
#include "game.h"
#include <stdio.h>

/* Gfx_InitDrivers 0x415310 over what gasm offers: one mode, 640 x 480 in 32 bits. The original looks
   for its default mode (0x504cbc, the first 640 x 480) among the 15- and 16-bit modes only and is
   fatal (-0x128, line 0x1d9) without one: the port, having a 32-bit mode only, takes it as the
   default instead. */
static const GfxMode mode_640 = { 0, 640, 480, 32, NULL };
static GfxModeEntry modes[1];               /* 0x504cd0 */
static int32_t per_depth[3];                /* 0x503204 modes per column */
static uint8_t per_column[3];               /* 0x503210 (bytes, Gfx_GetModeLists) */
static uint8_t mode_index;                  /* 0x503224 */
static int default_mode = -1;               /* 0x504cbc */

static void init_drivers(void)
{
    if (default_mode >= 0) return;
    modes[0].column = 2;
    modes[0].mode = &mode_640;
    snprintf(modes[0].name, sizeof modes[0].name, "%dx%dx%d", mode_640.w, mode_640.h, mode_640.bits);
    per_depth[0] = per_depth[1] = 0;
    per_depth[2] = 1;
    default_mode = 0;
    mode_index = 0;
}

void gfx_get_mode_lists(const void **list, int *count, int *columns, const uint8_t **cols)
{
    init_drivers();
    *columns = 0;
    *count = 0;
    *list = modes;
    for (int i = 0; i < 3; i++) {
        per_column[i] = (uint8_t)per_depth[i];
        ++*columns;
        *count += (int16_t)per_depth[i];
    }
    *cols = per_column;
}

int gfx_get_mode_index(void) { init_drivers(); return mode_index; }

/* Gfx_SetVideoMode 0x414db0 on a mode record: the display is gasm's surface; what follows the switch
   in the original is the local player's viewport (Player_SetViewport) and the HUD's view size
   (HUD_SetViewSize); the clip rectangle and the 32-bit pixel format are the renderer's already. */
static void set_video_mode(const GfxMode *m)
{
    game_set_screen(m->w, m->h);
    hud_set_view_size(m->w, m->h);
}

void gfx_select_mode(int i)
{
    init_drivers();
    mode_index = (uint8_t)i;
    if (mode_index >= sizeof modes / sizeof *modes) mode_index = 0;   /* (the original reads past the list) */
    const GfxMode *m = modes[mode_index].mode;
    set_video_mode(m);
    hud_load_fonts(m->w > 400 ? 2 : 1);
}
