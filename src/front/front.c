/* The frontend state machine (0x426320-0x42b7a0) and WinMain's frontend loop (0x437230). */
#include "front/front_internal.h"
#include "exe.h"
#include "text.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { FRONT_W = 640, FRONT_H = 480 };

/* ---------------------------------------------------------------- helpers */

typedef struct {
    bool str;
    const char *s;
    int d;
} FArg;

void front_fmt(char *dst, size_t cap, const char *fmt, const char *types, ...)
{
    FArg a[8];
    int na = 0;
    va_list ap;
    va_start(ap, types);
    for (; types && *types && na < 8; types++, na++) {
        a[na].str = *types == 's';
        if (a[na].str) a[na].s = va_arg(ap, const char *);
        else a[na].d = va_arg(ap, int);
    }
    va_end(ap);
    size_t o = 0;
    int ai = 0;
    if (!cap) return;
    if (!fmt) fmt = "";
    for (const char *p = fmt; *p && o + 1 < cap; p++) {
        if (*p != '%') { dst[o++] = *p; continue; }
        const char *q = p + 1;
        if (*q == '%') { dst[o++] = '%'; p = q; continue; }
        bool zero = false, left = false;
        for (; *q == '0' || *q == '-'; q++) {
            if (*q == '0') zero = true;
            else left = true;
        }
        int width = 0;
        for (; *q >= '0' && *q <= '9'; q++) width = width * 10 + (*q - '0');
        if (*q != 's' && *q != 'd' && *q != 'i' && *q != 'u') { dst[o++] = *p; continue; }
        char tmp[32];
        const char *v = "";
        if (ai < na) {
            if (a[ai].str && *q == 's') v = a[ai].s ? a[ai].s : "(null)";
            else if (a[ai].str) v = "0";
            else { snprintf(tmp, sizeof tmp, "%d", a[ai].d); v = tmp; }
            ai++;
        }
        int n = (int)strlen(v), pad = width > n ? width - n : 0;
        if (!left) {
            if (zero && pad && *v == '-' && o + 1 < cap) {   /* the sign goes before the zeros */
                dst[o++] = *v++;
                n--;
            }
            for (; pad > 0 && o + 1 < cap; pad--) dst[o++] = zero ? '0' : ' ';
        }
        for (int i = 0; i < n && o + 1 < cap; i++) dst[o++] = v[i];
        for (; pad > 0 && o + 1 < cap; pad--) dst[o++] = ' ';
        p = q;
    }
    dst[o] = 0;
}

void front_sample(const Front *f, int n)
{
    if (f->hooks.sample) f->hooks.sample(n);
}

/* The original writes ..\gtadata\player_a.dat ("wb" 0x4b07b4, fatal -202 / -204 when it can't); the
   port writes the user file (savedata.c). */
void front_save(Front *f)
{
    (void)f;
    save_store();
}

/* The Esc paths re-read the file ("rb" 0x4b07dc) to undo the changes made on the screen. */
void front_revert(Front *f)
{
    (void)f;
    save_reload();
}

static void draw_key(Front *f, Surface *s, int key, int label, int where)
{
    const Font *mt = front_fonts.mtext;
    if (!mt) return;
    char buf[256];
    front_fmt(buf, sizeof buf, f->tx[T_SSCOLON], "ss", f->tx[key], f->tx[label]);
    int x = 0;
    if (where == 1) x = FRONT_W - font_string_width(mt, buf);
    if (where == 2) x = (FRONT_W - font_string_width(mt, buf)) / 2;
    font_draw_string(s, mt, x, FRONT_H - mt->height, buf);
}
void front_draw_key_left(Front *f, Surface *s, int key, int label) { draw_key(f, s, key, label, 0); }
void front_draw_key_right(Front *f, Surface *s, int key, int label) { draw_key(f, s, key, label, 1); }
void front_draw_key_center(Front *f, Surface *s, int key, int label) { draw_key(f, s, key, label, 2); }

/* Front_DrawEnterPrompt 0x426fd0: "<rtn-key>: <label>" right-aligned on the bottom row. */
void front_draw_enter_prompt(Front *f, Surface *s, const char *label)
{
    const Font *mt = front_fonts.mtext;
    if (!mt) return;
    char buf[256];
    front_fmt(buf, sizeof buf, f->tx[T_SSCOLON], "ss", f->tx[T_RTN_KEY], label);
    font_draw_string(s, mt, FRONT_W - font_string_width(mt, buf), FRONT_H - mt->height, buf);
}

int front_sel_city(const Front *f) { return *front_chapter(f); }
int front_sel_mission(const Front *f) { return *front_mission(f); }
int front_sel_level(const Front *f) { return front_city(f)->level[*front_mission(f)]; }

