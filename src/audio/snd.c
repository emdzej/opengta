/* The low-level sound module 0x471bc0-0x473430: the Miles handles, the sample banks and the one-shot /
   voice / stream players the game-level module (sound.c) and the music module (music.c) use.

   Banks: GTADATA/AUDIO/LEVELnnn.RAW is read whole into the start of one buffer (at most 1 MB) and its .SDT
   gives 131 {offset, length, rate} records; VOCALCOM.RAW stays open and each voice is read into the
   buffer at +1 MB when played. The handles a mode allocates:
     game (Snd_StartupGame): 10 loops, 3 positional one-shots, 1 "3D" one-shot, 1 menu / scanner,
       1 voice, 1 talk - all 8-bit unsigned mono (LEVEL001..003, VOCALCOM);
     frontend (Snd_StartupFrontend): 4 stereo + 4 mono 16-bit signed (LEVEL000).
   Calls through a handle the current mode didn't allocate are no-ops (Miles ignores NULL handles). */
#include "snd_internal.h"
#include "exe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

SndLow snd = { .jitter_up = true, .sfx_volume = 64, .stream_volume = 100, .level_bank = -1 };

/* Never written by the exe (bss): Snd_RestoreVolumes restores these, i.e. zero. 0x771068 (10 loops),
   0x770ff0 (3 shots), 0x770994 (3D), 0x771004 (menu in bits 8..15, voice in 0..7). */
static const uint8_t saved_loop_vol[10], saved_shot_vol[3];
static const uint32_t saved_3d_vol, saved_menu_voice_vol;

static void fatal(const char *file, const char *what)
{
    /* Error_Fatal(-1 / -23, 0x5b, ..) in the original */
    fprintf(stderr, "sound: %s: %s\n", file, what);
}

/* __ftol of an int times a float constant (the x87 product is rounded to double precision). */
static int32_t ftol_f(int32_t v, float k) { return (int32_t)((double)v * (double)k); }

/* The random pitch offset of 0x4728d0 / 0x472a30 / 0x472c60 / 0x472e00: rand() % (rate / 8), alternately
   up and down (0x4b3118 flips each time). rand() is only drawn for a non-zero rate. */
static int32_t jitter(uint32_t rate)
{
    if (!rate) return 0;
    uint32_t r = (uint32_t)snd_rand() % (rate >> 3);
    int32_t d = snd.jitter_up ? (int32_t)r : -(int32_t)r;
    snd.jitter_up = !snd.jitter_up;
    return d;
}

static const uint8_t *level_data(int n) { return snd.buffer + snd.level[n].off; }

static bool valid_level(int n) { return n >= 0 && n < SND_LEVEL_ENTRIES; }

/* Snd_FreeBuffer 0x471bc0 */
void Snd_FreeBuffer(void)
{
    if (snd.locked) {
        free(snd.buffer);
        snd.buffer = NULL;
        snd.locked = false;
    }
}

static void lock_buffer(void)
{
    if (!snd.locked) {
        snd.buffer = calloc(1, SND_BUFFER_SIZE);       /* AIL_mem_alloc_lock(0x10fde8); fatal -7 if NULL */
        if (!snd.buffer) { fatal("buffer", "out of memory"); return; }
        snd.locked = true;
    }
}

static MssSample *new_handle(int format, unsigned flags)
{
    MssSample *s = mss_allocate_sample();
    mss_init_sample(s);
    mss_set_sample_type(s, format, flags);
    return s;
}

static void forget_handles(void)
{
    memset(snd.loop, 0, sizeof snd.loop);
    memset(snd.shot, 0, sizeof snd.shot);
    memset(snd.sfx_stereo, 0, sizeof snd.sfx_stereo);
    memset(snd.sfx_mono, 0, sizeof snd.sfx_mono);
    snd.pos3d = snd.menu = snd.voice = snd.talk = NULL;
    snd.stream = NULL;
}

/* Snd_StartupGame 0x471be0. AIL_startup, AIL_set_preference(DIG_MIXER_CHANNELS, 19),
   AIL_set_preference(DIG_USE_WAVEOUT, 0), AIL_waveOutOpen(22050 Hz, 2 channels, 8 bits) (if the
   DirectSound device is "Emulated" it reopens with waveOut: the same here). Re-enables sfx. */
