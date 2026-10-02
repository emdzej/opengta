/* The in-game HUD against the real data: mission 1 started as level_test does, HUD states set directly
   (score, multiplier, lives, wanted level, weapon and items, pager message and countdown, subtitle,
   arrow, big message, quit prompt, pause, video menu), frames rendered to out/hud/NN_name.png; plus checks
   of the area sub-directions, the zone-text slots, the big message split and the pager scrolling.
     ./build/hud_test              (data root: ./game or OPENGTA_DATA) */
#include "audio/audio.h"
#include "exe.h"
#include "font.h"
#include "game/game.h"
#include "game/gmath.h"
#include "game/mission.h"
#include "game/player.h"
#include "game/ped.h"
#include "game/route.h"
#include "hud/hud.h"
#include "hud/hud_internal.h"
#include "png.h"
#include "render/camera.h"
#include "render/poly.h"
#include "text.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

enum { W = 640, H = 480 };
static uint32_t fb[W * H];
static int failures;

#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: " __VA_ARGS__); printf("\n"); failures++; } } while (0)

uint8_t *plat_load_user_file(const char *name, size_t *size) { (void)name, (void)size; return NULL; }
bool plat_save_user_file(const char *name, const void *data, size_t size) { (void)name, (void)data, (void)size; return true; }
void plat_log(const char *msg) { printf("log: %s\n", msg); }

static void on_present(void *ctx) { (void)ctx; }

static bool start(int section)
{
    GameOptions o;
    game_default_options(&o);
    game_set_options(&o);
    g_game.present = on_present;
    map_clear_name();
    map_set_name(NULL, 2);
    if (!mission_set_ini_section(section)) return false;
    return game_run_begin();
}

/* n game frames (each game_run_step that isn't a wait) */
static void frames(int n)
{
    int calls = 0;
    while (n > 0 && calls++ < 20 * n + 100) {
        audio_render(NULL, 551);
        int r = game_run_step();
        if (r == GAME_STEP_FRAME) n--;
        if (r == GAME_STEP_DONE) break;
    }
}

static void shot(const char *name)
{
    char path[96];
    snprintf(path, sizeof path, "out/hud/%s.png", name);
    CHECK(png_write(path, fb, W, H, PNG_XRGB), "write %s", path);
    printf("  %s (crc %08x)\n", path, crc32(fb, sizeof fb));
}

static void check_area(void)
{
    /* rows top 2 / middle 3 / bottom 1, columns left 8 / middle 12 / right 4; small sides undivided */
    CHECK(area_sub_direction(0, 0, 20, 20) == 10, "top left %d", area_sub_direction(0, 0, 20, 20));
    CHECK(area_sub_direction(10, 10, 20, 20) == 15, "centre %d", area_sub_direction(10, 10, 20, 20));
    CHECK(area_sub_direction(19, 19, 20, 20) == 5, "bottom right %d", area_sub_direction(19, 19, 20, 20));
    CHECK(area_sub_direction(3, 3, 6, 6) == 0, "small zone %d", area_sub_direction(3, 3, 6, 6));
    CHECK(area_sub_direction(1, 9, 10, 10) == 9, "halves %d", area_sub_direction(1, 9, 10, 10));
    char b[64];
    int id = area_get_name(105, 119, b);
    printf("area at (105, 119): %d \"%s\"; %d nav zones\n", id, b, g_nav_count);
    CHECK(id != -1, "no area at the player's start");
}

int main(void)
{
    char err[256];
    if (!vfs_mount_default()) { printf("SKIP: no data root\n"); return 0; }
    if (!exe_init(err, sizeof err)) { printf("SKIP: %s\n", err); return 0; }
    math_init_tables();
    if (!camera_init_tables()) { printf("FAIL: camera tables\n"); return 1; }
    text_init_language(TEXT_ENGLISH);
    mkdir("out", 0755);
    mkdir("out/hud", 0755);
    poly_set_screen_rows(fb, W * 4, H);
    poly_set_clip(0, 0, W - 1, H - 1);
    game_set_screen(W, H);
    if (!start(1)) { printf("FAIL: mission 1 didn't start\n"); return 1; }
    printf("mission 1: HUD fonts res %d, pager columns %d, arrow sprites from %d (%d in the group)\n",
           hud_fonts.res, hud_fonts.pager_cols, sprite_group_base(SPRITE_GROUP_ARROW),
           sprite_group_count(SPRITE_GROUP_ARROW));
    CHECK(hud_fonts.res == 2, "640x480 takes the res 2 fonts (got %d)", hud_fonts.res);
    check_area();

    /* 1: the start: area sign (zone type 2), the score 0, lives 4, multiplier 1 */
    frames(30);
    shot("01_start");

    /* 2: score rolling to 12345, multiplier 3, wanted 3, pistol with 25 rounds, armour, jail card,
       speed-up, a pager message, a subtitle, an arrow */
    Player *p = &g_players[0];
    p->score = 12345;
    p->mult = 3;
    p->wanted_level = 3;
    p->weapon = 1;
    p->ammo[0] = 25;
    p->armour = 3;
    p->jail_free = 1;
    p->speedup = 100;
    p->timers[1] = 25 * 42;
    hud_brief(0, 1001, 0, 0);
    pager_add_message("WELCOME TO LIBERTY CITY");
    hud_show_subtitle(4, text_get("1002") ? text_get("1002") : "Hello");
    const Ped *d = ped_get(p->ped);
    hud_arrow_to_pos((d->spr.x >> 16) + 400, (d->spr.y >> 16) - 300, d->spr.z >> 16);
    frames(4);
    shot("02_rolling");
    frames(26);
    shot("03_state");
    CHECK(hud_is_pager_busy(), "pager showing");

    /* 3: WASTED! (4004) and MISSION COMPLETE! (2500, split in two lines) */
    hud_show_big_message_hi(text_get("4004"));
    frames(2);
    shot("04_wasted");
    hud_show_big_message(text_get("2500"), 1);
    frames(2);
    shot("05_mission_complete");

    /* 4: the pager countdown, the quit prompt, the pause screen, the video menu */
    pager_add_countdown("BOMB SET", 30, 7);
    frames(60);
    shot("06_countdown");
    /* each digit steps every 8 frames: 12345 is there after 5 steps of the last one */
    CHECK(!strcmp(p->hud_score, "000012345"), "score digits %s", p->hud_score);
    hud_toggle_quit_prompt();
    frames(2);
    shot("07_quit");
    hud_toggle_quit_prompt();
    hud_pause_on();
    frames(2);
    shot("08_pause");
    hud_pause_off();
    hud_toggle_video_menu();
    frames(2);
    shot("09_video_menu");
    hud_toggle_video_menu();

    /* zone texts: three slots, the same text again only restarts its timer */
    hud_clear_zone_text(2);
    hud_show_zone_text("ONE", 1);
    hud_show_zone_text("ONE", 1);
    hud_show_zone_text("TWO", 0xca);
    hud_show_car_name(0);
    frames(2);
    shot("10_zones");

    /* pager: a message scrolls through and frees its slot */
    pager_reset();
    pager_add_message("X");
    int n = 0;
    while (hud_is_pager_busy() && n < 2000) pager_update(), n++;
    printf("pager: a one-letter message took %d frames (speed %d px/frame)\n", n, g_pager.speed_px);
    CHECK(!hud_is_pager_busy() && n > 0 && n < 2000, "pager didn't finish (%d)", n);

    game_run_end();
    printf(failures ? "hud_test: %d FAILED\n" : "hud_test: ok\n", failures);
    return failures != 0;
}