/* Front_GetCurrentPlayerName 0x426990: the record of the current slot (its name comes first). */
const char *front_current_player_name(const Front *f) { return front_player(f)->name; }
/* Front_GetCurrentPlayerIndex 0x4269b0 */
int front_current_player(const Front *f) { (void)f; return (int8_t)save_data.current; }

/* Front_GetMultiTarget 0x4269c0. In single player only the type is written. */
void front_multi_target(const Front *f, int *type, int *target)
{
    if (f->mode == 0) {
        *type = -1;
        return;
    }
    *type = front_city(f)->race[*front_mission(f)] ? 2 : save_data.multi_target;
    *target = save_data.multi_target == 0 ? save_data.score_target : save_data.kill_target;
}

/* Front_SelectMission 0x4268d0: every city of the current table is searched (a section found in a
   later city wins over an earlier one); then, unless the selection is a race, the target type and its
   value are stored. */
void front_select_mission(Front *f, int section, int type, int target)
{
    SavePlayer *p = front_player(f);
    for (int c = 0; c < 3; c++)
        for (int i = 0; i < f->cities[c].count; i++)
            if (f->cities[c].section[i] == section) {
                p->mission[f->net] = i;
                p->chapter[f->net] = c;
                break;
            }
    if (!front_city(f)->race[*front_mission(f)]) {
        save_data.multi_target = (int8_t)type;
        if (type == 0) save_data.score_target = target;
        else save_data.kill_target = target;
    }
}

/* Front_AdvanceMission 0x42b1e0: after a completed mission (0x513230 == 1) of a level below 5, the
   next mission, or the first of the next city. */
void front_advance_mission(Front *f)
{
    if (f->reason != 1) return;
    int32_t *ch = front_chapter(f), *mi = front_mission(f);
    const FrontCity *c = &f->cities[*ch];
    if (c->level[*mi] >= 5) return;
    if (*mi == c->count - 1) {
        *mi = 0;
        (*ch)++;
    } else
        (*mi)++;
}

/* ---------------------------------------------------------------- settings and texts */

/* The chapter tables 0x4af4a0 (single player) and 0x4af418 (network), 3 x 0x2c bytes each. */
static bool load_tables(Front *f)
{
    for (int t = 0; t < 2; t++)
        for (int c = 0; c < 3; c++) {
            const uint8_t *e = exe_data((t ? 0x4af418u : 0x4af4a0u) + 0x2cu * c, 0x2c);
            if (!e) return false;
            FrontCity *fc = t ? &f->tab_net[c] : &f->tab_single[c];
            memset(fc, 0, sizeof *fc);
            fc->name = "";
            fc->count = (int8_t)e[4];
            for (int i = 0; i < 5; i++) fc->level[i] = (int8_t)e[5 + i];
            for (int i = 0; i < 4; i++) fc->section[i] = (int16_t)(e[0xa + 2 * i] | e[0xb + 2 * i] << 8);
            for (int i = 0; i < 6; i++) fc->race[i] = (int8_t)e[0x12 + i];
            for (int i = 0; i < 4; i++) fc->mname[i] = "";
            fc->max = (int8_t)e[0x28];
        }
    f->cities = f->tab_single;
    return true;
}

static const char *get_text(Front *f, const char *key)
{
    const char *t = text_get(key);
    if (!t) {   /* the original: fatal -85 / -86 */
        snprintf(f->error, sizeof f->error, "%s", text_error() ? text_error() : key);
        return "";
    }
    return t;
}

