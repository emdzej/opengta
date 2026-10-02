/* Replays (0x432c50-0x4337a0): every game records the non-zero control words of its frames as 8-byte
   records {u32 frame, u32 word} (at most 0x4000, the 0x20000-byte buffer 0x5ce750) and saves them to
   ..\gtadata\replay.rep when it ends; with the playback option (0x5031f0) a level instead plays the
   file back. Input_ReadControls 0x432e00 does the recording and the playback. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { REPLAY_RECORDING = 0, REPLAY_PLAYING = 1, REPLAY_DONE = 2 };
enum { REPLAY_MAX = 0x4000 };

typedef struct {
    int mode;                   /* 0x4b0bac */
    char path[256];             /* 0x5134e8 "..\gtadata\<name>" (the port keeps "GTADATA/<name>") */
    bool not_default;           /* 0x5135e8: the name isn't replay.rep */
    uint32_t frame;             /* 0x5135ec frame counter (Replay_TickFrame) */
    uint32_t pos;               /* 0x5134dc records written / read */
    uint32_t count;             /* 0x51364c records in the loaded file */
    uint32_t rec[REPLAY_MAX * 2];   /* 0x5ce750 (pointer 0x513648) */
} Replay;
extern Replay g_replay;
/* 0x5031f4: set by the frontend for its attract replays: any key abandons the playback */
extern bool g_replay_any_key_quits;

void replay_set_file_name(const char *name);   /* Replay_SetFileName 0x432c50 */
void replay_begin(void);                     /* Replay_Begin 0x432c90 */
void replay_end_save(void);                  /* Replay_EndSave 0x432d80 (and Replay_EndSave2 0x432dc0) */
static inline void replay_tick_frame(void) { g_replay.frame++; }               /* 0x433790 */
static inline bool replay_is_playing(void) { return g_replay.mode == REPLAY_PLAYING; }   /* 0x4337a0 */
bool replay_is_passthrough_key(int key);     /* Replay_IsPassthroughKey 0x433730 */
/* the recording side of Input_ReadControls: one record (File_Append with the option 0x502f44) */
void replay_record(uint32_t word);
