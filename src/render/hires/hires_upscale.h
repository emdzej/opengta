/* Pixel-art upscalers for the hires renderer's original art (opt-in, `--param upscale=<name>`; not part of
   the original, see docs/hires.md "Upscaling"). The tiles and sprites converted to true colour
   (hires_tex.c) are upscaled once, when they are converted, so the upscaled copy is what the cache
   holds: the original art's layer of the overlay stack, below every skin (skins still win; their own
   images are not upscaled). The renderer's bilinear filter then draws the upscaled texels.

   Names: none (the default), scale2x, scale4x (Scale2x / AdvMAME2x, once or twice: copies texels, never
   blends, so transparency stays exact), xbr, xbr4 (xBR level 2 at 2x, once or twice: blends along the
   edges it finds). */
#pragma once
#include "hires_raster.h"
#include <stdbool.h>
#include <stdint.h>

enum { HIRES_UPSCALE_NONE, HIRES_UPSCALE_SCALE2X, HIRES_UPSCALE_XBR };

/* Selects the upscaler (and forgets the converted textures, which were made with the previous one).
   False for an unknown name; the upscaler is then off. */
bool hires_upscale_set(const char *name);
const char *hires_upscale_name(void);   /* "none", "scale2x", ... */
int hires_upscale_factor(void);         /* 1 when off */

/* The upscaled copy of w x h texels (0xAABBGGRR, rows of w) with `kind` (HIRES_UPSCALE_*) applied
   `passes` times, each doubling: malloc'ed, (w << passes) x (h << passes); NULL if out of memory. */
uint32_t *hires_upscale(const uint32_t *src, int w, int h, int kind, int passes);

/* What hires_tex.c calls on a texture it has just converted: replaced by the upscaled copy (owned) when
   an upscaler is selected; left as it is otherwise or if memory is short. */
void hires_upscale_texture(HiresTexture *t);