void Snd_StartupGame(void)
{
    lock_buffer();
    if (snd.running || !snd.locked) return;
    snd.running = true;
    Snd_SetSfxEnabled(true);
    mss_startup(19);
    for (int i = 0; i < 10; i++) snd.loop[i] = new_handle(MSS_MONO_8, 0);
    for (int i = 0; i < 3; i++) snd.shot[i] = new_handle(MSS_MONO_8, 0);
    snd.pos3d = new_handle(MSS_MONO_8, 0);
    snd.menu = new_handle(MSS_MONO_8, 0);
    mss_set_sample_volume(snd.menu, ftol_f(snd.sfx_volume, 0.71f));
    snd.voice = new_handle(MSS_MONO_8, 0);
    mss_set_sample_volume(snd.voice, snd.sfx_volume);
    snd.talk = new_handle(MSS_MONO_8, 0);
    mss_set_sample_volume(snd.talk, ftol_f(snd.sfx_volume, 0.87f));
    mss_set_sample_loop_count(snd.talk, 1);
}

/* Snd_ShutdownSamples 0x471e80 ("AIL shutdown called from SAMPMAN") */
void Snd_ShutdownSamples(void)
{
    if (snd.running) snd.running = false;
    mss_shutdown(false);
    forget_handles();
}

/* Snd_StartupFrontend 0x471f10: as Snd_StartupGame with 12 channels and the 8 frontend handles
   (LEVEL000 is 16-bit). */
void Snd_StartupFrontend(void)
{
    lock_buffer();
    if (snd.running || !snd.locked) return;
    snd.running = true;
    Snd_SetSfxEnabled(true);
    mss_startup(12);
    for (int i = 0; i < 4; i++) {
        snd.sfx_stereo[i] = new_handle(MSS_STEREO_16, MSS_PCM_SIGN);
        mss_set_sample_loop_count(snd.sfx_stereo[i], 1);
        mss_set_sample_volume(snd.sfx_stereo[i], ftol_f(snd.sfx_volume, 0.79f));
    }
    for (int i = 0; i < 4; i++) {
        snd.sfx_mono[i] = new_handle(MSS_MONO_16, MSS_PCM_SIGN);
        mss_set_sample_loop_count(snd.sfx_mono[i], 1);
        mss_set_sample_volume(snd.sfx_mono[i], ftol_f(snd.sfx_volume, 0.79f));
    }
}

/* Snd_Shutdown 0x472110. The original busy-waits a second of AIL_ms_count before releasing the handles
   and shutting Miles down; here what is still playing is left to finish (mss_shutdown(true)) and the
   game-level module reports the wait (Snd_FrontendDraining). */
void Snd_Shutdown(void)
{
    snd.running = false;
    mss_shutdown(true);
    forget_handles();
}

/* Snd_ProbeDevice 0x472180: AIL_startup, one channel, opens the device (and leaves it open). */
bool Snd_ProbeDevice(void)
{
    mss_startup(1);
    return true;
}

/* Snd_CloseDevice 0x472260 */
void Snd_CloseDevice(void)
{
    mss_shutdown(false);
}

/* Snd_SetMasterVolume 0x472280. The argument is the sfx level byte (table 0x4ab050). Sfx level 0 with
   music on sets the digital master to music * 0.8 and disables the sfx (nothing re-enables them here);
   otherwise the master is 108 and the level becomes the sample volume. */
void Snd_SetMasterVolume(int sfx)
{
    if (!snd.running) return;
    int8_t level = (int8_t)sfx;
    if (sndg.music_vol > 0 && level == 0) {
        mss_set_master_volume(ftol_f(sndg.music_vol, 0.8f));
        Snd_SetSfxEnabled(false);
        return;
    }
    mss_set_master_volume(0x6c);
    snd.sfx_volume = level;
}