/* The key of each frontend string (string addresses in the exe, in the order of the globals). */
static const uint32_t text_key_va[T_COUNT] = {
    [T_SSCOLON] = 0x4b0790, [T_SDCOLON] = 0x4b0788, [T_DSCOLON] = 0x4b0780, [T_CD_MESSAGE] = 0x4b0774,
    [T_HIGH_SCORES] = 0x4b0768, [T_WIN_SCORE] = 0x4b075c, [T_WIN_RACE] = 0x4b0750,
    [T_WIN_KILLS] = 0x4b0744, [T_ON] = 0x4b0740, [T_OFF] = 0x4b073c, [T_CHAPTER] = 0x4b0730,
    [T_SPC_KEY] = 0x4b0728, [T_RTN_KEY] = 0x4b0720, [T_ESC_KEY] = 0x4b0718, [T_STATUS] = 0x4b0710,
    [T_QUIT] = 0x4b0708, [T_PLAY] = 0x4b0700, [T_CANCEL] = 0x4b06f8, [T_COMMSFAIL] = 0x4b06ec,
    [T_COMMSVERSION] = 0x4b06d8, [T_SVGAERROR] = 0x4b06cc, [T_LOADING] = 0x4b06c4,
    [T_LOADING_DEMO] = 0x4b06b8, [T_GATHER] = 0x4b06a8, [T_JOIN] = 0x4b069c, [T_OPTIONS] = 0x4b0694,
    [T_SOUND] = 0x4b068c, [T_MUSIC] = 0x4b0684, [T_TEXT] = 0x4b067c, [T_SLOW] = 0x4b0674,
    [T_NORMAL] = 0x4b066c, [T_FAST] = 0x4b0664, [T_MUSIC_MODE] = 0x4b0658,
    [T_TRANS_EFFECTS] = 0x4b0648, [T_CONSTANT] = 0x4b063c, [T_RADIO] = 0x4b0634, [T_SAVE] = 0x4b062c,
    [T_MULTI_OPTIONS] = 0x4b061c, [T_END_GAME] = 0x4b0610, [T_END_SCORE] = 0x4b0604,
    [T_SCORE] = 0x4b05fc, [T_KILLS] = 0x4b05f4, [T_END_KILLS] = 0x4b05e8, [T_SCORES] = 0x4b05e0,
    [T_DEL_KEY] = 0x4b05d8, [T_RENAME] = 0x4b05d0, [T_R_KEY] = 0x4b05c8, [T_RESET] = 0x4b05c0,
    [T_FINISH] = 0x4b05b8, [T_CRIME_RTA] = 0x4b05ac, [T_CRIME_HAR] = 0x4b05a0,
    [T_CRIME_HIJ] = 0x4b0594, [T_CRIME_CAR] = 0x4b0588, [T_CRIME_SHO] = 0x4b057c,
    [T_CRIME_MUR] = 0x4b0570, [T_CRIME_BAN] = 0x4b0564, [T_DNF] = 0x4b0554, [T_SHIGHEST] = 0x4b0548,
    [T_CRIMES] = 0x4b0540, [T_MISSIONS_COMP] = 0x4b0530, [T_SECRETS] = 0x4b0520,
    [T_RACE_RESULTS] = 0x4b0510, [T_FINAL_SCORES] = 0x4b0500, [T_ABANDON] = 0x4b04f8,
    [T_WINNER] = 0x4b04f0, [T_LOSER] = 0x4b04e8, [T_NO_WIN] = 0x4b04e0, [T_MENU] = 0x4b04d8,
    [T_STORY] = 0x4b04d0, [T_COLOUR] = 0x4b04c8, [T_CD_TITLE] = 0x4b04bc, [T_CD_TEXT] = 0x4b04b4,
    [T_CD_MESSAGE2] = 0x4b0774, [T_DEMO_MESSAGE] = 0x4b04a4, [T_FIX1] = 0x4b049c, [T_FIX2] = 0x4b0494,
    [T_FIX3] = 0x4b048c, [T_FIX4] = 0x4b0484, [T_FIX5] = 0x4b047c,
};

/* Front_LoadTexts 0x426320, once (0x511124): the city names ("city%d" 0x4b07a4) into both tables,
   the mission names ("mission%d" 0x4b0798 of each section) of both tables, then the strings. */
void front_load_texts(Front *f)
{
    if (f->texts_loaded) return;
    f->texts_loaded = true;
    char key[32];
    for (int c = 0; c < 3; c++) {
        front_fmt(key, sizeof key, exe_str(0x4b07a4), "d", c);
        f->tab_single[c].name = f->tab_net[c].name = get_text(f, key);
    }
    for (int t = 0; t < 2; t++)
        for (int c = 0; c < 3; c++) {
            FrontCity *fc = t ? &f->tab_net[c] : &f->tab_single[c];
            for (int i = 0; i < fc->count && i < 4; i++) {
                front_fmt(key, sizeof key, exe_str(0x4b0798), "d", fc->section[i]);
                fc->mname[i] = get_text(f, key);
            }
        }
    for (int i = 0; i < T_COUNT; i++) f->tx[i] = get_text(f, exe_str(text_key_va[i]));
}

/* Front_BuildChapterList 0x427630. Single player: for each city the highest mission whose level is
   unlocked (best != -1) becomes the city's max, and the last city with one becomes 0x5101b8 (-1 if
   none); max is set to -1 on every step of the search, so a city without missions keeps its value.
   The demo allows only the first mission of the first city. Network: the network table, all 3 cities. */
