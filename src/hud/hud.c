/* The in-game HUD, hud module 0x481030-0x486830 (the pager is pager.c's, the fonts and the string
   renderers font.c's). See docs/hud.md for the layout, the timers and the addresses. */
#include "hud.h"
#include "hud_internal.h"
#include "../audio/audio.h"
#include "../exe.h"
#include "../font.h"
#include "../text.h"
#include "../render/city.h"
#include "../render/poly.h"
#include "../render/poly_sprite.h"
#include "../render/poly_internal.h"
#include "../game/car.h"
#include "../game/game.h"
#include "../game/gmath.h"
#include "../game/mission_run.h"
#include "../game/ped.h"
#include "../game/player.h"
#include "../game/gfx.h"
#include "../game/heli.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* poly_internal.h's short names are for the rasteriser's own files */
#undef rows
#undef nrows
#undef pitch_px
#undef clip_x0
#undef clip_x1
#undef clip_y0
#undef clip_y1
#undef blend

HudFrame g_hud_frame;
int g_hud_screen_w = 640, g_hud_screen_h = 480;

/* ---------------------------------------------------------------- state */

enum { ZONE_SLOTS = 3, ZONE_TEXT = 0x79, POPUPS = 32 };

typedef struct {               /* 0x784080, 0x88 bytes each */
    int32_t timer;             /* +0x00 */
    int32_t active;            /* +0x04 */
    uint8_t type;              /* +0x08 */
    uint16_t width;            /* +0x0a */
    uint8_t res;               /* +0x0c: resolution the width is for */
    char text[ZONE_TEXT];      /* +0x0d */
    uint8_t wide;              /* +0x86 */
} ZoneSlot;

/* an arrow target record (6 words: 0x784744 the arrow, 0x784238 the queued one, 0x785108 red) */
typedef struct {
    int32_t type;              /* 0 car, 1 point, 2 none, 3 ped, 4 player */
    int16_t id;                /* +0x04 */
    int32_t x, y, z;           /* +0x08 16.16 */
    int32_t kind;              /* +0x14: 0 arrow, 1 red arrow, 2 another player's (size by id) */
} ArrowTarget;

typedef struct {               /* 0x77e7f0, 0x24 bytes each */
    int32_t x, y, z;           /* +0x00 world */
    int32_t sx, sy;            /* +0x0c screen */
    char text[12];             /* +0x14 "%d" (the original has 8 bytes before +0x1c) */
    int32_t grow;              /* +0x1c */
    uint8_t len;               /* +0x20: digits; 0 = free */
    uint8_t age;               /* +0x21 */
    uint8_t colour;            /* +0x22 */
} Popup;

typedef struct {               /* 0x7847d0, 0x24 bytes per player */
    int32_t u0;                /* +0x00: 1, then 0x47 / 0x67 / 0x27 / 0x87 for the players in the game */
    int32_t bounce;            /* +0x04 */
    int16_t tick;              /* +0x08 */
    int32_t state;             /* +0x0c: the leader's bounce: 0 still, 1 up, 2 down */
    int32_t score;             /* +0x10 */
    int32_t shown;             /* +0x14 */
    char str[16];              /* +0x18 "%8d" */
} ScoreRec;

typedef GfxModeEntry ModeName;   /* gfx.h */

static struct {
    ZoneSlot zone[ZONE_SLOTS];
    bool zones_any;            /* 0x77ed40 */
    bool zones_draw;           /* 0x77e7b4 */
    int zone_count;            /* 0x7850f8 */
    bool area_on;              /* 0x7846bc */
    bool area_changed;         /* 0x78422c */
    bool area_force;           /* 0x784734 */
    int area_last;             /* 0x7846b4 */
    char area_text[64];        /* 0x77ed08 */

    bool arrow_on, arrow_vis;  /* 0x784234, 0x784258 */
    bool red_on, red_vis;      /* 0x78472c, 0x784230 */
    ArrowTarget arrow, queued, red;   /* 0x784744, 0x784238, 0x785108 */
    int32_t arrow_dist, red_dist;     /* 0x784250, 0x77e7e0 */
    uint8_t arrow_colour;      /* 0x77e7d8 colour cycle with several players */
    bool others_on, others_draw;      /* 0x784218, 0x784720 */
    int32_t other_dist[PLAYER_MAX];   /* 0x784c2c */
    Sprite arrow_spr, red_spr, roof_spr;   /* 0x784770, 0x784658, 0x7846c0 */
    Sprite other_spr[PLAYER_MAX];     /* 0x77ed48 */
    bool roof_on, roof_vis;    /* 0x77ed02, 0x785103 */

    ScoreRec score[PLAYER_MAX];
    Popup popup[POPUPS];
    uint8_t popup_next;        /* 0x784738 */

    bool wanted_on;            /* 0x784221 */
    bool wanted_vis;           /* 0x77ed31 */
    int wanted;                /* 0x77e7a0 */
    uint8_t head_tick[4];      /* 0x77e7e4 */
    uint8_t head_alt[4];       /* 0x77e7bc */
    int flash_frenzy, flash_speed, flash_jail, flash_armour;   /* 0x78471c, 0x78475c, 0x78476c, 0x784254 */

    bool big_on, big_timer_on; /* 0x785102, 0x784864 */
    char big[3][0x2c];         /* 0x77ec80, 0x77ecac, 0x77ecd8 */
    int big_lines;             /* 0x77ed04 */
    int big_prio;              /* 0x77ed01 */
    int big_timer;             /* 0x77e7b8 */

    char sub_text[0x3c0];      /* 0x784868 */
    const char *sub_wrapped;   /* 0x77ed34 */
    int sub_lines;             /* 0x785104 */
    int sub_wrap_w;            /* 0x77e7a8 */
    bool sub_on, sub_timer_on; /* 0x784740, 0x785101 */
    int sub_kind;              /* 0x77e7e8 */
    int sub_timer;             /* 0x77eeb8 */
    bool sub_saved;            /* 0x785100 */
    int sub_saved_kind;        /* 0x784224 */
    char sub_saved_text[0x3c0];   /* 0x77eec0 */
    bool sub_pending;          /* 0x784260 */
    int sub_pending_kind;      /* 0x784228 */
    char sub_pending_text[0x3c0]; /* 0x784268 */

    bool quit_on;              /* 0x77e7a4 */
    const char *quit[3];       /* 0x77ec70, 0x77ec78, 0x77ec7c */
    bool pause_on;             /* 0x7846b8 */
    int pause_line, pause_timer;   /* 0x784c28, 0x77ed38 */
    const char *t_paused, *t_target, *t_missions, *t_secrets;   /* 0x77ed3c, 0x77e7dc, 0x784760, 0x784724 */
    bool debug;                /* 0x784728 */
    bool fonts_on;             /* 0x784220 */

    bool menu_on;              /* 0x77ed32 */
    int menu_sel;              /* 0x77e7c0 (0xffff: take the current mode) */
    const ModeName *modes;     /* 0x78421c */
    int mode_count;            /* 0x77ecaa */
    int mode_columns;          /* 0x784730 */
    const uint8_t *mode_per_column;   /* 0x77e7b0 */
    int mode_rows;             /* 0x784650: the longest column */
} H;

/* 0x5031f4: the game runs from the start menu's attract mode (talk subtitles are then not shown).
   The frontend owns it; not wired yet. */
static bool attract_mode;

/* ---------------------------------------------------------------- helpers */

_Noreturn void hud_fatal(int code, int line, int arg) { game_fatal(code, line, arg); }

