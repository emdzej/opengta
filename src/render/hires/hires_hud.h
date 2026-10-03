/* The HUD at hires (not part of the original; docs/hires.md, "HUD and menus"): drawn directly at 640N x
   480N over the hires city instead of the faithful 640 x 480 HUD scaled. HUD_Draw runs as ported (its
   blink counters, popup ages and the rest of its state are the game's) and draws the faithful frame; its
   glyph, sprite, arrow and score-digit draws are recorded (hires_text.h) and replayed here over the hires
   frame (hires.h) at N times the faithful positions: glyphs and HUD sprites scaled by the UI filter (or
   from skins), the arrows rotated and blended like the city's sprites. */
#pragma once
#include "../../style.h"
#include "../sprite.h"
#include "hires_text.h"

/* At the start of HUD_Draw (after hires_frame_begin): recording on. s = the style the HUD sprites are
   drawn from (for the skins' sprite layer). */
void hires_hud_begin(const Style *s);
/* At present (instead of hires_frame_end): the recorded HUD over the hires frame. */
void hires_hud_end(void);

/* ---- hooks (only when hires_ui_recording) ---- */

/* Sprite_DrawScreen 0x47bbc0 of a HUD sprite: info at (x, y), its own palette. */
void hires_hud_screen_sprite(int x, int y, const SpriteInfo *in);
/* Sprite_Draw 0x47bc00 of sp (the arrows, the roof marker): its centre projected, its size in pixels. */
void hires_hud_world_sprite(const Sprite *sp);

/* Replays the HUD commands (called by hires_ui_replay). */
void hires_hud_replay(const HrTarget *t, const HrUiCmd *c, int n);
