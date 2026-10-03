/* Textures of the hires renderer: the style's 8-bit tiles and sprites converted through their CLUTs to
   true colour (once, cached per tile + CLUT and per sprite + CLUT + delta mask), looked up through an
   overlay stack: skin n, ..., skin 1, then the original art. See docs/hires.md and docs/skins.md.

   Keys are what the faithful renderer reads: the texture tile a map tile shows now (side_remap /
   lid_remap, which tile animation rewrites) and the CLUT pointer of its table entry (side_clut /
   lid_clut, rewritten with it), so an animation step or a remap simply selects another entry: nothing
   needs invalidating. */
#pragma once
#include "../../style.h"
#include "../sprite.h"
#include "hires_raster.h"

/* Asset kinds of the overlay stack (the folders of a skin). */
enum { HIRES_SIDE, HIRES_LID, HIRES_AUX, HIRES_SPRITE, HIRES_KINDS };

/* What an overlay layer is asked for: asset n of a kind in style `style` (1..3), variant `remap` (lids:
   the ext remap 0..3; sides: the direction 0..3, whose CLUT the faithful renderer picks; sprites: the
   sprite's remap byte, 0 = its own palette). clut is the CLUT the original art would be drawn with,
   own_clut the plain variant's (tiles: remap / direction 0; sprites: the sprite's own palette) and
   texels the original 8-bit art (tiles: 64 x 64, 256-byte rows; sprites: info->data), so a layer can
   recolour its art the way the variant recolours the original. */
typedef struct {
    int style, kind, n, remap;
    const uint32_t *clut, *own_clut;
    const uint8_t *texels;
    const SpriteInfo *info;
} HiresAsset;
/* An overlay layer: its replacement for the asset, or NULL to let the layer below answer. The texture
   stays owned by the layer. */
typedef HiresTexture *(*HiresOverlayFn)(void *ctx, const HiresAsset *a);
void hires_overlay_push(HiresOverlayFn fn, void *ctx);    /* the last pushed wins */
void hires_overlay_clear(void);
int hires_overlay_count(void);

/* The lookups the hires city renderer uses. tile = the texture tile (side_remap[] / lid_remap[]), clut
   = the CLUT pointer of the faithful renderer's table, remap = the variant for the overlays, base_clut
   the table's variant 0 (what a skin's plain image stands for). */
HiresTexture *hires_tile(const Style *s, int tile, const uint32_t *clut, int remap, const uint32_t *base_clut);
/* Sprite number n (its info record in), drawn with clut; deltas = the delta mask to apply (0 = the raw
   graphic), remap = the sprite's remap byte (for the overlays). */
HiresTexture *hires_sprite(const Style *s, int n, const SpriteInfo *in, const uint32_t *clut, uint32_t deltas,
                           int remap, const uint32_t *own_clut);
/* The original art only (what the overlays fall back to). */
HiresTexture *hires_tile_original(const Style *s, int tile, const uint32_t *clut);

/* Kind and number of texture tile t: side n, lid n or aux n. */
void hires_tile_kind(const Style *s, int t, int *kind, int *n);

/* Forget every converted texture (a new style, or its CLUT converted to another format). */
void hires_textures_reset(void);
/* Converted textures held (tiles, sprites), for the tests. */
void hires_texture_stats(int *tiles, int *sprites);
