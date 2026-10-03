/* A small PNG decoder for skins (docs/skins.md): 8-bit greyscale, grey + alpha, RGB, RGBA and palette
   images (palette transparency from tRNS), non-interlaced, all five row filters; decompression by
   src/inflate.c. Anything else is refused with a message. Not part of the original. */
#pragma once
#include <stddef.h>
#include <stdint.h>

/* The image as w x h words 0xAABBGGRR (R, G, B, A bytes in memory), malloc'd; NULL on error (why in
   err). */
uint32_t *hires_png_decode(const uint8_t *data, size_t size, int *w, int *h, char *err, size_t errcap);
