/* The frontend's menu screens (0x4277c0-0x42b260) and their drawing helpers. */
#include "front/front_internal.h"
#include "exe.h"
#include "text.h"
#include <string.h>

enum { FRONT_W = 640, FRONT_H = 480 };

#define MT (front_fonts.mtext)
#define MH (front_fonts.mhead)
#define MM (front_fonts.mmiss)

static void draw(Surface *s, const Font *f, int x, int y, const char *t)
{
    if (f && t) font_draw_string(s, f, x, y, t);
}
static void draw_alt(Surface *s, const Font *f, int x, int y, const char *t)
{
    if (f && t) font_draw_string_alt(s, f, x, y, t);
}
static int width(const Font *f, const char *t) { return f && t ? font_string_width(f, t) : 0; }
static int width_alt(const Font *f, const char *t) { return f && t ? font_string_width_alt(f, t) : 0; }
/* x = 320 - width / 2, the frontend's usual centring */
static void draw_mid(Surface *s, const Font *f, int y, const char *t) { draw(s, f, 0x140 - width(f, t) / 2, y, t); }

/* ---------------------------------------------------------------- drawing helpers */

/* Front_DrawHighScores 0x427fc0: "high-scores" centred in 184 pixels at (x, y), then the level's
   three entries from the lowest (row y + 48) up to the best (y + 24), name at x and the score
   right-aligned in 72 pixels from x + 112. Entries with score 0 are skipped. An entry just set blinks
   (counter 0x51111c: drawn for 3 of every 6 counts; each blinking entry counts once per call). */
void front_draw_high_scores(Front *f, Surface *s, int x, int y, int level)
{
    if (MT) font_draw_centered(s, MT, f->tx[T_HIGH_SCORES], x, y, 0xb8);
    if (level < 0 || level >= SAVE_LEVELS) return;
    int yy = y + 0x30;
    uint8_t b = f->hs_blink;
    for (int k = 0; k < 3; k++, yy -= 0xc) {
        const SaveHiscore *e = &save_data.hiscore[level][k];
        if (e->score == 0) continue;
        if (f->hs_new[k]) {
            b++;
            if (b == 6) f->hs_blink = 0;
            else {
                f->hs_blink = b;
                if (b > 2) continue;
            }
        }
        char name[17], num[16];
        memcpy(name, e->name, 16);
        name[16] = 0;
        draw_alt(s, MM, x, yy, name);
        front_fmt(num, sizeof num, "%d", "d", e->score);   /* "%d" 0x4b07cc */
        int dx = 0x48 - width_alt(MM, num);
        if (dx < 0) dx = 0;
        draw_alt(s, MM, dx + 0x70 + x, yy, num);
        b = f->hs_blink;
    }
}

/* Front_DrawPlayerScores 0x429df0: the title centred in 224 pixels at (x - 26, y), then every
   unlocked level of the current table (best != -1), one row of 12 pixels each from y + 24: the
   mission name at x and the best score right-aligned in 72 pixels from x + 128. A best just set
   blinks (counter 0x51111d). */
void front_draw_player_scores(Front *f, Surface *s, int x, int y, int player, const char *title)
{
    if (MT) font_draw_centered(s, MT, title, x - 0x1a, y, 0xe0);
    SavePlayer *p = &save_data.player[player & 7];
    int drawn = -1;
    for (int c = 0; c < 3; c++) {
        const FrontCity *fc = &f->cities[c];
        int yy = y + (drawn * 3 + 6) * 4;
        for (int i = 0; i < fc->count; i++) {
            int level = fc->level[i];
            int32_t v = *front_best(p, level);
            if (v == -1) continue;
            yy += 0xc;
            drawn++;
            if (level >= 0 && level < 6 && f->best_new[level]) {
                if (++f->best_blink == 6) f->best_blink = 0;
                else if (f->best_blink > 2) continue;
            }
            draw_alt(s, MM, x, yy, i < 4 ? fc->mname[i] : "");
            char num[16];
            front_fmt(num, sizeof num, "%d", "d", v);
            int dx = 0x48 - width_alt(MM, num);
            if (dx < 0) dx = 0;
            draw_alt(s, MM, dx + 0x80 + x, yy, num);
        }
    }
}

/* Front_DrawPlayerPortrait 0x429f60: F_PLAYn (n = player mod 8) centred on x (rounded down to an even
   column) at y. Without a name: the name plate F_PLAYN at y + 150, the player's name centred at
   y + 164 and, when blinking (counter 0x51113c, 4 of 8 frames), "<" ">" (0x4b07d8 / 0x4b07e0) beside
   the plate. With a name (the rename buffer): the name in F_MHEAD and the "<" cursor after it. */
