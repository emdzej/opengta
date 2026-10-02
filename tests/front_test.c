/* Frontend static layers against the data: the backdrop (logo + lower panel), menu text in every
   frontend font, the player pictures, a cutscene still with wrapped text, HUD fonts, a .RAT picture
   with F_PAL.RAW, and PLAYER_A.DAT. Writes PNGs to out/front/ to look at. */
#include "exe.h"
#include "font.h"
#include "front/front_text.h"
#include "front/images.h"
#include "platform.h"
#include "savedata.h"
#include "text.h"
#include "vfs_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int fail;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fail = 1; } } while (0)

/* ---- platform stubs: user files in out/front/user/ */
void plat_log(const char *msg) { printf("log: %s\n", msg); }
static void user_path(char *p, size_t cap, const char *name) { snprintf(p, cap, "out/front/user/%s", name); }
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

/* ---- a minimal PNG writer: RGB, zlib with stored (uncompressed) deflate blocks */
static void be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = (uint8_t)v; }
static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t n)
{
    uint8_t h[8];
    be32(h, n);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    uint8_t *c = malloc(n + 4);
    memcpy(c, type, 4);
    if (n) memcpy(c + 4, data, n);
    uint8_t t[4];
    be32(t, crc32(c, n + 4));
    if (n) fwrite(data, 1, n, f);
    fwrite(t, 1, 4, f);
    free(c);
}
static bool write_png(const char *path, const Surface *s)
{
    size_t raw_n = (size_t)s->h * (1 + 3 * (size_t)s->w);
    uint8_t *raw = malloc(raw_n), *q = raw;
    for (int y = 0; y < s->h; y++) {
        *q++ = 0;
        for (int x = 0; x < s->w; x++) {
            uint32_t c = s->px[(size_t)y * s->stride + x];
            *q++ = (uint8_t)c;
            *q++ = (uint8_t)(c >> 8);
            *q++ = (uint8_t)(c >> 16);
        }
    }
    size_t blocks = raw_n / 65535 + 1, z_n = 2 + raw_n + 5 * blocks + 4;
    uint8_t *z = malloc(z_n), *o = z;
    *o++ = 0x78;
    *o++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_n; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    for (size_t off = 0; off < raw_n || off == 0;) {
        size_t n = raw_n - off > 65535 ? 65535 : raw_n - off;
        *o++ = off + n >= raw_n;
        *o++ = (uint8_t)n;
        *o++ = (uint8_t)(n >> 8);
        *o++ = (uint8_t)~n;
        *o++ = (uint8_t)(~n >> 8);
        memcpy(o, raw + off, n);
        o += n;
        off += n;
        if (!n) break;
    }
    be32(o, b << 16 | a);
    o += 4;
    FILE *f = fopen(path, "wb");
    if (!f) { free(raw); free(z); return false; }
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13] = {0};
    be32(ihdr, (uint32_t)s->w);
    be32(ihdr + 4, (uint32_t)s->h);
    ihdr[8] = 8;
    ihdr[9] = 2;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (uint32_t)(o - z));
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
    printf("wrote %s\n", path);
    return true;
}

static Surface new_surface(int w, int h)
{
    Surface s = {calloc((size_t)w * h, 4), w, h, w};
    return s;
}
static void fill(Surface *s, uint32_t c) { for (int i = 0; i < s->w * s->h; i++) s->px[i] = c; }

static const char *T(const char *key) { const char *t = text_get(key); return t ? t : key; }

