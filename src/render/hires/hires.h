/* The high-resolution renderer (opt-in, `--param hires=N`, N = 2..4): a display-only pass that draws the
   in-game view again at 640N x 480N from the same camera, map, tile tables and sprite draw trees as the
   faithful renderer, with sub-pixel geometry and filtered true-colour textures. Not part of the original;
   the faithful renderer keeps running every frame into its 640 x 480 back buffer (rendering feeds game
   state: the sprite draw trees, the corner caches), and this pass only reads. See docs/hires.md.

   Frame (the app wires it): the game draws the city into the faithful back buffer; at the start of
   HUD_Draw (hud_set_pre_draw_hook) hires_frame_begin() draws the hires city and keeps a copy of the
   faithful frame; the faithful HUD is drawn; at present hires_frame_end() lays the pixels the HUD changed
   over the hires frame, N x N each (nearest neighbour), and the hires frame is presented. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Cast shadows (an addition, --param shadows=1): the direction towards the sun in blocks per block of
   height, the shade's strength (0..1) and colour (0x00BBGGRR). Display only. */
extern bool hires_shadows;
extern float hires_sun_x, hires_sun_y, hires_shadow_strength;
extern uint32_t hires_shadow_tint;
/* The city colour grade (an addition, --param grade=1): each city its own palette. */
extern bool hires_grade;
#include "../../map.h"
#include "../../style.h"
#include "../camera.h"
#include <stdbool.h>
#include <stdint.h>

enum { HIRES_MAX = 4 };

/* n = 0 turns it off (and frees the buffers); 1..HIRES_MAX allocate a (w n) x (h n) frame for the
   faithful back buffer fb (w x h, X8R8G8B8). n = 1 is only useful with skins (hires_skin.h): the same
   frame size as the faithful one, drawn from the skins' art. False if n is out of range or memory is
   short (then it is off). */
bool hires_init(int n, const uint32_t *fb, int w, int h);
bool hires_active(void);
int hires_scale(void);                 /* 1 when off */

/* The hires city into the hires frame, and the copy of the faithful frame the HUD is told apart by. */
void hires_frame_begin(const Map *m, const Style *s, const Viewport *vp);
/* The HUD (whatever changed in the faithful frame since hires_frame_begin) over the hires frame. */
void hires_frame_end(void);
/* The hires frame: R, G, B, A bytes, 640n x 480n. */
const uint32_t *hires_pixels(int *w, int *h);
