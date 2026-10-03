/* skin_template: the skin author's tool (not part of the original; docs/skins.md, docs/howto/create-a-skin.md).

     skin_template [--data <dir>] <skin folder>              write the template into the folder
     skin_template [--data <dir>] --validate <skin folder>   check a skin against the manifest
     skin_template [--data <dir>] --at <map> <x> <y> [<z>]   the tiles of a map column, as skin files

   Run locally on your own game data (--data, else $OPENGTA_DATA, else ./game). The template is the list of
   every asset a skin can replace, made from that data: per style the side, lid and aux tiles (with the
   remaps and directions the maps draw them with, and which animations show the aux tiles), the sprites
   (group, size, deltas, the remaps cars and peds draw them with), the fonts (glyph codes and sizes) and
   the frontend pictures (sizes), as skin.json (for scripts) and CHECKLIST.md (for people), plus the empty
   folders and a skin.ini to fill in. It is metadata only: no pixel of the game is written. The game's
   art is not ours to redistribute, and a skin made from it could not be shared either: a skin is new art,
   drawn at the sizes the manifest gives.

   --validate builds the same manifest and checks every file of the skin folder against it: the names
   (style, kind, number, variant in range), PNG decodability (the game's own decoder), sizes and aspect
   ratios, paint masks against their sprite image, delta overlays against the sprite's deltas, skin.ini,
   and files the game would never read. Exit status 1 if there are errors.

   --at prints the blocks of one map column (map nyc, sanb, miami or a .CMP name; x, y, z in blocks as the
   city viewer's front=0 map= x= y= z= take them): each face's tile and the skin file that replaces it. */
#include "font.h"
#include "game/carinfo.h"
#include "map.h"
#include "render/hires/hires_png.h"
#include "render/hires/hires_skin.h"
#include "render/sprite.h"
#include "style.h"
#include "vfs.h"
#include "vfs_host.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* og_core's platform hooks (the game's saves and log; unused here) */
uint8_t *plat_load_user_file(const char *name, size_t *size) { (void)name, (void)size; return NULL; }
bool plat_save_user_file(const char *name, const void *data, size_t size) { (void)name, (void)data, (void)size; return true; }
void plat_log(const char *msg) { fprintf(stderr, "%s\n", msg); }

enum { STYLES = 3, KIND_SIDE, KIND_LID, KIND_AUX, KIND_SPRITE };

static const char *const GROUP_NAME[SPRITE_GROUPS] = {
    "arrow", "digits", "boat", "box", "bus", "car", "object", "ped", "speedo", "tank", "traffic lights",
    "train", "train doors", "bike", "tram", "wbus", "wcar", "ex", "tumcar", "tumtruck", "ferry",
};
static const char *const DIR_NAME[4] = { "top", "bottom", "left", "right" };   /* side_clut[t][d] */

/* ---- the manifest ---- */

typedef struct { int uses; uint8_t dirs; } SideUse;      /* dirs: bit d = drawn with direction d's CLUT */
typedef struct { int uses; uint8_t remaps; } LidUse;     /* remaps: bit r = drawn with remap r */
typedef struct { int which, block, speed, nframes; uint8_t frame[255]; } Anim;

typedef struct {
    int w, h, group, index, ndeltas;
    int dsize[SPRITE_DELTAS_MAX], dpix[SPRITE_DELTAS_MAX];   /* stream bytes, texels written */
    int ncars, car[16];                                       /* car info records drawn with it */
} SpriteMan;

typedef struct {
    int record, model, vtype, sprite, doors;
} CarMan;

typedef struct {
    bool ok;
    int number;
    char file[40], maps[4][40];
    int nmaps;
    int nside, nlid, naux;
    SideUse side[256];
    LidUse lid[256];
    int nanims;
    Anim anim[STYLE_ANIMS_MAX];
    uint8_t aux_dirs[256], aux_remaps[256];   /* what the animations showing aux n inherit */
    int aux_shown[256];                       /* animations that show it */
    int nsprites;
    SpriteMan *spr;
    int ped_remaps;
    int ncars;
    CarMan car[CARINFO_MAX];
} StyleMan;

typedef struct { char name[40], file[48]; int first, count, height; uint8_t w[256]; } FontMan;
typedef struct { char name[48], file[64]; int w, h; long bytes; } PicMan;

static StyleMan styles[STYLES];
static FontMan *fonts;
static int nfonts;
static PicMan *pics;
static int npics;

/* ---- listing the data ---- */

typedef struct { char (*names)[64]; int n, cap; const char *ext; } NameList;

static void collect(const char *name, bool is_dir, void *user)
{
    NameList *l = user;
    size_t n = strlen(name), e = strlen(l->ext);
    if (is_dir || n <= e || strcasecmp(name + n - e, l->ext) || n >= 64) return;
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 64;
        l->names = realloc(l->names, (size_t)l->cap * sizeof *l->names);
    }
    snprintf(l->names[l->n++], 64, "%s", name);
}

static int cmp_name(const void *a, const void *b) { return strcasecmp(a, b); }

static NameList list_data(const char *ext)
{
    NameList l = { NULL, 0, 0, ext };
    vfs_list("GTADATA", collect, &l);
    if (l.n) qsort(l.names, (size_t)l.n, sizeof *l.names, cmp_name);
    return l;
}

static void upper(char *s)
{
    for (; *s; s++) *s = (char)toupper((unsigned char)*s);
}

/* texels a delta stream writes inside the w x h graphic (Blit_ApplyDelta 0x48958a's records) */
static int delta_pixels(const SpriteInfo *in, int k)
{
    const uint8_t *p = in->delta[k].data, *end = p + in->delta[k].size;
    size_t at = 0;
    int n = 0;
    while (p + 3 <= end) {
        at += (uint16_t)(p[0] | p[1] << 8);
        unsigned c = p[2];
        p += 3;
        for (unsigned i = 0; i < c && p < end; i++, at++, p++) n += at / 256 < in->h && at % 256 < in->w;
    }
    return n;
}

static void scan_map(StyleMan *sm, const Map *m)
{
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            for (int z = 0; z < MAP_Z; z++) {
                const MapBlock *b = map_get_block(m, x, y, z);
                if (!b) continue;
                if (b->lid) sm->lid[b->lid].uses++, sm->lid[b->lid].remaps |= (uint8_t)(1u << ((b->ext & 0x18) >> 3));
                const uint8_t face[4] = { b->top, b->bottom, b->left, b->right };   /* directions 0..3 */
                for (int d = 0; d < 4; d++)
                    if (face[d]) sm->side[face[d]].uses++, sm->side[face[d]].dirs |= (uint8_t)(1u << d);
            }
}

/* the first character code of a font, as the game loads it: the HUD digit fonts from 0, the icon fonts
   (frontend keys, city names, cutscene captions) from 1, text fonts from 0x21 (Font_Load's callers:
   HUD_LoadFonts 0x481030, Front_LoadFonts 0x42d2e0, the cutscene screen) */
static int font_first(const char *name)
{
    if (!strncmp(name, "SCORE", 5) || !strncmp(name, "EXPSCOR", 7) || !strncmp(name, "MISSMUL", 7)) return 0;
    if (!strcmp(name, "F_KEY") || !strncmp(name, "F_CITY", 6) || !strncmp(name, "F8_CITY", 7) ||
        (!strncmp(name, "CUT", 3) && isdigit((unsigned char)name[3])))
        return 1;
    return 0x21;
}

/* the frontend pictures are headerless RGB: their sizes are the frontend's (Front_LoadImages 0x42da60,
   Front_LoadCutsceneBg 0x42d810, Front_Enter 0x42b690); other .RAW files aren't shown */
static void picture_size(long bytes, int *w, int *h)
{
    static const int size[][2] = { { 640, 480 }, { 640, 168 }, { 640, 312 }, { 102, 141 }, { 180, 50 }, { 64, 59 } };
    *w = *h = 0;
    for (size_t i = 0; i < sizeof size / sizeof *size; i++)
        if (bytes == (long)size[i][0] * size[i][1] * 3) *w = size[i][0], *h = size[i][1];
}

