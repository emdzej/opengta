/* Block animations (0x402240-0x402610): up to 64 animated map faces (doors, garage doors). Each takes a
   fresh side or lid tile number of the style (Style_AllocSide 0x47d540 / Style_AllocLid 0x47d510)
   that the face shows from then on, and animates that tile through a list of frames with
   Style_SetTileFrame 0x47d440. trigger.c's doors create and drive them; Game_Update ticks them. */
#pragma once
#include <stdint.h>

enum { BLOCKANIM_MAX = 64, BLOCKANIM_FRAMES = 10 };

/* one face animation (0x4bbc70, 0x74 bytes) */
typedef struct {
    int32_t which;              /* +0x00 1 lid, 0 side */
    uint8_t tile;               /* +0x04 the allocated tile number */
    uint8_t pad05[3];
    int32_t active;             /* +0x08 */
    int32_t tick;               /* +0x0c frames since the last step */
    int32_t speed;              /* +0x10 frames per step */
    int32_t index;              /* +0x14 current frame */
    int32_t event_type;         /* +0x18 Event_Dispatch on the last frame (5 = none) */
    int32_t event_arg;          /* +0x1c */
    int32_t nframes;            /* +0x20 */
    struct { int32_t kind; uint8_t tile; uint8_t pad[3]; } frame[BLOCKANIM_FRAMES];   /* +0x24 {kind, tile} */
} BlockAnim;
_Static_assert(sizeof(BlockAnim) == 0x74, "block animation record");

extern BlockAnim g_blockanims[BLOCKANIM_MAX];   /* 0x4bbc70 */
extern uint8_t g_blockanim_count;               /* 0x4bbc68 */

void blockanim_reset(void);                     /* BlockAnim_Reset 0x402240 */
/* BlockAnim_Create 0x402250: face 0..4 of block (x, y, z) gets a new tile showing what it showed. */
int blockanim_create(int x, int y, int z, int face);
/* BlockAnim_StartForward 0x402390 / _StartReverse 0x402440: n frames (tiles first .. first + n - 1 of
   `kind`, or backwards), one step every `speed` frames. */
void blockanim_start_forward(int a, int speed, int n, int kind, int first);
void blockanim_start_reverse(int a, int speed, int n, int kind, int first);
void blockanim_set_tile(int a, int kind, int tile);    /* BlockAnim_SetTile 0x402500: stop, show tile */
void blockanim_set_event(int a, int type, int arg);    /* BlockAnim_SetEvent 0x402550 */
void blockanim_tick(void);                             /* BlockAnim_Tick 0x402580 */
int blockanim_get_tile_slot(int a);                    /* BlockAnim_GetTileSlot 0x402610 */