void front_build_chapter_list(Front *f)
{
    if (f->mode != 0) {
        f->cities = f->tab_net;
        f->max_city = 2;
        return;
    }
    f->cities = f->tab_single;
    f->max_city = -1;
    SavePlayer *p = front_player(f);
    for (int c = 0; c < 3; c++) {
        FrontCity *fc = &f->cities[c];
        for (int i = fc->count - 1; i >= 0; i--) {
            fc->max = -1;
            if (*front_best(p, fc->level[i]) != -1) {
                f->max_city = (int16_t)c;
                fc->max = (int8_t)i;
                break;
            }
        }
    }
    if (f->demo) {
        f->max_city = 0;
        f->cities[0].max = 0;
    }
}

/* Front_ApplyVolumes 0x4280b0: both volumes must be 0..7 (fatal -208 otherwise). */
bool front_apply_volumes(Front *f)
{
    if (save_data.sfx_volume < 0 || save_data.sfx_volume > 7 || save_data.music_volume < 0 ||
        save_data.music_volume > 7) {
        snprintf(f->error, sizeof f->error, "error -208: bad volume in PLAYER_A.DAT");
        return false;
    }
    if (f->hooks.volumes) f->hooks.volumes(save_data.sfx_volume, save_data.music_volume);
    return true;
}

/* Front_LoadSettings 0x42b4a0: the file (savedata.c, which also applies the language override), then
   the texts, the chapter list, Gfx_ValidateModeIndex on +0x04 and the volumes. */
bool front_load_settings(Front *f)
{
    if (!save_load(f->demo)) {
        snprintf(f->error, sizeof f->error, "PLAYER_A.DAT: no file and no exe for the defaults");
        return false;
    }
    front_load_texts(f);
    front_build_chapter_list(f);
    if (f->hooks.validate_video_mode) f->hooks.validate_video_mode(&save_data.video_mode);
    return front_apply_volumes(f);
}

/* ---------------------------------------------------------------- enter / leave / set screen */

/* Front_Enter 0x42b690 */
bool front_enter(Front *f, int screen)
{
    if (f->hooks.enter) f->hooks.enter(save_data.sfx_volume, !f->demo || f->demo_music ? save_data.music_volume : -1);
    bool ok = front_load_images();
    ok = front_load_pictures() && ok;
    f->cut_bg_level = -1;
    f->after_cut = 1;
    f->ret = FRONT_CONTINUE;
    char err[128];
    if (!front_load_fonts(err, sizeof err)) {
        snprintf(f->error, sizeof f->error, "%s", err);
        ok = false;
    }
    if (!ok && !f->error[0]) snprintf(f->error, sizeof f->error, "frontend pictures missing");
    f->in_front = true;
    front_set_screen(f, screen);
    return ok;
}

/* Front_Leave 0x42b7a0 */
void front_leave(Front *f)
{
    front_free_fonts();
    front_free_pictures();
    front_free_images();
    f->in_front = false;
}

static int cut_font_count(int level) { return (int)exe_u32(0x4a73dc + 4u * (unsigned)level); }

static void free_cut_fonts(Front *f)
{
    for (int i = 0; i < 4; i++) {
        font_free(f->cut_font[i]);
        f->cut_font[i] = NULL;
    }
}

/* The network outcome (Front_SetScreen 0xd): the winners 0x5110b8 by score (target 0) or kills
   (target 1) of the present players (score != -1; best at least 1), or the race's first place. */
static void net_outcome(Front *f, const FrontCity *c, int mission)
{
    const FrontGameResult *r = &f->res;
    int count = 0;
    memset(f->winner, 0, sizeof f->winner);
    if (!c->race[mission]) {
        if (save_data.multi_target == 0) {
            int best = 1;
            for (int i = 0; i < 4; i++)
                if (best <= r->score[i]) best = r->score[i];
            for (int i = 0; i < 4; i++)
                if (r->score[i] == best) { f->winner[i] = 1; count++; }
        } else if (save_data.multi_target == 1) {
            int best = 1;
            for (int i = 0; i < 4; i++)
                if (r->score[i] != -1 && best <= r->frags[i]) best = r->frags[i];
            for (int i = 0; i < 4; i++)
                if (r->score[i] != -1 && r->frags[i] == best) { f->winner[i] = 1; count++; }
        }
        if (f->reason == 7) f->result_class = 3;
        else if (count == 0) f->result_class = 2;
        else f->result_class = f->winner[r->local & 3] == 0;
    } else {
        int first = -1;
        for (int i = 0; i < 4; i++)
            if (r->score[i] != -1 && r->rank[i] == 0) first = i;
        if (f->reason == 7) f->result_class = 3;
        else if (first == r->local) f->result_class = 0;
        else f->result_class = (first == -1) + 1;
    }
}