const char *hud_text(const char *key)
{
    /* WinMain selects the language (Text_InitLanguage 0x47d7f0) before any level; a host that starts
       a level without it (the tests) gets English rather than the fatal error */
    if (!text_file()[0]) text_init_language(TEXT_ENGLISH);
    const char *t = text_get(key);
    if (!t) {
        game_set_error_file(text_error());
        game_fatal(-0x55, 0, 0);
    }
    return t;
}

int hud_player_colour(int n)
{
    const uint8_t *t = exe_data(0x4b21dc, 4);
    return t && n >= 0 && n < 4 ? t[n] : 4 + (n & 3);
}

static int spr_hi(void) { return hud_fonts.res == 2 ? 0x18 : 0; }   /* 0x785120 */
static int spr_res(void) { return hud_fonts.res == 2; }            /* 0x785124 */

const SpriteInfo *hud_sprite(int n)
{
    return sprite_get_info(sprite_group_base(SPRITE_GROUP_ARROW) + spr_hi() + spr_res() + n);
}

/* The style's 8 font palettes as linear 256-colour copies for font.c's HUD renderers (the original
   reads colour e of the paged CLUT at word e * 64), and the palette of sprite 0. Rebuilt every frame
   so that a re-converted CLUT (video mode change) is followed. */
static uint32_t lin_aux[8][256], lin_spr0[256];

static void hud_build_cluts(void)
{
    const Style *st = g_game.style;
    if (!st) return;
    const uint32_t *aux[8];
    for (int i = 0; i < 8; i++) {
        for (int e = 0; e < 256; e++) lin_aux[i][e] = st->aux_clut[i][e * 64];
        aux[i] = lin_aux[i];
    }
    const uint32_t *p0 = sprite_select_palette(0);   /* Tile_SelectSprite(0) 0x4385b0 */
    for (int e = 0; e < 256; e++) lin_spr0[e] = p0[e * 64];
    font_set_hud_cluts(aux, lin_spr0);
}

const uint32_t *hud_select_aux(int n)
{
    n &= 7;
    if (g_game.style) poly_clut = g_game.style->aux_clut[n];
    return lin_aux[n];
}

static long ofs(int x, int y) { return surface_offset(&g_hud_frame.s, x, y); }

/* Font_Select(f); a glyph of code c, rows skip.. skip + h - 1 at dst, through clut (Font_GlyphPixels +
   Blit_Sprite; zero-width glyphs are not drawn) */
static void glyph(const Font *f, int c, int h, int skip, long dst, const uint32_t *clut)
{
    font_select(f);
    int w = font_glyph_width((uint16_t)c);
    const uint8_t *g = font_glyph_pixels((uint16_t)c);
    if (w != 0 && g) blit_sprite32(&g_hud_frame.s, dst, g + w * skip, w, h, clut);
}

/* HUD_DrawText 0x483df0 with font f selected raw (Font_SelectRaw) at (x, y) */
static void text_at(const Font *f, const char *s, int x, int y, bool alt)
{
    font_select_raw(f);
    hud_draw_text(&g_hud_frame.s, s, ofs(x, y), alt);
}

/* ---- positions of things (the static records of the original) ---- */

/* Car_GetCamTarget 0x408220, Ped_GetPosRect 0x45fb00 and Player_GetControlledPos 0x462ef0 (their
   static records, copied) */
static CameraTarget car_cam_target(int id) { return *car_get_cam_target((int16_t)id); }
static CameraTarget ped_pos_rect(int id) { return *ped_get_pos_rect((int16_t)id); }
static CameraTarget controlled_pos(int n)
{
    CameraTarget t;   /* (the heli's record Heli_GetPos is read as one too, as the original does) */
    memcpy(&t, player_get_controlled_pos(n), sizeof t);
    return t;
}
/* Player_GetViewFocusPos 0x462e10 */
static CameraTarget focus_pos(int n)
{
    switch (g_players[n].view_kind) {
    case 0: case 1: case 2: case 5: return player_view_target(n);
    case 3: case 4: return controlled_pos(n);
    default: hud_fatal(-0x4a, 0x1c2, g_players[n].view_kind);
    }
}
/* HUD_GetTargetPos 0x486180 */
static CameraTarget target_pos(const ArrowTarget *a)
{
    switch (a->type) {
    case 0: return car_cam_target(a->id);
    case 1: return (CameraTarget){ a->x, a->y, a->z, 0, 0, 0, 0 };
    case 3: return ped_pos_rect(a->id);
    case 4: return controlled_pos(a->id);
    default: hud_fatal(-0x4a, 0x5c, a->type);
    }
}

/* Map_IsCovered 0x438800 (src/map.c) */
static bool map_is_covered(int32_t x, int32_t y, int32_t z) { return g_game.map && map_covered(g_game.map, x, y, z); }

/* ---------------------------------------------------------------- fonts, sprites, init */

void hud_set_view_size(int w, int h) { g_hud_screen_w = w, g_hud_screen_h = h; }   /* 0x486830 */

/* HUD_InitArrows 0x4817e0: every arrow is the first HUD sprite, drawn blended with the font palette
   base + 1 remapped to a player colour (the local arrow the local player's, the red arrow player 2's). */
static void init_arrow(Sprite *sp, int colour_player)
{
    sprite_init(sp, 0, 0, 0, 0, sprite_group_base(SPRITE_GROUP_ARROW) + spr_hi());
    sprite_set_blend(sp);
    sprite_set_palette(sp, g_game.style ? g_game.style->font_pal_base + 1 : 0);
    sprite_set_remap(sp, hud_player_colour(colour_player));
}
static void hud_init_arrows(void)
{
    for (int n = player_first(); n > -1; n = player_next(n))
        if (n != g_player_local) init_arrow(&H.other_spr[n], n);
    init_arrow(&H.arrow_spr, g_player_local);
    init_arrow(&H.red_spr, 2);
}

/* HUD_LoadFonts 0x481030: res other than 1 / 2 is fatal -0x4a; nothing when res is unchanged. */
void hud_load_fonts(int res)
{
    char err[256];
    if (res != 1 && res != 2) hud_fatal(-0x4a, 0xf9, res);
    if (res == hud_fonts.res) return;
    if (!font_hud_load(res, err, sizeof err)) {
        game_set_error_file(err);
        hud_fatal(-0xca, 0, res);   /* Font_Load 0x4304d0: -202 on open */
    }
    hud_init_arrows();
    init_arrow(&H.roof_spr, g_player_local);
}

/* HUD_FreeFonts 0x4832c0 */
void hud_free_fonts(void)
{
    font_hud_free();
    H.fonts_on = false;
}

/* HUD_Init 0x483010. Only the popup that would be added next is cleared: the others survive into the
   next level (as in the original). */