void front_draw_player_portrait(Front *f, Surface *s, int x, int y, int player, const char *name, int blink)
{
    int p = ((player % 8) + 8) % 8;
    if (++f->portrait_blink > 7) f->portrait_blink = 0;
    const FrontPictures *fp = &front_pictures;
    gfx_blit_image(s, ((x - fp->play[p].w / 2) / 2) * 2, y, &fp->play[p]);
    const char *lt = exe_str(0x4b07d8), *gt = exe_str(0x4b07e0);
    if (!name) {
        gfx_blit_image(s, ((x - fp->playn.w / 2) / 2) * 2, y + 0x96, &fp->playn);
        if (blink && f->portrait_blink < 4) {
            draw_alt(s, MT, width_alt(MT, lt) * -2 - fp->playn.w / 2 + x, y + 0xa4, lt);
            draw_alt(s, MT, fp->playn.w / 2 + x + width_alt(MT, gt), y + 0xa4, gt);
        }
        const char *nm = save_data.player[p].name;
        char buf[17];
        memcpy(buf, nm, 16);
        buf[16] = 0;
        draw_alt(s, MT, x - width_alt(MT, buf) / 2, y + 0xa4, buf);
    } else {
        draw_alt(s, MH, x - width_alt(MH, name) / 2, y + 0xa4, name);
        if (blink && f->portrait_blink < 4) draw_alt(s, MH, width_alt(MH, name) / 2 + x, y + 0xa4, lt);
    }
}

/* ---------------------------------------------------------------- main (city / chapter select) */

/* Front_ScreenMain 0x4277c0. Left / right: city (0..0x5101b8, mission back to 0); up / down: mission
   (0..max of the city); Esc: player select; Enter: the mission's section is read (Mission_SetIniSection,
   Mission_ReadIni) and the loading screen follows (network: the multiplayer options, or for a race the
   connection list); Space (single player, level's cutscene seen): the cutscene again.
   Drawing: the cities from 0 to 0x5101b8, each a logo animation (F_CITY4 / F_CITY1 / F_CITY3, frame
   counters 0x51112c: the selected city's runs, the others finish their cycle) at fixed positions,
   its name below and, for the selected one, its missions "%d: %s"; the key marker at the selected
   mission (at 0, 0 if none). */
void front_screen_main(Front *f, uint32_t in, Surface *s)
{
    int32_t *ch = front_chapter(f), *mi = front_mission(f);
    if (in & FI_LEFT) {
        front_sample(f, 0);
        *mi = 0;
        if (--*ch < 0) *ch = f->max_city;
    }
    if (in & FI_RIGHT) {
        front_sample(f, 1);
        *mi = 0;
        if (++*ch > f->max_city) *ch = 0;
    }
    if (in & FI_UP) {
        front_sample(f, 7);
        if (--*mi < 0) *mi = f->cities[*ch].max;
    }
    if (in & FI_DOWN) {
        front_sample(f, 8);
        if (++*mi > f->cities[*ch].max) *mi = 0;
    }
    if (in & FI_ESC) {
        front_set_screen(f, FS_PLAYERS);
        front_sample(f, 4);
    }
    if (in & FI_ENTER) {
        front_sample(f, 2);
        int section = f->cities[*ch].section[*mi & 3];
        if (f->hooks.mission) f->hooks.mission(section);
        if (f->mode == 0) {
            f->start_code = FRONT_PLAY;
            front_set_screen(f, FS_LOADING);
            front_apply_volumes(f);
        } else if (!front_city(f)->race[*mi]) {
            front_set_screen(f, FS_MULTI_OPTIONS);
        } else {
            net_set_role(0);
            f->lobby = 1;
            f->lobby_first = 1;
            if (net_enum_providers()) {
                if (net_provider_count() <= f->provider) f->provider = 0;
                front_set_screen(f, FS_CONNECTIONS);
            }
            front_save(f);
        }
    }
    SavePlayer *p = front_player(f);
    if (in & FI_SPACE && f->net == 0) {
        int level = front_sel_level(f);
        if (level >= 0 && level < SAVE_LEVELS && p->seen[level] == 1) {
            f->after_cut = 0;
            f->reason = 1;
            front_set_screen(f, FS_CUTSCENE);
            front_sample(f, 4);
        }
    }

    front_draw_background(s, 1, 1, f->clock_ms);
    static const int font_of[3] = {3, 0, 2}, x0[3] = {0x1d6, 0x50, 0x170}, dx[3] = {0x14, 8, 0};
    int y0[3] = {0xf2, 0xe6, f->mode != 0 ? 0x136 : 0x152};
    int mx = 0, my = 0;
    if (f->mode == 0) front_draw_high_scores(f, s, 0x1b8, 0x82, front_sel_level(f));
    for (int c = 0; c <= f->max_city && c < 3; c++) {
        int fi = font_of[c];
        const Font *cf = front_fonts.city[fi];
        if (!cf || !MT || !MM) continue;
        if (c == *ch || f->city_frame[fi] != 0)
            if (++f->city_frame[fi] == cf->count) f->city_frame[fi] = 0;
        char g[2] = {(char)(f->city_frame[fi] + 1), 0};
        int x = x0[c], y = y0[c];
        font_draw_string(s, cf, x, y, g);
        y += cf->height;
        font_draw_string_alt(s, MT, x, y, f->cities[c].name);
        y += MT->height;
        x += dx[c];
        if (c == *ch)
            for (int i = 0; i <= f->cities[c].max && i < 4; i++) {
                if (i == *mi) {
                    mx = x - 0x45;
                    my = y - 0x20;
                }
                char buf[128];
                front_fmt(buf, sizeof buf, f->tx[T_DSCOLON], "ds", i + 1, f->cities[c].mname[i]);
                font_draw_string_alt(s, MM, x, y, buf);
                y += MM->height;
            }
    }
    if (++f->main_marker > 0xc) f->main_marker = 0;
    char m[2] = {(char)(f->main_marker + 1), 0};
    draw(s, front_fonts.key, mx, my, m);
    front_draw_key_left(f, s, T_ESC_KEY, T_CANCEL);
    front_draw_key_right(f, s, T_RTN_KEY, T_PLAY);
    if (f->net == 0) {
        int level = front_sel_level(f);
        if (level >= 0 && level < SAVE_LEVELS && p->seen[level] == 1) front_draw_key_center(f, s, T_SPC_KEY, T_STORY);
    }
}