/* strcpy into a 16-byte name field: the bytes after the terminator stay. */
static void copy_name(char *dst, const char *src)
{
    size_t n = strlen(src);
    if (n > 15) n = 15;
    memcpy(dst, src, n);
    dst[n] = 0;
}

/* The single-player half of Front_SetScreen 0xd: the level's best score, the level's high-score table
   (3 entries ascending: the new score goes in above every entry it beats, the lower ones move down
   and the lowest drops out), unlocking the next level after a completed mission. */
static void single_results(Front *f, const FrontCity *c, int mission, int score)
{
    SavePlayer *p = front_player(f);
    int level = c->level[mission];
    int32_t *best = front_best(p, level);
    if (*best < score) {
        *best = score;
        if (level >= 0 && level < 6) f->best_new[level] = 1;
    }
    if (level >= 0 && level < SAVE_LEVELS) {
        SaveHiscore *hs = save_data.hiscore[level];
        int k = 2;
        while (k >= 0 && score <= hs[k].score) k--;
        if (k >= 0) {
            for (int j = 0; j < k; j++) {
                hs[j].score = hs[j + 1].score;
                copy_name(hs[j].name, hs[j + 1].name);
            }
            hs[k].score = score;
            copy_name(hs[k].name, p->name);
            f->hs_new[k] = 1;
        }
    }
    int l = front_city(f)->level[*front_mission(f)];
    if (f->reason == 1 && l < 5 && *front_best(p, l + 1) == -1) *front_best(p, l + 1) = 0;
    f->results_done = true;
}

/* Front_SetScreen 0x427030 */
void front_set_screen(Front *f, int screen)
{
    int prev = f->screen;
    if (screen == FS_CUTSCENE) {
        f->cut_frame = 0;
        if (f->net == 0 && f->reason == 1) {
            int level = front_sel_level(f);
            f->cut_level = level;
            if (level >= 0 && level < SAVE_LEVELS) front_player(f)->seen[level] = 1;
        } else
            screen = FS_RESULTS;
    }
    switch (screen) {
    case FS_CUTSCENE: {
        /* the stills' fonts "..\gtadata\%s.fon" (0x4b07b8) of 0x4af540 (0x4af5a0 on the 8-bit path) */
        int level = f->cut_level;
        free_cut_fonts(f);
        for (int i = 0; i < cut_font_count(level) && i < 4; i++) {
            char path[64], err[128];
            front_fmt(path, sizeof path, exe_str(0x4b07b8), "s",
                      exe_str(exe_u32(0x4af540 + 4u * (unsigned)(level * 4 + i))));
            f->cut_font[i] = font_load(path, 1, true, err, sizeof err);
            if (!f->cut_font[i]) snprintf(f->error, sizeof f->error, "%s", err);
        }
        if (f->cut_bg_level != level) {
            front_load_cutscene_bg(exe_str(exe_u32(0x4af524 + 4u * (unsigned)level)));
            f->cut_bg_level = level;
        }
        if (f->hooks.voice_play) f->hooks.voice_play(level);
        break;
    }
    case FS_CD:
        f->cd_ok = true;
        break;
    case FS_LOBBY_JOIN:
    case FS_LOADING:
    case FS_START:
        f->results_done = false;
        break;
    case FS_MAIN:
        if (prev == FS_CUTSCENE) free_cut_fonts(f);
        memset(f->hs_new, 0, sizeof f->hs_new);
        memset(f->best_new, 0, sizeof f->best_new);
        front_build_chapter_list(f);
        break;
    case FS_PLAYERS:
        front_build_chapter_list(f);
        memset(f->hs_new, 0, sizeof f->hs_new);
        memset(f->best_new, 0, sizeof f->best_new);
        break;
    case FS_RENAME:
        f->rename[0] = 0;
        break;
    case FS_RESULTS: {
        if (prev == FS_CUTSCENE) free_cut_fonts(f);
        snprintf(f->result_text, sizeof f->result_text, "%s", f->res.text);
        int score = f->res.text_score;
        const FrontCity *c = front_city(f);
        int mission = *front_mission(f);
        memset(f->hs_new, 0, sizeof f->hs_new);
        memset(f->best_new, 0, sizeof f->best_new);
        /* a second visit (the cutscene from the results screen) takes the network path */
        if (f->mode == 0 && !f->results_done) single_results(f, c, mission, score);
        else net_outcome(f, c, mission);
        front_build_chapter_list(f);
        if (f->hooks.video_mode) save_data.video_mode = (int8_t)f->hooks.video_mode();
        front_save(f);
        break;
    }
    default:
        break;
    }
    f->changed = 1;
    f->screen = screen;
}