void hud_init(void)
{
    area_load_dir_prefixes();
    H.sub_pending = false;
    memset(H.sub_text, 0, sizeof H.sub_text);
    H.sub_timer_on = H.sub_on = H.sub_saved = false;
    H.zones_draw = H.zones_any = false;
    H.zone_count = 0;
    for (int i = 0; i < ZONE_SLOTS; i++) H.zone[i].active = 0;
    H.area_on = true;
    H.area_changed = false;
    H.area_force = true;
    H.area_last = -1;
    H.arrow_on = H.arrow_vis = H.red_on = H.red_vis = false;
    H.arrow.type = 2, H.red.type = 2, H.queued.type = 2;
    H.arrow_dist = H.red_dist = 0;
    H.arrow_colour = 0;
    H.others_on = H.others_draw = false;
    for (int n = player_first(); n > -1; n = player_next(n))
        if (n != g_player_local) H.others_on = H.others_draw = true, H.other_dist[n] = 0;
    for (int i = 0; i < PLAYER_MAX; i++) {
        ScoreRec *r = &H.score[i];
        r->score = 0, r->shown = 1, r->bounce = 0, r->u0 = 1, r->state = 0, r->tick = 0;
        snprintf(r->str, sizeof r->str, "%d", 0);
    }
    static const int32_t u0[4] = { 0x47, 0x67, 0x27, 0x87 };
    for (int n = player_first(); n > -1; n = player_next(n)) {
        H.score[n].u0 = u0[n & 3];
        H.score[n].shown = 1;
    }
    pager_hud_init();
    H.menu_on = false;
    H.quit_on = false;
    H.popup[H.popup_next & 0x1f].len = 0;
    H.fonts_on = true;
    H.debug = false;
    H.roof_vis = false;
    H.roof_on = true;
    H.wanted_vis = false;
    H.wanted_on = true;
    H.wanted = 5;
    memset(H.head_tick, 2, sizeof H.head_tick);
    memset(H.head_alt, 0, sizeof H.head_alt);
    H.flash_frenzy = H.flash_speed = H.flash_jail = H.flash_armour = 5;
    H.big_on = H.big_timer_on = false;
    H.t_paused = hud_text(exe_str(0x4b376c));
    H.t_target = hud_text(exe_str(0x4b375c));
    H.t_missions = hud_text(exe_str(0x4b0530));
    H.t_secrets = hud_text(exe_str(0x4b0520));
    H.pause_on = false;
}

/* ---------------------------------------------------------------- small setters */

void hud_pause_on(void) { H.pause_on = true, H.pause_line = 0, H.pause_timer = 0x28; }   /* 0x481510 */
void hud_pause_off(void) { H.pause_on = false; }                                        /* 0x481530 */
void hud_refresh_zone(void) { H.area_force = true; }                                    /* 0x482290 */
void hud_toggle_debug(void) { H.debug = !H.debug; }                                     /* 0x483000 */

void hud_arrow_to_car(int car)               /* 0x481900 */
{
    H.arrow_on = true;
    H.arrow.type = 0, H.arrow.id = (int16_t)car, H.arrow.kind = 0;
    H.arrow_dist = 0;
}
void hud_arrow_to_obj(int ped)               /* 0x481930 */
{
    H.arrow_on = true;
    H.arrow.id = (int16_t)ped, H.arrow.type = 3, H.arrow.kind = 0;
    H.arrow_dist = 0;
}
void hud_arrow_to_pos(int x, int y, int z)   /* 0x481960 */
{
    H.arrow_on = true;
    H.arrow.type = 1;
    H.arrow.x = (int32_t)((uint32_t)(int16_t)x << 16);
    H.arrow.y = (int32_t)((uint32_t)(int16_t)y << 16);
    H.arrow.z = (int32_t)((uint32_t)(int16_t)z << 16);
    H.arrow.kind = 0;
    H.arrow_dist = 0;
}
void hud_red_arrow_to_pos(int x, int y, int z)   /* 0x4819b0 */
{
    H.red.x = (int32_t)((uint32_t)(int16_t)x << 16);
    H.red_on = true;
    H.red.type = 1;
    H.red.y = (int32_t)((uint32_t)(int16_t)y << 16);
    H.red.z = (int32_t)((uint32_t)(int16_t)z << 16);
    H.red.kind = 1;
    H.red_dist = 0;
}
void hud_arrow_off(void) { H.arrow_on = H.arrow_vis = false, H.arrow_dist = 0; }    /* 0x481a00 */
void hud_red_arrow_off(void) { H.red_on = H.red_vis = false, H.red_dist = 0; }      /* 0x481a20 */

/* ---------------------------------------------------------------- zone texts */

static const Font *zone_font(int type) { return type == 2 ? hud_fonts.street : hud_fonts.sub; }
static uint16_t zone_width(const ZoneSlot *z)
{
    const Font *f = zone_font(z->type);
    if (!f) return 0;
    return (uint16_t)(z->wide ? font_string_width(f, z->text) : font_string_width_alt(f, z->text));
}
static int zone_time(int type) { return type == 3 ? 0x10e : type == 1 ? 0x5a : 0x2d; }

void hud_show_zone_text(const char *s, int type)   /* HUD_ShowZoneText 0x481a40 */
{
    size_t len = strlen(s);
    if (len > 0x78) hud_fatal(-0x82, 0x8b, (int)len);
    uint8_t t = (uint8_t)type;
    if (H.zones_any)
        for (int i = 0; i < ZONE_SLOTS; i++)
            if (H.zone[i].active && H.zone[i].type == t && !strcmp(s, H.zone[i].text)) {
                H.zone[i].timer = zone_time(t);
                return;
            }
    for (int i = 0; i < ZONE_SLOTS; i++) {
        ZoneSlot *z = &H.zone[i];
        if (z->active) continue;
        z->type = t;
        z->active = 1;
        z->res = (uint8_t)hud_fonts.res;
        memcpy(z->text, s, len + 1);
        z->wide = text_wide() && text_has_wide_chars(s);
        z->width = zone_width(z);
        z->timer = zone_time(t);
        H.zones_any = H.zones_draw = true;
        H.zone_count++;
        return;
    }
}

void hud_clear_zone_text(int type)          /* HUD_ClearZoneText 0x481c50 */
{
    if (!H.zones_any) return;
    for (int i = 0; i < ZONE_SLOTS; i++)
        if (H.zone[i].active && H.zone[i].type == (uint8_t)type) {
            H.zone_count--;
            H.zone[i].active = 0;
            if (H.zone_count == 0) {
                H.zones_draw = H.zones_any = false;
                H.zone_count = 0;
                return;
            }
        }
}

/* the inline copy of HUD_ClearZoneText that stops at the last slot (no count reset) */
static void clear_zone_inline(uint8_t type)
{
    if (!H.zones_any) return;
    for (int i = 0; i < ZONE_SLOTS; i++)
        if (H.zone[i].active && H.zone[i].type == type) {
            H.zone_count--;
            H.zone[i].active = 0;
            if (H.zone_count == 0) {
                H.zones_draw = H.zones_any = false;
                return;
            }
        }
}

void hud_show_car_name(int car)             /* HUD_ShowCarName 0x481cb0: FXT "car%d" of the model */
{
    char key[16];
    snprintf(key, sizeof key, exe_str(0x4b373c), car_get((int16_t)car)->model);
    clear_zone_inline(200);
    hud_show_zone_text(hud_text(key), 200);
}

/* HUD_DrawZoneTexts 0x484de0: slots 2, 1, 0 stacked from the top centre (below the wanted heads):
   area names in the street sign (sprite 1; at res 2 the two halves 0x19 and 0x1a side by side) with
   the street font 2 * res rows down, car names in the car sign (sprite 2) with the sub font clipped
   to the sign 3 * res rows down, everything else in the sub font with palette aux 1. */
