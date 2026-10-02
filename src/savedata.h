/* PLAYER_A.DAT: settings, high scores and the 8 player records, 0x414 bytes kept at 0x510298 by the
   frontend (Front_LoadSettings 0x42b4a0; saved by Front_SetScreen 0x427030 after a game and by the
   options screen 0x429180). Layout: docs/text-fonts.md. Little-endian, as stored (the host is). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { SAVE_SIZE = 0x414, SAVE_PLAYERS = 8, SAVE_LEVELS = 6, SAVE_HISCORES = 3 };

typedef struct {
    int32_t score;
    char name[16];
} SaveHiscore;                  /* 0x14 bytes */

typedef struct {
    char name[16];              /* +0x00 */
    int32_t best[SAVE_LEVELS];  /* +0x10: best score per level (city x part); -1 = level locked */
    int32_t chapter[2];         /* +0x28: selected chapter, [0] single player, [1] network (0x5110fc) */
    int32_t mission[2];         /* +0x30: selected mission of that chapter */
    int32_t seen[SAVE_LEVELS];  /* +0x38: the level's intro cutscene has been shown */
} SavePlayer;                   /* 0x50 bytes */

typedef struct {
    int8_t sfx_volume;          /* +0x00: 0..7 (default 4) */
    int8_t music_volume;        /* +0x01: 0..7 (default 6) */
    int8_t pager_speed;         /* +0x02: 1..3 (default 2; HUD_SetPagerSpeed 0x482140) */
    int8_t music_sequential;    /* +0x03: radio mode (Music_SetSequential); default: demo mode 0x5031bc */
    int8_t video_mode;          /* +0x04: display mode index (Gfx_ValidateModeIndex 0x414c70) */
    int8_t unk5;                /* +0x05: default 0 */
    int8_t unk6;                /* +0x06: not set by the defaults (0) */
    int8_t effects;             /* +0x07: default 1, copied to 0x5031e4 (sprite rasteriser option) */
    int8_t multi_target;        /* +0x08: network game target, 0 score, 1 kills */
    uint8_t pad9[3];
    int32_t score_target;       /* +0x0c: default 100000 */
    int32_t kill_target;        /* +0x10: default 10 */
    int8_t language;            /* +0x14: -1 = the registry's; else Text_SetLanguage(language) */
    uint8_t pad15[3];
    SaveHiscore hiscore[SAVE_LEVELS][SAVE_HISCORES];  /* +0x18: ascending, [2] is the best */
    SavePlayer player[SAVE_PLAYERS];                  /* +0x180 */
    uint8_t current;            /* +0x400: current player slot (default 1) */
    char net_name[19];          /* +0x401: network session/player name (default "GTA Game") */
} SaveData;

extern SaveData save_data;      /* 0x510298 */

/* The file half of Front_LoadSettings 0x42b4a0: the user's PLAYER_A.DAT (plat_load_user_file), else
   GTADATA/PLAYER_A.DAT of the data root (where the original reads and writes it), at least
   0x414 bytes; otherwise the defaults are built (names and scores from the exe) and saved. demo_mode is
   0x5031bc. Then, as the original, a language other than -1 is applied with text_set_language. Returns
   false only if neither a file nor the exe (for the defaults) is available. The rest of
   Front_LoadSettings (Front_LoadTexts, Front_BuildChapterList, mode and volume checks) belongs to the
   frontend. */
bool save_load(bool demo_mode);
/* The defaults of Front_LoadSettings (the exe must be loaded for the names; empty without it). */
void save_defaults(bool demo_mode);
/* File_Save 0x42e0e0 of the 0x414 bytes, to the user file. */
bool save_store(void);
/* The frontend's revert (fopen "rb" + fread of 0x414 bytes into the buffer, nothing if the file can't
   be opened): the user file, else GTADATA/PLAYER_A.DAT. A short file overwrites only its bytes. */
bool save_reload(void);
