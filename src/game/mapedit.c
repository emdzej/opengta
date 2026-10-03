/* The game's side of the map edits (see mapedit.h; the edits themselves are map.c's). */
#include "mapedit.h"
#include "game.h"
#include "../map.h"

/* "Map change overflow" (Error_Fatal -0x15; the line says which area: 0x14 columns, 0x18 blocks) */
static void check(bool ok)
{
    if (!ok) game_fatal(-0x15, g_game.map->chg_column_used > 0x2000 ? 0x14 : 0x18, 0);
}

void map_set_block_type(int x, int y, int z, uint32_t info) { check(map_edit_block_type(g_game.map, x, y, z, info)); }   /* 0x437b50 */
void map_set_block_kind(int x, int y, int z, int kind) { check(map_edit_block_kind(g_game.map, x, y, z, kind)); }        /* 0x437cb0 */
void map_or_block_flags(int x, int y, int z, int flags) { check(map_edit_or_flags(g_game.map, x, y, z, (uint32_t)flags)); }   /* 0x437e70 */
void map_set_block_face(int x, int y, int z, int face, int tile) { check(map_edit_block_face(g_game.map, x, y, z, face, tile)); }   /* 0x438020 */