static void draw_zone_texts(void)
{
    int res = g_hud_frame.res, vw = g_hud_frame.view_w;
    int y = H.wanted_vis ? res * 10 : 0;
    int base = sprite_group_base(SPRITE_GROUP_ARROW);
    for (int i = ZONE_SLOTS - 1; i >= 0; i--) {
        ZoneSlot *z = &H.zone[i];
        if (!z->active) continue;
        if (z->res != (uint8_t)res) {
            z->res = (uint8_t)res;
            z->width = zone_width(z);
        }
        int adv;
        if (z->type == 2) {
            const SpriteInfo *s1 = sprite_get_info(base + spr_hi() + 1);
            if (!s1) continue;
            if (spr_res() == 1) {
                const SpriteInfo *s2 = sprite_get_info(base + spr_hi() + 2);
                int x0 = (vw - (s2 ? s2->w : 0) - s1->w) / 2;
                sprite_draw_screen(x0, y, s1);
                if (s2) sprite_draw_screen(s1->w + x0, y, s2);
            } else {
                sprite_draw_screen((vw - s1->w) / 2, y, s1);
            }
            adv = s1->h;
            if (z->wide) font_select(hud_fonts.street);
            text_at(hud_fonts.street, z->text, (vw - z->width) / 2, y + res * 2, false);
        } else if (z->type == 200) {
            const SpriteInfo *s = hud_sprite(2);
            if (!s) continue;
            sprite_draw_screen((vw - s->w) / 2, y, s);
            adv = s->h;
            int row = z->wide ? y + res * (res != 1 ? 4 : 2) : y + res * 3;
            if (z->wide) font_select(hud_fonts.sub);
            else font_select_raw(hud_fonts.sub);
            hud_draw_text_clipped(&g_hud_frame.s, z->text, ofs((vw - z->width) / 2, row), 0, s->w - res * 6);
        } else {
            text_at(hud_fonts.sub, z->text, (vw - z->width) / 2, y, true);
            adv = font_height();
        }
        y += adv + 2;
    }
}

/* ---------------------------------------------------------------- big messages */

void hud_show_big_message(const char *s, int priority)   /* HUD_ShowBigMessage 0x481320 */
{
    size_t len = strlen(s);
    if (len > 0x28) hud_fatal(-0x82, 0x1d7, (int)len);
    if (H.big_on && (uint8_t)priority < H.big_prio) return;
    memcpy(H.big[0], s, len + 1);
    /* the jingle: 2501 "MISSION FAILED!" 2, 2500 "MISSION COMPLETE!" 1, 2504 "FRENZY FAILED!" 5 */
    if (!strcmp(s, hud_text(exe_str(0x4b3734)))) Snd_PlayVoice(2);
    else if (!strcmp(s, hud_text(exe_str(0x4b372c)))) Snd_PlayVoice(1);
    else if (!strcmp(s, hud_text(exe_str(0x4b3724)))) Snd_PlayVoice(5);
    char *sp = strchr(H.big[0], ' ');
    if (!sp) {
        H.big_lines = 1;
    } else {
        H.big_lines = 2;
        memmove(H.big[1], sp + 1, strlen(sp + 1) + 1);
        *sp = 0;
        sp = strchr(H.big[1], ' ');
        if (sp) {
            H.big_lines = 3;
            memmove(H.big[2], sp + 1, strlen(sp + 1) + 1);
            *sp = 0;
        }
    }
    H.big_on = true;
    H.big_timer_on = true;
    H.big_prio = (uint8_t)priority;
    H.big_timer = (int)len * 5;
}
void hud_show_big_message_lo(const char *s) { hud_show_big_message(s, 0); }   /* 0x4814f0 */
void hud_show_big_message_hi(const char *s) { hud_show_big_message(s, 1); }   /* 0x481500 */

void hud_tick_big_message(void)             /* HUD_TickBigMessage 0x486640 */
{
    if (H.big_timer_on && --H.big_timer < 1) H.big_on = H.big_timer_on = false;
}

/* HUD_DrawBigMessage 0x483890: the big font, each line centred, the block a quarter of the way down:
   y = (view_h - lines * (h + 1)) / 4, lines h + 1 apart, palette aux 0. */
static void draw_big_message(void)
{
    bool wide = text_has_wide_chars(H.big[0]);
    if (H.big_lines > 1) {
        wide |= text_has_wide_chars(H.big[1]);
        if (H.big_lines == 3) wide |= text_has_wide_chars(H.big[2]);
    }
    const Font *f = hud_fonts.big;
    if (wide) font_select(f);
    else font_select_raw(f);
    int h = font_height();
    int y = (g_hud_frame.view_h - (h + 1) * H.big_lines) / 4;
    for (int i = 0; i < H.big_lines; i++) {
        int w = wide ? font_string_width(f, H.big[i]) : font_string_width_alt(f, H.big[i]);
        if (wide) font_select(f);
        else font_select_raw(f);
        hud_draw_text(&g_hud_frame.s, H.big[i], ofs((g_hud_frame.view_w - w) / 2, y), false);
        y += 1 + h;
    }
}

/* ---------------------------------------------------------------- subtitles */

static int sub_icon(int kind)   /* the icon sprite of a subtitle kind */
{
    switch (kind) {
    case 0: return 0xb;
    case 1: return 10;
    case 2: return 0xc;
    case 3: return 0xe;
    case 4: return 0xd;
    case 5: return 0xf;
    default: hud_fatal(-0x4a, 0x169, H.sub_kind);
    }
}
static bool sub_talk(int kind)    /* kinds 0..2 talk, 3..5 not */
{
    if (kind < 0 || kind > 5) hud_fatal(-0x4a, 0x183, H.sub_kind);
    return kind <= 2;
}

/* HUD_ShowSubtitle 0x481d40. While a subtitle shows, one that isn't a talk subtitle replacing a
   non-talk one (or a talk one replacing a non-talk one) waits as the pending one (the last one wins).
   Talk subtitles are kept for F10. The text is wrapped to the screen width minus the icon; it stays
   (4 / speed) * (length + 25) / 2 frames (speed 0x4b3610: 1, 2 or 4; 0 would divide by zero). */
void hud_show_subtitle(int kind, const char *s)
{
    int icon = sub_icon(kind);
    const SpriteInfo *in = hud_sprite(icon);
    if (attract_mode && sub_talk(kind)) return;
    size_t len = strlen(s);
    if (H.sub_on && sub_talk(kind) <= sub_talk(H.sub_kind)) {
        if (H.sub_pending) (void)sub_talk(H.sub_pending_kind);
        H.sub_pending = true;
        H.sub_pending_kind = kind;
        snprintf(H.sub_pending_text, sizeof H.sub_pending_text, "%s", s);
        return;
    }
    if (sub_talk(kind)) {
        H.sub_saved = true;
        snprintf(H.sub_saved_text, sizeof H.sub_saved_text, "%s", s);
        H.sub_saved_kind = kind;
    }
    int iw = in ? in->w : 0;
    int width = text_wide() ? g_hud_screen_w - iw : g_hud_screen_w - iw - 2;
    H.sub_wrap_w = g_hud_screen_w;
    if (s != H.sub_text) snprintf(H.sub_text, sizeof H.sub_text, "%s", s);
    font_select(hud_fonts.sub);
    H.sub_wrapped = text_word_wrap(H.sub_text, width, &H.sub_lines);
    H.sub_on = true;
    H.sub_timer_on = true;
    H.sub_kind = kind;
    if (g_pager.speed_sub == 0) hud_fatal(-0x4a, 0, 0);   /* integer division by zero in the original */
    H.sub_timer = (4 / g_pager.speed_sub) * ((int)len + 1 + 0x18) / 2;
}

void hud_restore_subtitle(void)             /* HUD_RestoreSubtitle 0x482070 (F10) */
{
    if (H.sub_saved) hud_show_subtitle(H.sub_saved_kind, H.sub_saved_text);
}
const char *hud_get_subtitle_buf(void) { return H.sub_text; }   /* HUD_GetSubtitleBuf 0x482090 */

void hud_skip_subtitle(void)                /* HUD_SkipSubtitle 0x4820a0 */
{
    if (!H.sub_timer_on) return;
    if (H.sub_pending && sub_talk(H.sub_pending_kind)) H.sub_pending = false;
    if (sub_talk(H.sub_kind)) H.sub_timer = 1;
}

/* HUD_DrawSubtitle 0x4857a0: the wrapped text bottom left right of the icon, its last line ending at
   the bottom; the original takes the row pointer one entry before the table (0x503224), so the block
   is one row higher than the arithmetic says. The icon at the bottom left. */