/* ---------------------------------------------------------------- the small screens of Front_Step */

/* Cutscene stills: the animation rows 0x4a73f8 (6 levels x 5 rows of 193 bytes: rows 0-3 one per
   font, the glyph digit per tick, '0' = nothing; row 4 the text number), glyph positions 0x4a8a98
   ({x, y} per level x font), text keys 0x4af5fc[level * 4 + n]. Ticks: the voice position / 100, or
   without a voice a counter of frames 0..191. */
static void screen_cutscene(Front *f, uint32_t in, Surface *s)
{
    int st = f->hooks.voice_status ? f->hooks.voice_status() : -2;
    if (st != -1) {
        front_clear_or_draw_bg(s, 2);
        int tick = f->cut_frame;
        if (st == -2) {
            if (++f->cut_frame == 0xc0) f->cut_frame = 0;
        } else
            tick = st / 100;
        int level = f->cut_level;
        for (int i = 0; i < cut_font_count(level) && i < 4; i++) {
            const uint8_t *c = exe_data(0x4a73f8u + (unsigned)((level * 5 + i) * 0xc1 + tick), 1);
            if (!c || *c == '0') continue;
            char g[2] = {(char)(*c - '0'), 0};
            uint32_t pos = 0x4a8a98u + (unsigned)(level * 4 + i) * 8;
            if (f->cut_font[i])
                font_draw_string(s, f->cut_font[i], (int)exe_u32(pos), (int)exe_u32(pos + 4), g);
        }
        const uint8_t *t = exe_data(0x4a76fcu + (unsigned)(level * 0x3c5 + tick), 1);
        int8_t n = t ? (int8_t)(*t - '0') : 0;
        if (n != 0 && front_fonts.cuttext) {
            const char *txt = text_get(exe_str(exe_u32(0x4af5fc + 4u * (unsigned)(n + level * 4))));
            font_draw_centered(s, front_fonts.cuttext, txt ? txt : "", 0, (0xf0 - front_fonts.cuttext->height) * 2,
                               FRONT_W);
        }
        if (!(in & (FI_ENTER | FI_ESC)) && !(in & FI_SPACE)) return;
    }
    if (f->hooks.voice_stop) f->hooks.voice_stop();
    front_sample(f, 2);
    if (f->after_cut == 0) front_set_screen(f, FS_MAIN);
    else if (f->after_cut == 1) front_set_screen(f, FS_RESULTS);
}

/* Error (never set in this build): "Error" (0x4b07ac) and the message 0x510760. */
static void screen_error(Front *f, uint32_t in, Surface *s)
{
    if (in & (FI_ENTER | FI_ESC) || in & FI_SPACE) front_set_screen(f, FS_START);
    front_draw_background(s, 1, 0, f->clock_ms);
    front_draw_title(s, 0x198, exe_str(0x4b07ac));
    const Font *mt = front_fonts.mtext;
    if (mt) font_draw_string(s, mt, 0x140 - font_string_width(mt, f->err_msg) / 2, 0xf0, f->err_msg);
}

/* The CD check: entering screen 4 sets 0x511108, so this goes straight on to the start menu; the
   "cd-title" / "cd-text" page below is dead. */
static void screen_cd(Front *f, uint32_t in, Surface *s)
{
    if (!f->cd_ok) {
        if (in & (FI_ENTER | FI_ESC) || in & FI_SPACE) front_set_screen(f, FS_START);
        front_draw_background(s, 1, 0, f->clock_ms);
        front_draw_title(s, 0xb0, f->tx[T_CD_TITLE]);
        if (front_fonts.mtext) font_draw_wrapped(s, front_fonts.mtext, 0x78, 0xe0, 400, f->tx[T_CD_TEXT]);
        front_draw_enter_prompt(f, s, f->tx[T_PLAY]);
    } else {
        front_set_screen(f, FS_START);
        front_clear_or_draw_bg(s, 0);
    }
}

/* Credits (the way out: Esc on the start menu): the lines of 0x4af130 (NULL-terminated pointer list,
   '@' = indented name) scroll up 2 pixels a frame from row 192, clipped to rows 192-479; the end of the
   list or a key quits. The clip rectangle is left set. */