/* ---------------------------------------------------------------- loading */

/* Front_ScreenLoading 0x428bf0: returns the start code (0x510234) to WinMain on its first frame, so it
   stays on screen while the level loads. It also hands the options to the game (pager speed, effects,
   radio mode). Single player: the level's high scores and "<city> Chapter <n> :" at y 330; network:
   the target and the player's colour, the city name at y 300. Then the mission name, "Loading". */
void front_screen_loading(Front *f, uint32_t in, Surface *s)
{
    (void)in;
    f->ret = f->start_code;
    if (f->hooks.level_options)
        f->hooks.level_options(save_data.pager_speed, save_data.effects,
                               !f->demo || f->demo_music ? save_data.music_sequential != 0 : -1);
    front_draw_background(s, 1, 0, f->clock_ms);
    int y;
    char buf[256];
    if (f->mode == 0) {
        front_draw_high_scores(f, s, 0x1b8, 0x82, front_sel_level(f));
        y = 0x14a;
    } else {
        y = 300;
        int type = front_city(f)->race[*front_mission(f)] ? 2 : save_data.multi_target;
        const char *t = "";
        if (type == 0) { front_fmt(buf, sizeof buf, f->tx[T_WIN_SCORE], "d", save_data.score_target); t = buf; }
        else if (type == 1) { front_fmt(buf, sizeof buf, f->tx[T_WIN_KILLS], "d", save_data.kill_target); t = buf; }
        else if (type == 2) t = f->tx[T_WIN_RACE];
        draw_mid(s, MT, 0x168, t);
        front_fmt(buf, sizeof buf, f->tx[T_COLOUR], "s", f->hooks.colour_name ? f->hooks.colour_name() : "");
        draw_mid(s, MT, 0x186, buf);
    }
    const FrontCity *c = front_city(f);
    int mi = *front_mission(f);
    const char *title = c->name;
    if (f->mode == 0) {
        if (!text_wide()) front_fmt(buf, sizeof buf, f->tx[T_CHAPTER], "sd", c->name, mi + 1);
        else front_fmt(buf, sizeof buf, f->tx[T_CHAPTER], "ds", mi + 1, c->name);
        title = buf;
    }
    draw_mid(s, MT, y, title);
    const char *mn = c->mname[mi & 3];
    draw_alt(s, MT, 0x140 - width_alt(MT, mn) / 2, y + 0x1e, mn);
    front_draw_title(s, 0x198, f->tx[T_LOADING]);
}

/* ---------------------------------------------------------------- start menu */

/* Front_ScreenStart 0x428ee0: Play, and with the network (Net_IsActive, never in the demo) Gather
   Network, Join Network, then Options. Esc: the credits (and out). Play and Gather need the CD flag
   0x511108 (always set). */
void front_screen_start(Front *f, uint32_t in, Surface *s)
{
    int net = f->net_active && !f->demo;
    int items = net * 2 + 2;
    if (in & FI_UP) {
        front_sample(f, 7);
        if (--f->menu < 1) f->menu = items;
    }
    if (in & FI_DOWN) {
        front_sample(f, 8);
        if (++f->menu > items) f->menu = 1;
    }
    if (in & FI_ESC) {
        front_set_screen(f, FS_CREDITS);
        front_sample(f, 4);
    }
    if (in & FI_ENTER) {
        front_sample(f, 2);
        switch (f->menu) {
        case 1:
            if (!f->cd_ok) break;
            f->mode = 0;
            f->net = 0;
            front_set_screen(f, FS_PLAYERS);
            break;
        case 2:
            if (!net) { front_set_screen(f, FS_OPTIONS); break; }
            if (!f->cd_ok) break;
            f->mode = 1;
            f->net = 1;
            front_set_screen(f, FS_PLAYERS);
            break;
        case 3:
            if (!net) break;
            f->mode = 2;
            f->net = 1;
            front_set_screen(f, FS_PLAYERS);
            break;
        case 4:
            if (!net) break;
            front_set_screen(f, FS_OPTIONS);
            break;
        }
    }
    front_draw_background(s, 1, 0, f->clock_ms);
    front_draw_menu_item(s, 1, f->menu, f->tx[T_PLAY]);
    if (!net) front_draw_menu_item(s, 2, f->menu, f->tx[T_OPTIONS]);
    else {
        front_draw_menu_item(s, 2, f->menu, f->tx[T_GATHER]);
        front_draw_menu_item(s, 3, f->menu, f->tx[T_JOIN]);
        front_draw_menu_item(s, 4, f->menu, f->tx[T_OPTIONS]);
    }
    gfx_blit_image(s, 0x14, 0x186, &front_pictures.rstar);
    gfx_blit_image(s, 0x22c, 0x186, &front_pictures.rstarn);
    const char *msg = NULL;
    if (!f->cd_ok) msg = f->tx[T_CD_MESSAGE2];
    else if (f->demo) msg = f->tx[T_DEMO_MESSAGE];
    if (msg && MT) draw(s, MT, (FRONT_W - width(MT, msg)) / 2, (0xf0 - MT->height) * 2, msg);
    front_draw_key_center(f, s, T_ESC_KEY, T_QUIT);
}