static void draw_subtitle(void)
{
    const SpriteInfo *in = hud_sprite(sub_icon(H.sub_kind));
    if (!in) return;
    font_select(hud_fonts.sub);
    int h = font_height();
    if (H.sub_wrap_w != g_hud_screen_w) {
        int width = text_wide() ? g_hud_screen_w - in->w : g_hud_screen_w - in->w - 2;
        H.sub_wrap_w = g_hud_screen_w;
        H.sub_wrapped = text_word_wrap(H.sub_text, width, &H.sub_lines);
    }
    int x = text_wide() ? in->w : in->w + 2;
    int row = g_hud_frame.view_h - (h - 1) * H.sub_lines - 1;
    if (H.sub_wrapped) hud_draw_text_multiline(&g_hud_frame.s, H.sub_wrapped, hud_fonts.sub, ofs(x, row), 0);
    sprite_draw_screen(0, g_hud_frame.view_h - in->h, in);
}

/* HUD_Brief 0x4821c0. The original sprintfs the text with itself as the format (no FXT text a brief
   uses has a '%'). */
void hud_brief(int kind, int text, int time, int player)
{
    char key[16], buf[0x400];
    snprintf(key, sizeof key, "%d", text);   /* _itoa 0x4a6136, base 10 */
    snprintf(buf, sizeof buf, "%s", hud_text(key));
    if (player != g_player_local) return;   /* Player_IsLocal 0x461780 */
    switch (kind) {
    case 0: pager_add_message(buf); return;
    case 1: hud_show_subtitle(0, buf); break;
    case 2: pager_add_countdown(buf, time, text); return;
    case 3: hud_show_subtitle(2, buf); break;
    case 4: hud_show_subtitle(1, buf); break;
    case 5: hud_show_subtitle(5, buf); return;
    default: return;
    }
    Snd_PlayTalk(buf);
}

/* ---------------------------------------------------------------- score popups */

void hud_add_score_popup(int32_t x, int32_t y, int32_t z, int value, int player)   /* 0x481540 */
{
    if (H.popup_next > 0x1f) H.popup_next = 0;
    Popup *p = &H.popup[H.popup_next];
    snprintf(p->text, sizeof p->text, exe_str(0x4b07cc), value);
    p->len = (uint8_t)strlen(p->text);
    p->colour = (uint8_t)hud_player_colour(player);
    p->grow = 0;
    p->age = 0;
    p->x = x, p->y = y, p->z = z;
    H.popup_next++;
}

/* HUD_DrawScorePopups 0x481620: the digits of the EXPSCOR font scaled onto screen rectangles
   (Poly_DrawRect) in the player's colour: 4 frames at their size, then growing by 6 pixels a frame
   until past 99. Each grown rectangle starts at i * width but ends at (i + 1) * (width + grow). */
static void draw_score_popups(void)
{
    const Font *f = hud_fonts.expscor;
    if (!f) return;
    font_select(f);
    int w = font_glyph_width(0), h = font_height();
    for (int i = 0; i < POPUPS; i++) {
        Popup *p = &H.popup[i];
        int n = p->len;
        if (!n) continue;
        p->age++;
        render_world_to_screen(p->x, p->y, p->z, &p->sx, &p->sy);
        hud_select_aux(p->colour);
        font_select(f);
        if (p->age < 5) {
            int x0 = p->sx - (n * w >> 1), y0 = p->sy - (h >> 1);
            for (int k = 0; k < n; k++)
                poly_draw_rect(k * w + x0, (k + 1) * w + x0, y0, h - 1 + y0, w - 1, h - 1,
                               font_glyph_pixels((uint16_t)(p->text[k] - 0x30)));
        } else {
            int g = p->grow;
            int x0 = p->sx - ((g + w) * n >> 1), y0 = p->sy - ((g + h) >> 1);
            for (int k = 0; k < n; k++)
                poly_draw_rect(k * w + x0, (k + 1) * (g + w) + x0, y0, h + g - 1 + y0, w - 1, h - 1,
                               font_glyph_pixels((uint16_t)(p->text[k] - 0x30)));
            p->grow = g + 6;
            if (g + 6 > 99) p->len = 0;
        }
    }
}

/* ---------------------------------------------------------------- score, lives, multiplier */

/* the roll counter of score digit i (the 9 shorts at +0x13c, Player_UpdateScoreDigits) */
static int16_t roll_get(const Player *p, int i)
{
    int16_t v;
    memcpy(&v, (const uint8_t *)p + 0x13c + 2 * i, 2);
    return v;
}

/* HUD_DrawScore 0x484750: the 9 score digits right-aligned at the top right, row dh * player, in the
   player's colour; leading zeros are left out but a digit rolling in still shows; each digit slides
   down by roll * dh / 16 with the next one coming in above it. Left of the shown digits: the lives
   ("x" and two digits, aux of player 1's colour) on the top line and the multiplier 6 * res rows lower
   (4 * res with more players), in the MISSMUL font (glyph 10 is the "x"). With more players the frags
   in the score font instead of the lives. The tens digit of the lives is placed by the width of the
   multiplier (a quirk). */
static void draw_score(int n)
{
    const Font *sf = hud_fonts.score, *mf = hud_fonts.missmul;
    if (!sf || !mf) return;
    Player *p = &g_players[n];
    int vw = g_hud_frame.view_w, res = g_hud_frame.res;
    font_select(sf);
    int dw = font_glyph_width(0), dh = font_height();
    font_select(mf);
    int mw = font_glyph_width(0);
    const char *digits = p->hud_score, *mult = p->hud_mult;
    const uint32_t *clut = hud_select_aux(hud_player_colour(p->flag1 ? 2 : n));
    int row0 = dh * n;
    bool lead = true;
    for (int i = 0, x = vw - 9 * dw; i < 9; i++, x += dw) {
        int r = roll_get(p, i) * dh / 16;
        char d = digits[i];
        if (!lead || d != '0' || i == 8) {
            glyph(sf, d - '0', sf->height - r, 0, ofs(x, r + row0), clut);
            lead = false;
        }
        if (r > 0) {
            int next = d == '9' ? 0 : d - '0' + 1;
            glyph(sf, next, r, dh - r, ofs(x, row0), clut);
        }
    }
    int lz = 0;
    while (lz < 9 && digits[lz] == '0' && lz != 8) lz++;
    int nd = 9 - lz, sw = nd * dw;
    int mrow = (g_player_count == 1 ? res * 6 : res * 4) + row0;
    glyph(mf, mult[1] - '0', mf->height, 0, ofs(vw - sw - mw, mrow), clut);
    int k = 2;
    if (mult[0] != '0') {
        glyph(mf, mult[0] - '0', mf->height, 0, ofs(vw - mw * 2 - sw, mrow), clut);
        k = 3;
    }
    int kx = k * mw;
    glyph(mf, 10, mf->height, 0, ofs(vw - kx - sw, mrow), clut);
    if (g_player_count < 2) {
        clut = hud_select_aux(hud_player_colour(1));
        char l[8];
        int lives = g_players[0].lives;
        if (lives == -1) snprintf(l, sizeof l, "%s", exe_str(0x4b37c8));   /* "::" */
        else snprintf(l, sizeof l, exe_str(0x4b2224), lives);
        glyph(mf, l[1] - '0', mf->height, 0, ofs(vw - sw - mw, row0), clut);
        int k2 = 2;
        if (l[0] != '0') {
            glyph(mf, l[0] - '0', mf->height, 0, ofs(vw - kx - sw, row0), clut);
            k2 = 3;
        }
        glyph(mf, 10, mf->height, 0, ofs(vw - k2 * mw - sw, row0), clut);
    } else {
        char b[16];
        snprintf(b, sizeof b, exe_str(0x4b07cc), p->frags);
        int len = (int)strlen(b), x = vw - (nd + len) * dw - kx;
        for (int i = 0; i < len; i++, x += dw) glyph(sf, b[i] - '0', sf->height, 0, ofs(x, row0), clut);
    }
}