static bool build_manifest(void)
{
    char err[256];
    NameList maps = list_data(".CMP");
    Map *loaded[8] = { 0 };
    int nloaded = 0;
    for (int i = 0; i < maps.n && nloaded < 8; i++) {
        char rel[96];
        snprintf(rel, sizeof rel, "GTADATA/%s", maps.names[i]);
        Map *m = map_load(rel, err, sizeof err);
        if (!m) { fprintf(stderr, "skin_template: %s: %s\n", rel, err); continue; }
        int st = m->style ? m->style : 1;
        if (st >= 1 && st <= STYLES && styles[st - 1].nmaps < 4) {
            snprintf(styles[st - 1].maps[styles[st - 1].nmaps], 40, "%.39s", rel);
            styles[st - 1].nmaps++;
        }
        loaded[nloaded++] = m;
    }
    bool any = false;
    for (int si = 0; si < STYLES; si++) {
        StyleMan *sm = &styles[si];
        sm->number = si + 1;
        snprintf(sm->file, sizeof sm->file, "GTADATA/STYLE%03d.G24", si + 1);
        Style *s = style_load(si + 1, err, sizeof err);
        if (!s) { fprintf(stderr, "skin_template: %s: %s\n", sm->file, err); continue; }
        sm->ok = any = true;
        sm->nside = s->nside, sm->nlid = s->nlid, sm->naux = s->naux;
        for (int i = 0; i < nloaded; i++)
            if ((loaded[i]->style ? loaded[i]->style : 1) == si + 1) scan_map(sm, loaded[i]);
        sm->nanims = s->nanims;
        for (int i = 0; i < s->nanims; i++) {
            const uint8_t *d = s->anims[i].def;
            Anim *a = &sm->anim[i];
            a->block = d[0], a->which = d[1], a->speed = d[2], a->nframes = d[3];
            memcpy(a->frame, d + 4, d[3]);
            for (int f = 0; f < a->nframes; f++) {
                int x = a->frame[f];
                sm->aux_shown[x]++;
                if (a->which == 0) sm->aux_dirs[x] |= sm->side[a->block].dirs;
                else sm->aux_remaps[x] |= sm->lid[a->block].remaps;
            }
        }
        /* sprites, cars, peds */
        sm->nsprites = sprite_count();
        sm->spr = calloc((size_t)sm->nsprites, sizeof *sm->spr);
        for (int n = 0; n < sm->nsprites; n++) {
            const SpriteInfo *in = sprite_get_info(n);
            SpriteMan *p = &sm->spr[n];
            p->w = in->w, p->h = in->h, p->ndeltas = in->ndeltas < SPRITE_DELTAS_MAX ? in->ndeltas : SPRITE_DELTAS_MAX;
            p->group = -1;
            for (int g = 0; g < SPRITE_GROUPS; g++)
                if (sprite_group_count(g) && n >= sprite_group_base(g) && n < sprite_group_base(g) + sprite_group_count(g))
                    p->group = g, p->index = n - sprite_group_base(g);
            for (int k = 0; k < p->ndeltas; k++) p->dsize[k] = in->delta[k].size, p->dpix[k] = delta_pixels(in, k);
        }
        car_info_setup(s);
        sm->ncars = car_info_count();
        for (int i = 0; i < sm->ncars; i++) {
            const uint8_t *r = car_info_record(i);
            CarMan *c = &sm->car[i];
            *c = (CarMan){ i, r[0x6b], r[0x6a], carinfo_s16(r, 6), carinfo_s16(r, 0xac) };
            if (c->sprite >= 0 && c->sprite < sm->nsprites && sm->spr[c->sprite].ncars < 16)
                sm->spr[c->sprite].car[sm->spr[c->sprite].ncars++] = i;
        }
        sm->ped_remaps = s->font_pal_base - sprite_ped_palette();
        if (sm->ped_remaps < 0) sm->ped_remaps = 0;
        style_free(s);
    }
    for (int i = 0; i < nloaded; i++) map_free(loaded[i]);
    free(maps.names);

    NameList fl = list_data(".FON");
    fonts = calloc((size_t)(fl.n ? fl.n : 1), sizeof *fonts);
    for (int i = 0; i < fl.n; i++) {
        FontMan *f = &fonts[nfonts];
        snprintf(f->name, sizeof f->name, "%.*s", (int)strlen(fl.names[i]) - 4, fl.names[i]);
        upper(f->name);
        snprintf(f->file, sizeof f->file, "GTADATA/%s", fl.names[i]);
        f->first = font_first(f->name);
        Font *fo = font_load(f->file, (uint16_t)f->first, false, err, sizeof err);
        if (!fo) { fprintf(stderr, "skin_template: %s: %s\n", f->file, err); continue; }
        f->count = fo->count, f->height = fo->height;
        for (int g = 0; g < fo->count; g++) f->w[g] = fo->glyph[g].w;
        font_free(fo);
        nfonts++;
    }
    free(fl.names);

    NameList pl = list_data(".RAW");
    pics = calloc((size_t)(pl.n ? pl.n : 1), sizeof *pics);
    for (int i = 0; i < pl.n; i++) {
        PicMan *p = &pics[npics];
        snprintf(p->file, sizeof p->file, "GTADATA/%s", pl.names[i]);
        p->bytes = (long)vfs_size(p->file);
        picture_size(p->bytes, &p->w, &p->h);
        if (!p->w || strchr(pl.names[i], ' ')) continue;   /* not a picture the game shows (a palette, DOS leftovers) */
        snprintf(p->name, sizeof p->name, "%.*s", (int)strlen(pl.names[i]) - 4, pl.names[i]);
        upper(p->name);
        npics++;
    }
    free(pl.names);
    return any;
}

static const StyleMan *style_man(int number)
{
    return number >= 1 && number <= STYLES && styles[number - 1].ok ? &styles[number - 1] : NULL;
}
static const FontMan *font_man(const char *name)
{
    for (int i = 0; i < nfonts; i++)
        if (!strcmp(fonts[i].name, name)) return &fonts[i];
    return NULL;
}
static const PicMan *pic_man(const char *name)
{
    for (int i = 0; i < npics; i++)
        if (!strcmp(pics[i].name, name)) return &pics[i];
    return NULL;
}

/* remaps a sprite is drawn with: 0 none, else 1..count */
static int sprite_remaps(const StyleMan *sm, int n, const char **why)
{
    const SpriteMan *p = &sm->spr[n];
    if (p->ncars) { *why = "car colours"; return CAR_REMAPS; }
    if (p->group == SPRITE_GROUP_PED) { *why = "ped clothes"; return sm->ped_remaps; }
    *why = NULL;
    return 0;
}

/* ---- files ---- */

static bool mkdirs(const char *path)
{
    char p[1024];
    snprintf(p, sizeof p, "%s", path);
    for (char *c = p + 1; *c; c++)
        if (*c == '/') {
            *c = 0;
            if (mkdir(p, 0755) && errno != EEXIST) return false;
            *c = '/';
        }
    return !mkdir(p, 0755) || errno == EEXIST;
}

static bool exists(const char *dir, const char *rel)
{
    char p[1024];
    struct stat st;
    snprintf(p, sizeof p, "%s/%s", dir, rel);
    return !stat(p, &st) && S_ISREG(st.st_mode);
}

static void bits(char *out, size_t cap, uint8_t mask, const char *const *names)
{
    out[0] = 0;
    for (int i = 0; i < 8; i++)
        if (mask >> i & 1) {
            size_t n = strlen(out);
            if (names) snprintf(out + n, cap - n, "%s%s", n ? ", " : "", names[i]);
            else snprintf(out + n, cap - n, "%s%d", n ? ", " : "", i);
        }
}

/* ---- skin.json ---- */

static void json_str(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') fputc('\\', f);
        if ((unsigned char)*s >= 0x20) fputc(*s, f);
    }
    fputc('"', f);
}

static void json_bits(FILE *f, uint8_t mask)
{
    fputc('[', f);
    for (int i = 0, n = 0; i < 8; i++)
        if (mask >> i & 1) fprintf(f, "%s%d", n++ ? ", " : "", i);
    fputc(']', f);
}