/* Snd_GetSampleRate 0x4722f0 */
int32_t Snd_GetSampleRate(int n) { return valid_level(n) ? (int32_t)snd.level[n].rate : 0; }

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* fread(table, 12, max, f): reads the whole records the file has, leaves the rest as they were. */
static bool read_sdt(const char *rel, SndEntry *t, int max)
{
    VfsFile *f = vfs_open(rel);
    if (!f) { fatal(rel, "missing"); return false; }
    uint8_t rec[12];
    for (int i = 0; i < max; i++) {
        if (vfs_read_at(f, (uint64_t)i * 12, rec, 12) != 12) break;
        t[i] = (SndEntry){ rd32(rec), rd32(rec + 4), rd32(rec + 8) };
    }
    vfs_close(f);
    return true;
}

/* Snd_LoadLevelBank 0x472300: "..\GTADATA\AUDIO\LEVEL%03d" + .RAW / .SDT (0x4b31c0). Level 0 is the
   frontend's; in game Snd_InitGame loads 1 and Game_Init then Snd_Reset(style number), so NYC, San
   Andreas and Vice City use LEVEL001, 002 and 003 (the CMP header's sample byte is 0 for all three and
   isn't used). LEVEL000.SDT has only 16 records: the other 115 entries keep the previous bank's values. */
bool Snd_LoadLevelBank(int n)
{
    if (!snd.running) return false;
    char raw[48], sdt[48];
    snprintf(raw, sizeof raw, "GTADATA/AUDIO/LEVEL%03d.RAW", n & 0xff);
    snprintf(sdt, sizeof sdt, "GTADATA/AUDIO/LEVEL%03d.SDT", n & 0xff);
    VfsFile *f = vfs_open(raw);
    if (!f) { fatal(raw, "missing"); return false; }
    uint64_t size = vfs_file_size(f);
    if (size > SND_LEVEL_MAX) {                        /* Error_Fatal(-23, .., size - 1 MB) */
        vfs_close(f);
        fatal(raw, "larger than 1 MB");
        return false;
    }
    int64_t got = vfs_read_at(f, 0, snd.buffer, (size_t)size);
    vfs_close(f);
    if (got != (int64_t)size) { fatal(raw, "read error"); return false; }
    if (!read_sdt(sdt, snd.level, SND_LEVEL_ENTRIES)) return false;
    snd.level_bank = n & 0xff;
    return true;
}

int Snd_LevelBank(void) { return snd.level_bank; }
bool Snd_Running(void) { return snd.running; }

/* Snd_OpenVocalBank 0x472470: "..\GTADATA\AUDIO\VOCALCOM" (0x4b31dc). The .RAW stays open. */
bool Snd_OpenVocalBank(void)
{
    if (!snd.running) return false;
    if (snd.vocal_file) vfs_close(snd.vocal_file);     /* (the original leaks the old FILE) */
    snd.vocal_file = vfs_open("GTADATA/AUDIO/VOCALCOM.RAW");
    if (!snd.vocal_file) { fatal("GTADATA/AUDIO/VOCALCOM.RAW", "missing"); return false; }
    return read_sdt("GTADATA/AUDIO/VOCALCOM.SDT", snd.vocal, SND_VOCAL_ENTRIES);
}

/* Snd_CloseVocalBank 0x4725a0 */
void Snd_CloseVocalBank(void)
{
    if (snd.running && snd.vocal_file) {
        vfs_close(snd.vocal_file);
        snd.vocal_file = NULL;
    }
}

/* The pitch and volume helpers of the loop channels. */
static int32_t iabs(int32_t x) { return x < 0 ? -x : x; }
int32_t Snd_CalcRateA(int32_t x) { return (iabs(x) + 60) * 100; }                     /* 0x4725c0 trains */
int32_t Snd_CalcEngineRate(int32_t x) { return x < 0 ? 22050 - x * 400 : x * 1400 + 22050; }   /* 0x4725e0 */

/* Snd_GearRate 0x472620: the 13-entry rate table 0x4b30dc (18000 .. 77000), indexed by speed: reverse by
   -speed (at most 12), forward by speed / 10 + speed % 10 + 1 (the digit sum, not a gear: a quirk),
   at most 12. */