static void screen_credits(Front *f, uint32_t in, Surface *s)
{
    const Font *mt = front_fonts.mtext;
    if (!mt) { f->ret = FRONT_QUIT; return; }
    if (!f->credit_init) {
        f->credit_init = true;
        f->credit_h = mt->height;
    }
    gfx_set_clip_rect(0, 0xc0, 0x27f, 0x1df);
    f->credit_scroll += 2;
    int i = f->credit_scroll / f->credit_h, r = f->credit_scroll % f->credit_h, end = i + 12;
    if (exe_u32(0x4af130 + 4u * (unsigned)i) == 0 || in & (FI_ENTER | FI_ESC) || in & FI_SPACE) f->ret = FRONT_QUIT;
    front_draw_background(s, 1, 0, f->clock_ms);
    int dy = 0;
    for (; i < end; i++) {
        uint32_t va = exe_u32(0x4af130 + 4u * (unsigned)i);
        if (!va) break;
        const char *p = exe_str(va);
        if (*p) {
            int x = 0x40;
            if (*p == '@') {
                p++;
                x = 0x80;
            }
            font_draw_credit_line(s, mt, x, 0xc0 - r + dy, p);
        }
        dy += mt->height;
    }
}

/* SVGA error (after Game_Run returned 5). */
static void screen_svga(Front *f, uint32_t in, Surface *s)
{
    if (in & (FI_ENTER | FI_ESC) || in & FI_SPACE) front_set_screen(f, FS_START);
    front_draw_background(s, 1, 0, f->clock_ms);
    const Font *mt = front_fonts.mtext;
    if (mt) font_draw_string(s, mt, 0x140 - font_string_width(mt, f->tx[T_SVGAERROR]) / 2, 0x1ac, f->tx[T_SVGAERROR]);
    front_draw_key_right(f, s, T_ESC_KEY, T_QUIT);
}

/* Front_Step 0x426a50. A screen change during the step (0x511078) resets the menu selection. The page
   flip (thunk_Gfx_Flip 0x42da50) of each screen is the caller presenting the surface. */
int front_step(Front *f, uint32_t in, Surface *s)
{
    f->changed = 0;
    switch (f->screen) {
    case FS_CONNECTIONS: front_screen_connection_list(f, in, s); break;
    case FS_CUTSCENE: screen_cutscene(f, in, s); break;
    case FS_ERROR: screen_error(f, in, s); break;
    case FS_LOBBY_HOST: front_screen_lobby_host(f, in, s); break;
    case FS_CD: screen_cd(f, in, s); break;
    case FS_LOBBY_JOIN: front_screen_lobby_join(f, in, s); break;
    case FS_LOADING: front_screen_loading(f, in, s); break;
    case FS_START: front_screen_start(f, in, s); break;
    case FS_MAIN: front_screen_main(f, in, s); break;
    case FS_OPTIONS: front_screen_options(f, in, s); break;
    case FS_CREDITS: screen_credits(f, in, s); break;
    case FS_PLAYERS: front_screen_player_select(f, in, s); break;
    case FS_RENAME: front_screen_rename(f, in, s); break;
    case FS_RESULTS: front_screen_results(f, in, s); break;
    case FS_RESET: front_screen_reset_player(f, in, s); break;
    case FS_SESSIONS: front_screen_session_list(f, in, s); break;
    case FS_NET_NAME: front_screen_enter_name(f, in, s); break;
    case FS_COMMS_FAIL: front_screen_comms_error(f, in, FS_COMMS_FAIL, s); break;
    case FS_MULTI_OPTIONS: front_screen_multi_options(f, in, s); break;
    case FS_COMMS_VERSION: front_screen_comms_error(f, in, FS_COMMS_VERSION, s); break;
    case FS_SVGA: screen_svga(f, in, s); break;
    default: goto out;
    }
    if (f->changed == 1) f->menu = 1;
out:
    if (f->hooks.update) f->hooks.update();
    return f->ret;
}

/* ---------------------------------------------------------------- WinMain */

/* Movie_PlayIntro 0x44b160 (not ported: Smacker). The original flushes the key queue, opens
   ..\gtadata\movie.smk (pointer 0x4b1dc8) with SMACKW32, switches to the movie mode
   (Gfx_SetVideoMode(-2)) and shows each frame doubled to 640x480 (MGL_stretchBltCoord of the dirty
   rectangles), with the movie's palette, until the last frame or a key other than Alt (0x38); then it
   clears the screen and closes the sound device it opened for the soundtrack. */
void movie_play_intro(void) {}

/* Input_GetKey 0x414a80 adds 0x100 to the keypad / cursor scan codes 0x47-0x53 when the event has no
   character (cursor keys, and the keypad with Num Lock off). Our codes carry the extended flag
   instead; non-extended keypad keys are taken as Num Lock off. Other extended keys (keypad Enter)
   lose the flag, as in the original. */
static int original_code(uint16_t k)
{
    int c = k & 0xff;
    switch (c) {
    case 0x47: case 0x48: case 0x49: case 0x4b: case 0x4d: case 0x4f: case 0x50: case 0x51: case 0x52:
    case 0x53:
        return c + 0x100;
    default:
        return c;
    }
}