/* ---------------------------------------------------------------- options */

/* Front_ScreenOptions 0x429180: Sound 0..7, Music 0..7, Text (pager) speed 1..3 (wraps), music mode
   radio / constant (not in the demo), transparency effects on / off. Enter saves PLAYER_A.DAT, Esc
   reads it back (undoing the changes); both return to the start menu. */
void front_screen_options(Front *f, uint32_t in, Surface *s)
{
    SaveData *d = &save_data;
    if (in & FI_UP) {
        front_sample(f, 7);
        if (--f->menu < 1) f->menu = 5;
    }
    if (in & FI_DOWN) {
        front_sample(f, 8);
        if (++f->menu > 5) f->menu = 1;
    }
    int dir = (in & FI_RIGHT) != 0;
    if (in & FI_LEFT) dir--;
    if (!(in & FI_ENTER)) {
        if (in & FI_ESC) {
            front_set_screen(f, FS_START);
            front_revert(f);
            front_apply_volumes(f);
            front_sample(f, 4);
        }
    } else {
        front_sample(f, 2);
        front_set_screen(f, FS_START);
        front_save(f);
    }
    if (dir != 0) {
        switch (f->menu) {
        case 1:
            if (dir > 0 ? d->sfx_volume > 6 : d->sfx_volume < 1) break;
            d->sfx_volume += (int8_t)dir;
            front_apply_volumes(f);
            front_sample(f, 0xe);
            break;
        case 2:
            if (dir > 0 ? d->music_volume < 7 : d->music_volume > 0) {
                d->music_volume += (int8_t)dir;
                front_apply_volumes(f);
            }
            break;
        case 3:
            d->pager_speed += (int8_t)dir;
            if (d->pager_speed > 3) d->pager_speed = 1;
            if (d->pager_speed < 1) d->pager_speed = 3;
            front_sample(f, d->pager_speed + 8);
            break;
        case 4:
            if (f->demo) break;
            if (d->music_sequential == 0) {
                front_sample(f, 5);
                d->music_sequential = 1;
            } else {
                front_sample(f, 6);
                d->music_sequential = 0;
            }
            break;
        case 5:
            d->effects = d->effects == 0;
            break;
        }
    }
    front_draw_background(s, 1, 0, f->clock_ms);
    front_draw_title(s, 0xb0, f->tx[T_OPTIONS]);
    char buf[256];   /* the shared buffer 0x510c78: an unknown pager speed shows the previous line */
    if (d->sfx_volume > 0) front_fmt(buf, sizeof buf, f->tx[T_SDCOLON], "sd", f->tx[T_SOUND], d->sfx_volume);
    else front_fmt(buf, sizeof buf, f->tx[T_SSCOLON], "ss", f->tx[T_SOUND], f->tx[T_OFF]);
    front_draw_menu_item_b(s, 1, f->menu, buf);
    if (d->music_volume > 0) front_fmt(buf, sizeof buf, f->tx[T_SDCOLON], "sd", f->tx[T_MUSIC], d->music_volume);
    else front_fmt(buf, sizeof buf, f->tx[T_SSCOLON], "ss", f->tx[T_MUSIC], f->tx[T_OFF]);
    front_draw_menu_item_b(s, 2, f->menu, buf);
    int sp = d->pager_speed == 1 ? T_SLOW : d->pager_speed == 2 ? T_NORMAL : d->pager_speed == 3 ? T_FAST : -1;
    if (sp >= 0) front_fmt(buf, sizeof buf, f->tx[T_SSCOLON], "ss", f->tx[T_TEXT], f->tx[sp]);
    front_draw_menu_item_b(s, 3, f->menu, buf);
    front_fmt(buf, sizeof buf, f->tx[T_SSCOLON], "ss", f->tx[T_MUSIC_MODE],
              f->tx[d->music_sequential ? T_CONSTANT : T_RADIO]);
    front_draw_menu_item_b(s, 4, f->menu, buf);
    front_fmt(buf, sizeof buf, f->tx[T_SSCOLON], "ss", f->tx[T_TRANS_EFFECTS], f->tx[d->effects ? T_ON : T_OFF]);
    front_draw_menu_item_b(s, 5, f->menu, buf);
    front_draw_key_left(f, s, T_ESC_KEY, T_CANCEL);
    front_draw_key_right(f, s, T_RTN_KEY, T_SAVE);
}

