/* Sound and music: the game's sound modules ported function by function, over a reimplementation of the
   Miles Sound System calls they make (mss.h). docs/audio.md has the formats, the channel model and the
   Miles calls replaced.

   - Low-level sound module 0x471bc0-0x473430 (snd.c): sample banks (GTADATA/AUDIO/LEVELnnn.SDT/.RAW,
     VOCALCOM), the Miles handles, positional one-shots, voices, the cutscene/briefing WAV stream.
   - Game-level sound 0x402640-0x405760 (sound.c): 3D one-shots, the 10 looping channels (engines, horns,
     sirens, trains, cranes, the helicopter, ...) kept sorted by distance, ped voices, the police scanner
     speech queue, start-up/shut-down per mode; plus the audio mode switch 0x414620-0x4146d0.
   - Music / radio 0x40c300-0x40d620 (music.c): Music/TrackN.wav streams, radio stations per car.
   - The 70 Hz AIL timer 0x47dc00-0x47dcb0 (timer.c) that paces Game_Run.

   Everything runs on the caller's thread: the game calls the Snd_* / Music_* functions from its frame,
   the platform calls audio_render after each frame. No wall clock: the only time is audio rendered.

   The names are the original's (tools/ghidra/names.tsv), so the game core can call them as the exe does.
   What they read from the game goes through SndHost (queries) and SndWorld (a per-frame snapshot of the
   cars, trains, ... that Snd_UpdateGame needs), not through other modules' globals. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "app.h"

/* Mixes `frames` stereo frames (interleaved float) at APP_AUDIO_RATE and advances audio time (the AIL
   timer and ms clock). out may be NULL to advance silently. */
void audio_render(float *out, unsigned frames);

/* ---- what the sound code asks the game ---- */

typedef struct SndHost {
    void *user;
    /* Player_GetViewTargetPos 0x462d50 of the local player 0x74f83c: the listener (16.16 world coords). */
    void (*listener)(void *user, int32_t *x, int32_t *y, int32_t *z);
    /* Map_IsCovered 0x438800: true if something solid is above (x, y, z): sounds there are halved. */
    bool (*covered)(void *user, int32_t x, int32_t y, int32_t z);
    /* Game_IsFrozen 0x4309d0 (paused). */
    bool (*frozen)(void *user);
    /* The C runtime's rand() 0x49cb27, which the sound code shares with the game. */
    int (*crt_rand)(void *user);
    /* The C runtime's time(NULL) 0x49cb97 (Music_Startup seeds the radio track offsets with it). */
    int32_t (*crt_time)(void *user);
} SndHost;
/* Any NULL member gets a default: listener at 0, nothing covered, never frozen, an MSVC-compatible
   rand() of its own (seed 1), time 0. */
void Snd_SetHost(const SndHost *host);
/* The C runtime rand() the default uses (seed*214013 + 2531011, bits 16..30), for a host to share. */
int snd_crt_rand(void);
void snd_crt_srand(uint32_t seed);

/* Launch options (Game_SetOptions 0x4146d0): 0x502f6c sound on, 0x5031bc demo, 0x50317d (music in demo). */
typedef struct {
    bool sound;
    bool demo;
    bool demo_music;
} SndOptions;
void Snd_SetOptions(const SndOptions *o);

/* A car as Snd_GatherLoops 0x402690 and Music_UpdateRadio 0x40cde0 read it (car table 0x4be248, stride
   0x2b0; its car_info at +0x2ac). Field = offset in the original. */
typedef struct {
    int16_t id;              /* +0x00 own index */
    int16_t control;         /* +0x04 control mode (1 = physics/player) */
    int16_t status;          /* +0x08 -1 = free slot */
    int16_t speed;           /* +0x1c */
    int16_t engine_off;      /* +0x80 (0 = the engine section runs) */
    int16_t engine_on;       /* +0x88 (1 = the engine section runs) */
    int16_t damage;          /* +0xfc 0..100 */
    int16_t burning;         /* +0xfe */
    int16_t fast_pitch;      /* +0x110 (non-zero triples the engine rate) */
    int16_t siren;           /* +0x11a horn/siren state: 1, 2 */
    int16_t horn_time;       /* +0x136 */
    int8_t horn_pattern;     /* +0x138 (-1 none): row of the horn pattern table 0x4ab091 */
    uint8_t skid_flag;       /* +0x146 */
    int8_t b147;             /* +0x147 (-1: extra loop 0x45 for engine 6) */
    int16_t brake_peak;      /* +0x14a peak speed for the bus air brake: Snd_GatherLoops writes it */
    float skid;              /* +0x150 */
    int32_t x, y, z;         /* +0x250 sprite position */
    /* car_info */
    uint8_t convertible;     /* +0xa6 bit 0 (CarInfo_IsConvertible 0x40bdb0) */
    uint8_t engine;          /* +0xa7 engine sound: loop sample engine + 0x2d */
    uint8_t radio;           /* +0xa8 radio station type */
    uint8_t horn;            /* +0xa9 horn: sample horn/10 + 0x3a; >= 60 = siren */
    uint8_t sound_fn;        /* +0xaa sound function 0..5 (2 boat, 4 tank, 5 bus) */
    uint8_t fast_change;     /* +0xab rate slews by 3000 instead of 500 */
} SndCar;