static void write_json(FILE *f)
{
    fprintf(f, "{\n  \"format\": \"opengta-skin-manifest\",\n  \"version\": 1,\n"
               "  \"note\": \"Metadata of the replaceable assets of your game data (names, numbers, sizes). No game pixels. See docs/skins.md.\",\n"
               "  \"layout\": {\n"
               "    \"tile\": \"style<NNN>/<side|lid|aux>/<n>.png, variant: <n>_r<0..3>.png\",\n"
               "    \"sprite\": \"style<NNN>/sprite/<n>.png, <n>_r<remap>.png, <n>_mask.png (paint), <n>_index.png (remap index map), <n>_delta<k>.png\",\n"
               "    \"font\": \"font/<FONT>/<code>.png (code = first + glyph index, decimal)\",\n"
               "    \"picture\": \"pictures/<NAME>.png\"\n  },\n");
    fprintf(f, "  \"styles\": [");
    bool first_style = true;
    for (int si = 0; si < STYLES; si++) {
        const StyleMan *sm = &styles[si];
        if (!sm->ok) continue;
        fprintf(f, "%s\n    {\n      \"style\": %d, \"folder\": \"style%03d\", \"file\": ", first_style ? "" : ",", sm->number, sm->number);
        first_style = false;
        json_str(f, sm->file);
        fprintf(f, ", \"maps\": [");
        for (int i = 0; i < sm->nmaps; i++) fprintf(f, "%s", i ? ", " : ""), json_str(f, sm->maps[i]);
        fprintf(f, "],\n      \"tile_size\": [64, 64],\n");
        /* sides: directions 0 top, 1 bottom, 2 left, 3 right */
        fprintf(f, "      \"side\": { \"count\": %d, \"variants\": \"direction: 0 top, 1 bottom, 2 left, 3 right\", \"tiles\": [", sm->nside);
        for (int n = 0; n < sm->nside; n++) {
            fprintf(f, "%s\n        { \"n\": %d, \"faces\": %d, \"directions\": ", n ? "," : "", n, sm->side[n].uses);
            json_bits(f, sm->side[n].dirs);
            fprintf(f, " }");
        }
        fprintf(f, "\n      ] },\n");
        fprintf(f, "      \"lid\": { \"count\": %d, \"variants\": \"remap 0..3 (the block's ext bits 3-4)\", \"tiles\": [", sm->nlid);
        for (int n = 0; n < sm->nlid; n++) {
            fprintf(f, "%s\n        { \"n\": %d, \"faces\": %d, \"remaps\": ", n ? "," : "", n, sm->lid[n].uses);
            json_bits(f, sm->lid[n].remaps);
            fprintf(f, " }");
        }
        fprintf(f, "\n      ] },\n");
        fprintf(f, "      \"aux\": { \"count\": %d, \"variants\": \"those of the tile whose animation shows it\", \"tiles\": [", sm->naux);
        for (int n = 0; n < sm->naux; n++) {
            fprintf(f, "%s\n        { \"n\": %d, \"animations\": %d, \"directions\": ", n ? "," : "", n, sm->aux_shown[n]);
            json_bits(f, sm->aux_dirs[n]);
            fprintf(f, ", \"remaps\": ");
            json_bits(f, sm->aux_remaps[n]);
            fprintf(f, " }");
        }
        fprintf(f, "\n      ] },\n      \"animations\": [");
        for (int i = 0; i < sm->nanims; i++) {
            const Anim *a = &sm->anim[i];
            fprintf(f, "%s\n        { \"%s\": %d, \"every\": %d, \"aux_frames\": [", i ? "," : "", a->which ? "lid" : "side", a->block, a->speed);
            for (int k = 0; k < a->nframes; k++) fprintf(f, "%s%d", k ? ", " : "", a->frame[k]);
            fprintf(f, "] }");
        }
        fprintf(f, "\n      ],\n      \"sprite_groups\": [");
        for (int g = 0, k = 0; g < SPRITE_GROUPS; g++) {
            fprintf(f, "%s\n        { \"group\": ", k++ ? "," : "");
            json_str(f, GROUP_NAME[g]);
            int base = -1, count = 0;
            for (int n = 0; n < sm->nsprites; n++)
                if (sm->spr[n].group == g) { if (base < 0) base = n; count++; }
            fprintf(f, ", \"first\": %d, \"count\": %d }", base < 0 ? 0 : base, count);
        }
        fprintf(f, "\n      ],\n      \"sprites\": [");
        for (int n = 0; n < sm->nsprites; n++) {
            const SpriteMan *p = &sm->spr[n];
            fprintf(f, "%s\n        { \"n\": %d, \"group\": ", n ? "," : "", n);
            json_str(f, p->group >= 0 ? GROUP_NAME[p->group] : "?");
            fprintf(f, ", \"index\": %d, \"w\": %d, \"h\": %d", p->index, p->w, p->h);
            const char *why;
            int r = sprite_remaps(sm, n, &why);
            if (r) fprintf(f, ", \"remaps\": [1, %d], \"remap_use\": \"%s\"", r, why);
            if (p->ncars) {
                fprintf(f, ", \"car_records\": [");
                for (int i = 0; i < p->ncars; i++) fprintf(f, "%s%d", i ? ", " : "", p->car[i]);
                fputc(']', f);
            }
            if (p->ndeltas) {
                fprintf(f, ", \"deltas\": [");
                for (int k = 0; k < p->ndeltas; k++) fprintf(f, "%s%d", k ? ", " : "", p->dpix[k]);
                fputc(']', f);
            }
            fprintf(f, " }");
        }
        fprintf(f, "\n      ],\n      \"cars\": [");
        for (int i = 0; i < sm->ncars; i++) {
            const CarMan *c = &sm->car[i];
            fprintf(f, "%s\n        { \"record\": %d, \"model\": %d, \"vtype\": %d, \"sprite\": %d, \"doors\": %d }", i ? "," : "",
                    c->record, c->model, c->vtype, c->sprite, c->doors);
        }
        fprintf(f, "\n      ],\n      \"ped_remaps\": %d\n    }", sm->ped_remaps);
    }
    fprintf(f, "\n  ],\n  \"fonts\": [");
    for (int i = 0; i < nfonts; i++) {
        const FontMan *fo = &fonts[i];
        fprintf(f, "%s\n    { \"name\": ", i ? "," : "");
        json_str(f, fo->name);
        fprintf(f, ", \"file\": ");
        json_str(f, fo->file);
        fprintf(f, ", \"first\": %d, \"height\": %d, \"glyphs\": [", fo->first, fo->height);
        for (int g = 0; g < fo->count; g++) fprintf(f, "%s[%d, %d]", g ? ", " : "", fo->first + g, fo->w[g]);
        fprintf(f, "] }");
    }
    fprintf(f, "\n  ],\n  \"pictures\": [");
    for (int i = 0; i < npics; i++) {
        fprintf(f, "%s\n    { \"name\": ", i ? "," : "");
        json_str(f, pics[i].name);
        fprintf(f, ", \"file\": ");
        json_str(f, pics[i].file);
        fprintf(f, ", \"w\": %d, \"h\": %d }", pics[i].w, pics[i].h);
    }
    fprintf(f, "\n  ]\n}\n");
}

/* ---- CHECKLIST.md ---- */

static const char *box(const char *dir, const char *rel) { return exists(dir, rel) ? "[x]" : "[ ]"; }