/* Front_ScreenMultiOptions 0x429650: the network game's end condition, score (10000 .. 999999999 in
   steps of 10000) or kills (1 .. 1000). Esc: back to the main screen, file re-read; Enter: the
   connection list (file saved). */
void front_screen_multi_options(Front *f, uint32_t in, Surface *s)
{
    SaveData *d = &save_data;
    if (!(in & FI_UP)) {
        if (in & FI_DOWN && ++f->menu > 2) f->menu = 1;
    } else if (--f->menu < 1)
        f->menu = 2;
    int dir = 0;
    if (in & FI_RIGHT) {
        front_sample(f, 1);
        dir = 1;
    } else if (in & FI_LEFT) {
        front_sample(f, 0);
        dir = -1;
    }
    if (!(in & FI_ESC)) {
        if (in & FI_ENTER) {
            front_sample(f, 2);
            net_set_role(0);
            f->lobby = 1;
            f->lobby_first = 1;
            if (net_enum_providers()) {
                if (net_provider_count() <= f->provider) f->provider = 0;
                front_set_screen(f, FS_CONNECTIONS);
            }
            front_save(f);
        }
    } else {
        front_set_screen(f, FS_MAIN);
        front_revert(f);
        front_sample(f, 4);
    }
    if (dir != 0) {
        if (f->menu == 1) d->multi_target = d->multi_target == 0;
        else if (f->menu == 2) {
            if (d->multi_target == 0) {
                d->score_target += dir * 10000;
                if (d->score_target < 10000) d->score_target = 10000;
                else if (d->score_target > 999999999) d->score_target = 999999999;
            } else if (d->multi_target == 1) {
                d->kill_target += dir;
                if (d->kill_target < 1) d->kill_target = 1;
                else if (d->kill_target > 1000) d->kill_target = 1000;
            }
        }
    }
    front_draw_background(s, 1, 0, f->clock_ms);
    front_draw_title(s, 0xb0, f->tx[T_MULTI_OPTIONS]);
    char buf[256];
    if (d->multi_target == 0 || d->multi_target == 1) {
        bool score = d->multi_target == 0;
        front_fmt(buf, sizeof buf, f->tx[T_SSCOLON], "ss", f->tx[T_END_GAME], f->tx[score ? T_SCORE : T_KILLS]);
        front_draw_menu_item_b(s, 1, f->menu, buf);
        front_fmt(buf, sizeof buf, f->tx[T_SDCOLON], "sd", f->tx[score ? T_END_SCORE : T_END_KILLS],
                  score ? d->score_target : d->kill_target);
        front_draw_menu_item_b(s, 2, f->menu, buf);
    }
    front_draw_key_left(f, s, T_ESC_KEY, T_CANCEL);
    front_draw_key_right(f, s, T_RTN_KEY, T_PLAY);
}

/* ---------------------------------------------------------------- players */

/* Front_ScreenPlayerSelect 0x429a20: left / right through the 8 slots (in French, German and Italian
   only slots 1, 5, 6, 7), Enter: the main screen (join: the connection list), Esc: the start menu
   (file saved), Del / Backspace: rename (file saved), R: reset (single player). */
void front_screen_player_select(Front *f, uint32_t in, Surface *s)
{
    int8_t cur = (int8_t)save_data.current;
    if (in & FI_LEFT) {
        front_sample(f, 3);
        if (--cur < 0) cur = 7;
        if (text_is_foreign()) {
            if (cur == 0) cur = 7;
            else if (cur > 1 && cur < 5) cur = 1;
        }
    }
    if (in & FI_RIGHT) {
        front_sample(f, 3);
        if (++cur > 7) cur = 0;
        if (text_is_foreign()) {
            if (cur == 0) cur = 1;
            else if (cur > 1 && cur < 5) cur = 5;
        }
    }
    save_data.current = (uint8_t)cur;
    if (in & FI_ESC) {
        front_set_screen(f, FS_START);
        front_save(f);
        front_sample(f, 4);
    }
    if (in & FI_ENTER) {
        front_sample(f, 2);
        if (f->hooks.player_name) f->hooks.player_name(0, front_player(f)->name, cur);
        if (f->mode == 2) {
            net_set_role(1);
            f->lobby = 0;
            f->lobby_first = 1;
            if (net_enum_providers()) {
                if (net_provider_count() <= f->provider) f->provider = 0;
                front_set_screen(f, FS_CONNECTIONS);
            }
        } else
            front_set_screen(f, FS_MAIN);
    }
    if (in & FI_DELETE) {
        front_sample(f, 0xc);
        front_save(f);
        front_set_screen(f, FS_RENAME);
    }
    if (in & FI_CHAR && (in & 0xff0000) == 0x720000 && f->mode == 0) {   /* 'r' */
        front_sample(f, 0xd);
        front_set_screen(f, FS_RESET);
    }
    front_draw_background(s, 1, 0, f->clock_ms);
    front_draw_player_portrait(f, s, 0x13f, 0xc0, cur, NULL, 1);
    if (f->mode == 0) front_draw_player_scores(f, s, 0x19e, 0xca, cur, f->tx[T_SCORES]);
    char buf[256];
    front_fmt(buf, sizeof buf, f->tx[T_SSCOLON], "ss", f->tx[T_DEL_KEY], f->tx[T_RENAME]);
    draw(s, MM, 0x1b8, 400, buf);
    if (f->mode == 0 && MM) {
        front_fmt(buf, sizeof buf, f->tx[T_SSCOLON], "ss", f->tx[T_R_KEY], f->tx[T_RESET]);
        draw(s, MM, 0x1b8, MM->height + 400, buf);
    }
    front_draw_key_left(f, s, T_ESC_KEY, T_CANCEL);
    front_draw_key_right(f, s, T_RTN_KEY, T_PLAY);
}

