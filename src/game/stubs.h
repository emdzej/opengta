/* What a single-player gasm build cannot port: the DirectPlay network layer of the game core
   (0x412950-0x412d1f, 0x44b900-0x44c2ff over Net_* 0x486880-0x4876ff). gasm has no DirectPlay, and the
   frontend's network screens (src/front/front_net.c) find no service provider, so no network game
   ever starts: "a network game" (0x502f5c) stays 0 and the session state (0x501d7c) never reaches 2
   ("in a game"). Each function here does exactly what the original does in that state, which is
   nothing but the frame counter of Net_SyncFrameInputs.

   Also here: the call counters of the level start the tests read (stub_calls, from the time the
   creators were stubs; car.c and trigger.c still count). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* call counters, indexed by STUB_* */
enum {
    STUB_CAR_SPAWN, STUB_PED_CREATE, STUB_TRIGGER, STUB_DOOR, STUB_CRANE, STUB_CARLIST,
    STUB_TRAFFIC_PRIME, STUB_CLEAR_BLOCK, STUB_COUNT
};
extern int stub_calls[STUB_COUNT];
void stubs_reset(void);                      /* the per-level counters */

/* ---- Game_Run 0x4148a0 ---- */
/* Net_ResetSync 0x44bd30: in a network game with other players the pending input slots 0x6b4020 are
   cleared and Net_Poll runs; returns 1 (start) otherwise, which is always here */
bool net_reset_sync(void);
/* Net_unk_0044b900 0x44b900: in a network game Net_Leave 0x412a00 (Net_Close when in a game) */
void net_unk_44b900(void);
/* Net_SyncFrameInputs 0x44b930: in a network game with other players the lock-step exchange of the
   frame's control words (sequence byte, checksum, resend after 250 ms, "connlost" after 12 s); then,
   always, the sequence byte 0x6b400c counts the frame */
void net_sync_frame_inputs(uint32_t *controls);
extern uint8_t g_net_frame_seq;              /* 0x6b400c */
void net_end_game(void);                     /* Net_EndGame 0x412d00 (state 2 -> 1): src/front/front_net.c */

/* ---- Game_HandleKey 0x430dc0 ---- */
/* Net_BuildChatPrefix 0x44c1b0 (F1..F4): with other players (Net_Query2 0x412ad0, 0 outside a game)
   the chat line opens with "name:" or "-> name" (HUD_ChatBegin); nothing otherwise */
void net_build_chat_prefix(int to);