static void write_checklist(FILE *f, const char *dir)
{
    char rel[256], b[128];
    fprintf(f, "# Skin checklist\n\n"
               "Every asset this skin can replace, from your game data (made by `skin_template`; metadata only, no\n"
               "game pixels). `[x]`: the file is in the folder. Anything you leave out is drawn from the original art.\n"
               "Layout and rules: docs/skins.md; step by step: docs/howto/create-a-skin.md. Check the skin with\n"
               "`skin_template --validate <folder>`.\n\n"
               "Faces: how many block faces of the maps show the tile (0: not placed by the maps; it may still appear\n"
               "through an animation or a map change during a mission).\n");
    for (int si = 0; si < STYLES; si++) {
        const StyleMan *sm = &styles[si];
        if (!sm->ok) continue;
        fprintf(f, "\n## Style %d (`style%03d/`, %s", sm->number, sm->number, sm->file);
        for (int i = 0; i < sm->nmaps; i++) fprintf(f, "%s%s", i ? ", " : ": ", sm->maps[i]);
        fprintf(f, ")\n\nTiles are 64 x 64 in the original; images should be square (see the sizes in docs/skins.md).\n");
        fprintf(f, "\n### Lids (%d)\n\nVariants: remap 0..3 (`lid/<n>_r<r>.png`; without one, the plain image is shaded like the remap).\n\n", sm->nlid);
        for (int n = 1; n < sm->nlid; n++) {
            if (!sm->lid[n].uses) continue;
            snprintf(rel, sizeof rel, "style%03d/lid/%d.png", sm->number, n);
            bits(b, sizeof b, sm->lid[n].remaps, NULL);
            fprintf(f, "- %s `lid/%d.png`: %d faces, remaps %s\n", box(dir, rel), n, sm->lid[n].uses, b);
        }
        fprintf(f, "\n### Sides (%d)\n\nVariants: the direction the face looks (`side/<n>_r<d>.png`: 0 top, 1 bottom, 2 left, 3 right).\n\n", sm->nside);
        for (int n = 1; n < sm->nside; n++) {
            if (!sm->side[n].uses) continue;
            snprintf(rel, sizeof rel, "style%03d/side/%d.png", sm->number, n);
            bits(b, sizeof b, sm->side[n].dirs, DIR_NAME);
            fprintf(f, "- %s `side/%d.png`: %d faces, directions %s\n", box(dir, rel), n, sm->side[n].uses, b);
        }
        fprintf(f, "\n### Animations and aux tiles (%d aux tiles)\n\nAn animated tile shows aux tiles in turn, then itself: replace the frames too, or the\n"
                   "animation shows the original ones. Aux tiles take the variants of the tile they animate.\n\n", sm->naux);
        for (int i = 0; i < sm->nanims; i++) {
            const Anim *a = &sm->anim[i];
            fprintf(f, "- %s %d (every %d frames):", a->which ? "lid" : "side", a->block, a->speed);
            for (int k = 0; k < a->nframes; k++) {
                snprintf(rel, sizeof rel, "style%03d/aux/%d.png", sm->number, a->frame[k]);
                fprintf(f, " %s `aux/%d.png`", box(dir, rel), a->frame[k]);
            }
            fputc('\n', f);
        }
        int unused = 0;
        for (int n = 0; n < sm->naux; n++) unused += !sm->aux_shown[n];
        if (unused) {
            fprintf(f, "\nAux tiles no animation shows (used by map changes, if at all):");
            for (int n = 0, k = 0; n < sm->naux; n++)
                if (!sm->aux_shown[n]) fprintf(f, "%s %d", k++ ? "," : "", n);
            fputc('\n', f);
        }
        fprintf(f, "\nLids and sides the maps don't place:");
        for (int n = 1, k = 0; n < sm->nlid; n++)
            if (!sm->lid[n].uses) fprintf(f, "%s lid %d", k++ ? "," : "", n);
        fprintf(f, ";");
        for (int n = 1, k = 0; n < sm->nside; n++)
            if (!sm->side[n].uses) fprintf(f, "%s side %d", k++ ? "," : "", n);
        fprintf(f, ".\n\n### Sprites (%d)\n\nSizes are the original's in pixels (an image is stretched over that footprint: keep the aspect).\n"
                   "Remapped sprites: `sprite/<n>_r<r>.png` per remap, one `sprite/<n>_mask.png` paint mask (cars) or one\n"
                   "`sprite/<n>_index.png` remap index map (ped clothes: several parts per remap). Deltas\n"
                   "(damage, doors, lights): `sprite/<n>_delta<k>.png`, else the original delta shades the image.\n", sm->nsprites);
        int last_group = -2;
        for (int n = 0; n < sm->nsprites; n++) {
            const SpriteMan *p = &sm->spr[n];
            if (p->group != last_group) {
                last_group = p->group;
                int c = 0;
                for (int k = 0; k < sm->nsprites; k++) c += sm->spr[k].group == p->group;
                fprintf(f, "\n#### %s (%d)\n\n", p->group >= 0 ? GROUP_NAME[p->group] : "?", c);
            }
            snprintf(rel, sizeof rel, "style%03d/sprite/%d.png", sm->number, n);
            fprintf(f, "- %s `sprite/%d.png`: %d x %d", box(dir, rel), n, p->w, p->h);
            const char *why;
            int r = sprite_remaps(sm, n, &why);
            if (r) {
                snprintf(rel, sizeof rel, "style%03d/sprite/%d_mask.png", sm->number, n);
                fprintf(f, ", remaps 1-%d (%s; mask %s)", r, why, box(dir, rel));
            }
            if (p->ncars) {
                fprintf(f, ", car record%s", p->ncars > 1 ? "s" : "");
                for (int i = 0; i < p->ncars; i++) fprintf(f, " %d (model %d)", p->car[i], sm->car[p->car[i]].model);
            }
            if (p->ndeltas) {
                int empty = 0;
                for (int k = 0; k < p->ndeltas; k++) empty += !p->dpix[k];
                fprintf(f, ", deltas 0-%d", p->ndeltas - 1);
                if (empty) fprintf(f, " (%d empty)", empty);
            }
            fputc('\n', f);
        }
    }
    fprintf(f, "\n## Fonts (`font/<FONT>/<code>.png`)\n\nOne image per glyph, named by its character code (decimal). Sizes: glyph width x the font's height.\n\n");
    for (int i = 0; i < nfonts; i++) {
        const FontMan *fo = &fonts[i];
        int have = 0;
        for (int g = 0; g < fo->count; g++) {
            snprintf(rel, sizeof rel, "font/%s/%d.png", fo->name, fo->first + g);
            have += exists(dir, rel);
        }
        fprintf(f, "- %s `font/%s/`: %d glyphs (codes %d-%d), height %d", have == fo->count && have ? "[x]" : "[ ]", fo->name, fo->count,
                fo->first, fo->first + fo->count - 1, fo->height);
        if (have && have < fo->count) fprintf(f, " (%d done)", have);
        fputc('\n', f);
    }
    fprintf(f, "\n## Pictures (`pictures/<NAME>.png`)\n\n");
    for (int i = 0; i < npics; i++) {
        snprintf(rel, sizeof rel, "pictures/%s.png", pics[i].name);
        fprintf(f, "- %s `pictures/%s.png`: %d x %d\n", box(dir, rel), pics[i].name, pics[i].w, pics[i].h);
    }
}

static int generate(const char *dir)
{
    char p[1024];
    if (!mkdirs(dir)) { fprintf(stderr, "skin_template: can't create %s: %s\n", dir, strerror(errno)); return 1; }
    static const char *const kinds[] = { "side", "lid", "aux", "sprite" };
    for (int si = 0; si < STYLES; si++)
        for (int k = 0; k < 4 && styles[si].ok; k++) {
            snprintf(p, sizeof p, "%s/style%03d/%s", dir, si + 1, kinds[k]);
            if (!mkdirs(p)) { fprintf(stderr, "skin_template: can't create %s\n", p); return 1; }
        }
    snprintf(p, sizeof p, "%s/font", dir), mkdirs(p);
    snprintf(p, sizeof p, "%s/pictures", dir), mkdirs(p);
    if (!exists(dir, "skin.ini")) {
        snprintf(p, sizeof p, "%s/skin.ini", dir);
        FILE *f = fopen(p, "w");
        if (!f) { fprintf(stderr, "skin_template: %s: %s\n", p, strerror(errno)); return 1; }
        fprintf(f, "; An OpenGTA skin (docs/skins.md). Only your own art: nothing copied, traced or derived from the game.\n"
                   "name = My skin\nauthor = \n; the hires scale the art is drawn for (1..4)\nscale = 2\n");
        fclose(f);
        printf("wrote %s\n", p);
    }
    snprintf(p, sizeof p, "%s/skin.json", dir);
    FILE *f = fopen(p, "w");
    if (!f) { fprintf(stderr, "skin_template: %s: %s\n", p, strerror(errno)); return 1; }
    write_json(f);
    fclose(f);
    printf("wrote %s\n", p);
    snprintf(p, sizeof p, "%s/CHECKLIST.md", dir);
    if (!(f = fopen(p, "w"))) { fprintf(stderr, "skin_template: %s: %s\n", p, strerror(errno)); return 1; }
    write_checklist(f, dir);
    fclose(f);
    printf("wrote %s\n", p);
    for (int si = 0; si < STYLES; si++) {
        const StyleMan *sm = &styles[si];
        if (!sm->ok) continue;
        int lids = 0, sides = 0;
        for (int n = 1; n < 256; n++) lids += sm->lid[n].uses > 0, sides += sm->side[n].uses > 0;
        printf("style %d: %d lids (%d on the maps), %d sides (%d), %d aux tiles (%d animations), %d sprites, %d car records\n",
               sm->number, sm->nlid, lids, sm->nside, sides, sm->naux, sm->nanims, sm->nsprites, sm->ncars);
    }
    printf("%d fonts, %d pictures. Folders for the images are in %s (no game pixels were written).\n", nfonts, npics, dir);
    return 0;
}

/* ---- --validate ---- */

static int nerr, nwarn, nnote;

static void report(int level, const char *path, const char *fmt, ...)
{
    static const char *const tag[] = { "note", "warning", "error" };
    char m[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(m, sizeof m, fmt, ap);
    va_end(ap);
    printf("%-7s %s: %s\n", tag[level], path, m);
    if (level == 2) nerr++;
    else if (level == 1) nwarn++;
    else nnote++;
}
enum { NOTE, WARN, ERR };

typedef struct { char (*rel)[256]; int n, cap; } Files;

static void walk(const char *root, const char *sub, Files *out)
{
    char p[1024];
    snprintf(p, sizeof p, "%s%s%s", root, *sub ? "/" : "", sub);
    DIR *d = opendir(p);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;   /* ., .., hidden files (.DS_Store) */
        char rel[256], full[1300];
        snprintf(rel, sizeof rel, "%.127s%s%.127s", sub, *sub ? "/" : "", e->d_name);
        snprintf(full, sizeof full, "%s/%s", root, rel);
        struct stat st;
        if (stat(full, &st)) continue;
        if (S_ISDIR(st.st_mode)) walk(root, rel, out);
        else if (S_ISREG(st.st_mode)) {
            if (out->n == out->cap) out->cap = out->cap ? out->cap * 2 : 256, out->rel = realloc(out->rel, (size_t)out->cap * sizeof *out->rel);
            snprintf(out->rel[out->n++], 256, "%s", rel);
        }
    }
    closedir(d);
}

