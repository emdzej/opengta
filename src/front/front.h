/* The frontend state machine: what WinMain 0x437230 does around the game (settings, intro movie, the
   35 ms menu loop with its key mapping, the return from a game) and the frontend module 0x426320-
   0x42b7a0 (Front_Step 0x426a50, Front_SetScreen 0x427030 and the screens). Layout and screens:
   docs/frontend.md.

   The original's WinMain loop blocks: it calls Front_Step once per 35 ms until it returns non-zero,
   leaves the frontend, runs Game_Run and enters the frontend again. Here one front_frame call is one
   iteration of that loop (APP_FRAME_HZ); the caller runs the game when a frame returns FRONT_PLAY..JOIN
   and calls front_game_over afterwards.

   Everything the frontend needs from subsystems that are not its own (sound, mission.ini, players,
   network) goes through FrontHooks (NULL hooks behave like the original with sound off) and
   FrontGameResult. The network layer (DirectPlay) is stubbed in front_net.c: the network entries are
   there, as on a machine with DirectPlay, and find no connection. */
#pragma once
#include "font.h"
#include "savedata.h"
#include "surface.h"
#include <stdbool.h>
#include <stdint.h>

/* Front_Step's return value (0x5110b4), which WinMain acts on. */
enum {
    FRONT_CONTINUE = 0,
    FRONT_PLAY = 1,         /* single player game (0x510234 = 1) */
    FRONT_HOST = 2,         /* network game, host */
    FRONT_JOIN = 3,         /* network game, joined */
    FRONT_QUIT = 4,         /* the end of the credits */
};

/* Screen ids (0x5101d0) */
enum {
    FS_CONNECTIONS = 0, FS_CUTSCENE = 1, FS_ERROR = 2, FS_LOBBY_HOST = 3, FS_CD = 4, FS_LOBBY_JOIN = 5,
    FS_LOADING = 6, FS_START = 7, FS_MAIN = 8, FS_OPTIONS = 9, FS_CREDITS = 10, FS_PLAYERS = 0xb,
    FS_RENAME = 0xc, FS_RESULTS = 0xd, FS_RESET = 0xe, FS_SESSIONS = 0xf, FS_NET_NAME = 0x10,
    FS_COMMS_FAIL = 0x11, FS_MULTI_OPTIONS = 0x12, FS_COMMS_VERSION = 0x13, FS_SVGA = 0x14,
};

/* Frontend input bits, built by WinMain from the key events (front_map_key). */
enum {
    FI_UP = 1, FI_DOWN = 2, FI_LEFT = 4, FI_RIGHT = 8, FI_ENTER = 0x10, FI_ESC = 0x20,
    FI_CHAR = 0x40,         /* the character is in bits 16-23 (table 0x4a8b78) */
    FI_DELETE = 0x80,       /* Backspace or Delete */
    FI_SHIFT = 0x100,       /* a Shift key is down */
    FI_SPACE = 0x200,
};

/* One city of a chapter table (0x2c bytes in the original: single player 0x4af4a0, network
   0x4af418, three cities each). The static parts are read from the exe; name, mname and max are
   filled at runtime as in the original. */
typedef struct {
    const char *name;       /* +0x00: text "city%d" */
    int8_t count;           /* +0x04: missions (chapters) of the city */
    int8_t level[5];        /* +0x05: level index of each mission (best scores, high scores, cutscene) */
    int16_t section[4];     /* +0x0a: MISSION.INI section of each mission */
    int8_t race[6];         /* +0x12: network race flag of each mission */
    const char *mname[4];   /* +0x18: text "mission%d" of each section */
    int8_t max;             /* +0x28: highest selectable mission (-1 = none), Front_BuildChapterList */
} FrontCity;

