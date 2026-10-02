#include "style.h"
#include "render/poly.h"
#include "render/sprite.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static Style *fail(Style *s, uint8_t *file, char *err, size_t cap, const char *rel, const char *msg)
{
    if (err) snprintf(err, cap, "%s: %s", rel, msg);
    free(file);
    style_free(s);
    return NULL;
}

/* Mem_AllocAligned64K 0x43cc40: malloc(size + 64K), the first 64 KB boundary inside. Zeroed here (the
   original's malloc'd memory is undefined where nothing is read into it). */
static uint8_t *alloc64k(size_t size, uint8_t **alloc)
{
    *alloc = calloc(1, size + 0x10000);
    if (!*alloc) return NULL;
    return (uint8_t *)(((uintptr_t)*alloc + 0xffff) & ~(uintptr_t)0xffff);
}

/* ---- tile tables (the block renderer's, 0x4376c0-0x4379c0) ---- */

/* Tile_BuildSideTable 0x437750: four CLUTs per side tile, palette index [tile * 4 + direction]. */
static void tile_build_side_table(Style *s, int n)
{
    for (int t = 0; t < n; t++)
        for (int r = 0; r < 4; r++) s->side_clut[t][r] = style_clut_of(s, s->pal_index[(int16_t)(r + t * 4)]);
}

/* Tile_BuildLidTable 0x4377b0: four CLUTs (remaps) per lid, palette index [(lid + first) * 4 + remap]. */
static void tile_build_lid_table(Style *s, int n, int first)
{
    s->lid_clut_base = first;
    for (int t = 0; t < n; t++)
        for (int r = 0; r < 4; r++) s->lid_clut[t][r] = style_clut_of(s, s->pal_index[(int16_t)((t + first) * 4 + r)]);
}

/* Tile_BuildAuxSideTable 0x437830: the same for the aux tiles (animation frames). */
static void tile_build_aux_side_table(Style *s, int n, int first)
{
    for (int t = 0; t < n; t++)
        for (int r = 0; r < 4; r++)
            s->aux_side_clut[t][r] = style_clut_of(s, s->pal_index[(int16_t)((t + first) * 4 + r)]);
}

/* Tile_BuildAuxTable 0x4376f0: 8 CLUTs from palette index [first + i]. */
static void tile_build_aux_table(Style *s, int first)
{
    for (int i = 0; i < 8; i++) s->aux_clut[i] = style_clut_of(s, s->pal_index[(int16_t)(i + first)]);
}

/* Tile_SetSkipSide 0x4376c0: the side tile flat blocks never draw, per city (other styles keep the
   previous value). */
static void tile_set_skip_side(Style *s, int style)
{
    if (style == 1) s->skip_side = 0xc2;
    else if (style == 2) s->skip_side = 0xc1;
    else if (style == 3) s->skip_side = 0xc5;
}

/* Tile_SetSideEntry 0x4378a0: side tile b takes its CLUTs from its own palettes (kind 0), lid frame
   (kind 1) or aux frame (kind 2). */
static void tile_set_side_entry(Style *s, uint8_t b, int kind, unsigned frame)
{
    frame &= 0xff;
    if (kind == 0) {
        for (int r = 0; r < 4; r++) s->side_clut[b][r] = style_clut_of(s, s->pal_index[(int16_t)(b * 4 + r)]);
    } else if (kind == 1) {
        memcpy(s->side_clut[b], s->lid_clut[frame], sizeof s->side_clut[b]);
    } else if (kind == 2) {
        memcpy(s->side_clut[b], s->aux_side_clut[frame], sizeof s->side_clut[b]);
    }   /* else: Error_Fatal in the original */
}