/* WinMain's mapping of one key event. Shift presses / releases (0x2a / 0xaa left, 0x36 / 0xb6 right)
   update the state; a character key (codes below 0x54 with an entry in 0x4a8b78) gives 0x40 and the
   character in bits 16-23. */
uint32_t front_map_key(Front *f, int code)
{
    uint32_t bits = 0;
    switch (code) {
    case 0xaa: f->shift &= ~1u; break;
    case 0xb6: f->shift &= ~2u; break;
    case 0x01: bits = FI_ESC; break;
    case 0x0e: case 0x53: case 0x153: bits = FI_DELETE; break;
    case 0x1c: bits = FI_ENTER; break;
    case 0x2a: f->shift |= 1; break;
    case 0x36: f->shift |= 2; break;
    case 0x39: bits = FI_SPACE; break;
    case 0x148: bits = FI_UP; break;
    case 0x14b: bits = FI_LEFT; break;
    case 0x14d: bits = FI_RIGHT; break;
    case 0x150: bits = FI_DOWN; break;
    default:
        if (code > 0 && code < 0x54) {
            const uint8_t *ch = exe_data(0x4a8b78u + (unsigned)code, 1);
            if (ch && *ch) bits = (uint32_t)*ch << 16 | FI_CHAR;
        }
        break;
    }
    if (f->shift) bits |= FI_SHIFT;
    return bits;
}

bool front_init(Front *f)
{
    f->error[0] = 0;
    if (!exe_loaded() || !load_tables(f)) {
        snprintf(f->error, sizeof f->error, "the exe is needed for the frontend tables");
        return false;
    }
    f->cut_bg_level = -1;
    f->screen = -1;
    if (!front_load_settings(f)) return false;
    if (f->hooks.player_name) f->hooks.player_name(0, exe_str(0x4b0c7c), 0);   /* "Player" */
    movie_play_intro();
    f->nqueue = 0;
    f->shift = 0;
    return front_enter(f, FS_CD);
}

/* One pass of WinMain's inner loop: Input_GetKey reads one key event (key down or up; repeats are
   skipped), the event becomes input bits, Front_Step runs; the loop then sleeps the rest of 35 ms. On a
   non-zero return the frontend is left; code 1 ends a network game (Net_EndGame, Net_InitPlayers), 3
   re-reads MISSION.INI (Mission_ReadIni) before Game_Run. */
FrontStep front_frame(Front *f, const FrontInput *in, Surface *s)
{
    if (in) {
        for (int i = 0; i < in->nkeys; i++)
            if (f->nqueue < (int)(sizeof f->queue / sizeof *f->queue)) f->queue[f->nqueue++] = (uint16_t)original_code(in->keys[i]);
        if (in->held) {   /* we see presses only: releases of Shift come from the held state */
            if (!in->held[0x2a]) f->shift &= ~1u;
            if (!in->held[0x36]) f->shift &= ~2u;
        }
    }
    int ev = 0;
    if (f->nqueue) {
        ev = f->queue[0];
        memmove(f->queue, f->queue + 1, (size_t)--f->nqueue * sizeof *f->queue);
    }
    uint32_t bits = front_map_key(f, ev);
    FrontStep r = {front_step(f, bits, s), 0, 0, 0};
    f->clock_ms += 35;
    if (r.code != FRONT_CONTINUE) {
        r.player = (int8_t)save_data.current;
        r.section = front_city(f)->section[*front_mission(f) & 3];
        r.level = front_sel_level(f);
        front_leave(f);
        if (r.code == FRONT_PLAY) {
            net_end_game();
            net_init_players();
        } else if (r.code == FRONT_JOIN && f->hooks.mission)
            f->hooks.mission(r.section);
    }
    return r;
}

/* WinMain after Game_Run: 4 -> comms failure, 5 -> SVGA error, the start menu if 0x5031f4 was set,
   else the cutscene of the level (which goes on to the results). The key queue is flushed. */
bool front_game_over(Front *f, const FrontGameResult *res)
{
    if (res) f->res = *res;
    f->reason = f->res.reason;
    int screen;
    if (f->res.run_code == 4) screen = FS_COMMS_FAIL;
    else if (f->res.run_code == 5) screen = FS_SVGA;
    else if (!f->res.to_start_menu) screen = FS_CUTSCENE;
    else screen = FS_START;
    f->nqueue = 0;
    f->shift = 0;
    return front_enter(f, screen);
}

void front_shutdown(Front *f)
{
    free_cut_fonts(f);
    if (f->in_front) front_leave(f);
}
