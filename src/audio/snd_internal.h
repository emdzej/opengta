/* State shared by the sound modules (src/audio), named after the original's globals. */
#pragma once
#include "audio.h"
#include "mss.h"
#include "vfs.h"

/* A bank entry (.SDT record): offset and length in the .RAW, playback rate. */
typedef struct {
    uint32_t off, len, rate;
} SndEntry;

enum {
    SND_LEVEL_ENTRIES = 0x83,            /* Snd_LoadLevelBank reads 131 records */
    SND_VOCAL_ENTRIES = 0x47,            /* Snd_OpenVocalBank reads 71 */
    SND_BUFFER_SIZE = 0x10fde8,          /* the locked buffer: level RAW (<= 1 MB), then the voice */
    SND_LEVEL_MAX = 0x100000,
    SND_NO_SAMPLE = 0x84,                /* "no sample" in the loop tables */
};

/* The low-level module (0x7706xx-0x7710xx). */
typedef struct {
    bool locked;                         /* 0x771079 buffer allocated */
    bool running;                        /* 0x771078 Miles started */
    uint8_t *buffer;                     /* 0x770634 (also 0x77063c, 0x770fec: level data base) */
    SndEntry level[SND_LEVEL_ENTRIES];   /* 0x7709c8 */
    SndEntry vocal[SND_VOCAL_ENTRIES];   /* 0x770640 */
    int level_bank;                      /* (not in the original) the bank loaded, for tests */
    MssSample *loop[10];                 /* 0x77060c */
    MssSample *shot[3];                  /* 0x770ff4 */
    MssSample *pos3d;                    /* 0x7709ac */
    MssSample *menu;                     /* 0x771074 frontend / police scanner */
    MssSample *voice;                    /* 0x771008 */
    MssSample *talk;                     /* 0x7709b0 */
    MssSample *sfx_stereo[4];            /* 0x77099c frontend set */
    MssSample *sfx_mono[4];              /* 0x7709b4 frontend set */
    uint8_t rr_mono, rr_stereo;          /* 0x77107a, 0x77107b round-robin indices */
    bool jitter_up;                      /* 0x4b3118 sign of the next random pitch offset (starts 1) */
    int32_t sfx_volume;                  /* 0x4b3114 (64) */
    int32_t stream_volume;               /* 0x4b3110 (100) */
    VfsFile *vocal_file;                 /* 0x771010 VOCALCOM.RAW, kept open */
    MssStream *stream;                   /* 0x770608 GTADATA/AUDIO/n.WAV */
    int32_t stream_length;               /* 0x77100c */
    uint32_t stream_started;             /* 0x770638 */
} SndLow;
extern SndLow snd;

/* The game-level module and the options it reads (0x4ab04x, 0x4bdxxx). */
typedef struct {
    bool music_on;                       /* 0x4ab040 (1) */
    bool sfx_on;                         /* 0x4ab041 (1) */
    uint8_t music_vol;                   /* 0x4ab042 (0x7f) */
    uint8_t sfx_vol;                     /* 0x4ab043 (0x7f) */
    bool scanner_fast;                   /* 0x4ab044 (1): scanner words at 6000 Hz, else 5000 */
    bool music_usable;                   /* 0x4ab058 (1) */
    bool running;                        /* 0x4bddf0 */
    bool cutscene_voice;                 /* 0x4bddf3 */
    int mode;                            /* 0x5031f8 audio mode: 1 game, 2 frontend */
    SndOptions opt;                      /* 0x502f6c, 0x5031bc, 0x50317d */
} SndGame;
extern SndGame sndg;

/* the host, with defaults filled in */
extern SndHost snd_host;
extern SndWorld *snd_world;
int snd_rand(void);                                  /* 0x49cb27 */
void snd_listener(int32_t *x, int32_t *y, int32_t *z);
bool snd_covered(int32_t x, int32_t y, int32_t z);
bool snd_frozen(void);
uint32_t snd_lcg(void);                              /* the 0x4bddac generator: next value */
extern uint32_t snd_lcg_state;                       /* 0x4bddac */