/* Tile_SetLidEntry 0x4379c0: the same for lid b (kind 1 = its own palettes). */
static void tile_set_lid_entry(Style *s, uint8_t b, int kind, unsigned frame)
{
    frame &= 0xff;
    if (kind == 0) {
        memcpy(s->lid_clut[b], s->side_clut[frame], sizeof s->lid_clut[b]);
    } else if (kind == 1) {
        for (int r = 0; r < 4; r++)
            s->lid_clut[b][r] = style_clut_of(s, s->pal_index[(int16_t)((b + s->lid_clut_base) * 4 + r)]);
    } else if (kind == 2) {
        memcpy(s->lid_clut[b], s->aux_side_clut[frame], sizeof s->lid_clut[b]);
    }
}

/* ---- animation ---- */

/* Style_InitAnims 0x47d570: u8 count, then {block, which (0 side, 1 lid), speed, n, frame[n]}. */
static bool style_init_anims(Style *s, const uint8_t *p, size_t size)
{
    const uint8_t *end = p + size;
    if (size < 1) { s->nanims = 0; return true; }
    s->nanims = *p++;
    if (s->nanims > STYLE_ANIMS_MAX) return false;   /* Error_Fatal: more than 0x400 bytes of records */
    for (int i = 0; i < s->nanims; i++) {
        if (p + 4 > end || p + 4 + p[3] > end) return false;   /* (unchecked in the original) */
        s->anims[i].def = p;
        s->anims[i].tick = 0;
        s->anims[i].frame = 0;
        p += p[3] + 4;
    }
    return true;
}

/* Style_SetTileFrame 0x47d440: tile `block` (which 0 side, 1 lid) shows tile `frame` of kind 0 side,
   1 lid, 2 aux. */
static void style_set_tile_frame(Style *s, unsigned block, int which, unsigned frame, int kind)
{
    int16_t base = kind == 0 ? s->side_base : kind == 1 ? s->lid_base : kind == 2 ? s->aux_base : 0;
    if (which == 0) {
        if (kind >= 0 && kind <= 2) s->side_remap[block & 0xff] = (int16_t)((frame & 0xff) + base);
        tile_set_side_entry(s, (uint8_t)block, kind, frame);
    } else if (which == 1) {
        if (kind >= 0 && kind <= 2) s->lid_remap[block & 0xff] = (int16_t)((frame & 0xff) + base);
        tile_set_lid_entry(s, (uint8_t)block, kind, frame);
    }   /* else Error_Fatal */
}

/* Style_UpdateAnims 0x47d610: every `speed` frames step to the next frame (an aux tile); after the
   last frame the tile shows itself again (kind = which), so a cycle is n + 1 steps. */
void style_update_anims(Style *s)
{
    for (int i = 0; i < s->nanims; i++) {
        StyleAnim *a = &s->anims[i];
        const uint8_t *d = a->def;
        if (++a->tick != d[2]) continue;
        a->tick = 0;
        uint8_t f = ++a->frame;
        if (f > d[3]) {
            a->frame = 0;
            style_set_tile_frame(s, d[0], d[1], d[0], d[1]);
        } else {
            style_set_tile_frame(s, d[0], d[1], d[f + 3], 2);
        }
    }
}

/* ---- palettes ---- */

/* Style_ConvertPalettes 0x47cd10: every CLUT word (B, G, R, x) becomes
   (R >> r_adj) << r_pos | (B >> b_adj) << b_pos | (G >> g_adj) << g_pos. Converting again re-reads the
   CLUT from the file first (here: from the copy kept at load). */
void style_convert_palettes(Style *s, const PixelFormat *fmt)
{
    if (!memcmp(&s->clut_fmt, fmt, sizeof *fmt)) return;
    if (s->clut_converted) memcpy(s->clut, s->clut_file, s->clut_bytes);
    uint8_t *p = (uint8_t *)s->clut;
    for (uint32_t i = 0; i < s->clut_bytes / 4; i++, p += 4) {
        uint32_t px = (uint32_t)(p[2] >> fmt->r_adj) << fmt->r_pos | (uint32_t)(p[0] >> fmt->b_adj) << fmt->b_pos |
                      (uint32_t)(p[1] >> fmt->g_adj) << fmt->g_pos;
        p[0] = (uint8_t)px;
        p[1] = (uint8_t)(px >> 8);
        p[2] = (uint8_t)(px >> 16);
        p[3] = (uint8_t)(px >> 24);
    }
    s->clut_fmt = *fmt;
    s->clut_converted = true;
}

