#include "replay.h"
#include "../platform.h"
#include "../vfs.h"
#include "game.h"
#include "input.h"
#include "player.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Replay g_replay;
bool g_replay_any_key_quits;

/* The file name without the original's "..\gtadata\" prefix, for the user-file store. */
static const char *base_name(void) { return g_replay.path + 8; }

/* Replay_SetFileName 0x432c50: WinMain passes "replay.rep" (0x4b0c78). The original compares 11
   characters (the name and its terminator) with "replay.rep" and keeps "not the default". */
void replay_set_file_name(const char *name)
{
    snprintf(g_replay.path, sizeof g_replay.path, "GTADATA/%s", name);
    g_replay.not_default = strncmp(name, "replay.rep", 11) != 0;
}

/* Replay_Begin 0x432c90 (Game_Init): the key queue flushed, the held actions and the joystick reset,
   frame and record counters 0. With the playback option (0x5031f0) the file is loaded (a size that
   isn't a multiple of 8 is fatal -0x29; an empty file records instead); else recording starts (the
   file truncated first with the append option 0x502f44). */
void replay_begin(void)
{
    input_flush_keys();
    input_reset_state();
    g_replay.frame = 0;
    g_replay.pos = 0;
    if (!g_replay.path[0]) replay_set_file_name("replay.rep");
    if (g_game.opt.opt5031f0 == 0) {
        if (g_game.opt.opt502f44 != 0) plat_save_user_file(base_name(), "", 0);   /* File_Truncate */
        g_replay.mode = REPLAY_RECORDING;
        return;
    }
    g_replay.mode = REPLAY_PLAYING;
    /* File_LoadInto: the user's recording if there is one, else the data folder's */
    size_t n = 0;
    uint8_t *b = plat_load_user_file(base_name(), &n);
    if (!b) b = vfs_read_all(g_replay.path, &n);
    if (n > sizeof g_replay.rec) n = sizeof g_replay.rec;
    if (b) memcpy(g_replay.rec, b, n);   /* little-endian host: the records as in the file */
    free(b);
    g_replay.count = (uint32_t)(n >> 3);
    if (g_replay.count * 8 != n) game_fatal(-0x29, 0x36, (int)n);
    if (g_replay.count == 0) g_replay.mode = REPLAY_RECORDING;
}

/* Replay_EndSave 0x432d80: recording stops (mode 2); a recording with records is written unless it
   was appended as it went. */
void replay_end_save(void)
{
    int was = g_replay.mode;
    g_replay.mode = REPLAY_DONE;
    if (was == REPLAY_RECORDING && g_replay.pos != 0 && g_game.opt.opt502f44 == 0)
        plat_save_user_file(base_name(), g_replay.rec, g_replay.pos * 8);   /* File_Save */
}

/* Replay_IsPassthroughKey 0x433730: while playing, the keys that still reach Game_HandleKey: Alt,
   F6, keypad +, Esc, F12, R and the releases of Alt, F6, keypad +, Esc, F12, R. */
bool replay_is_passthrough_key(int k)
{
    if (g_replay.mode != REPLAY_PLAYING) return false;
    switch (k) {
    case 0x38: case 0x40: case 0x4e: case 0x01: case 0x58: case 0x13:
    case 0xb8: case 0xc0: case 0xce: case 0x81: case 0xd8: case 0x93:
        return true;
    }
    return false;
}

void replay_record(uint32_t word)
{
    if (g_replay.pos >= REPLAY_MAX) return;
    g_replay.rec[g_replay.pos * 2 + 1] = word;
    g_replay.rec[g_replay.pos * 2] = g_replay.frame;
    g_replay.pos++;
    /* File_Append of the one record: the port rewrites the file, same content */
    if (g_game.opt.opt502f44 != 0) plat_save_user_file(base_name(), g_replay.rec, g_replay.pos * 8);
}