int32_t Snd_GearRate(int32_t x)
{
    int i;
    if (x == 0) i = 0;
    else if (x < 0) i = -x < 12 ? -x : 12;
    else {
        i = x / 10 + x % 10 + 1;
        if (i > 12) i = 12;
    }
    return (int32_t)exe_u32(0x4b30dc + 4u * (uint32_t)i);
}

int32_t Snd_CalcRateB(int32_t x)                    /* 0x472670 bus */
{
    if (x < 0) return (12 - x) * 500;
    if (x < 8) return x * 333 + 6000;
    if (x < 12) return x * 750;
    if (x < 18) return x * 500;
    return x * 333;
}

int32_t Snd_CalcRateC(int32_t x) { return (iabs(x) + 2) * 2000; }                     /* 0x4726d0 tank */

/* Snd_CalcVolume 0x4726f0: 40 when not moving forward, else min(speed * 1.5 + 40, 100). */
int32_t Snd_CalcVolume(int32_t x)
{
    if (x <= 0) return 0x28;
    double v = (double)x * 1.5 + 40.0;
    if (!(100.0 > v)) v = 100.0;
    return (int32_t)v;
}

int32_t Snd_CalcDelta(int32_t a, int32_t b) { return (a - b) / 3000; }               /* 0x472730 Doppler */

/* Snd_DistToVolume 0x472770: Manhattan distance (16.16) to volume: 127 within 1/8 block, then falling
   linearly to 0 at 0x3ffffff (16 blocks). */
int32_t Snd_DistToVolume(int32_t d)
{
    int32_t v = (0x3ffffff - iabs(d)) >> 19;
    return v >= 0x7f ? 0x7f : v;
}

int32_t Snd_DistToPan(int32_t dx) { return (0x3ffffff - dx) >> 20; }                 /* 0x472790 */

/* The 10 loop channels 0x4727a0-0x472870 */
static MssSample *loop_ch(int ch) { return ch >= 0 && ch < 10 ? snd.loop[ch] : NULL; }
void Snd_LoopSetRate(int ch, int32_t rate) { mss_set_sample_playback_rate(loop_ch(ch), rate); }
void Snd_LoopSetPan(int ch, int32_t pan) { mss_set_sample_pan(loop_ch(ch), pan); }
void Snd_LoopSetVolume(int ch, int32_t vol) { mss_set_sample_volume(loop_ch(ch), vol); }

void Snd_LoopSetSample(int ch, int n)               /* 0x472800: loops forever */
{
    if (!valid_level(n) || !snd.buffer) return;
    mss_set_sample_address(loop_ch(ch), level_data(n), snd.level[n].len);
    mss_set_sample_loop_count(loop_ch(ch), 0);
}

void Snd_LoopStart(int ch) { mss_start_sample(loop_ch(ch)); }
void Snd_LoopStop(int ch) { mss_end_sample(loop_ch(ch)); }

/* The 3 positional one-shots 0x472890-0x4729d0 */
static MssSample *shot_ch(int ch) { return ch >= 0 && ch < 3 ? snd.shot[ch] : NULL; }
void Snd_ShotSetPan(int ch, int32_t pan) { mss_set_sample_pan(shot_ch(ch), pan); }
void Snd_ShotSetVolume(int ch, int32_t vol) { mss_set_sample_volume(shot_ch(ch), vol); }

/* Snd_ShotSetSample 0x4728d0: plays once with the random pitch offset. The original tests the channel
   number (< 0x4e, always true) where the sample number was surely meant. */
void Snd_ShotSetSample(int ch, int n)
{
    if (!valid_level(n) || !snd.buffer) return;
    mss_set_sample_address(shot_ch(ch), level_data(n), snd.level[n].len);
    mss_set_sample_loop_count(shot_ch(ch), 1);
    if ((unsigned)ch < 0x4e) {
        int32_t d = jitter(snd.level[n].rate);
        mss_set_sample_playback_rate(shot_ch(ch), (int32_t)snd.level[n].rate + d);
    } else {
        mss_set_sample_playback_rate(shot_ch(ch), (int32_t)snd.level[n].rate);
    }
}

bool Snd_ShotIsPlaying(int ch) { return mss_sample_status(shot_ch(ch)) == MSS_PLAYING; }