static int cmp_rel(const void *a, const void *b) { return strcmp(a, b); }

/* a decimal number without leading zeros (what the loader asks for), advancing *s; -1 if none */
static int number(const char **s, bool *zeros)
{
    const char *p = *s;
    if (!isdigit((unsigned char)*p)) return -1;
    *zeros = p[0] == '0' && isdigit((unsigned char)p[1]);
    long v = 0;
    while (isdigit((unsigned char)*p) && v < 100000) v = v * 10 + (*p++ - '0');
    *s = p;
    return (int)v;
}

static uint32_t *decode(const char *dir, const char *rel, int *w, int *h)
{
    char p[1300];
    snprintf(p, sizeof p, "%s/%s", dir, rel);
    FILE *f = fopen(p, "rb");
    if (!f) { report(ERR, rel, "can't read it"); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(n > 0 ? (size_t)n : 1);
    size_t got = d ? fread(d, 1, (size_t)n, f) : 0;
    fclose(f);
    char err[160];
    uint32_t *px = d && got == (size_t)n ? hires_png_decode(d, (size_t)n, w, h, err, sizeof err) : NULL;
    if (!px) report(ERR, rel, "%s", d && got == (size_t)n ? err : "can't read it");
    free(d);
    return px;
}

static bool any_alpha(const uint32_t *px, int w, int h)
{
    for (int i = 0; i < w * h; i++)
        if (px[i] >> 24 != 0xff) return true;
    return false;
}

/* aspect ratio of a W x H image against w x h, in percent */
static int aspect_off(int W, int H, int w, int h)
{
    double a = (double)W / H, b = (double)w / h;
    double d = a > b ? a / b : b / a;
    return (int)((d - 1) * 100 + 0.5);
}

typedef struct { int style, kind, n, variant; } Seen;   /* variant: -1 plain, -2 mask, -3 index map, 0.. _r, 100 + k delta */
static Seen *seen;
static int nseen;
static bool have(int style, int kind, int n, int variant)
{
    for (int i = 0; i < nseen; i++)
        if (seen[i].style == style && seen[i].kind == kind && seen[i].n == n && seen[i].variant == variant) return true;
    return false;
}

static void check_ini(const char *dir, int *scale)
{
    char p[1300];
    snprintf(p, sizeof p, "%s/skin.ini", dir);
    FILE *f = fopen(p, "r");
    *scale = 1;
    if (!f) { report(ERR, "skin.ini", "missing: the game skips a skin without it"); return; }
    char line[512];
    bool name = false, author = false, sc = false;
    while (fgets(line, sizeof line, f)) {
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (*s == ';' || *s == '#' || *s == '[' || *s == '\n' || *s == '\r' || !*s) continue;
        char *eq = strchr(s, '=');
        if (!eq) { report(WARN, "skin.ini", "line without '=' ignored: %.60s", s); continue; }
        char *e = eq;
        while (e > s && (e[-1] == ' ' || e[-1] == '\t')) e--;
        *e = 0;
        char *v = eq + 1;
        while (*v == ' ' || *v == '\t') v++;
        v[strcspn(v, "\r\n")] = 0;
        if (!strcmp(s, "name")) name = *v != 0;
        else if (!strcmp(s, "author")) author = *v != 0;
        else if (!strcmp(s, "scale")) {
            sc = true;
            *scale = atoi(v);
            if (*scale < 1 || *scale > 4) report(WARN, "skin.ini", "scale = %s: the hires scales are 1..4", v), *scale = 1;
        } else report(WARN, "skin.ini", "unknown key '%s' (name, author, scale)", s);
    }
    fclose(f);
    if (!name) report(WARN, "skin.ini", "no name = (the folder name is shown instead)");
    if (!author) report(NOTE, "skin.ini", "no author =");
    if (!sc) report(NOTE, "skin.ini", "no scale = (taken as 1)");
}

typedef struct { int w, h; } Size;
static int si_of(const StyleMan *sm) { return sm->number - 1; }

static int validate(const char *dir)
{
    struct stat st;
    if (stat(dir, &st) || !S_ISDIR(st.st_mode)) { fprintf(stderr, "skin_template: %s is not a folder\n", dir); return 2; }
    const char *base = strrchr(dir, '/');
    base = base && base[1] ? base + 1 : dir;
    printf("skin folder %s (skin=%s)\n", dir, base);
    if (strchr(base, ',') || base[0] == ' ' || base[strlen(base) - 1] == ' ')
        report(ERR, base, "a skin's folder name can't contain ',' or start / end with a space (skin=a,b is a list)");
    int scale;
    check_ini(dir, &scale);
    Files fs = { 0 };
    walk(dir, "", &fs);
    if (fs.n) qsort(fs.rel, (size_t)fs.n, sizeof *fs.rel, cmp_rel);
    seen = calloc((size_t)(fs.n ? fs.n : 1), sizeof *seen);
    /* plain sprite image sizes, for the masks: index by style * 0x10000 + n */
    Size *sprite_size = calloc(STYLES * 0x1000, sizeof *sprite_size);
    int replaced[STYLES][4] = { { 0 } }, nfont = 0, npic = 0, nimages = 0;
    for (int pass = 0; pass < 2; pass++)   /* plain images first, so masks can be checked against them */
        for (int i = 0; i < fs.n; i++) {
            const char *rel = fs.rel[i];
            char seg[4][128] = { { 0 } };
            int nseg = 0;
            for (const char *p = rel; *p && nseg < 4;) {
                size_t l = strcspn(p, "/");
                snprintf(seg[nseg++], sizeof seg[0], "%.*s", (int)l, p);
                p += l + (p[l] == '/');
            }
            if (nseg == 0) continue;   /* an empty path */
            /* a per-resolution variant "<name>@<k>x.png" (k = 1..4) is checked like "<name>.png" */
            char *at = strrchr(seg[nseg - 1], '@');
            if (at) {
                if (at[1] >= '1' && at[1] <= '4' && !strcmp(at + 2, "x.png")) memmove(at, at + 3, strlen(at + 3) + 1);
                else { if (!pass) report(ERR, rel, "a resolution variant is <name>@1x.png .. <name>@4x.png"); continue; }
            }
            const char *last = seg[nseg - 1];
            size_t ll = strlen(last);
            bool png = ll > 4 && !strcmp(last + ll - 4, ".png"), PNG = ll > 4 && !strcasecmp(last + ll - 4, ".png");
            if (nseg == 1) {   /* the skin's own files */
                if (pass) continue;
                if (!strcmp(last, "skin.ini") || !strcmp(last, "skin.json") || !strcmp(last, "CHECKLIST.md")) continue;
                if (!strncasecmp(last, "README", 6) || !strncasecmp(last, "LICENSE", 7) || !strncasecmp(last, "COPYING", 7) ||
                    (ll > 3 && !strcasecmp(last + ll - 3, ".md")) || (ll > 4 && !strcasecmp(last + ll - 4, ".txt")))
                    continue;
                report(WARN, rel, "not read by the game");
                continue;
            }
            if (PNG && !png) { if (!pass) report(ERR, rel, "the game asks for \".png\" in lower case"); continue; }
            /* style<NNN>/<kind>/<n>[_r<r>|_mask|_delta<k>].png */
            if (!strncmp(seg[0], "style", 5) && nseg == 3) {
                int number_ = atoi(seg[0] + 5);
                const StyleMan *sm = style_man(number_);
                char want[16];
                snprintf(want, sizeof want, "style%03d", number_);
                if (strcmp(seg[0], want) || !sm) { if (!pass) report(ERR, rel, "unknown style folder (style001, style002, style003)"); continue; }
                int kind = !strcmp(seg[1], "side") ? KIND_SIDE : !strcmp(seg[1], "lid") ? KIND_LID : !strcmp(seg[1], "aux") ? KIND_AUX
                         : !strcmp(seg[1], "sprite") ? KIND_SPRITE : -1;
                if (kind < 0) { if (!pass) report(ERR, rel, "unknown folder '%s' (side, lid, aux, sprite)", seg[1]); continue; }
                if (!png) { if (!pass) report(WARN, rel, "not a .png: not read by the game"); continue; }
                const char *p = last;
                bool zeros;
                int n = number(&p, &zeros), variant = -1;
                bool bad = n < 0;
                if (!bad && !strcmp(p, ".png")) variant = -1;
                else if (!bad && !strncmp(p, "_r", 2)) {
                    p += 2;
                    bool z2;
                    variant = number(&p, &z2);
                    bad = variant < 0 || strcmp(p, ".png") || z2;
                } else if (!bad && !strcmp(p, "_mask.png") && kind == KIND_SPRITE) variant = -2;
                else if (!bad && !strcmp(p, "_index.png") && kind == KIND_SPRITE) variant = -3;
                else if (!bad && !strncmp(p, "_delta", 6) && kind == KIND_SPRITE) {
                    p += 6;
                    bool z2;
                    int k = number(&p, &z2);
                    bad = k < 0 || strcmp(p, ".png") || z2;
                    variant = 100 + k;
                } else bad = true;
                if (bad) {
                    if (!pass) report(ERR, rel, kind == KIND_SPRITE ? "name it <n>.png, <n>_r<remap>.png, <n>_mask.png, <n>_index.png or <n>_delta<k>.png"
                                                                    : "name it <n>.png or <n>_r<0..3>.png");
                    continue;
                }
                if (zeros) { if (!pass) report(ERR, rel, "leading zeros: the game asks for %s/%d.png", seg[1], n); continue; }
                const bool plain = variant == -1;
                if ((pass == 0) != plain) continue;
                const int count = kind == KIND_SIDE ? sm->nside : kind == KIND_LID ? sm->nlid : kind == KIND_AUX ? sm->naux : sm->nsprites;
                if (n >= count) { report(ERR, rel, "style %d has %s 0..%d", sm->number, seg[1], count - 1); continue; }
                int w, h;
                uint32_t *px = decode(dir, rel, &w, &h);
                if (!px) continue;
                nimages++;
                seen[nseen++] = (Seen){ sm->number, kind, n, variant };
                if (kind != KIND_SPRITE) {
                    if (plain) replaced[si_of(sm)][kind - KIND_SIDE]++;
                    if (variant > 3) report(ERR, rel, "%s variants are 0..3", seg[1]);
                    if (w != h) report(WARN, rel, "%d x %d: tiles are square (it is drawn stretched)", w, h);
                    if (w < 64 || h < 64) report(WARN, rel, "%d x %d is less detail than the original's 64 x 64", w, h);
                    if (w > 128 * 4 || h > 128 * 4) report(NOTE, rel, "%d x %d: box-filtered to %d at load (128 per hires step)", w, h, 128 * 4);
                    if (kind == KIND_LID && sm->lid[n].uses == 0 && plain) report(NOTE, rel, "lid %d isn't placed by the maps", n);
                    if (kind == KIND_SIDE && sm->side[n].uses == 0 && plain) report(NOTE, rel, "side %d isn't placed by the maps", n);
                    if (kind == KIND_AUX && !sm->aux_shown[n] && plain) report(NOTE, rel, "no animation shows aux %d", n);
                    if (variant >= 0 && variant <= 3) {
                        uint8_t used = kind == KIND_LID ? sm->lid[n].remaps : kind == KIND_SIDE ? sm->side[n].dirs : (uint8_t)(sm->aux_dirs[n] | sm->aux_remaps[n]);
                        if (!(used >> variant & 1)) report(NOTE, rel, "the maps don't draw %s %d with variant %d", seg[1], n, variant);
                    }
                } else {
                    const SpriteMan *sp = &sm->spr[n];
                    const char *why;
                    const int remaps = sprite_remaps(sm, n, &why);
                    if (plain) {
                        replaced[si_of(sm)][3]++;
                        sprite_size[si_of(sm) * 0x1000 + n] = (Size){ w, h };
                    }
                    if (variant == -1 || (variant >= 0 && variant < 100)) {
                        int off = aspect_off(w, h, sp->w, sp->h);
                        if (off > 3) report(WARN, rel, "%d x %d: the sprite is %d x %d (aspect off by %d%%, drawn stretched)", w, h, sp->w, sp->h, off);
                        if (w < sp->w || h < sp->h) report(WARN, rel, "%d x %d is less detail than the original's %d x %d", w, h, sp->w, sp->h);
                        const int cap = 2 * 4 * (sp->w > sp->h ? sp->w : sp->h);
                        if (w > cap || h > cap) report(NOTE, rel, "%d x %d: box-filtered at load (at most %d at hires=4)", w, h, cap);
                        if (!any_alpha(px, w, h)) report(NOTE, rel, "no transparency: the whole rectangle is drawn");
                    }
                    if (variant >= 0 && variant < 100) {
                        if (variant == 0) report(WARN, rel, "_r0 is never asked for: remap 0 is the plain image");
                        else if (!remaps) report(NOTE, rel, "sprite %d isn't drawn remapped by cars or peds", n);
                        else if (variant > remaps) report(WARN, rel, "sprite %d has remaps 1..%d (%s)", n, remaps, why);
                    } else if (variant == -2) {
                        if (!remaps) report(WARN, rel, "sprite %d isn't drawn remapped by cars or peds: the mask is never used", n);
                        const Size b = sprite_size[si_of(sm) * 0x1000 + n];
                        if (!b.w) report(WARN, rel, "no sprite/%d.png: the mask recolours that image, it is unused", n);
                        else if (b.w != w || b.h != h) {
                            if (aspect_off(w, h, b.w, b.h) > 1) report(ERR, rel, "%d x %d doesn't match sprite/%d.png (%d x %d)", w, h, n, b.w, b.h);
                            else report(NOTE, rel, "%d x %d, sprite/%d.png is %d x %d (scaled to it)", w, h, n, b.w, b.h);
                        }
                        bool grey = true;
                        for (int k = 0; k < w * h && grey; k++) {
                            uint32_t c = px[k];
                            int r = c & 0xff, g = c >> 8 & 0xff, bl = c >> 16 & 0xff;
                            grey = abs(r - g) < 16 && abs(g - bl) < 16;
                        }
                        if (!grey) report(WARN, rel, "a paint mask should be grey: white = paint, black = not (colours are read as brightness)");
                    } else if (variant == -3) {
                        if (!remaps) report(WARN, rel, "sprite %d isn't drawn remapped by cars or peds: the index map is never used", n);
                        const Size b = sprite_size[si_of(sm) * 0x1000 + n];
                        if (!b.w) report(WARN, rel, "no sprite/%d.png: the index map recolours that image, it is unused", n);
                        else if (aspect_off(w, h, b.w, b.h) > 1) report(ERR, rel, "%d x %d doesn't match sprite/%d.png (%d x %d)", w, h, n, b.w, b.h);
                        bool any = false;
                        for (int k = 0; k < w * h && !any; k++) any = (px[k] & 0xff) != 0;
                        if (!any) report(WARN, rel, "every index is 0 (red channel): nothing is recoloured");
                    } else if (variant >= 100) {
                        const int k = variant - 100;
                        if (k >= sp->ndeltas) report(ERR, rel, "sprite %d has %d deltas (0..%d)", n, sp->ndeltas, sp->ndeltas - 1);
                        else if (!sp->dpix[k]) report(WARN, rel, "delta %d of sprite %d is empty in the original: never shown", k, n);
                        const Size b = sprite_size[si_of(sm) * 0x1000 + n];
                        if (!b.w) report(WARN, rel, "no sprite/%d.png: deltas apply to the skin's image of the sprite", n);
                        else if (aspect_off(w, h, b.w, b.h) > 3) report(WARN, rel, "%d x %d: sprite/%d.png is %d x %d (drawn stretched over it)", w, h, n, b.w, b.h);
                        if (!any_alpha(px, w, h)) report(WARN, rel, "opaque: a delta image covers the whole sprite (make the rest transparent)");
                    }
                }
                free(px);
                continue;
            }
            if (pass) continue;
            /* font/<FONT>/<code>.png */
            if (!strcmp(seg[0], "font") && nseg == 3) {
                const FontMan *fo = font_man(seg[1]);
                const char *p = last;
                bool zeros;
                int code = number(&p, &zeros);
                if (!fo) { report(ERR, rel, "no font %s in the game data (the .FON name in capitals)", seg[1]); continue; }
                if (code < 0 || strcmp(p, ".png") || zeros) { report(ERR, rel, "name it <character code>.png (decimal)"); continue; }
                if (code < fo->first || code >= fo->first + fo->count || !fo->w[code - fo->first]) {
                    report(ERR, rel, "%s has glyphs %d..%d", fo->name, fo->first, fo->first + fo->count - 1);
                    continue;
                }
                int w, h;
                uint32_t *px = decode(dir, rel, &w, &h);
                if (!px) continue;
                nimages++, nfont++;
                int gw = fo->w[code - fo->first];
                if (aspect_off(w, h, gw, fo->height) > 10) report(WARN, rel, "%d x %d: the glyph is %d x %d", w, h, gw, fo->height);
                free(px);
                continue;
            }
            /* pictures/<NAME>.png */
            if (!strcmp(seg[0], "pictures") && nseg == 2) {
                char name[128];
                snprintf(name, sizeof name, "%.*s", ll > 4 ? (int)ll - 4 : 0, last);
                const PicMan *pm = pic_man(name);
                if (!png || !pm) { report(ERR, rel, "not a picture of the game (pictures/<NAME>.png, NAME as the .RAW file in capitals)"); continue; }
                int w, h;
                uint32_t *px = decode(dir, rel, &w, &h);
                if (!px) continue;
                nimages++, npic++;
                if (aspect_off(w, h, pm->w, pm->h) > 2) report(WARN, rel, "%d x %d: the picture is %d x %d", w, h, pm->w, pm->h);
                free(px);
                continue;
            }
            report(WARN, rel, "not read by the game (style<NNN>/<side|lid|aux|sprite>/, font/<FONT>/, pictures/)");
        }
    /* variants and masks whose plain image is missing are reported above; tiles: a variant alone is fine */
    printf("\n");
    for (int si = 0; si < STYLES; si++) {
        const StyleMan *sm = &styles[si];
        if (!sm->ok) continue;
        int lids = 0, sides = 0;
        for (int n = 1; n < 256; n++) lids += sm->lid[n].uses > 0, sides += sm->side[n].uses > 0;
        int rl = 0, rs = 0;
        for (int n = 1; n < 256; n++) rl += sm->lid[n].uses && have(sm->number, KIND_LID, n, -1), rs += sm->side[n].uses && have(sm->number, KIND_SIDE, n, -1);
        if (replaced[si][0] + replaced[si][1] + replaced[si][2] + replaced[si][3])
            printf("style %d: lids %d (%d of the %d the maps place), sides %d (%d of %d), aux %d of %d, sprites %d of %d\n", sm->number,
                   replaced[si][1], rl, lids, replaced[si][0], rs, sides, replaced[si][2], sm->naux, replaced[si][3], sm->nsprites);
    }
    if (nfont || npic) printf("fonts: %d glyphs, pictures: %d\n", nfont, npic);
    printf("%d images; %d errors, %d warnings, %d notes (made for hires=%d)\n", nimages, nerr, nwarn, nnote, scale);
    free(fs.rel), free(seen), free(sprite_size);
    return nerr ? 1 : 0;
}

/* ---- --at ---- */

static int at(const char *map, int x, int y, int z)
{
    char rel[96], err[256], name[64];
    snprintf(name, sizeof name, "%s", map);
    upper(name);
    if (!strcmp(name, "NYC") || !strcmp(name, "SANB") || !strcmp(name, "MIAMI")) snprintf(rel, sizeof rel, "GTADATA/%s.CMP", name);
    else snprintf(rel, sizeof rel, "GTADATA/%s", name);
    Map *m = map_load(rel, err, sizeof err);
    if (!m) { fprintf(stderr, "skin_template: %s: %s\n", rel, err); return 2; }
    const int st = m->style ? m->style : 1;
    const StyleMan *sm = style_man(st);
    if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) { fprintf(stderr, "skin_template: x and y are 0..255\n"); map_free(m); return 2; }
    printf("%s column (%d, %d), style %d: files under style%03d/ (z = 0 is the top layer)\n", rel, x, y, st, st);
    static const char *const face_name[4] = { "top", "bottom", "left", "right" };
    bool any = false;
    for (int zz = 0; zz < MAP_Z; zz++) {
        if (z >= 0 && zz != z) continue;
        const MapBlock *b = map_get_block(m, x, y, zz);
        if (!b || (!b->lid && !b->left && !b->right && !b->top && !b->bottom)) continue;
        any = true;
        const int slope = b->type_map >> 8 & 0x3f;
        printf("z %d:%s%s\n", zz, b->type_map & 0x80 ? " flat" : "", slope ? " slope" : "");
        const int r = (b->ext & 0x18) >> 3;
        if (b->lid) {
            printf("  lid    %3d  lid/%d.png", b->lid, b->lid);
            if (r) printf(" (remap %d: lid/%d_r%d.png, else the plain image shaded)", r, b->lid, r);
            putchar('\n');
        }
        const uint8_t face[4] = { b->top, b->bottom, b->left, b->right };
        for (int d = 0; d < 4; d++) {
            if (!face[d]) continue;
            printf("  %-6s %3d  side/%d.png", face_name[d], face[d], face[d]);
            if (d) printf(" (direction %d: side/%d_r%d.png, else the plain image shaded)", d, face[d], d);
            putchar('\n');
        }
        for (int i = 0; sm && i < sm->nanims; i++) {
            const Anim *a = &sm->anim[i];
            bool hit = a->which ? b->lid == a->block : (b->top == a->block || b->bottom == a->block || b->left == a->block || b->right == a->block);
            if (!hit) continue;
            printf("  %s %d is animated, frames:", a->which ? "lid" : "side", a->block);
            for (int k = 0; k < a->nframes; k++) printf(" aux/%d.png", a->frame[k]);
            putchar('\n');
        }
    }
    if (!any) printf("  (no faces)\n");
    map_free(m);
    return 0;
}