/* ---------------------------------------------------------------- weapon and items */

static bool flash(int *c)   /* the 5-frame blink of the item icons: on for 3 frames of 5 */
{
    if (--*c == 0) {
        *c = 5;
        return true;
    }
    return *c > 2;
}

/* HUD_DrawWeaponInfo 0x484190: top left, below the pager while it shows: the weapon icon (5..8) with
   the ammo ("%02.2d", or the frenzy seconds for a temporary weapon, then the icon blinks) in its
   corner; under it a row of item icons: get-out-of-jail (0x14 / 0x15 blinking), armour (0x12 / 0x13,
   its count in aux 1), speed-up (0x16 / 0x17); then the two mission timers in seconds. */
static void draw_weapon_info(void)
{
    const Player *p = &g_players[g_player_local];
    int res = g_hud_frame.res;
    const Font *sub = hud_fonts.sub;
    int y = g_pager.visible ? g_pager.height : 0;
    int w = p->weapon;
    char b[32];
    if (w != 0) {
        font_select_raw(sub);
        int icon = 0, dx = 0, dy = 0;
        switch (w) {
        case 1: icon = 0, dx = res * 0xd, dy = res * 9; break;
        case 2: icon = 1, dx = res * 0xc, dy = res * 10; break;
        case 3: icon = 2, dy = res * 7, dx = res * 0xd; break;
        case 4: icon = 3, dx = res * 16, dy = res * 10; break;
        default: hud_fatal(-0x4a, 0x157, w);
        }
        int ammo = p->ammo[w - 1];   /* Player_GetAmmo 0x461830: +0x157 + weapon */
        if (ammo == 'd') {
            snprintf(b, sizeof b, exe_str(0x4b07cc), p->timers[0] / 0x19);
            if (--H.flash_frenzy == 0) H.flash_frenzy = 5;
        } else {
            snprintf(b, sizeof b, exe_str(0x4b37c0), ammo);
        }
        const SpriteInfo *in = hud_sprite(5 + icon);
        if (!in) return;
        if (ammo != 'd' || H.flash_frenzy > 2) {
            sprite_draw_screen(0, y, in);
            text_at(sub, b, in->w - dx, in->h - dy + y, false);
        }
        y += 1 + in->h;
    }
    int x = 0, maxh = 0;
    if (p->jail_free) {
        const SpriteInfo *in = hud_sprite(0x14 + flash(&H.flash_jail));
        if (in) {
            sprite_draw_screen(0, y, in);
            x = in->w + 1;
            maxh = in->h + 1;
        }
    }
    if (p->armour != 0) {
        const SpriteInfo *in = hud_sprite(0x12 + flash(&H.flash_armour));
        if (in) {
            sprite_draw_screen(x, y, in);
            snprintf(b, sizeof b, exe_str(0x4b07cc), p->armour);
            text_at(sub, b, in->w - res * 5 + x, in->h - res * 5 + y, true);
            x += in->w + 1;
            if (maxh < in->h + 1) maxh = in->h + 1;
        }
    }
    if (p->speedup != 0) {
        const SpriteInfo *in = hud_sprite(0x16 + flash(&H.flash_speed));
        if (in) {
            sprite_draw_screen(x, y, in);
            if (maxh < in->h + 1) maxh = in->h + 1;
        }
    }
    int y2 = y + maxh;
    if (p->timers[1] != -1) {
        snprintf(b, sizeof b, exe_str(0x4b07cc), p->timers[1] / 0x19);
        text_at(sub, b, 0, y2, false);
        font_select_raw(sub);
        y2 += font_height();
    }
    if (p->timers[2] != -1) {
        snprintf(b, sizeof b, exe_str(0x4b07cc), p->timers[2] / 0x19);
        text_at(sub, b, 0, y2, false);
    }
}

/* ---------------------------------------------------------------- wanted level */

/* HUD_DrawWanted 0x485680: one cop head per wanted level (at most 4) centred at the top of the
   screen, each switching between sprites 0x10 and 0x11 every second frame. */
static void draw_wanted(void)
{
    const SpriteInfo *a = hud_sprite(0x10), *b = hud_sprite(0x11);
    if (!a || !b) return;
    if (H.wanted < 0 || H.wanted > 4) hud_fatal(-0x4a, 0x172, H.wanted);
    int n = H.wanted;
    int x = (g_hud_screen_w - (a->w + 1) * n) / 2;
    for (int i = 0; i < n; i++, x += a->w + 1) {
        sprite_draw_screen(x, 0, H.head_alt[i] ? b : a);
        if (--H.head_tick[i] == 0) {
            H.head_tick[i] = 2;
            H.head_alt[i] = !H.head_alt[i];
        }
    }
}

/* ---------------------------------------------------------------- pause, quit, video menu */

/* HUD_DrawPauseInfo 0x483e90: "Game Paused" centred at (view_h - 3 (h + 1)) / 2 and two lines lower
   one of the target score, the missions passed, the secrets found, 40 frames each in turn. */
static void draw_pause_info(void)
{
    const Font *f = hud_fonts.sub;
    font_select(f);
    int h = font_height();
    int y = (g_hud_frame.view_h + (h + 1) * -3) / 2, vw = g_hud_frame.view_w;
    int w = font_string_width(f, H.t_paused);
    font_select(f);
    hud_draw_text(&g_hud_frame.s, H.t_paused, ofs((vw - w) / 2, y), false);
    char b[128];
    switch (H.pause_line) {
    case 0: snprintf(b, sizeof b, exe_str(0x4b37a4), H.t_target, mission_get_target_score()); break;
    case 1: {
        int total = mission_get_mission_total();
        snprintf(b, sizeof b, exe_str(0x4b37ac), H.t_missions, mission_get_counter_remaining(), total);
        break;
    }
    case 2: {
        int total = mission_get_secret_total();
        snprintf(b, sizeof b, exe_str(text_wide() ? 0x4b37b8 : 0x4b37ac), H.t_secrets,
                 mission_get_secret_remaining(), total);
        break;
    }
    default: hud_fatal(-0x4a, 0x1d8, H.pause_line);
    }
    w = font_string_width(f, b);
    font_select(f);
    hud_draw_text(&g_hud_frame.s, b, ofs((vw - w) / 2, y + (h + 1) * 2), false);
    if (--H.pause_timer < 1) {
        H.pause_timer = 0x28;
        if (++H.pause_line > 2) H.pause_line = 0;
    }
}

/* HUD_ToggleQuitPrompt 0x4822a0 (Esc): "quit1".."quit3" for the processed player when no video menu
   and no chat line is open; again: off. */
void hud_toggle_quit_prompt(void)
{
    Player *p = &g_players[g_player_viewed];
    if (p->local1) return;
    if (!p->local2) {
        if (p->u184 != -1) return;
        p->local2 = 1;
        if (player_is_viewed_local()) {
            H.quit_on = true;
            H.quit[0] = hud_text(exe_str(0x4b3754));
            H.quit[1] = hud_text(exe_str(0x4b374c));
            H.quit[2] = hud_text(exe_str(0x4b3744));
        }
    } else {
        p->local2 = 0;
        if (player_is_viewed_local()) H.quit_on = false;
    }
}