/* Front_LoadTexts 0x426320: the frontend strings (one global each in the original, 0x5101b0..). */
enum {
    T_SSCOLON, T_SDCOLON, T_DSCOLON, T_CD_MESSAGE, T_HIGH_SCORES, T_WIN_SCORE, T_WIN_RACE, T_WIN_KILLS,
    T_ON, T_OFF, T_CHAPTER, T_SPC_KEY, T_RTN_KEY, T_ESC_KEY, T_STATUS, T_QUIT, T_PLAY, T_CANCEL,
    T_COMMSFAIL, T_COMMSVERSION, T_SVGAERROR, T_LOADING, T_LOADING_DEMO, T_GATHER, T_JOIN, T_OPTIONS,
    T_SOUND, T_MUSIC, T_TEXT, T_SLOW, T_NORMAL, T_FAST, T_MUSIC_MODE, T_TRANS_EFFECTS, T_CONSTANT,
    T_RADIO, T_SAVE, T_MULTI_OPTIONS, T_END_GAME, T_END_SCORE, T_SCORE, T_KILLS, T_END_KILLS, T_SCORES,
    T_DEL_KEY, T_RENAME, T_R_KEY, T_RESET, T_FINISH, T_CRIME_RTA, T_CRIME_HAR, T_CRIME_HIJ, T_CRIME_CAR,
    T_CRIME_SHO, T_CRIME_MUR, T_CRIME_BAN, T_DNF, T_SHIGHEST, T_CRIMES, T_MISSIONS_COMP, T_SECRETS,
    T_RACE_RESULTS, T_FINAL_SCORES, T_ABANDON, T_WINNER, T_LOSER, T_NO_WIN, T_MENU, T_STORY, T_COLOUR,
    T_CD_TITLE, T_CD_TEXT, T_CD_MESSAGE2, T_DEMO_MESSAGE, T_FIX1, T_FIX2, T_FIX3, T_FIX4, T_FIX5,
    T_COUNT
};

/* Calls into other subsystems. Any may be NULL: the frontend then behaves as the original does with
   sound off (Snd_GetCutsceneVoiceStatus returns -2) and without the game side. */
typedef struct {
    void (*sample)(int n);              /* Snd_PlaySampleN 0x404860 (frontend bank sample n) */
    void (*sfx)(int n);                 /* Snd_PlaySfx 0x472e00, called directly once (name entry) */
    int (*voice_status)(void);          /* Snd_GetCutsceneVoiceStatus 0x404e60: -2 no voice, -1 ended,
                                           else the stream position (the cutscene clock is pos / 100) */
    void (*voice_play)(int level);      /* Snd_PlayCutsceneVoice 0x404e00 */
    void (*voice_stop)(void);           /* Snd_StopCutsceneVoice 0x404e40 */
    /* Front_ApplyVolumes 0x4280b0: sfx and music 0..7, a volume of 0 also disables the channel
       (Snd_SetSfxEnabled / Snd_SetSfxVolume / Snd_SetMusicEnabled / Snd_SetMusicVolume). */
    void (*volumes)(int sfx, int music);
    /* Front_Enter 0x42b690: Audio_EnterFrontend 0x414670, Snd_SetSfxVolume(sfx) and, unless demo,
       Snd_SetMusicVolume(music) (music < 0: not set). */
    void (*enter)(int sfx, int music);
    void (*update)(void);               /* Audio_Update 0x414620, every Front_Step */
    /* Mission_SetIniSection 0x44ab50 + Mission_ReadIni 0x44ab90 when a mission is chosen. */
    void (*mission)(int section);
    void (*player_name)(int player, const char *name, int colour);   /* Player_SetNameColour 0x462b90 */
    /* Front_ScreenLoading 0x428bf0, every frame of the loading screen: HUD_SetPagerSpeed 0x482140,
       the effects option 0x5031e4, Music_SetSequential (sequential < 0: not called, demo). */
    void (*level_options)(int pager_speed, int effects, int sequential);
    const char *(*colour_name)(void);   /* Player_GetColourName (network loading screen) */
    int (*video_mode)(void);            /* Gfx_GetModeIndex 0x414d30 (saved after a game); NULL keeps it */
    void (*validate_video_mode)(int8_t *mode);   /* Gfx_ValidateModeIndex 0x414c70 */
} FrontHooks;