/* ---- loader ---- */

Style *style_load(int n, char *err, size_t errcap)
{
    char rel[32];
    if (n == 0) n = 1;
    snprintf(rel, sizeof rel, "GTADATA/STYLE%03d.G24", n);
    size_t size;
    uint8_t *d = vfs_read_all(rel, &size);
    if (!d) return fail(NULL, NULL, err, errcap, rel, "can't read");
    Style *s = calloc(1, sizeof *s);
    if (!s) return fail(NULL, d, err, errcap, rel, "out of memory");
    s->number = n;
    if (size < 64) return fail(s, d, err, errcap, rel, "too short");
    uint32_t *h = &s->h.version;
    for (int i = 0; i < 16; i++) h[i] = rd32(d + 4 * i);
    const StyleHeader *H = &s->h;
    if (H->version != STYLE_VERSION) return fail(s, d, err, errcap, rel, "wrong version");
    uint32_t tiles = H->side_size + H->lid_size + H->aux_size;
    if (tiles > STYLE_TILES_MAX) return fail(s, d, err, errcap, rel, "too many tiles");
    if (H->side_size % 4096 || H->lid_size % 4096 || H->aux_size % 4096)
        return fail(s, d, err, errcap, rel, "tile sizes not whole tiles");
    uint32_t tiles_read = (tiles + 0x3fff) & ~0x3fffu;   /* the file pads the tiles to 16 KB */
    s->clut_bytes = (H->clut_size + 0xffff) & ~0xffffu;
    size_t need = 64 + (size_t)tiles_read + H->anim_size + s->clut_bytes + H->palette_index_size +
                  H->object_info_size + H->car_size + H->sprite_info_size + H->sprite_graphics_size +
                  H->sprite_numbers_size;
    if (size < need) return fail(s, d, err, errcap, rel, "truncated");

    /* the style buffer: blend table, tiles, rotation cache, anims (plus slack up to the end of the
       cache page, which the rasteriser can address) */
    size_t bufsize = STYLE_ROT_CACHE_OFS + 0x10000 + H->anim_size;
    if (!(s->buf = alloc64k(bufsize, &s->buf_alloc))) return fail(s, d, err, errcap, rel, "out of memory");
    s->anim_data = s->buf + STYLE_ROT_CACHE_OFS + STYLE_ROT_CACHE;
    if (!(s->pal_index = calloc(1, H->palette_index_size + 2))) return fail(s, d, err, errcap, rel, "out of memory");
    s->npal_index = H->palette_index_size / 2;
    uint8_t *cl = alloc64k((size_t)s->clut_bytes + H->object_info_size + H->car_size, &s->clut_alloc);
    if (!cl || !(s->clut_file = malloc(s->clut_bytes ? s->clut_bytes : 1))) return fail(s, d, err, errcap, rel, "out of memory");
    s->clut = (uint32_t *)cl;
    s->object_info = cl + s->clut_bytes;
    s->car_info = s->object_info + H->object_info_size;
    uint8_t *sp = alloc64k((size_t)H->sprite_numbers_size + H->sprite_graphics_size + 0x10000 + H->sprite_info_size,
                           &s->sprite_alloc);
    if (!sp) return fail(s, d, err, errcap, rel, "out of memory");
    s->sprite_graphics = sp + 0x10000;
    s->sprite_info = s->sprite_graphics + H->sprite_graphics_size;
    s->sprite_numbers = s->sprite_info + H->sprite_info_size;

    size_t o = 64;
    memcpy(s->buf + STYLE_TILE_OFS, d + o, tiles_read), o += tiles_read;
    memcpy(s->anim_data, d + o, H->anim_size), o += H->anim_size;
    memcpy(cl, d + o, s->clut_bytes), o += s->clut_bytes;
    memcpy(s->clut_file, cl, s->clut_bytes);
    memcpy(s->pal_index, d + o, H->palette_index_size), o += H->palette_index_size;
    memcpy(s->object_info, d + o, H->object_info_size), o += H->object_info_size;
    memcpy(s->car_info, d + o, H->car_size), o += H->car_size;
    /* Sprite_LoadInfo 0x47ca50 reads these three (and relocates the info records; not ported yet) */
    memcpy(s->sprite_info, d + o, H->sprite_info_size), o += H->sprite_info_size;
    memcpy(s->sprite_graphics, d + o, H->sprite_graphics_size), o += H->sprite_graphics_size;
    memcpy(s->sprite_numbers, d + o, H->sprite_numbers_size), o += H->sprite_numbers_size;
    free(d);
    s->clut_fmt = (PixelFormat){ -1, -1, -1, -1, -1, -1 };   /* as file: never equal to a display format */

    s->nside = (int)(H->side_size / 4096);
    s->nlid = (int)(H->lid_size / 4096);
    s->naux = (int)(H->aux_size / 4096);
    if (s->nside > 256 || s->nlid > 256 || s->naux > 256) return fail(s, NULL, err, errcap, rel, "too many tiles");
    /* every palette the tile tables use must exist (the original doesn't check) */
    uint32_t npals = s->clut_bytes / 1024;
    for (uint32_t i = 0; i < s->npal_index; i++)
        if (s->pal_index[i] < 0 || (uint32_t)s->pal_index[i] >= npals) return fail(s, NULL, err, errcap, rel, "bad palette index");
    if (s->npal_index < 4u * (unsigned)(s->nside + s->nlid + s->naux)) return fail(s, NULL, err, errcap, rel, "palette index too short");

    s->lid_base = (int16_t)s->nside;
    s->sprite_pal_base = (s->nside + s->nlid + s->naux) * 4;
    s->aux_base = (int16_t)(s->nlid + s->nside);
    s->side_base = 0;
    /* tile table entries the map can name but the style lacks stay at palette 0 (NULL in the original) */
    for (int t = 0; t < 256; t++)
        for (int r = 0; r < 4; r++) s->side_clut[t][r] = s->lid_clut[t][r] = s->aux_side_clut[t][r] = s->clut;
    tile_build_side_table(s, s->nside);
    tile_build_lid_table(s, s->nlid, s->nside);
    tile_build_aux_side_table(s, s->naux, s->nlid + s->nside);
    for (int i = 0; i < s->nside; i++) s->side_remap[i] = (int16_t)(i + s->side_base);
    for (int i = 0; i < s->nlid; i++) s->lid_remap[i] = (int16_t)(i + s->lid_base);
    if (!style_init_anims(s, s->anim_data, H->anim_size)) return fail(s, NULL, err, errcap, rel, "bad anims");
    s->car_pal_base = (int)((H->spriteclut_size + H->tileclut_size) / 1024);
    s->font_pal_base = (int)((H->newcarclut_size + H->spriteclut_size + H->tileclut_size) / 1024);
    if (s->font_pal_base + 8 > (int)s->npal_index) return fail(s, NULL, err, errcap, rel, "palette index too short");
    tile_build_aux_table(s, s->font_pal_base);
    poly_build_blend_table(s->buf, 0.5f);
    poly_init(s->buf + STYLE_TILE_OFS, s->buf + STYLE_ROT_CACHE_OFS, STYLE_ROT_CACHE >> 12, 0);
    tile_set_skip_side(s, n);
    if (!sprite_load_info(s)) return fail(s, NULL, err, errcap, rel, "bad sprite info");   /* Sprite_LoadInfo 0x47ca50 */
    return s;
}

void style_free(Style *s)
{
    if (!s) return;
    free(s->buf_alloc);
    free(s->clut_alloc);
    free(s->clut_file);
    free(s->pal_index);
    free(s->sprite_alloc);
    free(s);
}