/* HUD_ToggleVideoMenu 0x482330 (F11): the mode list (Gfx_GetModeLists 0x414c20); the type 1 zone
   texts are cleared. */
void hud_toggle_video_menu(void)
{
    Player *p = &g_players[g_player_viewed];
    if (p->local2) return;
    if (!p->local1) {
        if (p->u184 != -1) return;
        p->local1 = 1;
        if (!player_is_viewed_local()) return;
        const void *modes;
        gfx_get_mode_lists(&modes, &H.mode_count, &H.mode_columns, &H.mode_per_column);
        H.modes = modes;
        clear_zone_inline(1);
        H.menu_on = true;
        H.menu_sel = 0xffff;
        H.mode_rows = 0;
        for (int i = 0; i < H.mode_columns; i++)
            if (H.mode_rows < H.mode_per_column[i]) H.mode_rows = H.mode_per_column[i];
    } else {
        p->local1 = 0;
        if (player_is_viewed_local()) H.menu_on = false;
    }
}

/* HUD_DrawVideoMenu 0x485a20: the modes in columns (one per colour depth) as wide as the first name +
   16, the block centred; the selected one in aux 0, the others aux 1. */
static void draw_video_menu(void)
{
    if (!H.modes || !hud_fonts.sub) return;
    if (H.menu_sel == 0xffff) H.menu_sel = gfx_get_mode_index() & 0xff;
    int colw = font_string_width_alt(hud_fonts.sub, H.modes[0].name) + 0x10;
    int lh = hud_fonts.sub->height + 2;
    int y0 = (g_hud_frame.view_h - lh * H.mode_rows) / 2;
    int x0 = (g_hud_frame.view_w - H.mode_columns * colw) / 2;
    int prev = -1, row = 0;
    for (int i = 0; i < H.mode_count; i++) {
        const ModeName *m = &H.modes[i];
        if (prev != m->column) prev = m->column, row = 0;
        text_at(hud_fonts.sub, m->name, m->column * colw + x0, lh * row + y0, i != (int16_t)H.menu_sel);
        row++;
    }
}

/* HUD_WantsKey 0x482c80: the keys the local player's chat line, video menu or quit prompt take */
bool hud_wants_key(int key)
{
    const Player *p = &g_players[g_player_local];
    const uint8_t *map = exe_data(0x4b3618, 0x3a);
    if (p->u184 != -1 && key >= 0 && key < 0x3a && map && map[key]) return true;
    if (p->local1 && (key == 0x148 || key == 0x150 || key == 0x1c || key == 0x14b || key == 0x14d)) return true;
    return p->local2 && key == 0x1c;
}

/* HUD_HandleKey 0x482d00: a typing player's characters (scan code table 0x4b3618) go to the chat
   line; with the video menu open the cursor keys move the selection (left / right by a column) and
   Enter sets the mode; with the quit prompt Enter abandons the game. */
bool hud_handle_key(int key)
{
    Player *p = &g_players[g_player_viewed];
    const uint8_t *map = exe_data(0x4b3618, 0x3a);
    if (p->u184 != -1 && key >= 0 && key < 0x3a && map && map[key]) {
        hud_chat_key(p->u184, (int8_t)map[key]);
        return true;
    }
    if (!p->local1) {
        if (!p->local2 || key != 0x1c) return false;
        game_request_abandon();
        return true;
    }
    const uint8_t *cnt = H.mode_per_column;
    switch (key) {
    case 0x14b:                              /* left */
        if (player_is_viewed_local() && H.mode_columns != 0 && cnt) {
            int acc = 0, j = 0, pos = 0;
            for (; j < H.mode_columns; j++) {
                pos = H.menu_sel - acc;
                acc += cnt[j];
                if (H.menu_sel < acc) break;
            }
            if (j >= 1 && cnt[j - 1] >= pos + 1) H.menu_sel -= cnt[j - 1];
            else if (j > 1 && cnt[j - 2] >= pos + 1 && cnt[j - 1] == 0) H.menu_sel -= cnt[j - 2];
        }
        break;
    case 0x1c:                               /* Enter */
        p->local1 = 0;
        if (player_is_viewed_local()) {
            H.menu_on = false;
            gfx_select_mode(H.menu_sel & 0xff);
            if (g_game.style) style_convert_palettes(g_game.style, &PIXFMT_32);
            int m = gfx_get_mode_index() & 0xff;
            if (H.modes && m < H.mode_count) hud_show_zone_text(H.modes[m].name, 1);
            game_request_redraw();
        }
        break;
    case 0x148:                              /* up */
        if (player_is_viewed_local() && --H.menu_sel < 0) H.menu_sel = 0;
        break;
    case 0x14d:                              /* right */
        if (player_is_viewed_local() && cnt) {
            int acc = 0, j = 0;
            for (; j < H.mode_columns; j++) {
                acc += cnt[j];
                if (H.menu_sel < acc) break;
            }
            if (j < H.mode_columns && cnt[j] + H.menu_sel < H.mode_count) H.menu_sel += cnt[j];
        }
        break;
    case 0x150:                              /* down */
        if (player_is_viewed_local() && ++H.menu_sel >= H.mode_count) H.menu_sel = H.mode_count - 1;
        break;
    default: return false;
    }
    return true;
}

/* ---------------------------------------------------------------- arrows */

/* HUD_UpdateArrow 0x486220: the arrow points from the target to the player's focus, at a distance
   from it: a quarter of the camera height + size + dist, where dist eases (8 pixels a frame) toward
   the target's distance while the target is in the view rectangle and shrinks to 0 outside it. The
   size is 0x20 (arrow), 0x26 (red arrow), 0x26 + 6 * player (another player's). */
static void update_arrow(Sprite *sp, const ArrowTarget *a, int32_t *dist)
{
    CameraTarget t = target_pos(a);
    CameraTarget f = focus_pos(g_player_local);
    const ViewRect *r = &g_players[g_player_local].rect;
    int size = 0;
    if (a->kind == 0) size = 0x20;
    else if (a->kind == 1) size = 0x26;
    else if (a->kind == 2) size = a->id * 6 + 0x26;
    else hud_fatal(-0x4a, 0x202, a->type);
    sp->angle = (int16_t)math_atan2(f.y - t.y, f.x - t.x);
    int tx = (int16_t)(t.x >> 16), ty = (int16_t)(t.y >> 16);
    int q = render_cam.height >> 2;
    if (tx < r->left || ty < r->top || r->right < tx || r->bottom < ty) {
        if (*dist > 0) *dist -= 8;
        if (*dist < 0) *dist = 0;
    } else {
        int dy = abs((int16_t)(f.y >> 16) - ty) - size, dx = abs((int16_t)(f.x >> 16) - tx) - size;
        int m = dx <= dy ? dy : dx;
        if (m < 0) m = 0;
        if (*dist + q + size < m - size) *dist += 8;
        else *dist = m - q - size;
    }
    int a10 = sp->angle & 0x3ff;
    sp->x = f.x - (q + *dist + size) * math_sin(a10);
    sp->y = f.y - (q + *dist + size) * math_cos(a10);
    sp->z = f.z;
}

static bool same_pos(const CameraTarget *a, const CameraTarget *b)
{
    return a->x == b->x && a->y == b->y && a->z == b->z;
}

/* ---------------------------------------------------------------- the frame */