/* ---- --extract: the original graphics, as reference, in the skin layout ----
   For artists working over the real thing, on your own copy: every tile (with the remap and direction
   variants the maps use), every sprite (remap 0, plus the damage/door deltas as overlays and the car paint
   masks), every font glyph and every frontend picture, as PNGs at their original size, named as a skin
   names them. These are the game's own graphics: keep them for yourself, don't distribute them. */

#include "../tests/png.h"
#include "front/images.h"

static int nwritten;
static bool extract_remaps;   /* --remaps: every ped clothes remap too (tens of thousands of files) */

static bool put_png(const char *dir, const char *rel, const uint32_t *px, int w, int h)
{
    char p[1100];
    snprintf(p, sizeof p, "%s/%s", dir, rel);
    char d[1100];
    snprintf(d, sizeof d, "%s", p);
    char *sl = strrchr(d, '/');
    if (sl) *sl = 0, mkdirs(d);
    if (!png_write(p, px, w, h, PNG_RGBA)) { fprintf(stderr, "skin_template: can't write %s\n", p); return false; }
    nwritten++;
    return true;
}

static uint32_t abgr(uint32_t xrgb, bool opaque) { return (opaque ? 0xff000000u : 0) | (xrgb & 0xff) << 16 | (xrgb & 0xff00) | (xrgb >> 16 & 0xff); }

