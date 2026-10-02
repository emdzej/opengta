/* The frontend state machine driven by scripted key presses, as the WinMain loop would feed it:
   start menu -> options (change, revert) -> player select -> rename (a cheat name, then a real name)
   -> city select -> loading (start level) -> back from a "game" -> cutscene still -> results with the
   new high score -> main again; the network entries (multiplayer options, the empty connection list);
   Esc out through the credits. Writes PNGs to out/frontend/. Deterministic: frame-counted clock, the
   shipped PLAYER_A.DAT, no sound. */
#include "exe.h"
#include "front/front.h"
#include "front/front_text.h"
#include "platform.h"
#include "png.h"
#include "savedata.h"
#include "text.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int fail;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fail = 1; } } while (0)

/* ---- platform stubs: user files in out/frontend/user/ */
void plat_log(const char *msg) { printf("log: %s\n", msg); }
static void user_path(char *p, size_t cap, const char *name) { snprintf(p, cap, "out/frontend/user/%s", name); }
uint8_t *plat_load_user_file(const char *name, size_t *size)
{
    char p[256];
    user_path(p, sizeof p, name);
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(n > 0 ? (size_t)n : 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    if (b && size) *size = (size_t)n;
    return b;
}
bool plat_save_user_file(const char *name, const void *data, size_t size)
{
    char p[256];
    user_path(p, sizeof p, name);
    FILE *f = fopen(p, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, size, f) == size;
    return fclose(f) == 0 && ok;
}

/* ---- hooks: a log of what the frontend asks of the other subsystems */
static char samples[512];
static int last_section = -1, vol_sfx = -1, vol_music = -1, pager = -1;
static void hk_sample(int n)
{
    size_t l = strlen(samples);
    if (l + 4 < sizeof samples) snprintf(samples + l, sizeof samples - l, "%d ", n);
}
static void hk_mission(int section) { last_section = section; }
static void hk_volumes(int s, int m) { vol_sfx = s; vol_music = m; }
static void hk_level(int p, int e, int q) { (void)e; (void)q; pager = p; }

static Surface scr;
static uint64_t hash;
static uint8_t held[KEY_COUNT];

static uint64_t fnv(const void *p, size_t n, uint64_t h)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 0x100000001b3ull;
    return h;
}

/* One frame with up to one key press (0 = none). */
static FrontStep frame(Front *f, uint16_t key)
{
    FrontInput in = {&key, key != 0, held};
    FrontStep r = front_frame(f, &in, &scr);
    hash = fnv(scr.px, (size_t)scr.w * scr.h * 4, hash);
    return r;
}
static FrontStep press(Front *f, uint16_t key, int idle)
{
    FrontStep r = frame(f, key);
    for (int i = 0; i < idle && r.code == FRONT_CONTINUE; i++) r = frame(f, 0);
    return r;
}
static void shot(const char *name)
{
    char p[128];
    snprintf(p, sizeof p, "out/frontend/%s.png", name);
    if (png_write(p, scr.px, scr.w, scr.h, PNG_ABGR)) printf("wrote %s\n", p);
}

/* The scan code of a character, from WinMain's table 0x4a8b78. */
static uint16_t code_of(char c)
{
    const uint8_t *t = exe_data(0x4a8b78, 0x54);
    for (int i = 0; t && i < 0x54; i++)
        if (t[i] == (uint8_t)c) return (uint16_t)i;
    return 0;
}
static void type(Front *f, const char *s)
{
    for (; *s; s++) {
        bool up = *s >= 'A' && *s <= 'Z';
        held[0x2a] = up;
        if (up) press(f, 0x2a, 0);
        press(f, *s == ' ' ? 0x39 : code_of(up ? (char)(*s + 0x20) : *s), 0);
        held[0x2a] = 0;
    }
}

enum { K_ESC = 0x01, K_BACK = 0x0e, K_ENTER = 0x1c, K_UP = 0x148, K_LEFT = 0x14b, K_RIGHT = 0x14d, K_DOWN = 0x150 };

