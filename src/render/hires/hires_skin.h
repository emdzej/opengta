/* Skins: packs of replacement images layered over the original art in the hires renderer (not part of
   the original; docs/skins.md). `--param skin=a,b` stacks skins/a then skins/b over the style's tiles
   and sprites; an asset neither provides comes from the original. Layout of a skin:

     skins/<name>/skin.ini                       name=, author=, scale= (the hires scale it targets)
     skins/<name>/style<NNN>/side/<n>.png        side tile n (any square size)
     skins/<name>/style<NNN>/lid/<n>.png         lid tile n; lid/<n>_r<r>.png for remap r (0..3)
     skins/<name>/style<NNN>/aux/<n>.png         aux tile n (animation frames)
     skins/<name>/style<NNN>/sprite/<n>.png      sprite n (the game's sprite numbers); sprite/<n>_r<r>.png
                                                 for remap r, or sprite/<n>_mask.png: the paint to recolour
                                                 like the remap does; sprite/<n>_delta<k>.png: delta k
                                                 (damage, doors) drawn over it when the sprite shows it,
                                                 else the original delta's texels stretched over the image

   Files are read with hires_skin_reader (the file layer by default; the gasm backend reads its assets
   directly, so a skin folder can be given next to the installer too). Images are decoded on first use and
   kept; ones much larger than the hires scale can show are box-filtered down at load.

   The skin_template tool (tools/skin-template.c) writes the list of every replaceable asset of the
   user's data (skin.json, CHECKLIST.md) and checks a skin folder against it (--validate). */
#pragma once
#include "hires_raster.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t *(*HiresFileReader)(const char *path, size_t *size);   /* malloc'd file or NULL */
extern HiresFileReader hires_skin_reader;                               /* default: vfs_read_all */

typedef struct { char name[64], title[96], author[96]; int scale; } HiresSkinInfo;

/* Loads the comma-separated skins (later ones win) for hires scale n and puts them on the overlay
   stack (replacing any loaded before). Skins without skin.ini are skipped with a message. log may be
   NULL. Returns how many were loaded. */
int hires_skins_load(const char *list, int n, void (*log)(const char *msg));
void hires_skins_free(void);
int hires_skins_count(void);
const HiresSkinInfo *hires_skin_info(int i);
/* Images loaded and files probed but missing, over all skins (for the tests). */
void hires_skin_stats(int *loaded, int *missing);

/* ---- the lookup API for other kinds of art (stable: the frontend, HUD and font layers use it) ----

   Any image of the loaded skins by its path inside a skin folder, e.g. "font/BIG1/65.png" or
   "pictures/F_UPPER.png" (the layouts of those kinds: docs/skins.md, "Fonts and pictures"). The topmost
   skin that has the file wins (the last of `skin=a,b`); which = its index for hires_skin_info (-1: none;
   may be NULL). Decoded on first use and kept, missing files remembered, so asking every frame is cheap.
   cap > 0 box-filters the image down (halving) until neither side exceeds cap texels; images are cached
   per (path, cap). Paths with ".." or a leading '/' are refused. The texture belongs to the skin stack:
   valid until hires_skins_load / hires_skins_free. NULL when no skin has it, or none are loaded.
   Unaffected by hires_init (works at any scale, and for the frontend). */
HiresTexture *hires_skin_image(const char *rel, int cap, int *which);
