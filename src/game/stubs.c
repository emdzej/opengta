/* The network layer of the game core in a single-player build (stubs.h). */
#include "stubs.h"
#include <string.h>

int stub_calls[STUB_COUNT];

void stubs_reset(void)
{
    memset(stub_calls, 0, sizeof stub_calls);
}

/* 0x502f5c, "a network game": never set without a DirectPlay session */
static const bool net_game = false;
uint8_t g_net_frame_seq;                     /* 0x6b400c */

bool net_reset_sync(void)
{
    (void)net_game;   /* (with net_game: clear 0x6b4020..0x6b402c, Net_Poll) */
    return true;
}

void net_unk_44b900(void) {}                 /* (with net_game: Net_Leave) */

void net_sync_frame_inputs(uint32_t *controls)
{
    (void)controls;   /* (with net_game and other players: the exchange into controls[]) */
    /* the dword 0x6b400c: below 0xff its low byte counts up; at 0xff it becomes its second byte << 8,
       i.e. the counter wraps to 0 (bytes 2 and 3 cleared: nothing else uses them) */
    g_net_frame_seq = (uint8_t)(g_net_frame_seq + 1);
}

void net_build_chat_prefix(int to) { (void)to; }   /* (Net_Query2 is 0: no one to chat with) */
