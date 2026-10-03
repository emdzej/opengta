/* The game's map queries on the loaded level's map (g_game.map): Map_GetLidBelow 0x4387b0 (map
   module, next to Map_IsCovered 0x438800, src/map.c) and Map_TestBlockAttr 0x44b310 (the block
   attribute query of the map_query group 0x44b310-0x44b590). */
#pragma once
#include <stdint.h>

/* Map_GetLidBelow 0x4387b0: the lid tile of block (x, y, (z >> 22) + 1) (16.16), 0 above the column */
int map_get_lid_below(int32_t x, int32_t y, int32_t z);
/* Map_TestBlockAttr 0x44b310: a block's attributes (the last block's type map is cached, 0x6b3ea8):
   1 railway, 2 crossing (lights bits = 1), 3 road / 6 / 7 types, 4 type map without the type bits,
   5 the type map's high bits, 6 the lights bits, 7 lights 4 / 5 (the value), 8 lights 2, 9 pavement.
   Outside the map 0. */
int map_test_block_attr(int what, int bx, int by, int bz);
