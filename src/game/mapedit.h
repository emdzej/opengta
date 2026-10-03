/* The game's side of the map edits (Map_Set* 0x437b50-0x438020, map.c): the copy-on-write changes of
   the loaded map g_game.map, with the original's fatal error when a change area overflows. Used by the
   doors and triggers (trigger.c), the mission helpers (mission_obj.c) and the block animations. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

void map_set_block_type(int x, int y, int z, uint32_t info);   /* Map_SetBlockType 0x437b50 */
void map_set_block_kind(int x, int y, int z, int kind);        /* Map_SetBlockKind 0x437cb0 */
void map_or_block_flags(int x, int y, int z, int flags);       /* Map_OrBlockFlags 0x437e70 */
void map_set_block_face(int x, int y, int z, int face, int tile);   /* Map_SetBlockFace 0x438020 */
