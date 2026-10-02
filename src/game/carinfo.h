/* The style's car info records (CarInfo_Setup 0x40c100). Each record is 0xae + 8 * doors bytes (the
   door count a s16 at +0xac); field offsets in docs/cars.md. CarInfo_Setup rebases the sprite number
   (+6) by the sprite group of the vtype (+0x6a), converts the 16.16 physics values (+0x7c..+0x94,
   +0x9e, +0xa2) to floats in place, and gives model 4 a top speed of 0x32. */
#pragma once
#include "../style.h"
#include <stdint.h>
#include <string.h>

enum { CARINFO_MAX = 256 };

/* CarInfo_Setup 0x40c100 on the style's car section. The original converts the section of the loaded
   style in place, inside Style_Load; the port converts its own copy (so it can be run again for the
   same style) and runs from Cars_Init (game.c doesn't call it at style load yet). */
void car_info_setup(const Style *s);
int car_info_count(void);                       /* 0x501570 */
const uint8_t *car_info_record(int i);          /* 0x501574[i] (NULL past the count) */

static inline int16_t carinfo_s16(const uint8_t *in, int off) { return (int16_t)(in[off] | in[off + 1] << 8); }
/* a converted float field (the records are only 2-aligned) */
static inline float carinfo_float(const uint8_t *in, int off)
{
    float f;
    memcpy(&f, in + off, 4);
    return f;
}