/* Snd_ShotStart 0x4729a0: sets the volume to the sfx volume, replacing the distance volume Snd_Play3D
   has just set (so positional one-shots are only panned: a quirk of the original). */
void Snd_ShotStart(int ch)
{
    mss_set_sample_volume(shot_ch(ch), snd.sfx_volume);
    mss_start_sample(shot_ch(ch));
}

void Snd_ShotStop(int ch) { mss_end_sample(shot_ch(ch)); }

void Snd_StopAllLoops(void) { for (int i = 0; i < 10; i++) mss_end_sample(snd.loop[i]); }     /* 0x4729f0 */
void Snd_StopAllShots(void) { for (int i = 0; i < 3; i++) mss_end_sample(snd.shot[i]); }      /* 0x472a10 */

/* Snd_PlayPositional 0x472a30 on the "3D" handle: volume half the distance volume (halved again if the
   point is covered at the listener's height), pan by x. Unlike Snd_Play3D the volume sticks. */
void Snd_PlayPositional(int32_t x, int32_t y, int n)
{
    int32_t lx, ly, lz;
    snd_listener(&lx, &ly, &lz);
    int32_t dx = x - lx;
    int32_t d = iabs(y - ly) + iabs(dx);
    if (d >= 0x3ffffff) return;
    int32_t v = (0x3ffffff - iabs(d)) >> 19;
    if (v >= 0x7f) v = 0x7f;
    uint32_t vol = (uint32_t)ftol_f(v, 0.5f);
    if (snd_covered(x, y, lz)) vol >>= 1;
    int32_t pan = (dx + 0x3ffffff) >> 20;
    if (!valid_level(n) || !snd.buffer) return;
    int32_t j = jitter(snd.level[n].rate);
    mss_set_sample_address(snd.pos3d, level_data(n), snd.level[n].len);
    mss_set_sample_loop_count(snd.pos3d, 1);
    mss_set_sample_playback_rate(snd.pos3d, (int32_t)snd.level[n].rate + j);
    mss_set_sample_pan(snd.pos3d, pan);
    mss_set_sample_volume(snd.pos3d, (int32_t)vol);
    mss_start_sample(snd.pos3d);
}

/* Snd_PlayMenuSample 0x472b90 / Snd_PlayMenuSampleLow 0x472d90: the menu handle, the police scanner's
   words, at a fixed 6000 / 5000 Hz whatever the sample's rate. */
static void menu_sample(int n, int32_t rate)
{
    if (!valid_level(n) || !snd.buffer) return;
    mss_set_sample_address(snd.menu, level_data(n), snd.level[n].len);
    mss_set_sample_loop_count(snd.menu, 1);
    mss_set_sample_playback_rate(snd.menu, rate);
    mss_set_sample_volume(snd.menu, snd.sfx_volume);
    mss_start_sample(snd.menu);
}
void Snd_PlayMenuSample(int n) { menu_sample(n, 6000); }
void Snd_PlayMenuSampleLow(int n) { menu_sample(n, 5000); }

void Snd_VoiceSetPan(int32_t pan) { mss_set_sample_pan(snd.voice, pan); }            /* 0x472c00 */
void Snd_VoiceSetVolume(int32_t vol) { mss_set_sample_volume(snd.voice, vol); }      /* 0x472c20 */
bool Snd_VoiceIsPlaying(void) { return mss_sample_status(snd.voice) == MSS_PLAYING; }  /* 0x472c40 */

/* Snd_VoicePlay 0x472c60: reads VOCALCOM sample n into the buffer at +1 MB (replacing the one that may
   still be playing from there) and plays it once at the sfx volume (so the volume callers set before is
   lost: a quirk). Samples from 0x1e on get the random pitch offset. */
