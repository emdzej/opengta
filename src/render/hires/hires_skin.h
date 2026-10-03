/* Skins: packs of replacement images layered over the original art in the hires renderer (not part of
   the original; docs/skins.md). `--param skin=a,b` stacks skins/a then skins/b over the style's tiles
   and sprites; an asset neither provides comes from the original. Layout of a skin:

     skins/<name>/skin.ini                       name=, author=, scale= (the hires scale it targets)
     skins/<name>/style<NNN>/side/<n>.png        side tile n (any square size)
     skins/<name>/style<NNN>/lid/<n>.png         lid tile n; lid/<n>_r<r>.png for remap r (0..3)
     skins/<name>/style<NNN>/aux/<n>.png         aux tile n (animation frames)
     skins/<name>/style<NNN>/sprite/<n>.png      sprite n (the game's sprite numbers); sprite/<n>_r<r>.png
                                                 for remap r, or sprite/<n>_mask.png: the paint to recolour
                                                 like the remap does

   Files are read with hires_skin_reader (the file layer by default; the gasm backend reads its assets
   directly, so a skin folder can be given next to the installer too). Images are decoded on first use and
   kept; ones much larger than the hires scale can show are box-filtered down at load. */
#pragma once
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