/* HUD_Update 0x485d70 */
void hud_update(void)
{
    if (H.area_on) {
        int id = area_get_name((uint8_t)(render_cam.x >> 6), (uint8_t)(render_cam.y >> 6), H.area_text);
        if (id == H.area_last && !H.area_force) {
            H.area_changed = false;
        } else {
            H.area_changed = id != -1;
            H.area_force = false;
            H.area_last = id;
        }
    }
    if (H.zones_any)
        for (int i = 0; i < ZONE_SLOTS; i++) {
            ZoneSlot *z = &H.zone[i];
            if (!z->active) continue;
            if (z->timer == 0) {
                H.zone_count--;
                z->active = 0;
                if (H.zone_count == 0) {
                    H.zones_draw = H.zones_any = false;
                    break;
                }
            } else {
                z->timer--;
            }
        }
    if (H.arrow_on) {
        CameraTarget t = target_pos(&H.arrow), f = focus_pos(g_player_local);
        bool show = true;
        if (same_pos(&t, &f)) {
            if (H.queued.type == 2) {
                show = false;
            } else {
                H.arrow = H.queued;
                H.queued.type = 2;
                t = target_pos(&H.arrow);
                f = focus_pos(g_player_local);
                if (same_pos(&t, &f)) show = false;
            }
        }
        H.arrow_vis = show;
        if (show) update_arrow(&H.arrow_spr, &H.arrow, &H.arrow_dist);
    }
    if (H.red_on) {
        CameraTarget t = target_pos(&H.red), f = focus_pos(g_player_local);
        H.red_vis = !same_pos(&t, &f);
        if (H.red_vis) update_arrow(&H.red_spr, &H.red, &H.red_dist);
    }
    if (H.roof_on) {
        CameraTarget f = focus_pos(g_player_local);
        if (!map_is_covered(f.x, f.y, f.z)) {
            H.roof_vis = false;
        } else {
            H.roof_spr.x = f.x, H.roof_spr.y = f.y, H.roof_spr.z = f.z;
            H.roof_vis = true;
            H.roof_spr.angle = (int16_t)((f.angle - 0x200) & 0x3ff);
        }
    }
    if (H.others_on && H.others_draw)
        for (int n = player_first(); n > -1; n = player_next(n))
            if (n != g_player_local) {
                ArrowTarget a = { 4, (int16_t)n, 0, 0, 0, 2 };
                update_arrow(&H.other_spr[n], &a, &H.other_dist[n]);
            }
    if (H.wanted_on) {
        H.wanted = g_players[g_player_local].wanted_level;
        H.wanted_vis = H.wanted != 0;
        if (g_game.opt.opt5031c4) {
            char b[64];
            snprintf(b, sizeof b, exe_str(0x4b37cc), g_players[g_player_local].wanted_points);
            hud_clear_zone_text(0x58);
            hud_show_zone_text(b, 0x58);
        }
    }
    if (g_pager.update) pager_update();
    if (H.sub_timer_on && --H.sub_timer < 1) {
        H.sub_on = false;
        H.sub_timer_on = false;
        if (H.sub_pending) {
            H.sub_pending = false;
            hud_show_subtitle(H.sub_pending_kind, H.sub_pending_text);
        }
    }
    if (H.big_timer_on && --H.big_timer < 1) H.big_on = H.big_timer_on = false;
}

static void draw_sprite(Sprite *sp)
{
    sp->info = sprite_get_info(sp->frame);   /* the port: the info of the style loaded now */
    sprite_draw(sp);
}

/* HUD_Draw 0x483390, in its order: the area name into the zone texts, the debug texts, the zone
   texts, the scores, the arrows and the roof marker (sprites on), the wanted heads, the pager, the
   weapon and items, the subtitle, the score popups, the video menu, the big message, the quit
   prompt, the pause info. Nothing with the option 0x502f70. */
void hud_draw(void)
{
    if (g_game.opt.opt502f70 != 0 || !hud_fonts.res || poly_nrows <= 0) return;
    g_hud_frame.s = (Surface){ poly_rows[0], render_cam.w, poly_nrows, poly_pitch_px };
    g_hud_frame.view_w = render_cam.w;
    g_hud_frame.view_h = render_cam.h;
    g_hud_frame.res = hud_fonts.res;
    hud_build_cluts();
    if (H.area_changed) {
        hud_clear_zone_text(2);
        hud_show_zone_text(H.area_text, 2);
    }
    if (H.debug) {
        Player *p = &g_players[g_player_local];
        int kind = p->view_kind;
        CameraTarget t = player_view_target(g_player_local);
        char b[128];
        snprintf(b, sizeof b, exe_str(0x4b378c), exe_str(exe_u32(0x4b3654 + 4u * (uint32_t)kind)),
                 (int)(int16_t)p->view_id, t.x >> 22, t.y >> 22, t.z >> 22);
        hud_clear_zone_text(4);
        hud_show_zone_text(b, 4);
        if (kind == 0) {
            snprintf(b, sizeof b, exe_str(0x4b3778), (int)t.speed, (int)(uint8_t)car_get((int16_t)p->view_id)->damage);
            hud_clear_zone_text(5);
            hud_show_zone_text(b, 5);
        }
    }
    if (H.zones_draw) draw_zone_texts();
    player_update_score_digits();
    for (int n = player_first(); n > -1; n = player_next(n)) {
        ScoreRec *r = &H.score[n];
        if (++r->tick == 2) {
            r->tick = 0;
            if (r->state == 1) {
                if (--r->bounce < -6) r->state = 2;
            } else if (r->state == 2) {
                if (++r->bounce >= 0) r->state = 1;
            } else {
                r->bounce = 0;
            }
        }
        snprintf(r->str, sizeof r->str, exe_str(0x4b3774), g_players[n].score);
    }
    int count = 0, best = 0;
    for (int n = player_first(); n > -1; n = player_next(n)) {
        count++;
        H.score[n].score = g_players[n].score;
        if (best < H.score[n].score) best = H.score[n].score;
    }
    if (count > 1)
        for (int n = player_first(); n > -1; n = player_next(n)) {
            ScoreRec *r = &H.score[n];
            if (r->score == best && r->score != 0) {
                if (r->state == 0) r->state = 1;
            } else {
                r->state = 0, r->bounce = 0;
            }
        }
    for (int n = player_first(); n > -1; n = player_next(n))
        if (H.score[n].shown) draw_score(n);
    bool spr = g_game.opt.draw_sprites != 0;
    if (H.arrow_vis && spr) {
        if (g_player_count > 1) {
            if (++H.arrow_colour == 4) H.arrow_colour = 0;
            sprite_set_remap(&H.arrow_spr, hud_player_colour(H.arrow_colour));
        }
        draw_sprite(&H.arrow_spr);
    }
    if (H.red_vis && spr) draw_sprite(&H.red_spr);
    if (H.roof_vis && spr) draw_sprite(&H.roof_spr);
    if (H.others_draw && spr)
        for (int n = player_first(); n > -1; n = player_next(n))
            if (n != g_player_local) draw_sprite(&H.other_spr[n]);
    if (H.wanted_vis) draw_wanted();
    if (g_pager.visible) hud_draw_pager(&g_hud_frame.s);
    draw_weapon_info();
    if (H.sub_on) draw_subtitle();
    draw_score_popups();
    if (H.menu_on) draw_video_menu();
    if (H.big_on) draw_big_message();
    if (H.quit_on) {
        font_select(hud_fonts.sub);
        int h = font_height();
        int y = (g_hud_frame.view_h - (h * 3 + 3)) / 2;
        hud_draw_centered_line(&g_hud_frame.s, H.quit[0], y);
        hud_draw_centered_line(&g_hud_frame.s, H.quit[1], y + 1 + h);
        hud_draw_centered_line(&g_hud_frame.s, H.quit[2], y + 2 + h * 2);
    }
    const Player *lp = &g_players[g_player_local];
    if (H.pause_on && !lp->local2 && !lp->local1) draw_pause_info();
}