/* The cheat names (pointers 0x4af660..0x4af69c) are stored with a filler character between the
   letters: the name matches if each of its characters equals every second byte of the string and
   the string ends there. */
static bool cheat_match(const char *name, int k)
{
    uint32_t va = exe_u32(0x4af660 + 4u * (unsigned)k);
    if (!va) return false;
    size_t i = 0;
    for (; name[i]; i++) {
        const uint8_t *c = exe_data(va + 2 * (uint32_t)i, 1);
        if (!c || *c != (uint8_t)name[i]) return false;
    }
    const uint8_t *c = exe_data(va + 2 * (uint32_t)i, 1);
    return c && *c == 0;
}

/* The effect of cheat k of the list (Front_ScreenRename). */
static void cheat_apply(Front *f, int k)
{
    SavePlayer *p = front_player(f);
    switch (k) {
    case 0: case 1: case 2:   /* every level of the current table unlocked */
        for (int c = 0; c < 3; c++)
            for (int i = 0; i < f->cities[c].count; i++)
                if (*front_best(p, f->cities[c].level[i]) == -1) *front_best(p, f->cities[c].level[i]) = 0;
        break;
    case 3: case 4: f->cheats.f502f35 = true; break;
    case 5: case 6: f->cheats.f5031ec = true; break;
    case 7: case 8: f->cheats.f503198 = true; break;
    case 9: f->cheats.f503194 = true; break;
    case 10: for (int l = 0; l < SAVE_LEVELS; l++) p->seen[l] = 1; break;   /* every cutscene */
    case 11: save_data.language = 99; break;    /* SPECIAL.FXT from the next start */
    case 12: save_data.language = -1; break;
    case 13: f->cheats.f502f74 = true; break;
    case 14: f->cheats.f50318c = true; break;
    case 15: f->cheats.f5031e5 = true; break;
    }
}

/* Front_ScreenRename 0x42a160: typing (14 characters at most; Shift gives upper case, Space a space),
   Del / Backspace. Enter: the name (empty: the slot's default name from 0x4a7394) and, outside the
   demo, the cheat names; Esc: the file re-read. Both go back to the player select. */
void front_screen_rename(Front *f, uint32_t in, Surface *s)
{
    size_t len1 = strlen(f->rename) + 1;
    if (in & FI_DELETE && len1 != 1) {
        front_sample(f, 0xd);
        f->rename[len1 - 2] = 0;
    }
    if ((in & FI_CHAR || in & FI_SPACE) && len1 - 1 < 0xe) {
        front_sample(f, 0xc);
        char c;
        if (!(in & FI_SPACE)) {
            c = (char)(in >> 16);
            if (in & FI_SHIFT && c >= 'a' && c <= 'z') c -= 0x20;
        } else
            c = ' ';
        f->rename[len1] = 0;
        f->rename[len1 - 1] = c;
    }
    if (in & FI_ESC) {
        front_set_screen(f, FS_PLAYERS);
        front_revert(f);
        front_sample(f, 4);
    } else if (in & FI_ENTER) {
        front_set_screen(f, FS_PLAYERS);
        SavePlayer *p = front_player(f);
        const char *src = len1 == 1 ? exe_str(exe_u32(0x4a7394 + 8u * (save_data.current & 7))) : f->rename;
        size_t n = strlen(src);
        if (n > 15) n = 15;
        memcpy(p->name, src, n);
        p->name[n] = 0;
        int snd = 2;
        if (!f->demo) {
            char nm[17];
            memcpy(nm, p->name, 16);
            nm[16] = 0;
            for (int k = 0; k < 16; k++)
                if (cheat_match(nm, k)) {
                    cheat_apply(f, k);
                    snd = 7;
                    break;
                }
        }
        front_sample(f, snd);
        front_save(f);
    }
    int8_t cur = (int8_t)save_data.current;
    front_draw_background(s, 1, 0, f->clock_ms);
    front_draw_player_portrait(f, s, 0x13f, 0xc0, cur, f->rename, 1);
    if (f->mode == 0) front_draw_player_scores(f, s, 0x19e, 0xca, cur, f->tx[T_SCORES]);
    front_draw_key_left(f, s, T_ESC_KEY, T_CANCEL);
    front_draw_key_right(f, s, T_RTN_KEY, len1 == 1 ? T_RESET : T_FINISH);
}