void Snd_VoicePlay(int n)
{
    if (n < 0 || n >= SND_VOCAL_ENTRIES) return;
    uint8_t *dst = snd.buffer ? snd.buffer + SND_LEVEL_MAX : NULL;
    uint32_t len = snd.vocal[n].len;
    if (len > SND_BUFFER_SIZE - SND_LEVEL_MAX) len = SND_BUFFER_SIZE - SND_LEVEL_MAX;   /* (the original would overrun) */
    if (snd.running) {
        if (!snd.vocal_file || !dst ||
            vfs_read_at(snd.vocal_file, snd.vocal[n].off, dst, len) != (int64_t)len) {
            fatal("GTADATA/AUDIO/VOCALCOM.RAW", "read error");
            return;
        }
    }
    if (!dst) return;
    mss_set_sample_address(snd.voice, dst, snd.vocal[n].len > len ? len : snd.vocal[n].len);
    mss_set_sample_loop_count(snd.voice, 1);
    int32_t rate = (int32_t)snd.vocal[n].rate;
    if (n >= 0x1e) rate += jitter(snd.vocal[n].rate);
    mss_set_sample_playback_rate(snd.voice, rate);
    mss_set_sample_volume(snd.voice, snd.sfx_volume);
    mss_start_sample(snd.voice);
}

/* Snd_PlaySfx 0x472e00, the frontend's sounds (LEVEL000): samples 0..2 on the next of the 4 stereo
   handles with the random pitch offset, centred; the others on the next of the 4 mono handles at their
   own rate, with a random pan 64..95 (never left of centre). */
void Snd_PlaySfx(int n)
{
    if (!valid_level(n) || !snd.buffer) return;
    uint32_t rate = snd.level[n].rate;
    const uint8_t *addr = level_data(n);
    int32_t j = jitter(rate);
    if (n > 2) {
        snd.rr_mono = (uint8_t)((snd.rr_mono + 1) & 3);
        MssSample *s = snd.sfx_mono[snd.rr_mono];
        mss_set_sample_address(s, addr, snd.level[n].len);
        mss_set_sample_loop_count(s, 1);
        mss_set_sample_playback_rate(s, (int32_t)rate);
        mss_set_sample_pan(s, (snd_rand() & 31) + 0x40);
        mss_set_sample_volume(s, snd.sfx_volume);
        mss_start_sample(s);
        return;
    }
    snd.rr_stereo = (uint8_t)((snd.rr_stereo + 1) & 3);
    MssSample *s = snd.sfx_stereo[snd.rr_stereo];
    mss_set_sample_address(s, addr, snd.level[n].len);
    mss_set_sample_loop_count(s, 1);
    mss_set_sample_playback_rate(s, (int32_t)rate + j);
    mss_set_sample_pan(s, 0x40);
    mss_set_sample_volume(s, snd.sfx_volume);
    mss_start_sample(s);
}

/* Snd_AnySfxPlaying 0x472fe0 */
bool Snd_AnySfxPlaying(void)
{
    for (int i = 0; i < 4; i++)
        if (mss_sample_status(snd.sfx_mono[i]) == MSS_PLAYING) return true;
    for (int i = 0; i < 4; i++)
        if (mss_sample_status(snd.sfx_stereo[i]) == MSS_PLAYING) return true;
    return false;
}

bool Snd_MenuIsPlaying(void) { return mss_sample_status(snd.menu) == MSS_PLAYING; }  /* 0x473050 */
void Snd_MenuStop(void) { mss_end_sample(snd.menu); }                                 /* 0x473070 */
void Snd_VoiceStop(void) { mss_end_sample(snd.voice); }                               /* 0x473080 */

/* Snd_TalkPlay 0x473090: a level sample on the talk handle at its rate, volume sfx * 0.55. */
void Snd_TalkPlay(int n)
{
    if (!snd.running || !valid_level(n) || !snd.buffer) return;
    mss_end_sample(snd.talk);
    mss_set_sample_address(snd.talk, level_data(n), snd.level[n].len);
    mss_set_sample_playback_rate(snd.talk, (int32_t)snd.level[n].rate);
    mss_set_sample_volume(snd.talk, ftol_f(snd.sfx_volume, 0.55f));
    mss_start_sample(snd.talk);
}

/* Snd_PlayMumble 0x473120, babble for a message: from a random sample a in 0x4f..0x5d it plays the bank
   bytes up to the start of a random sample b in 0x6d..0x7b (straight through all the samples between),
   a third of that for messages under 40 characters, two thirds under 90, at 16000 Hz, volume sfx * 0.47. */