int main(void)
{
    char err[256];
    if (!vfs_mount_default()) { printf("SKIP: no data root\n"); return 0; }
    if (!exe_init(err, sizeof err)) { printf("SKIP: %s\n", err); return 0; }
    mkdir("out", 0777);
    mkdir("out/frontend", 0777);
    mkdir("out/frontend/user", 0777);
    remove("out/frontend/user/PLAYER_A.DAT");   /* start from the shipped GTADATA/PLAYER_A.DAT */
    text_init_language(TEXT_ENGLISH);
    scr = (Surface){calloc(640 * 480, 4), 640, 480, 640};
    hash = 0xcbf29ce484222325ull;

    /* WinMain's key mapping */
    Front kf = {0};
    CHECK(front_map_key(&kf, 0x1e) == ('a' << 16 | FI_CHAR), "key a -> %x", front_map_key(&kf, 0x1e));
    CHECK(front_map_key(&kf, 0x148) == FI_UP && front_map_key(&kf, 0x1c) == FI_ENTER, "arrows / enter");
    front_map_key(&kf, 0x36);
    CHECK(front_map_key(&kf, 0x39) == (FI_SPACE | FI_SHIFT), "shift + space");
    front_map_key(&kf, 0xb6);
    CHECK(front_map_key(&kf, 0x0e) == FI_DELETE && front_map_key(&kf, 0x153) == FI_DELETE, "delete");

    static Front f;
    f.net_active = true;
    f.hooks.sample = hk_sample;
    f.hooks.mission = hk_mission;
    f.hooks.volumes = hk_volumes;
    f.hooks.level_options = hk_level;
    CHECK(front_init(&f), "front_init: %s", f.error);
    CHECK(!f.error[0], "text lookups: %s", f.error);
    printf("settings: sfx %d music %d player %d (%s) cities %d\n", save_data.sfx_volume, save_data.music_volume,
           save_data.current, front_current_player_name(&f), f.max_city);
    CHECK(f.screen == FS_CD && vol_sfx == save_data.sfx_volume, "init state");

    /* 1. screen 4 (CD check) goes straight to the start menu */
    press(&f, 0, 30);
    CHECK(f.screen == FS_START, "start menu (screen %d)", f.screen);
    shot("start");

    /* 2. options: item 4 of the network menu; sound up, then Esc reverts */
    int sfx0 = save_data.sfx_volume;
    press(&f, K_DOWN, 0); press(&f, K_DOWN, 0); press(&f, K_DOWN, 0);
    CHECK(f.menu == 4, "menu %d", f.menu);
    press(&f, K_ENTER, 2);
    CHECK(f.screen == FS_OPTIONS && f.menu == 1, "options");
    press(&f, K_RIGHT, 0);
    CHECK(save_data.sfx_volume == sfx0 + 1 && vol_sfx == sfx0 + 1, "sound up %d", save_data.sfx_volume);
    press(&f, K_DOWN, 0); press(&f, K_DOWN, 0); press(&f, K_RIGHT, 6);   /* text speed */
    shot("options");
    press(&f, K_ESC, 2);
    CHECK(f.screen == FS_START && save_data.sfx_volume == sfx0, "options reverted (%d)", save_data.sfx_volume);

    /* 3. Play -> player select */
    CHECK(f.menu == 1, "menu reset %d", f.menu);
    press(&f, K_ENTER, 4);
    CHECK(f.screen == FS_PLAYERS, "player select (screen %d)", f.screen);
    shot("players");

    /* 4. rename to a cheat name: every level unlocked */
    press(&f, K_BACK, 1);
    CHECK(f.screen == FS_RENAME, "rename");
    type(&f, "nineinarow");
    press(&f, 0, 3);
    shot("rename");
    press(&f, K_ENTER, 1);
    SavePlayer *p = &save_data.player[save_data.current];
    CHECK(f.screen == FS_PLAYERS && !strcmp(p->name, "nineinarow"), "renamed '%s'", p->name);
    CHECK(p->best[5] == 0 && p->best[3] == 0, "cheat unlocked levels (%d %d)", p->best[3], p->best[5]);
    press(&f, K_BACK, 1);
    type(&f, "Tester");
    press(&f, K_ENTER, 1);
    CHECK(!strcmp(p->name, "Tester"), "renamed '%s'", p->name);
    press(&f, 0, 8);
    shot("players_renamed");

    /* 5. city select: all three cities now; San Andreas, its second chapter */
    press(&f, K_ENTER, 4);
    CHECK(f.screen == FS_MAIN && f.max_city == 2, "main (screen %d, cities %d)", f.screen, f.max_city);
    shot("main");
    press(&f, K_RIGHT, 0);
    press(&f, K_DOWN, 12);
    CHECK(front_sel_city(&f) == 1 && front_sel_mission(&f) == 1 && front_sel_level(&f) == 3, "selection");
    shot("main_sanandreas");

    /* 6. Enter: the loading screen, which starts the level on its first frame */
    FrontStep st = press(&f, K_ENTER, 4);
    printf("start: code %d section %d level %d player %d (mission hook %d)\n", st.code, st.section, st.level,
           st.player, last_section);
    CHECK(st.code == FRONT_PLAY && st.section == 103 && st.level == 3 && last_section == 103, "start level");
    CHECK(pager == save_data.pager_speed, "level options");
    shot("loading");
    CHECK(!f.in_front, "left the frontend");

    /* 7. back from the game: mission complete -> the cutscene still of level 3, then the results */
    FrontGameResult res = {0};
    res.reason = 1;
    res.local = 0;
    res.score[0] = res.text_score = 54321;
    for (int i = 1; i < 4; i++) res.score[i] = -1;
    for (int k = 0; k < 10; k++) res.kills[k] = k * 3;
    res.mission_counter = 4;
    res.mission_total = 12;
    res.secret_counter = 1;
    res.secret_total = 5;
    snprintf(res.text, sizeof res.text, "%s", text_get("2500") ? text_get("2500") : "MISSION COMPLETE!");
    CHECK(front_game_over(&f, &res), "front_game_over: %s", f.error);
    CHECK(f.screen == FS_CUTSCENE && f.cut_level == 3 && p->seen[3] == 1, "cutscene (screen %d)", f.screen);
    press(&f, 0, 100);
    shot("cutscene");
    press(&f, K_ENTER, 6);
    CHECK(f.screen == FS_RESULTS, "results (screen %d)", f.screen);
    CHECK(save_data.hiscore[3][2].score == 54321 && !strcmp(save_data.hiscore[3][2].name, "Tester"),
          "new high score %d %s", save_data.hiscore[3][2].score, save_data.hiscore[3][2].name);
    CHECK(p->best[3] == 54321 && p->best[4] == 0, "best / unlock");
    shot("results");

    /* 8. Enter: main, the selection moved on to Vice City */
    press(&f, K_ENTER, 6);
    CHECK(f.screen == FS_MAIN && front_sel_city(&f) == 2 && front_sel_mission(&f) == 0, "advanced (%d %d)",
          front_sel_city(&f), front_sel_mission(&f));
    shot("main_after");

    /* 9. network: Gather -> player -> main (network table) -> options -> connections (none) */
    press(&f, K_ESC, 1);
    press(&f, K_ESC, 1);
    CHECK(f.screen == FS_START, "start again");
    press(&f, K_DOWN, 0);
    press(&f, K_ENTER, 1);
    press(&f, K_ENTER, 1);
    CHECK(f.screen == FS_MAIN && f.mode == 1 && f.cities == f.tab_net, "network main");
    press(&f, 0, 10);
    shot("main_network");
    press(&f, K_ENTER, 1);
    CHECK(f.screen == FS_MULTI_OPTIONS, "multi options (screen %d)", f.screen);
    press(&f, K_DOWN, 0);
    press(&f, K_RIGHT, 4);
    CHECK(save_data.score_target == 110000, "score target %d", save_data.score_target);
    shot("multi_options");
    press(&f, K_ENTER, 4);
    CHECK(f.screen == FS_CONNECTIONS, "connections (screen %d)", f.screen);
    shot("connections");
    press(&f, K_ESC, 1);
    press(&f, K_ESC, 1);
    CHECK(f.screen == FS_START, "start (screen %d)", f.screen);

    /* 10. Esc: the credits, to the end */
    press(&f, K_ESC, 0);
    int frames = 0;
    for (; frames < 20000; frames++) {
        st = frame(&f, 0);
        if (frames == 400) shot("credits");
        if (st.code != FRONT_CONTINUE) break;
    }
    printf("credits: %d frames, code %d\n", frames, st.code);
    CHECK(st.code == FRONT_QUIT, "quit");

    printf("samples: %s\n", samples);
    printf("frame hash %016llx\n", (unsigned long long)hash);
    front_shutdown(&f);
    free(scr.px);
    text_free();
    printf(fail ? "FAIL\n" : "PASS\n");
    return fail;
}