/* Front_ScreenResetPlayer 0x42b260: Cancel / Reset (Esc = Cancel). Reset gives the slot its default
   name, level 0 only, no cutscenes seen, selections 0, and saves. */
void front_screen_reset_player(Front *f, uint32_t in, Surface *s)
{
    if (in & FI_UP) {
        front_sample(f, 7);
        if (--f->menu < 1) f->menu = 2;
    }
    if (in & FI_DOWN) {
        front_sample(f, 8);
        if (++f->menu > 2) f->menu = 1;
    }
    if (in & FI_ESC) {
        front_sample(f, 4);
        in |= FI_ENTER;
        f->menu = 1;
    }
    if (in & FI_ENTER || in & FI_SPACE) {
        front_sample(f, 2);
        if (f->menu == 1) front_set_screen(f, FS_PLAYERS);
        else if (f->menu == 2) {
            SavePlayer *p = front_player(f);
            const char *src = exe_str(exe_u32(0x4a7394 + 8u * (save_data.current & 7)));
            size_t n = strlen(src);
            if (n > 15) n = 15;
            memcpy(p->name, src, n);
            p->name[n] = 0;
            p->best[0] = 0;
            for (int l = 1; l < SAVE_LEVELS; l++) p->best[l] = -1;
            for (int l = 0; l < SAVE_LEVELS; l++) p->seen[l] = 0;
            p->chapter[0] = p->mission[0] = p->chapter[1] = p->mission[1] = 0;
            front_set_screen(f, FS_PLAYERS);
            front_save(f);
        }
    }
    front_draw_background(s, 1, 0, f->clock_ms);
    front_draw_menu_item(s, 1, f->menu, f->tx[T_CANCEL]);
    front_draw_menu_item(s, 2, f->menu, f->tx[T_RESET]);
    front_draw_player_portrait(f, s, 0x1bf, 0xc0, (int8_t)save_data.current, NULL, 0);
}

/* ---------------------------------------------------------------- results */

static void net_results(Front *f, Surface *s)
{
    const FrontGameResult *r = &f->res;
    char buf[256];
    if (!front_city(f)->race[*front_mission(f)]) {
        int n = 0;
        for (int i = 0; i < 4; i++) n += r->score[i] != -1;
        int x = n == 2 ? 0x92 : n == 3 ? 0x49 : 0;
        front_draw_title(s, 0x1a, f->tx[T_FINAL_SCORES]);
        draw(s, MT, x, 0xac, f->tx[T_SCORE]);
        draw(s, MT, x, 0xc4, f->tx[T_KILLS]);
        x += 0x38;
        for (int i = 0; i < 4; i++) {
            if (r->score[i] == -1) continue;
            bool show = true;
            if (f->winner[i]) {
                if (++f->winner_blink[i] == 6) f->winner_blink[i] = 0;
                show = f->winner_blink[i] < 3;
            }
            if (show && MT) {
                front_fmt(buf, sizeof buf, i == r->local ? "%s" : "(%s)", "s", r->name[i]);   /* 0x4b07ec / 0x4b07e4 */
                font_draw_centered(s, MT, buf, x, 0x88, 0x74);
                front_fmt(buf, sizeof buf, "%d", "d", r->score[i]);
                font_draw_centered(s, MT, buf, x, 0xac, 0x74);
                front_fmt(buf, sizeof buf, "%d", "d", r->frags[i]);
                font_draw_centered(s, MT, buf, x, 0xc4, 0x74);
            }
            x += 0x92;
        }
    } else {
        front_draw_title(s, 0x1a, f->tx[T_RACE_RESULTS]);
        for (int i = 0; i < 4; i++) {
            if (r->score[i] == -1) continue;
            int rank = r->rank[i], v = r->race_time[i], y = rank * 0x24 + 0x6e;
            if (v == -1) draw(s, MT, 0x170, y, f->tx[T_DNF]);
            else {
                /* "%02d:%02d:%02d" (0x4b07f0): minutes (at most 99), seconds, hundredths of 1/25 s ticks */
                int mn = v / 1500, sc = (v % 1500) / 25, base = mn * 60;
                if (mn > 99) mn = 99;
                front_fmt(f->race_buf, sizeof f->race_buf, "%02d:%02d:%02d", "ddd", mn, sc,
                          ((v - (sc + base) * 25) * 100) / 25);
                draw(s, MT, 0x170, y, f->race_buf);
            }
            front_fmt(buf, sizeof buf, i == r->local ? "%s" : "(%s)", "s", r->name[i]);
            draw(s, MT, 0xd0, y, buf);
            front_fmt(buf, sizeof buf, "%d", "d", rank + 1);
            draw(s, MT, 0xb0, y, buf);
        }
    }
    const char *t = NULL;
    switch (f->result_class) {
    case 0: {
        const Font *cf = front_fonts.city[1];
        if (cf) {
            char g[2] = {(char)(f->win_anim + 1), 0};
            font_draw_string(s, cf, 0x40, 0x13c, g);
            if (++f->win_anim == cf->count) f->win_anim = 0;
        }
        t = f->tx[T_WINNER];
        break;
    }
    case 1: t = f->tx[T_LOSER]; break;
    case 2: t = f->tx[T_NO_WIN]; break;
    case 3: t = f->tx[T_ABANDON]; break;
    }
    if (t) front_draw_title(s, 0x13c, t);
}

