/* The part of the Miles Sound System (MSS32.DLL, "MSS V3.6B", 1997) that GTA uses, reimplemented: digital
   samples (HSAMPLE) played from memory at a given rate / volume / pan with a loop count, WAV file streams
   (HSTREAM), the digital master volume, the millisecond clock and the timer service. docs/audio.md lists
   every AIL_* import of the exe and what it does here.

   The mixer is MSS's own software mixer (the "waveOut" path of MSS32.DLL, used when DirectSound is
   emulated): 32-bit build buffer, point-sampled resampling in 16.16 fixed point, per-voice lookup tables
   for 8-bit data, then a clip to 16 bits and the output format the game asks for (AIL_waveOutOpen with
   22050 Hz, stereo, 8 bits per sample). Integer only, deterministic, no threads, no wall clock: time is the
   number of frames rendered. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "app.h"

/* The output: what Snd_StartupGame 0x471be0 / Snd_StartupFrontend 0x471f10 pass to AIL_waveOutOpen. */
#define MSS_RATE APP_AUDIO_RATE        /* 22050 (0x5622) */
/* 8-bit output (the original's format: the mix is clipped to 16 bits and only the high byte is kept).
   Set to 0 for 16-bit output (a deviation; the mix is otherwise identical). */
#ifndef MSS_OUTPUT_8BIT
#define MSS_OUTPUT_8BIT 1
#endif

/* Sample formats (AIL_set_sample_type) and flags. */
enum { MSS_MONO_8 = 0, MSS_MONO_16 = 1, MSS_STEREO_8 = 2, MSS_STEREO_16 = 3 };
enum { MSS_PCM_SIGN = 1 };
/* Sample / stream status (AIL_sample_status). Streams report -1 when they are NULL or failed. */
enum { MSS_FREE = 1, MSS_DONE = 2, MSS_PLAYING = 4, MSS_STOPPED = 8 };

typedef struct MssSample MssSample;    /* HSAMPLE */
typedef struct MssStream MssStream;    /* HSTREAM */

/* AIL_startup + AIL_set_preference(DIG_MIXER_CHANNELS = 0, channels) + AIL_waveOutOpen: resets the
   driver with `channels` sample handles (MSS's default is 16; GTA asks for 19 in game and 12 in the
   frontend; streams take one each), all released, master volume 127. */
void mss_startup(int channels);
/* AIL_waveOutClose + AIL_shutdown: releases every handle and stream; timers keep running (AIL_shutdown
   would stop them; GTA stops its timer itself). With drain, samples still playing from memory are left
   to finish (the original waits for them before shutting down: see Snd_FrontendDraining); they no
   longer count against the channels of the next mss_startup. */
void mss_shutdown(bool drain);
bool mss_draining(void);           /* such samples are still playing */
uint32_t mss_drained_ms(void);     /* mss_ms_count when the last of them finished */
bool mss_running(void);

/* AIL_set_digital_master_volume (0..127). */
void mss_set_master_volume(int32_t vol);
int32_t mss_master_volume(void);
/* AIL_ms_count: milliseconds of audio rendered since the first mss_startup. */
uint32_t mss_ms_count(void);

/* HSAMPLE. All calls accept NULL (MSS's do: they test the handle). */
MssSample *mss_allocate_sample(void);                         /* AIL_allocate_sample_handle; NULL if none free */
void mss_release_sample(MssSample *s);                         /* AIL_release_sample_handle */
void mss_init_sample(MssSample *s);                            /* AIL_init_sample: 11025 Hz, mono 8, vol 100, pan 64, 1 loop */
void mss_set_sample_type(MssSample *s, int format, unsigned flags);
void mss_set_sample_address(MssSample *s, const void *data, uint32_t len);  /* bytes; rewinds */
void mss_set_sample_playback_rate(MssSample *s, int32_t hz);
void mss_set_sample_volume(MssSample *s, int32_t vol);         /* 0..127 (clamped when mixed) */
void mss_set_sample_pan(MssSample *s, int32_t pan);            /* 0 left .. 64 centre .. 127 right */
void mss_set_sample_loop_count(MssSample *s, int32_t n);       /* 0 = forever */
void mss_start_sample(MssSample *s);                           /* from the beginning */
void mss_end_sample(MssSample *s);
int mss_sample_status(const MssSample *s);
int32_t mss_sample_volume(const MssSample *s);

/* HSTREAM: a WAV file (RIFF, PCM 8/16-bit, mono/stereo) streamed from the file layer in chunks. */
MssStream *mss_open_stream(const char *rel);                   /* AIL_open_stream; NULL if missing / not WAV */
void mss_close_stream(MssStream *st);
void mss_start_stream(MssStream *st);                          /* from the current position */
void mss_pause_stream(MssStream *st, bool pause);
void mss_service_stream(MssStream *st);                        /* refills its buffer if needed */
void mss_set_stream_volume(MssStream *st, int32_t vol);
int32_t mss_stream_volume(const MssStream *st);               /* -1 for NULL */
void mss_set_stream_loop_count(MssStream *st, int32_t n);      /* default 1; 0 = forever */
void mss_set_stream_position(MssStream *st, int32_t pos);     /* bytes into the data, clamped (not aligned) */
int32_t mss_stream_position(const MssStream *st);             /* bytes played, -1 for NULL */
int mss_stream_status(const MssStream *st);                    /* -1 NULL / read error, else sample status */
/* AIL_stream_info: sample rate, format, data length in bytes; any pointer may be NULL. */
void mss_stream_info(const MssStream *st, int32_t *rate, int32_t *format, int32_t *length);

/* Timer service: AIL_register_timer / AIL_set_timer_frequency / AIL_start_timer / AIL_stop_timer /
   AIL_release_timer_handle. Callbacks run from mss_render (or mss_advance) at the timer's rate in rendered
   time. Returns -1 when all handles are used. */
typedef void (*MssTimerFn)(void *user);
int mss_register_timer(MssTimerFn fn, void *user);
void mss_set_timer_frequency(int t, int32_t hz);
void mss_start_timer(int t);
void mss_stop_timer(int t);
void mss_release_timer(int t);

/* Mixes `frames` stereo frames (interleaved float, -1..1) at MSS_RATE and advances the clock and timers.
   NULL out = advance without output. */
void mss_render(float *out, unsigned frames);