/* What the frontend reads from the game after Game_Run (results screen, high scores). */
typedef struct {
    int run_code;           /* Game_Run 0x4148a0: 4 comms failure, 5 SVGA error, else normal */
    bool to_start_menu;     /* 0x5031f4: back to the start menu (WinMain clears it and 0x5031f0) */
    int reason;             /* 0x513230: 1 mission complete, 7 abandoned, ... (Game_SetExit 0x4309e0) */
    int local;              /* 0x74f83c: the local player */
    int score[4];           /* Player_GetScore 0x4643b0, -1 = no such player */
    int frags[4];           /* Player_GetFrags */
    int rank[4];            /* Mission_GetPlayerRank (race) */
    int race_time[4];       /* Mission_GetPlayerVal (race time in 1/25 s, -1 did not finish) */
    char name[4][32];       /* Player_GetName */
    int kills[10];          /* Player_GetTotalKills 0x4628f0(0, kind) */
    int mission_counter;    /* Mission_GetCounterRemaining 0x43d1f0 */
    int mission_total;      /* Mission_GetMissionTotal 0x43d1d0 */
    int secret_counter;     /* Mission_GetSecretRemaining 0x43d220 */
    int secret_total;       /* Mission_GetSecretTotal 0x43d1e0 */
    char text[0x400];       /* Mission_GetResultText 0x445670: the result text (0x510870) ... */
    int text_score;         /* ... and its return value, the local player's score */
} FrontGameResult;

/* The rename screen's cheat names set these launch switches (Game_SetOptions 0x4146d0 globals). */
typedef struct {
    bool f502f35;           /* "6031769", "itstantrum" */
    bool f5031ec;           /* "iamthelaw", "stevesmates" */
    bool f503198;           /* "callmenigel", "buckfast" */
    bool f503194;           /* "porkcharsui": debug keys / free camera */
    bool f502f74;           /* "hate machine" */
    bool f50318c;           /* "itcouldbeyou" */
    bool f5031e5;           /* "suckmyrocket" */
} FrontCheats;

/* Input for one frame: key presses since the previous frame, as plat_key_presses gives them (scan
   codes, +0x100 for extended keys), and optionally the held keys (plat_keys) for the Shift state. */
typedef struct {
    const uint16_t *keys;
    int nkeys;
    const uint8_t *held;    /* KEY_COUNT entries or NULL */
} FrontInput;

typedef struct {
    int code;               /* FRONT_CONTINUE .. FRONT_QUIT */
    int section;            /* the chosen MISSION.INI section (code 1..3) */
    int level;              /* its level index (0..5 single player) */
    int player;             /* the player slot (save_data.current) */
} FrontStep;

typedef struct {
    /* configuration (WinMain / Game_SetOptions) */
    bool demo;              /* 0x5031bc */
    bool demo_music;        /* 0x50317d */
    bool net_active;        /* Net_IsActive 0x412950 (0x501d7c: Net_Start ran, the default) */
    FrontHooks hooks;
    FrontCheats cheats;
    char error[256];        /* why front_init failed */

    const char *tx[T_COUNT];
    bool texts_loaded;      /* 0x511124 */
    FrontCity tab_single[3];    /* 0x4af4a0 */
    FrontCity tab_net[3];       /* 0x4af418 */
    FrontCity *cities;      /* 0x511084: one of the two */
    int16_t max_city;       /* 0x5101b8 */

    int screen;             /* 0x5101d0 */
    int ret;                /* 0x5110b4 */
    int changed;            /* 0x511078: Front_SetScreen ran during this step */
    int menu;               /* 0x5110a0: the selected item (shared by the menu screens) */
    int mode;               /* 0x511104: 0 single player, 1 gather (host), 2 join */
    int net;                /* 0x5110fc: 0 single player, 1 network (index of chapter[] / mission[]) */
    bool cd_ok;             /* 0x511108 */
    bool results_done;      /* 0x511100: the high scores of this game are in */
    int after_cut;          /* 0x5106c8: where the cutscene goes, 0 main, 1 results */
    int start_code;         /* 0x510234 */
    int reason;             /* 0x513230 (the main screen sets 1 to replay a cutscene) */

    int cut_level;          /* 0x511114 */
    int cut_frame;          /* 0x511118 */
    int cut_bg_level;       /* 0x4af410: level whose still is loaded */
    Font *cut_font[4];      /* 0x5106dc */

    uint8_t hs_new[3];      /* 0x5110ac: high score entry just set (blinks) */
    uint8_t best_new[6];    /* 0x510290: level best just set (blinks) */
    uint8_t hs_blink;       /* 0x51111c */
    uint8_t best_blink;     /* 0x51111d */
    int city_frame[4];      /* 0x51112c: city logo animation frames */
    int main_marker;        /* 0x511128 */
    int portrait_blink;     /* 0x51113c */
    int name_blink;         /* 0x511140 */
    int credit_scroll;      /* 0x511144 */
    int credit_h;           /* 0x5110e8 */
    bool credit_init;       /* 0x5101e4 bit 0 */
    char rename[16];        /* 0x5101f0 */
    char err_msg[256];      /* 0x510760 (screen 2; nothing sets it in this build) */

    int result_class;       /* 0x5110e0: 0 winner, 1 loser, 2 no winner, 3 abandoned */
    uint8_t winner[4];      /* 0x5110b8 */
    uint8_t winner_blink[4];/* 0x511120 */
    uint8_t win_anim;       /* 0x511148 */
    char race_buf[16];      /* 0x5106bc */
    char result_text[0x400];/* 0x510870 */

    int provider;           /* 0x51110c */
    int session;            /* 0x511110 */
    int lobby;              /* 0x5110ec */
    int lobby_first;        /* 0x4af414 */

    FrontGameResult res;
    bool in_front;          /* between Front_Enter and Front_Leave */
    uint32_t clock_ms;      /* stands in for clock() (Front_DrawBackground): 35 ms per frame */
    unsigned shift;         /* WinMain's Shift state: bit 0 left, bit 1 right */
    uint16_t queue[64];     /* key events not read yet (WinMain reads one per frame) */
    int nqueue;
} Front;