/* tile t through the CLUT (colour 0 transparent) */
static void tile_png(const Style *s, int t, const uint32_t *clut, uint32_t *out)
{
    const uint8_t *page = s->buf + style_tile_page(t);
    int u0 = (t & 3) * 64, v0 = ((t >> 2) & 3) * 64;
    for (int v = 0; v < 64; v++)
        for (int u = 0; u < 64; u++) {
            uint8_t e = page[(v0 + v) * 256 + u0 + u];
            out[v * 64 + u] = abgr(clut[e * 64], e != 0);
        }
}

static void sprite_png(const SpriteInfo *in, const uint8_t *pix, const uint32_t *clut, uint32_t *out)
{
    for (int v = 0; v < in->h; v++)
        for (int u = 0; u < in->w; u++) {
            uint8_t e = pix[v * 256 + u];
            out[v * in->w + u] = abgr(clut[e * 64], e != 0);
        }
}

static int extract(const char *dir)
{
    char err[256], rel[256];
    static uint32_t tile[64 * 64], a[256 * 256], b[256 * 256];
    static uint8_t page[256 * 256];
    for (int si = 0; si < STYLES; si++) {
        const StyleMan *sm = &styles[si];
        if (!sm->ok) continue;
        Style *s = style_load(si + 1, err, sizeof err);
        if (!s) { fprintf(stderr, "skin_template: %s\n", err); continue; }
        style_convert_palettes(s, &PIXFMT_32);
        for (int n = 0; n < s->nside; n++)
            for (int d = 0; d < 4; d++) {
                if (d && !(sm->side[n].dirs >> d & 1)) continue;
                tile_png(s, s->side_base + n, s->side_clut[n][d], tile);
                snprintf(rel, sizeof rel, d ? "style%03d/side/%d_r%d.png" : "style%03d/side/%d.png", si + 1, n, d);
                put_png(dir, rel, tile, 64, 64);
            }
        for (int n = 0; n < s->nlid; n++)
            for (int r = 0; r < 4; r++) {
                if (r && !(sm->lid[n].remaps >> r & 1)) continue;
                tile_png(s, s->lid_base + n, s->lid_clut[n][r], tile);
                snprintf(rel, sizeof rel, r ? "style%03d/lid/%d_r%d.png" : "style%03d/lid/%d.png", si + 1, n, r);
                put_png(dir, rel, tile, 64, 64);
            }
        for (int n = 0; n < s->naux; n++) {
            tile_png(s, s->aux_base + n, s->aux_side_clut[n][0], tile);
            snprintf(rel, sizeof rel, "style%03d/aux/%d.png", si + 1, n);
            put_png(dir, rel, tile, 64, 64);
        }
        for (int n = 0; n < sprite_count(); n++) {
            const SpriteInfo *in = sprite_get_info(n);
            if (!in || !in->w || !in->h || !in->data) continue;
            const uint32_t *clut = sprite_remap_clut(in->clut, 0, 0);
            sprite_png(in, in->data, clut, a);
            snprintf(rel, sizeof rel, "style%03d/sprite/%d.png", si + 1, n);
            put_png(dir, rel, a, in->w, in->h);
            /* deltas as overlays: only the pixels the delta changes */
            for (int k = 0; k < in->ndeltas && k < SPRITE_DELTAS_MAX; k++) {
                for (int v = 0; v < in->h; v++) memcpy(page + v * 256, in->data + v * 256, in->w);
                blit_apply_delta(page, in->delta[k].data, in->delta[k].size);
                sprite_png(in, page, clut, b);
                int changed = 0;
                for (int v = 0; v < in->h; v++)
                    for (int u = 0; u < in->w; u++) {
                        bool same = page[v * 256 + u] == in->data[v * 256 + u];
                        if (same) b[v * in->w + u] = 0;
                        else changed++;
                    }
                if (!changed) continue;
                snprintf(rel, sizeof rel, "style%03d/sprite/%d_delta%d.png", si + 1, n, k);
                put_png(dir, rel, b, in->w, in->h);
            }
            const char *why;
            /* peds: every clothes remap as its own image (_r<r>) */
            if (extract_remaps && n < sm->nsprites && sm->spr[n].group == SPRITE_GROUP_PED)
                for (int r = 1; r < sprite_remaps(sm, n, &why); r++) {
                    /* as the game draws a ped: the remap within the ped palettes (0x7750d0) */
                    sprite_png(in, in->data, sprite_remap_clut(in->clut, r, sprite_ped_palette()), b);
                    snprintf(rel, sizeof rel, "style%03d/sprite/%d_r%d.png", si + 1, n, r);
                    put_png(dir, rel, b, in->w, in->h);
                }
            /* car paint: the pixels another remap recolours, white on black */
            if (n < sm->nsprites && sprite_remaps(sm, n, &why) && sm->spr[n].ncars) {
                /* the texels at least 3 of the first car record's 12 remaps recolour (remap r is palette car
                   base + record * 12 + r - 1) by more than HIRES_PAINT_STEP */
                const int pal = sprite_car_palette(sm->spr[n].car[0]);
                bool pt[256] = { false };
                for (int e = 1; e < 256; e++) {
                    int changed = 0;
                    for (int r = 1; r <= CAR_REMAPS; r++) {
                        const uint32_t *c = sprite_remap_clut(in->clut, r, pal);
                        int d = 0;
                        for (int k = 0; k < 24; k += 8) d += abs((int)(c[e * 64] >> k & 0xff) - (int)(clut[e * 64] >> k & 0xff));
                        changed += d > HIRES_PAINT_STEP;
                    }
                    pt[e] = changed >= 3;
                }
                int paint = 0;
                for (int v = 0; v < in->h; v++)
                    for (int u = 0; u < in->w; u++) {
                        bool p = pt[in->data[v * 256 + u]];
                        b[v * in->w + u] = p ? 0xffffffffu : 0xff000000u;
                        paint += p;
                    }
                if (paint) {
                    snprintf(rel, sizeof rel, "style%03d/sprite/%d_mask.png", si + 1, n);
                    put_png(dir, rel, b, in->w, in->h);
                }
            }
        }
        style_free(s);
        printf("style %d extracted\n", si + 1);
    }
    for (int i = 0; i < nfonts; i++) {
        const FontMan *f = &fonts[i];
        Font *fo = font_load(f->file, (uint16_t)f->first, true, err, sizeof err);
        if (!fo) continue;
        for (int g = 0; g < fo->count; g++) {
            int w = fo->glyph[g].w, h = fo->height;
            if (!w || !h || !fo->glyph[g].px) continue;
            for (int k = 0; k < w * h; k++) {
                uint8_t e = fo->glyph[g].px[k];
                a[k] = e ? (fo->pal ? (fo->pal[e] | 0xff000000u) : 0xffffffffu) : 0;
            }
            int code = f->first == 33 && g >= 95 ? 128 + (g - 95) : f->first + g;
            snprintf(rel, sizeof rel, "font/%s/%d.png", f->name, code);
            put_png(dir, rel, a, w, h);
        }
        font_free(fo);
    }
    for (int i = 0; i < npics; i++) {
        const PicMan *p = &pics[i];
        Image im = { 0 };
        char name[80];
        snprintf(name, sizeof name, "%.*s", (int)strlen(p->file) - 4, p->file);
        if (!gfx_image_alloc(&im, p->w, p->h) || !gfx_load_raw_image(&im, name, false)) { gfx_image_free(&im); continue; }
        for (int k = 0; k < p->w * p->h; k++) im.px[k] |= 0xff000000u;
        snprintf(rel, sizeof rel, "pictures/%s.png", p->name);
        put_png(dir, rel, im.px, p->w, p->h);
        gfx_image_free(&im);
    }
    printf("wrote %d images of the original graphics to %s - your own copy's: keep them, don't distribute them.\n", nwritten, dir);
    return 0;
}