typedef struct {
    uint8_t speed;           /* +0x04 (0 = not moving: no sound) */
    int32_t pan_x;           /* +0x100 (the pan uses this x, the distance the next three) */
    int32_t x, y, z;         /* +0x538 */
} SndTrain;

typedef struct {
    int32_t state;           /* crane +0 (8, 9 silent; 0, 6 idle) */
    int32_t x, y;            /* its object's +0x2c, +0x30 */
} SndCrane;

typedef struct {
    /* Cars_GetCount 0x407150 / Car_Get 0x408200: cars[i] is slot i (the view car is cars[view_id]). */
    int ncars;
    SndCar *cars;
    /* The local player: Player_GetControlledKind 0x462fc0 (0 car, 2 ...), Player_GetControlledId 0x462fe0,
       Player_GetViewKind 0x462f80 (0 = in a car), Player_GetViewId 0x462fa0. */
    int player_kind, player_id;
    int view_kind, view_id;
    /* Train_GetCount 0x46e7c0 / Train_Get 0x46e7a0 */
    int ntrains;
    const SndTrain *trains;
    /* Crane_Get 0x4756a0 for 0..3 */
    SndCrane cranes[4];
    /* the helicopter: state 0x501be0 (0/1 = none), position 0x501bfc, 0x501c00 */
    int32_t heli_state, heli_x, heli_y;
    /* Ped_GetGroupLeaderPos 0x44ef10 */
    bool leader;
    int32_t leader_x, leader_y;
} SndWorld;
/* The snapshot Snd_UpdateGame uses (kept by pointer: it must stay valid; NULL = an empty world). */
void Snd_SetWorld(SndWorld *w);

/* ---- game-level sound module (0x402640-0x405760) ---- */

void Snd_SetSfxEnabled(bool on);                    /* 0x402640 */
void Snd_SetMusicEnabled(bool on);                  /* 0x402650 */
void Snd_RestoreSfx(void);                          /* 0x402660 */
void Snd_SuspendSfx(void);                          /* 0x402670 */
void Snd_GatherLoops(void);                         /* 0x402690 */
void Snd_PlayTalk(const char *text);                /* 0x404410 babble sized to a message */
void Snd_UpdateLoopChannels(void);                  /* 0x404430 */
void Snd_Play3D(int32_t x, int32_t y, int32_t z, int sample);   /* 0x4046b0 */
void Snd_PlayAt(int32_t x, int32_t y, int32_t z, int sample);   /* 0x4047f0 */
void Snd_PlayAtXY(int32_t x, int32_t y, int sample);            /* 0x404810 */
void Snd_PlayUI(int32_t x, int32_t y, int32_t z, int sample);   /* 0x404840 (Snd_PlayPositional) */
void Snd_PlaySampleN(int sample);                   /* 0x404860 (Snd_PlaySfx) */
/* 0x404880: queue a scanner report: car model, (always 0 in the game), direction 1..9, area sample number
   (nav data; 0 = none: nothing queued). */