/* WinMain 0x437230 up to its loop: Front_LoadSettings 0x42b4a0 (PLAYER_A.DAT, texts, chapter list,
   video mode, volumes), Player_SetNameColour(0, "Player", 0), Movie_PlayIntro 0x44b160 (stub) and
   Front_Enter(4). The exe must be loaded (exe_init) and the language selected (text_init_language).
   Configure f->demo / net_active / hooks before. False if a file is missing (f->error says which). */
bool front_init(Front *f);
/* One iteration of WinMain's frontend loop: one key event -> input bits, Front_Step, and when it
   returns non-zero, Front_Leave. Draws into s (640 x 480). */
FrontStep front_frame(Front *f, const FrontInput *in, Surface *s);
/* WinMain after Game_Run (the caller repeats the game while Game_Run returns 3): Front_Enter with
   0x11 / 0x14 / the cutscene (which turns into the results screen) / the start menu. */
bool front_game_over(Front *f, const FrontGameResult *res);
void front_shutdown(Front *f);                  /* Front_Leave if needed, cutscene fonts */

/* WinMain's key mapping: a key event (scan code; +0x100 extended; +0x80 release) to input bits, with
   the Shift state. The character table is 0x4a8b78 in the exe. */
uint32_t front_map_key(Front *f, int code);

/* Pieces of the module, for the game and tests. */
int front_step(Front *f, uint32_t in, Surface *s);          /* Front_Step 0x426a50 */
void front_set_screen(Front *f, int screen);                /* Front_SetScreen 0x427030 */
bool front_enter(Front *f, int screen);                     /* Front_Enter 0x42b690 */
void front_leave(Front *f);                                 /* Front_Leave 0x42b7a0 */
bool front_load_settings(Front *f);                         /* Front_LoadSettings 0x42b4a0 */
void front_load_texts(Front *f);                            /* Front_LoadTexts 0x426320 */
void front_build_chapter_list(Front *f);                    /* Front_BuildChapterList 0x427630 */
bool front_apply_volumes(Front *f);                         /* Front_ApplyVolumes 0x4280b0 */
void front_advance_mission(Front *f);                       /* Front_AdvanceMission 0x42b1e0 */
/* Front_SelectMission 0x4268d0 (network): select mission number `section` and, unless it is a race,
   the network target type and value. */
void front_select_mission(Front *f, int section, int type, int target);
const char *front_current_player_name(const Front *f);      /* Front_GetCurrentPlayerName 0x426990 */
int front_current_player(const Front *f);                   /* Front_GetCurrentPlayerIndex 0x4269b0 */
/* Front_GetMultiTarget 0x4269c0: type -1 single player, 0 score, 1 kills, 2 race; target value. */
void front_multi_target(const Front *f, int *type, int *target);
/* The selected city / mission of the current player (chapter[net], mission[net]) and its level. */
int front_sel_city(const Front *f);
int front_sel_mission(const Front *f);
int front_sel_level(const Front *f);

/* Movie_PlayIntro 0x44b160: starts MOVIE.SMK (src/movie/intro.c, docs/movie.md); front_frame steps it until it ends. */
void movie_play_intro(void);