int main(void)
{
    char err[256];
    if (!vfs_mount_default()) { printf("SKIP: no data root\n"); return 0; }
    if (!exe_init(err, sizeof err)) printf("note: %s\n", err);
    mkdir("out", 0777);
    mkdir("out/front", 0777);
    mkdir("out/front/user", 0777);
    text_init_language(TEXT_ENGLISH);

    CHECK(front_load_images(), "front images");
    CHECK(front_load_pictures(), "player pictures");
    CHECK(front_load_fonts(err, sizeof err), "front fonts: %s", err);
    FrontFonts *ff = &front_fonts;
    printf("fonts: cuttext %d/%d mhead %d/%d mmiss %d/%d mtext %d/%d key %d/%d city1 %d/%d\n",
           ff->cuttext->count, ff->cuttext->height, ff->mhead->count, ff->mhead->height, ff->mmiss->count,
           ff->mmiss->height, ff->mtext->count, ff->mtext->height, ff->key->count, ff->key->height,
           ff->city[0]->count, ff->city[0]->height);

    Surface s = new_surface(640, 480);

    /* 1. the start menu look: animated logo frame 0, lower panel 0, heading, menu items, marker */
    front_draw_background(&s, 1, 0, 0);
    front_draw_title(&s, 176, "GRAND THEFT AUTO");
    front_draw_menu_item(&s, 1, 1, T("GatherNetwork"));
    front_draw_menu_item(&s, 2, 1, T("JoinNetwork"));
    front_draw_menu_item_b(&s, 4, 0, "MENU TEXT: F_MTEXT 0123456789");
    font_draw_string(&s, ff->mmiss, 196, 400, "F_MMISS: the quick brown fox jumps over the lazy dog 0123456789");
    font_draw_string(&s, ff->cuttext, 20, 420, "CUTTEXT: The quick brown fox jumps over the lazy dog.");
    font_draw_string(&s, ff->city[0], 500, 300, "\x01");
    int w = font_string_width(ff->mtext, "Enter");
    font_draw_string(&s, ff->mtext, 640 - w, 480 - ff->mtext->height, "Enter");
    write_png("out/front/menu.png", &s);

    /* 2. the logo strip (frame 4 after the clock), lower panel 1, the player pictures */
    front_draw_background(&s, 1, 1, 0);
    for (int t = 100; t <= 500; t += 100) front_draw_background(&s, 1, 1, (uint32_t)t);
    CHECK(front_images.logo_frame == 5, "logo frame %d", front_images.logo_frame);
    front_draw_background(&s, 0, 1, 0);
    for (int i = 0; i < 8; i++) gfx_blit_image(&s, 8 + i * 79, 190, &front_pictures.play[i]);
    gfx_blit_image(&s, 230, 340, &front_pictures.playn);
    gfx_blit_image(&s, 20, 340, &front_pictures.rstar);
    gfx_blit_image(&s, 90, 340, &front_pictures.rstarn);
    font_draw_centered(&s, ff->mtext, "Ulrika", 230, 350, 180);
    font_draw_string_alt(&s, ff->mtext, 420, 350, "\xc3\x89T\xc3\x89 \xc3\x84\xc3\x96\xc3\x9c");
    write_png("out/front/players.png", &s);

    /* 3. a cutscene still with the chapter text, word-wrapped in CUTTEXT */
    CHECK(front_load_cutscene_bg("GTADATA/CUT0"), "cut0");
    front_clear_or_draw_bg(&s, 2);
    font_draw_wrapped(&s, ff->cuttext, 40, 300, 560, T("1001"));
    int lines;
    font_select(ff->cuttext);
    text_word_wrap(T("1001"), 560, &lines);
    printf("1001 wraps to %d lines at 560 px\n", lines);
    gfx_set_clip_rect(0, 100, 0x27f, 130);
    font_draw_credit_line(&s, ff->mhead, 40, 90, "CREDIT LINE CLIPPED TO ROWS 100-130");
    gfx_set_clip_rect(0, 0, 0x27f, 0x1df);
    write_png("out/front/cut0.png", &s);

    /* 4. HUD fonts: drawn with their own file palettes (in the game: the style's font palettes) */
    fill(&s, surface_rgb(40, 60, 40));
    static const char *hud_names[] = {"BIG", "SUB", "STREET", "PAGER"};
    int y = 10;
    for (int res = 1; res <= 2; res++)
        for (int i = 0; i < 4; i++) {
            char rel[32];
            snprintf(rel, sizeof rel, "GTADATA/%s%d.FON", hud_names[i], res);
            Font *f = font_load(rel, 0x21, true, err, sizeof err);
            CHECK(f, "%s", err);
            if (!f) continue;
            const uint32_t *aux[8] = {f->pal, f->pal, f->pal, f->pal, f->pal, f->pal, f->pal, f->pal};
            font_set_hud_cluts(aux, f->pal);
            font_select(f);
            char line[64];
            snprintf(line, sizeof line, i == 0 && res == 2 ? "%s%d: BUSTED!" : "%s%d: BUSTED! WASTED! 1234",
                     hud_names[i], res);
            hud_draw_text(&s, line, surface_offset(&s, 10, y), false);
            y += f->height + 2;
            font_free(f);
        }
    CHECK(font_hud_load(1, err, sizeof err), "hud fonts: %s", err);
    Font *subp = font_load("GTADATA/SUB1.FON", 0x21, true, err, sizeof err);
    const uint32_t *aux2[8] = {subp->pal, subp->pal, subp->pal, subp->pal, subp->pal, subp->pal, subp->pal, subp->pal};
    font_set_hud_cluts(aux2, subp->pal);
    hud_draw_centered_line(&s, "CENTRED SUB LINE", 400);
    font_select(hud_fonts.pager);
    hud_draw_text_clipped(&s, "PAGER TEXT SCROLLING THROUGH A WINDOW", surface_offset(&s, 200, 430), 23, 120);
    printf("pager columns (res 1): %d\n", hud_fonts.pager_cols);
    write_png("out/front/hud.png", &s);
    font_free(subp);
    font_hud_free();

    /* 5. the dead 8-bit path: F_UPPER.RAT through F_PAL.RAW against F_UPPER.RAW */
    Image8 rat = {640, 168, NULL};
    uint32_t pal[256];
    CHECK(gfx_load_rat_image(&rat, "GTADATA/F_UPPER", false), "rat");
    CHECK(gfx_load_palette("GTADATA/F_PAL.RAW", pal), "f_pal");
    fill(&s, 0);
    for (int yy = 0; yy < 168; yy++)
        for (int x = 0; x < 640; x++) {
            s.px[yy * 640 + x] = front_images.upper.px[yy * 640 + x];
            s.px[(yy + 200) * 640 + x] = pal[rat.px[yy * 640 + x]];
        }
    write_png("out/front/rat_vs_raw.png", &s);
    free(rat.px);

    /* 6. PLAYER_A.DAT: the shipped file, then the defaults built from the exe */
    remove("out/front/user/PLAYER_A.DAT");
    CHECK(save_load(false), "save_load");
    printf("PLAYER_A.DAT: sfx %d music %d pager %d mode %d lang %d current %d net '%s'\n",
           save_data.sfx_volume, save_data.music_volume, save_data.pager_speed, save_data.video_mode,
           save_data.language, save_data.current, save_data.net_name);
    for (int i = 0; i < SAVE_PLAYERS; i++)
        printf("  player %d %-8s best %d %d %d %d %d %d\n", i, save_data.player[i].name,
               save_data.player[i].best[0], save_data.player[i].best[1], save_data.player[i].best[2],
               save_data.player[i].best[3], save_data.player[i].best[4], save_data.player[i].best[5]);
    for (int l = 0; l < SAVE_LEVELS; l++)
        printf("  level %d hiscores %d %s / %d %s / %d %s\n", l, save_data.hiscore[l][0].score,
               save_data.hiscore[l][0].name, save_data.hiscore[l][1].score, save_data.hiscore[l][1].name,
               save_data.hiscore[l][2].score, save_data.hiscore[l][2].name);
    CHECK(!strcmp(save_data.player[0].name, "Ulrika") && !strcmp(save_data.net_name, "GTA Game"), "shipped names");
    if (exe_loaded()) {
        memset(&save_data, 0, sizeof save_data);
        save_defaults(false);
        CHECK(!strcmp(save_data.player[7].name, "Kivlov"), "default name '%s'", save_data.player[7].name);
        CHECK(save_data.hiscore[5][2].score == 3 && !strcmp(save_data.hiscore[5][2].name, "STEVE"), "default scores");
        CHECK(save_store(), "save");
        size_t n;
        uint8_t *b = plat_load_user_file("PLAYER_A.DAT", &n);
        CHECK(b && n == SAVE_SIZE, "stored size");
        free(b);
        CHECK(save_load(false) && save_data.player[0].best[1] == -1, "reload user file");
        remove("out/front/user/PLAYER_A.DAT");
    }

    front_free_fonts();
    front_free_pictures();
    front_free_images();
    free(s.px);
    text_free();
    printf(fail ? "FAIL\n" : "PASS\n");
    return fail;
}