/* Front_ScreenResults 0x42a750, after a game. Single player: the score, the level's high scores, the
   player's bests, the crime counts, the result text, the portrait, missions passed and secrets found.
   Network: final scores (winners blink) or race results and the outcome. Enter: main screen (single
   player: the selection moves on after a completed mission); Esc: start menu; Space (completed, the
   cutscene seen): the cutscene again. */
void front_screen_results(Front *f, uint32_t in, Surface *s)
{
    const FrontGameResult *r = &f->res;
    int8_t cur = (int8_t)save_data.current;
    bool wide = text_wide();
    char buf[256];
    front_draw_background(s, 0, 0, f->clock_ms);
    if (f->mode == 0) {
        front_fmt(buf, sizeof buf, f->tx[T_SDCOLON], "sd", f->tx[T_SCORE], r->score[0]);
        front_draw_title(s, 0x1a, buf);
        front_draw_high_scores(f, s, 0x1a2, 0x70, front_sel_level(f));
        front_fmt(buf, sizeof buf, f->tx[T_SHIGHEST], "s", front_player(f)->name);
        front_draw_player_scores(f, s, 0x191, wide ? 0xaf : 0xac, cur, buf);
        if (MT) font_draw_centered(s, MT, f->tx[T_CRIMES], 0x40, 0x78, 0x78);
        int h = MM ? MM->height : 0;
        static const int crime[7] = {T_CRIME_RTA, T_CRIME_HAR, T_CRIME_HIJ, T_CRIME_CAR, T_CRIME_SHO, T_CRIME_MUR,
                                     T_CRIME_BAN};
        static const int kind[7] = {2, 3, 4, 5, 7, 8, 9};
        for (int k = 0; k < 7; k++) draw(s, MM, 0x40, 0x9e + k * h, f->tx[crime[k]]);
        if (MT) font_draw_wrapped(s, MT, 0x40, wide ? 0x147 : 0x138, wide ? 0x21c : 0x1fe, f->result_text);
        for (int k = 0; k < 7; k++) {
            front_fmt(buf, sizeof buf, "%d", "d", (int16_t)r->kills[kind[k]]);
            draw(s, MM, 0xc0, 0x9e + k * h, buf);
        }
        const Image *pic = &front_pictures.play[cur & 7];
        gfx_blit_image(s, (FRONT_W - pic->w) / 2, 0x6e, pic);
        /* "%s %d / %d" (0x4b0800) */
        front_fmt(buf, sizeof buf, "%s %d / %d", "sdd", f->tx[T_MISSIONS_COMP], r->mission_counter, r->mission_total);
        draw(s, MM, (FRONT_W - width(MM, buf)) / 2 - 2, wide ? 0x115 : 0x109, buf);
        front_fmt(buf, sizeof buf, "%s %d / %d", "sdd", f->tx[T_SECRETS], r->secret_counter, r->secret_total);
        draw(s, MM, (FRONT_W - width(MM, buf)) / 2 - 2, wide ? 0x126 : 0x115, buf);
    } else
        net_results(f, s);
    front_draw_key_right(f, s, T_RTN_KEY, T_PLAY);
    front_draw_key_left(f, s, T_ESC_KEY, T_MENU);
    SavePlayer *p = front_player(f);
    bool seen = f->cut_level >= 0 && f->cut_level < SAVE_LEVELS && p->seen[f->cut_level] != 0;
    if (seen && f->reason == 1) front_draw_key_center(f, s, T_SPC_KEY, T_STORY);
    if (in & FI_ESC) {
        front_set_screen(f, FS_START);
        front_advance_mission(f);
        front_sample(f, 4);
        return;
    }
    if (!(in & FI_ENTER)) {
        if (seen && in & FI_SPACE && f->reason == 1) {
            front_set_screen(f, FS_CUTSCENE);
            front_sample(f, 4);
        }
        return;
    }
    front_sample(f, 2);
    if (f->mode == 0) {
        front_set_screen(f, FS_MAIN);
        front_advance_mission(f);
    } else if (f->mode == 1)
        front_set_screen(f, FS_MAIN);
    else if (f->mode == 2) {
        net_set_role(1);
        f->lobby = 0;
        f->lobby_first = 1;
        if (net_enum_providers()) {
            if (net_provider_count() <= f->provider) f->provider = 0;
            front_set_screen(f, FS_CONNECTIONS);
        }
    }
}