void Snd_PoliceRadio(int model, int kind, int direction, int area);
void Snd_SetMusicVolume(int level);                 /* 0x404bf0 level 0..7 */
void Snd_SetSfxVolume(int level);                   /* 0x404c30 level 0..7 */
void Snd_PlayRadioStatic(void);                     /* 0x404c70 */
void Snd_SetEmitterA(int slot, int32_t x, int32_t y, int32_t z);   /* 0x404cb0 slots 0..4, loop 0x46 */
void Snd_ClearEmitterA(int slot);                   /* 0x404cf0 */
void Snd_EnableAlarmLoop(void);                     /* 0x404d10 loop 0x47 */
void Snd_DisableAlarmLoop(void);                    /* 0x404d20 */
void Snd_PlaySample28(void);                        /* 0x404d30 */
void Snd_SetEmitterB(int slot, int32_t x, int32_t y, int32_t z);   /* 0x404d50 slots 0..3, loop 0x1c */
void Snd_ClearEmitterB(int slot);                   /* 0x404d90 */
void Snd_Pause(void);                               /* 0x404db0 */
void Snd_Resume(void);                              /* 0x404de0 */
void Snd_PlayCutsceneVoice(int n);                  /* 0x404e00 GTADATA/AUDIO/n.WAV, n < 6 */
void Snd_StopCutsceneVoice(void);                   /* 0x404e40 */
int Snd_GetCutsceneVoiceStatus(void);               /* 0x404e60 ms played, -1 finished, -2 none */
void Snd_PlayVoice(int n);                          /* 0x404e90 VOCALCOM sample */
void Snd_PlayRandomVoice(void);                     /* 0x404ed0 */
void Snd_PlayPedVoice(int32_t x, int32_t y, int32_t z, int kind, int variant);   /* 0x404f50 */
void Snd_PlayScream(int32_t x, int32_t y, int32_t z);           /* 0x405090 */
void Snd_PlayVoiceB(int32_t x, int32_t y, int32_t z);           /* 0x4051b0 */
void Snd_PlayVoiceC(int32_t x, int32_t y, int32_t z);           /* 0x4052c0 */
void Snd_PlayAtObject(int32_t x, int32_t y, int32_t z);         /* 0x4053d0 (takes a player: pass its view target) */
void Snd_SetMusicAvailable(bool on);                /* 0x405450 */
void Snd_InitFrontend(void);                        /* 0x405460 */
void Snd_ShutdownFrontend(bool quiet);              /* 0x4054a0 */
void Snd_UpdateFrontend(void);                      /* 0x4054f0 */
void Snd_UpdateGame(void);                          /* 0x405510 once per game frame */
void Snd_InitGame(void);                            /* 0x405600 */
void Snd_ShutdownGame(void);                        /* 0x4056c0 */
void Snd_Reset(int bank);                           /* 0x4056e0 per level: bank = style number (city 1..3) */
void Snd_StopAll(void);                             /* 0x405760 (Game_Shutdown) */
/* The original's Snd_ShutdownFrontend(0) blocks until the exit jingle has played and Snd_Shutdown waits
   another second; here they keep playing on their own (the handles are released when done). True while
   that is still going on, for a caller that wants to wait like the original. */
bool Snd_FrontendDraining(void);

/* The audio mode switch (game module): 0x5031f8 = 0 none, 1 game, 2 frontend. */
void Audio_Update(void);                            /* 0x414620 */
void Audio_EnterGame(void);                         /* 0x414640 */
void Audio_EnterFrontend(void);                     /* 0x414670 */
void Audio_Shutdown(void);                          /* 0x4146a0 */
int Audio_Mode(void);

/* ---- music / radio (0x40c300-0x40d620) ---- */

int Music_CatalogTracks(void);                      /* 0x40c300 0 or -51 */
void Music_Init(void);                              /* 0x40c470 */
void Music_Shutdown(void);                          /* 0x40c490 */
bool Music_Startup(void);                           /* 0x40c5c0 */
bool Music_PlayTrack(int track, int32_t pos);       /* 0x40c700 */
int Music_PlayCurrent(int track);                   /* 0x40c830 */
void Music_SetVolume(int vol);                      /* 0x40c8b0 stream volume 0..127 */
void Music_Stop(void);                              /* 0x40c910 */
int Music_ServiceSequential(void);                  /* 0x40ca00 */
void Music_StopSavePos(void);                       /* 0x40cb90 */
void Music_ServiceFrontend(void);                   /* 0x40cc80 */
void Music_ServicePaused(void);                     /* 0x40cd40 */
void Music_UpdateRadio(void);                       /* 0x40cde0 */
void Music_NextStation(void);                       /* 0x40d140 (also 0x404c60) */
int Music_GetStatus(void);                          /* 0x40d550 0 none, 1 playing, 3 failed */
void Music_Pause(void);                             /* 0x40d5c0 */
/* State for tests and the HUD: 0x501aac (0 off, 1 stopped, 2 playing, 3 paused, 4 playing on foot),
   0x501aa8 current track 0..9, 0x4ab2e0 current station (10 = on foot). */
int Music_State(void);
int Music_Track(void);
int Music_Station(void);
int Music_StreamVolume(void);                       /* the stream's volume, -1 without one */
/* 0x501aa4: 0 radio (stations), 1 sequential. Music_SetSequential is 0x40d620. */
void Music_SetSequential(bool on);
bool Music_Sequential(void);

/* ---- low-level module (0x471bc0-0x473430) ---- */

