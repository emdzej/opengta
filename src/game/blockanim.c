/* Block animations (0x402240-0x402610; see blockanim.h). */
#include "blockanim.h"
#include "event.h"
#include "game.h"
#include "mapedit.h"
#include "../map.h"
#include "../style.h"
#include <stdbool.h>
#include <string.h>

BlockAnim g_blockanims[BLOCKANIM_MAX];
uint8_t g_blockanim_count;

/* Style_AllocLid 0x47d510 / Style_AllocSide 0x47d540: the next tile number past the style's lids /
   sides (the counts 0x7752fc / 0x775534 grow; more than 256 is fatal -0x47 / -0x48). */
static int style_alloc_lid(Style *s)
{
    if (s->nlid > 0xff) game_fatal(-0x47, 0x4a, 0);
    return (uint8_t)s->nlid++;
}
static int style_alloc_side(Style *s)
{
    if (s->nside > 0xff) game_fatal(-0x48, 0x4b, 0);
    return (uint8_t)s->nside++;
}

/* BlockAnim_Reset 0x402240 */
void blockanim_reset(void) { g_blockanim_count = 0; }

/* BlockAnim_Create 0x402250: more than 64 is fatal (-0x46). The face keeps its picture: the new tile is
   set to show the face's old tile (of its own kind). Inactive, no event (5). */
int blockanim_create(int x, int y, int z, int face)
{
    if (g_blockanim_count > 0x3f) game_fatal(-0x46, 0x49, 0);
    int n = g_blockanim_count;
    BlockAnim *a = &g_blockanims[n];
    Style *s = g_game.style;
    if (face == 4) {
        a->which = 1;
        a->tile = (uint8_t)style_alloc_lid(s);
    } else {
        a->which = 0;
        a->tile = (uint8_t)style_alloc_side(s);
    }
    int old = map_get_face(g_game.map, x, y, z, face);
    map_set_block_face(x, y, z, face, a->tile);
    style_set_tile_frame(s, a->tile, a->which, (unsigned)old, a->which);
    a->active = 0;
    a->event_type = 5;
    g_blockanim_count++;
    return n;
}

/* frame k of animation a: past the 10 frames of a record the list runs on into the next record, as in
   the original (the port stops at the end of the table) */
static uint8_t *frame_rec(BlockAnim *a, int k)
{
    uint8_t *p = (uint8_t *)a + 0x24 + 8 * k;
    if (k < 0 || p + 8 > (uint8_t *)(g_blockanims + BLOCKANIM_MAX)) game_fatal(-0x4a, 0x402390, k);
    return p;
}

static void start(int i, int speed, int n, int kind, int first, bool reverse)
{
    BlockAnim *a = &g_blockanims[i & 0xff];
    for (int k = 0; k < n; k++) {
        int32_t *rec = (int32_t *)frame_rec(a, k);
        rec[0] = kind;
        ((uint8_t *)rec)[4] = (uint8_t)(reverse ? n - k + first - 1 : k + first);
    }
    a->speed = speed;
    a->nframes = n;
    a->index = 0;
    a->active = 1;
    a->tick = 0;
    style_set_tile_frame(g_game.style, a->tile, a->which, a->frame[0].tile, a->frame[0].kind);
}

/* BlockAnim_StartForward 0x402390 */
void blockanim_start_forward(int a, int speed, int n, int kind, int first) { start(a, speed, n, kind, first, false); }
/* BlockAnim_StartReverse 0x402440 */
void blockanim_start_reverse(int a, int speed, int n, int kind, int first) { start(a, speed, n, kind, first, true); }

/* BlockAnim_SetTile 0x402500 */
void blockanim_set_tile(int i, int kind, int tile)
{
    BlockAnim *a = &g_blockanims[i & 0xff];
    style_set_tile_frame(g_game.style, a->tile, a->which, (unsigned)tile, kind);
    a->active = 0;
}

/* BlockAnim_SetEvent 0x402550 */
void blockanim_set_event(int i, int type, int arg)
{
    g_blockanims[i & 0xff].event_type = type;
    g_blockanims[i & 0xff].event_arg = arg;
}

/* BlockAnim_Tick 0x402580: an active animation steps every `speed` frames; reaching its last frame
   it stops and dispatches its event (before that frame is shown). */
void blockanim_tick(void)
{
    for (int i = 0; i < g_blockanim_count; i++) {
        BlockAnim *a = &g_blockanims[i];
        if (a->active != 1) continue;
        if (++a->tick != a->speed) continue;
        a->tick = 0;
        if (++a->index == a->nframes - 1) {
            a->active = 0;
            event_dispatch(a->event_type, a->event_arg);
        }
        const int32_t *rec = (const int32_t *)frame_rec(a, a->index);
        style_set_tile_frame(g_game.style, a->tile, a->which, ((const uint8_t *)rec)[4], rec[0]);
    }
}

/* BlockAnim_GetTileSlot 0x402610 */
int blockanim_get_tile_slot(int a) { return g_blockanims[a & 0xff].tile; }