int main(int argc, char **argv)
{
    const char *data = NULL, *dir = NULL, *at_map = NULL;
    int at_x = 0, at_y = 0, at_z = -1;
    bool check = false, extract_too = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc) data = argv[++i];
        else if (!strcmp(argv[i], "--extract")) extract_too = true;
        else if (!strcmp(argv[i], "--remaps")) extract_remaps = true;
        else if (!strcmp(argv[i], "--validate")) check = true;
        else if (!strcmp(argv[i], "--at") && i + 3 < argc) {
            at_map = argv[++i], at_x = atoi(argv[++i]), at_y = atoi(argv[++i]), dir = "-";
            if (i + 1 < argc && isdigit((unsigned char)argv[i + 1][0])) at_z = atoi(argv[++i]);
        }
        else if (argv[i][0] == '-') dir = NULL, i = argc;
        else dir = argv[i];
    }
    if (!dir) {
        fprintf(stderr, "usage: skin_template [--data <game folder>] <skin folder>             write the template\n"
                        "       skin_template [--data <game folder>] --extract <folder>  the template plus every original\n"
                        "                     graphic as reference PNGs in the skin layout (keep them, don't distribute them);\n"
                        "                     --remaps adds every ped clothes remap (_r<r>, tens of thousands of files)\n"
                        "       skin_template [--data <game folder>] --validate <skin folder>  check a skin\n"
                        "       skin_template [--data <game folder>] --at <nyc|sanb|miami> <x> <y> [<z>]  a map column's tiles\n"
                        "The game data: --data, else $OPENGTA_DATA, else ./game (the installed game or the unzipped installer).\n");
        return 2;
    }
    if (!(data ? vfs_mount_path(data) : vfs_mount_default()) || !vfs_exists("GTADATA/MISSION.INI")) {
        fprintf(stderr, "skin_template: no game data at %s (--data <folder>)\n", data ? data : getenv("OPENGTA_DATA") ? getenv("OPENGTA_DATA") : "./game");
        return 2;
    }
    if (!build_manifest()) { fprintf(stderr, "skin_template: no style could be read\n"); return 2; }
    if (at_map) return at(at_map, at_x, at_y, at_z);
    char d[1024];
    snprintf(d, sizeof d, "%s", dir);
    for (size_t n = strlen(d); n > 1 && d[n - 1] == '/';) d[--n] = 0;
    if (check) return validate(d);
    int r = generate(d);
    return r || !extract_too ? r : extract(d);
}