void Snd_FreeBuffer(void);                          /* 0x471bc0 */
void Snd_StartupGame(void);                         /* 0x471be0 */
void Snd_ShutdownSamples(void);                     /* 0x471e80 */
void Snd_StartupFrontend(void);                     /* 0x471f10 */
void Snd_Shutdown(void);                            /* 0x472110 */
bool Snd_ProbeDevice(void);                         /* 0x472180 */
void Snd_CloseDevice(void);                         /* 0x472260 */
void Snd_SetMasterVolume(int sfx);                  /* 0x472280 */
int32_t Snd_GetSampleRate(int n);                   /* 0x4722f0 */
bool Snd_LoadLevelBank(int n);                      /* 0x472300 */
bool Snd_OpenVocalBank(void);                       /* 0x472470 */
void Snd_CloseVocalBank(void);                      /* 0x4725a0 */
int32_t Snd_CalcRateA(int32_t x);                   /* 0x4725c0 */
int32_t Snd_CalcEngineRate(int32_t x);              /* 0x4725e0 */
int32_t Snd_GearRate(int32_t x);                    /* 0x472620 */
int32_t Snd_CalcRateB(int32_t x);                   /* 0x472670 */
int32_t Snd_CalcRateC(int32_t x);                   /* 0x4726d0 */
int32_t Snd_CalcVolume(int32_t x);                  /* 0x4726f0 */
int32_t Snd_CalcDelta(int32_t a, int32_t b);        /* 0x472730 */
int32_t Snd_DistToVolume(int32_t d);                /* 0x472770 */
int32_t Snd_DistToPan(int32_t dx);                  /* 0x472790 */
void Snd_LoopSetRate(int ch, int32_t rate);         /* 0x4727a0 */
void Snd_LoopSetPan(int ch, int32_t pan);           /* 0x4727c0 */
void Snd_LoopSetVolume(int ch, int32_t vol);        /* 0x4727e0 */
void Snd_LoopSetSample(int ch, int n);              /* 0x472800 */
void Snd_LoopStart(int ch);                         /* 0x472850 */
void Snd_LoopStop(int ch);                          /* 0x472870 */
void Snd_ShotSetPan(int ch, int32_t pan);           /* 0x472890 */
void Snd_ShotSetVolume(int ch, int32_t vol);        /* 0x4728b0 */
void Snd_ShotSetSample(int ch, int n);              /* 0x4728d0 */
bool Snd_ShotIsPlaying(int ch);                     /* 0x472980 */
void Snd_ShotStart(int ch);                         /* 0x4729a0 */
void Snd_ShotStop(int ch);                          /* 0x4729d0 */
void Snd_StopAllLoops(void);                        /* 0x4729f0 */
void Snd_StopAllShots(void);                        /* 0x472a10 */
void Snd_PlayPositional(int32_t x, int32_t y, int n);   /* 0x472a30 */
void Snd_PlayMenuSample(int n);                     /* 0x472b90 6000 Hz */
void Snd_VoiceSetPan(int32_t pan);                  /* 0x472c00 */
void Snd_VoiceSetVolume(int32_t vol);               /* 0x472c20 */
bool Snd_VoiceIsPlaying(void);                      /* 0x472c40 */
void Snd_VoicePlay(int n);                          /* 0x472c60 */
void Snd_PlayMenuSampleLow(int n);                  /* 0x472d90 5000 Hz */
void Snd_PlaySfx(int n);                            /* 0x472e00 */
bool Snd_AnySfxPlaying(void);                       /* 0x472fe0 */
bool Snd_MenuIsPlaying(void);                       /* 0x473050 */
void Snd_MenuStop(void);                            /* 0x473070 */
void Snd_VoiceStop(void);                           /* 0x473080 */
void Snd_TalkPlay(int n);                           /* 0x473090 */
void Snd_PlayMumble(const char *text);              /* 0x473120 */
void Snd_MuteAll(void);                             /* 0x473200 */
void Snd_RestoreVolumes(void);                      /* 0x473260 */
void Snd_MusicPlay(int n);                          /* 0x4732f0 */
void Snd_MusicService(void);                        /* 0x4733a0 */
void Snd_MusicClose(void);                          /* 0x4733c0 */
int Snd_MusicGetPos(void);                          /* 0x4733e0 */
void Snd_SetStreamVolume(int vol);                  /* 0x473430 */
/* The level bank currently loaded (-1 none) and its entry count read from the .SDT. */
int Snd_LevelBank(void);
bool Snd_Running(void);                             /* 0x771078 Miles started */

/* ---- the 70 Hz AIL timer (0x47dc00-0x47dcb0) ---- */

#define TIMER_HZ 70                                 /* AIL_set_timer_frequency(.., 0x46) */
#define GAME_FRAME_TICKS 3                          /* Game_Run: Timer_WaitTicks(3) */
void Timer_Start(void);                             /* 0x47dc00 (needs sound on) */
/* 0x47dc80: the original busy-waits until n ticks have passed since the last wait, then restarts the
   count. Here it does not block: true (and the count restarts) once n ticks have passed, false while
   the frame should keep waiting. Always true when the timer isn't running (0x775584) or 0x502f34 is
   clear (Game_Run sets it). */
bool Timer_WaitTicks(unsigned n);
void Timer_Stop(void);                              /* 0x47dcb0 */
void Timer_SetEnabled(bool on);                     /* 0x502f34 */
uint32_t Timer_Ticks(void);                         /* 0x775580 */
