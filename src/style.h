/* Styles (.G24, version 336): Style_Load 0x47cf10, the tile tables of the block renderer
   (Tile_* 0x4376c0-0x4379c0), palette conversion (Style_ConvertPalettes 0x47cd10) and tile animation
   (Style_InitAnims 0x47d570, Style_UpdateAnims 0x47d610, Style_SetTileFrame 0x47d440).
   Layout: docs/formats.md.

   Memory is laid out like the original's, because the rasteriser addresses textures the way the
   original does (a texel is page[v << 8 | u] with 8-bit u and v that wrap inside a 64 KB page):
   - buf (0x7752e8, 64 KB aligned): the 64 KB blend table, then 0x190000 bytes of tile pages (tile t is
     in page t >> 4 at u = (t & 3) * 64, v = ((t >> 2) & 3) * 64), then the rotated-tile cache
     (STYLE_ROT_CACHE bytes, 0x4b335c), then the anim section;
   - clut (0x7750cc): the CLUT pages, 64 palettes per 64 KB page, colour e of palette p at word
     (p & ~63) * 256 + (p & 63) + e * 64; after style_convert_palettes the words are display pixels.
     Object info and car info follow it, as in the original's allocation. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    STYLE_VERSION = 336,
    STYLE_TILES_MAX = 0x190000,          /* side + lid + aux bytes, checked by Style_Load */
    STYLE_TILE_OFS = 0x10000,            /* tiles follow the blend table */
    STYLE_ROT_CACHE_OFS = 0x1a0000,      /* 0x7752ec */
    STYLE_ROT_CACHE = 0x8000,            /* 0x4b335c: 8 rotated-tile slots */
    STYLE_ANIMS_MAX = 0x80,              /* 0x5f2750 holds 0x400 bytes of 8-byte anim records */
};

typedef struct {
    uint32_t version, side_size, lid_size, aux_size, anim_size, clut_size, tileclut_size, spriteclut_size,
        newcarclut_size, fontclut_size, palette_index_size, object_info_size, car_size, sprite_info_size,
        sprite_graphics_size, sprite_numbers_size;
} StyleHeader;

/* Display pixel format for the CLUT conversion (the MGL DC pixel_format_t fields +0x20c..+0x220 that
   Gfx_SelectMode copies to 0x7750c8.. ): pixel = (r >> r_adj) << r_pos | (g >> g_adj) << g_pos | ... */
typedef struct { int r_pos, r_adj, g_pos, g_adj, b_pos, b_adj; } PixelFormat;
/* The 32 bpp DirectDraw format the game gets at 640x480x32: 0x00RRGGBB. */
static const PixelFormat PIXFMT_32 = { 16, 0, 8, 0, 0, 0 };

typedef struct { uint8_t tick, frame; const uint8_t *def; } StyleAnim;   /* 0x5f2750, 8 bytes each */

typedef struct Style {
    int number;                          /* 0x7752d8 */
    StyleHeader h;
    uint8_t *buf;                        /* 0x7752e8 (64 KB aligned inside buf_alloc) */
    uint8_t *buf_alloc;
    uint8_t *anim_data;                  /* buf + STYLE_ROT_CACHE_OFS + STYLE_ROT_CACHE */
    uint32_t *clut;                      /* 0x7750cc / 0x7752f0 */
    uint8_t *clut_alloc;
    uint32_t clut_bytes;                 /* 0x775314: CLUT size rounded up to 64 KB */
    uint8_t *clut_file;                  /* the CLUT as read, for re-conversion (the original re-reads the file) */
    PixelFormat clut_fmt;                /* 0x775308.. : format the CLUT is in */
    bool clut_converted;                 /* 0x775545 */
    uint8_t *object_info, *car_info;     /* follow the CLUT (Obj_LoadInfos 0x44c6b0 / CarInfo_Setup inputs) */
    int16_t *pal_index;                  /* 0x77552c */
    uint32_t npal_index;
    uint8_t *sprite_info, *sprite_graphics, *sprite_numbers;  /* 0x7750b0 block: Sprite_LoadInfo inputs */
    uint8_t *sprite_alloc;
    int nside, nlid, naux;               /* 0x775534, 0x7752fc, 0x7750d4 */
    int16_t side_base, lid_base, aux_base;   /* 0x775310 (0), 0x7752f4 (nside), 0x7750bc (nside + nlid) */
    int sprite_pal_base;                 /* 0x77531c: 4 * all tiles, the first sprite palette */
    int car_pal_base;                    /* 0x77530c: (tile + sprite CLUT) / 1024 */
    int font_pal_base;                   /* 0x7752f8: (tile + sprite + new car CLUT) / 1024 */
    /* Tile tables of the block renderer: CLUT pointers per tile and remap (4 per tile) */
    const uint32_t *side_clut[256][4];   /* 0x5bfbf8, [tile][0 top, 1 bottom, 2 left, 3 right] */
    const uint32_t *aux_side_clut[256][4];  /* 0x5c0c10 (aux tiles, for animation frames) */
    const uint32_t *lid_clut[256][4];    /* 0x5c1c48, [lid][remap] */
    int lid_clut_base;                   /* 0x5c1c18 */
    const uint32_t *aux_clut[8];         /* 0x5c2c4c */
    int16_t side_remap[256];             /* 0x775320: texture tile of side tile n (animation changes it) */
    int16_t lid_remap[256];              /* 0x7750d8: texture tile of lid n */
    int skip_side;                       /* 0x5c1c14: side tile flat blocks don't draw (Tile_SetSkipSide 0x4376c0) */
    int nanims;                          /* 0x77554c */
    StyleAnim anims[STYLE_ANIMS_MAX];
} Style;

/* Style_Load 0x47cf10: STYLE%03d.G24 for number n (0 = 1). Also Poly_BuildBlendTable + Poly_Init on
   its buffers, as the original does. NULL on error (message in err). */
Style *style_load(int n, char *err, size_t errcap);
void style_free(Style *s);
/* Style_ConvertPalettes 0x47cd10: rewrite the CLUT for the display format (no-op if it already is). */
void style_convert_palettes(Style *s, const PixelFormat *fmt);
/* Style_SetTileFrame 0x47d440: tile `block` of `which` (0 side, 1 lid) shows tile `frame` of kind 0 side,
   1 lid, 2 aux (through Tile_SetSideEntry 0x4378a0 / Tile_SetLidEntry 0x4379c0). */
void style_set_tile_frame(Style *s, unsigned block, int which, unsigned frame, int kind);
/* Style_UpdateAnims 0x47d610: one game frame of tile animation. */
void style_update_anims(Style *s);

/* The CLUT pointer of palette p (Tile_* tables, Tile_SelectSprite 0x4385b0). */
static inline const uint32_t *style_clut_of(const Style *s, int p)
{
    return s->clut + (p & ~63) * 0x100 + (p & 63);
}
/* Byte offset of tile t's page in buf. */
static inline uint32_t style_tile_page(int t) { return STYLE_TILE_OFS + ((uint32_t)t >> 4) * 0x10000; }
