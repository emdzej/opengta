/* The in-game HUD (hud module 0x481030-0x486830, minus the fonts and string renderers, which are
   font.c's): zone / street / car-name texts, the arrows, score, multiplier, lives, wanted level,
   weapon and item icons, the pager (messages, countdowns, chat lines), subtitles, the big messages,
   the quit prompt, the pause screen and the in-game video mode list. See docs/hud.md.

   Also here because nobody else had them and the HUD needs them: the area names (Area_* 0x44b4a0-
   0x44b7a0, area.c) and Player_UpdateScoreDigits 0x462ab0 (player module).

   The HUD draws into the game's back buffer, the surface of poly_set_screen_rows (0x503228 row
   table, 0x503218 pitch): 32 bpp X8R8G8B8 (0x00RRGGBB) like the original's DirectDraw surface, not
   the R,G,B,A byte order of the frontend's Surface. The CLUTs are taken from the style after
   Style_ConvertPalettes (PIXFMT_32: 0x00RRGGBB), and the blitters store CLUT words unchanged, so
   the pixels come out in the back buffer's format; nothing here converts colours. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* ---- level start / end, video mode ---- */
void hud_init(void);                          /* HUD_Init 0x483010 */
/* HUD_LoadFonts 0x481030 (res 1 or 2; Gfx_SelectMode 0x414cc0 passes 2 for modes taller than 400
   lines): the fonts (font_hud_load) and the HUD sprites (arrows, roof marker). Only when res changes. */
void hud_load_fonts(int res);
void hud_free_fonts(void);                    /* HUD_FreeFonts 0x4832c0 */
void hud_set_view_size(int w, int h);         /* HUD_SetViewSize 0x486830 (0x785170: the screen width) */
void hud_set_pager_speed(int speed);          /* HUD_SetPagerSpeed 0x482140: 0..3 (the frontend's 1..3) */

/* ---- per frame ---- */
void hud_update(void);                        /* HUD_Update 0x485d70 (from Game_Update) */
void hud_draw(void);                          /* HUD_Draw 0x483390 (from Game_Render) */
void hud_tick_big_message(void);              /* HUD_TickBigMessage 0x486640 (while paused) */

/* ---- keys ---- */
bool hud_handle_key(int key);                 /* HUD_HandleKey 0x482d00 (scan code; true: consumed) */
bool hud_wants_key(int key);                  /* HUD_WantsKey 0x482c80 (Input_ReadControls) */
void hud_toggle_video_menu(void);             /* HUD_ToggleVideoMenu 0x482330 */
void hud_toggle_quit_prompt(void);            /* HUD_ToggleQuitPrompt 0x4822a0 */
void hud_toggle_debug(void);                  /* HUD_ToggleDebug 0x483000 */
void hud_pause_on(void);                      /* HUD_PauseOn 0x481510 */
void hud_pause_off(void);                     /* HUD_PauseOff 0x481530 */
void hud_refresh_zone(void);                  /* HUD_RefreshZone 0x482290 */

/* ---- texts ---- */
/* HUD_ShowZoneText 0x481a40: types 1 (90 frames), 3 (270), anything else 45; 2 = area name (street
   sign, street font), 200 = car name (car sign); 4/5 debug, 0x58 wanted points, 0xb4 demo, 0xca F8. */
void hud_show_zone_text(const char *s, int type);
void hud_clear_zone_text(int type);           /* HUD_ClearZoneText 0x481c50 */
void hud_show_car_name(int car);              /* HUD_ShowCarName 0x481cb0 */
/* HUD_ShowBigMessage 0x481320 (at most 40 bytes, split at the first two spaces); Lo 0x4814f0 / Hi 0x481500 */
void hud_show_big_message(const char *s, int priority);
void hud_show_big_message_lo(const char *s);
void hud_show_big_message_hi(const char *s);
/* HUD_ShowSubtitle 0x481d40: kinds 0..2 (talk: saved for F10) and 3..5, each with its icon */
void hud_show_subtitle(int kind, const char *s);
void hud_restore_subtitle(void);              /* HUD_RestoreSubtitle 0x482070 */
const char *hud_get_subtitle_buf(void);       /* HUD_GetSubtitleBuf 0x482090 */
void hud_skip_subtitle(void);                 /* HUD_SkipSubtitle 0x4820a0 */
/* HUD_Brief 0x4821c0: FXT text number `text` for the player: kind 0 pager, 1/3/4 subtitle 0/2/1 with
   babble, 2 pager countdown (`time` seconds), 5 subtitle 5. */
void hud_brief(int kind, int text, int time, int player);
/* HUD_AddScorePopup 0x481540: the number rising at a world position in the player's colour */
void hud_add_score_popup(int32_t x, int32_t y, int32_t z, int value, int player);

/* ---- arrows (positions in pixels: the original takes shorts and shifts them to 16.16) ---- */
void hud_arrow_to_car(int car);               /* HUD_ArrowToCar 0x481900 */
void hud_arrow_to_obj(int ped);               /* HUD_ArrowToObj 0x481930: target type 3, which is a ped */
void hud_arrow_to_pos(int x, int y, int z);   /* HUD_ArrowToPos 0x481960 */
void hud_red_arrow_to_pos(int x, int y, int z);   /* HUD_RedArrowToPos 0x4819b0 */
void hud_arrow_off(void);                     /* HUD_ArrowOff 0x481a00 */
void hud_red_arrow_off(void);                 /* HUD_RedArrowOff 0x481a20 */

/* ---- pager (pager.c) ---- */
bool hud_is_pager_busy(void);                 /* HUD_IsPagerBusy 0x482440 */
void pager_reset(void);                       /* Pager_Reset 0x482450 */
void pager_add_message(const char *s);        /* Pager_AddMessage 0x4824a0 */
void pager_add_countdown(const char *s, int value, int id);   /* Pager_AddCountdown 0x4827c0 */
void pager_remove_countdown(int id);          /* Pager_RemoveCountdown 0x482710 */
void pager_resume(void);                      /* Pager_Resume 0x482fb0 */
/* HUD_ChatBegin 0x4828a0 (Net_BuildChatPrefix): opens (or closes) the viewed player's chat line */
void hud_chat_begin(int to, const char *prefix, int max_len, int min_len);
void hud_chat_key(int slot, int ch);          /* HUD_ChatKey 0x482b10: 0xd Enter, 0x7f Backspace */

/* ---- not HUD: the area module (src/hud/area.c; Area_GetSample is the police radio's) ---- */
void area_load_dir_prefixes(void);            /* Area_LoadDirPrefixes 0x44b510 (WinMain; here lazily) */
void area_localize_names(void);               /* Area_LocalizeNames 0x44b4a0 */
/* Area_GetName 0x44b5c0: "direction area" of block (x, y) into out (64 bytes); zone * 256 + direction,
   or -1 ("unknown area"). */
int area_get_name(uint8_t x, uint8_t y, char *out);
int area_sub_direction(uint8_t dx, uint8_t dy, uint8_t w, uint8_t h);   /* Area_SubDirection 0x44b6d0 */
void area_get_sample(uint8_t x, uint8_t y, uint8_t *area, uint8_t *dir);   /* Area_GetSample 0x44b7b0 */