void Snd_PlayMumble(const char *text)
{
    if (!snd.running || !snd.buffer) return;
    int a = snd_rand() % 15 + 0x4f;
    int b = snd_rand() % 15 + 0x6d;
    uint32_t len = snd.level[b].off - snd.level[a].off;
    size_t n = text ? strlen(text) : 0;
    if (n < 0x28) len /= 3;
    else if (n < 0x5a) len = len / 3 * 2;
    if (snd.level[a].off + (uint64_t)len > SND_LEVEL_MAX) len = 0;   /* (wrapped: b before a can't happen) */
    mss_set_sample_address(snd.talk, level_data(a), len);
    mss_set_sample_loop_count(snd.talk, 1);
    mss_set_sample_playback_rate(snd.talk, 16000);
    mss_set_sample_volume(snd.talk, ftol_f(snd.sfx_volume, 0.47f));
    mss_start_sample(snd.talk);
}

/* Snd_MuteAll 0x473200 (pause) */
void Snd_MuteAll(void)
{
    for (int i = 0; i < 10; i++) mss_set_sample_volume(snd.loop[i], 0);
    for (int i = 0; i < 3; i++) mss_set_sample_volume(snd.shot[i], 0);
    mss_set_sample_volume(snd.pos3d, 0);
    mss_set_sample_volume(snd.menu, 0);
    mss_set_sample_volume(snd.voice, 0);
}

/* Snd_RestoreVolumes 0x473260: restores volumes from globals nothing ever saves to, i.e. sets them all to
   0 (the loops come back as Snd_UpdateLoopChannels slews them, the others when next started). */
void Snd_RestoreVolumes(void)
{
    for (int i = 0; i < 10; i++) mss_set_sample_volume(snd.loop[i], saved_loop_vol[i]);
    for (int i = 0; i < 3; i++) mss_set_sample_volume(snd.shot[i], saved_shot_vol[i]);
    mss_set_sample_volume(snd.pos3d, (int32_t)(saved_3d_vol & 0xff));
    mss_set_sample_volume(snd.menu, (int32_t)(saved_menu_voice_vol >> 8 & 0xff));
    mss_set_sample_volume(snd.voice, (int32_t)(saved_menu_voice_vol & 0xff));
}

/* Snd_MusicPlay 0x4732f0: streams "..\GTADATA\AUDIO\%d.WAV" (0x4b31f8; 0..5, the cutscene / briefing
   speech, 22050 Hz 16-bit stereo) once at the stream volume 0x4b3110. A stream still open from before is
   left playing (the handle is overwritten, as in the original). */
void Snd_MusicPlay(int n)
{
    if (!snd.running) return;
    char rel[40];
    snprintf(rel, sizeof rel, "GTADATA/AUDIO/%d.WAV", n & 0xff);
    snd.stream = mss_open_stream(rel);
    if (!snd.stream) return;
    int32_t rate = 0, len = 0;
    mss_stream_info(snd.stream, &rate, NULL, &len);
    snd.stream_length = rate ? (int32_t)((int64_t)len * 500 / rate) : 0;   /* (the original divides by 0 then) */
    snd.stream_started = mss_ms_count();
    mss_set_stream_volume(snd.stream, snd.stream_volume & 0xff);
    mss_start_stream(snd.stream);
}

void Snd_MusicService(void)                         /* 0x4733a0 */
{
    if (snd.running && snd.stream) mss_service_stream(snd.stream);
}

void Snd_MusicClose(void)                           /* 0x4733c0 (the handle is not cleared) */
{
    if (snd.running) mss_close_stream(snd.stream);
}

/* Snd_MusicGetPos 0x4733e0: milliseconds played (bytes * 250 / 22050, right for 22050 Hz 16-bit
   stereo), -1 once it isn't playing, -2 without Miles. */
int Snd_MusicGetPos(void)
{
    if (!snd.running) return -2;
    if (mss_stream_status(snd.stream) != MSS_PLAYING) return -1;
    return (int)((int64_t)mss_stream_position(snd.stream) * 250 / 22050);
}

void Snd_SetStreamVolume(int vol) { snd.stream_volume = (snd.stream_volume & ~0xff) | (vol & 0xff); }  /* 0x473430 */
