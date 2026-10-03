/* Movie_PlayIntro 0x44b160 as a frame step (intro.h, docs/movie.md).

   What the original does, with the SMACKW32.DLL calls it makes (imported by ordinal):
   - Input_FlushKeys; Snd_ProbeDevice 0x472180 (AIL_startup + waveOutOpen: the Miles driver 0x771000)
     and, if that worked, SmackSoundUseMSS(driver): the movie's sound goes through Miles;
   - SmackOpen("..\gtadata\movie.smk" (pointer 0x4b1dc8), SMACKTRACKS 0xfe000, -1); on failure
     Snd_CloseDevice and return;
   - Gfx_SetVideoMode(-2): the 8-bit palettised mode (MGL mode 0x13), display and memory DCs; if the mode
     can't be set (0x4ac06c) the movie is closed unplayed;
   - SmackToBuffer(movie, 0, 20, pitch, height, the memory DC's pixels, 0): frames are unpacked at
     (0, 20) of the memory DC;
   - for each frame: Gfx_SetPalette 0x414d40 with the movie's palette if it changed; SmackDoFrame; every
     dirty rectangle (SmackToBufferRect) stretched x2 to the display (MGL_stretchBltCoord: (x, y, w, h)
     -> (2x, 2y, 2w, 2h)); SmackNextFrame; SmackWait until the frame's time is up; one Input_GetKey:
     the loop goes on while there was no event or the event was 0x38 (Alt pressed) and frames remain;
   - the palette is zeroed, the display cleared, the memory DC copied to the display 1:1 (the last
     frame, unscaled, in the top-left corner: invisible under the black palette), the black palette set;
     SmackClose, Snd_CloseDevice.

   So the 320 x 200 movie fills (0, 40)-(639, 439) of a 640 x 480 screen, each pixel 2 x 2. Everything
   else is palette index 0 (cleared display and memory DC), i.e. the movie palette's colour 0. Only Alt
   *presses* are ignored: Input_GetKey also returns releases (+0x80), so letting go of Alt (0xb8), or of
   any key held when the movie started, ends it, as in the original.

   Timing: the original waits for SmackWait between frames (40 ms for MOVIE.SMK: 25 fps, synchronised to
   the sound). Here each call is one frontend frame of 35 ms; frame n is shown on the first call whose
   time (35 ms * calls before it) has reached n * 40 ms, and the key check that follows a frame happens
   on the call that shows the next one (or would). The sound: SMACKW32 feeds the decoded audio to a
   Miles sample; here the whole track is decoded at the start and played as one Miles sample (16-bit
   signed, the track's rate, volume 127, centred) that starts with frame 0: the same samples at the same
   times, without the double buffering. */
#include "movie/intro.h"
#include "movie/smacker.h"
#include "audio/audio.h"
#include "audio/mss.h"
#include <stdlib.h>
#include <string.h>

enum { STEP_US = 35000, SCREEN_W = 640, SCREEN_H = 480, MOVIE_TOP = 20, QUEUE = 64 };

static struct {
    Smacker *smk;
    bool playing;
    uint32_t next;           /* the next frame to show */
    int shown;               /* the frame on screen (-1 none) */
    uint64_t clock_us;       /* time of this step since the start */
    uint8_t *pcm;            /* the soundtrack (one track) */
    uint32_t pcm_len;
    MssSample *sample;
    uint16_t queue[QUEUE];   /* the key events not read yet (MGL's event queue) */
    int nqueue;
} M;

/* SmackSoundUseMSS + the first track's audio: decoded whole, played from memory. */
static void open_sound(void)
{
    Smacker *s = M.smk;
    int t = 0;
    while (t < SMK_TRACKS && !s->track[t].present) t++;
    if (t == SMK_TRACKS || s->track[t].codec) return;
    SmkTrack *tr = &s->track[t];
    uint32_t cap = 0;
    for (uint32_t i = 0; i < s->nphys; i++) {
        if (!smk_frame_audio(s, i) || !tr->len) continue;
        if (M.pcm_len + tr->len > cap) {
            uint32_t ncap = (M.pcm_len + tr->len) * 2;
            uint8_t *n = realloc(M.pcm, ncap);
            if (!n) return;
            M.pcm = n;
            cap = ncap;
        }
        memcpy(M.pcm + M.pcm_len, tr->buf, tr->len);
        M.pcm_len += tr->len;
    }
    if (!M.pcm_len || !(M.sample = mss_allocate_sample())) return;
    mss_init_sample(M.sample);
    int fmt = (tr->stereo ? MSS_STEREO_8 : MSS_MONO_8) | (tr->bits16 ? 1 : 0);
    mss_set_sample_type(M.sample, fmt, tr->bits16 ? MSS_PCM_SIGN : 0);   /* 8-bit Smacker audio is unsigned */
    mss_set_sample_address(M.sample, M.pcm, M.pcm_len);
    mss_set_sample_playback_rate(M.sample, (int32_t)tr->rate);
    mss_set_sample_volume(M.sample, 127);
    mss_set_sample_pan(M.sample, 64);
}

static bool enabled;

void movie_intro_set_enabled(bool on) { enabled = on; }

/* Movie_PlayIntro 0x44b160, up to the loop */
bool movie_intro_start(void)
{
    movie_intro_stop();
    if (!enabled) return false;
    M.nqueue = 0;                                  /* Input_FlushKeys 0x414a70 */
    bool sound = Snd_ProbeDevice();
    if (!(M.smk = smk_open("GTADATA/MOVIE.SMK", NULL, 0))) {
        Snd_CloseDevice();
        return false;
    }
    if (sound) open_sound();
    M.playing = true;
    M.next = 0;
    M.shown = -1;
    M.clock_us = 0;
    return true;
}

static void draw(Surface *s, bool black)
{
    const Smacker *k = M.smk;
    uint32_t pal[256];
    for (int i = 0; i < 256; i++)
        pal[i] = black ? surface_rgb(0, 0, 0) : surface_rgb(k->pal[3 * i], k->pal[3 * i + 1], k->pal[3 * i + 2]);
    for (int y = 0; y < s->h; y++) {
        uint32_t *row = s->px + (size_t)y * s->stride;
        int my = y / 2 - MOVIE_TOP;
        if (my < 0 || my >= (int)k->h || M.shown < 0) {
            for (int x = 0; x < s->w; x++) row[x] = pal[0];
            continue;
        }
        const uint8_t *src = k->video + (size_t)my * k->stride;
        for (int x = 0; x < s->w; x++) row[x] = x / 2 < (int)k->w ? pal[src[x / 2]] : pal[0];
    }
}

static void finish(Surface *s)
{
    if (s) draw(s, true);                          /* zeroed palette, cleared screen */
    movie_intro_stop();                            /* SmackClose, Snd_CloseDevice */
}

/* One pass of Movie_PlayIntro's frame loop (or of its SmackWait). */
bool movie_intro_step(const uint16_t *events, int nevents, Surface *s)
{
    if (!M.playing) return false;
    for (int i = 0; i < nevents; i++)
        if (M.nqueue < QUEUE) M.queue[M.nqueue++] = events[i];
    Smacker *k = M.smk;
    if (M.clock_us >= (uint64_t)M.next * k->frame_us) {   /* SmackWait is over */
        if (M.next > 0) {
            int key = 0;                                   /* Input_GetKey 0x414a80: one event */
            if (M.nqueue) {
                key = M.queue[0];
                memmove(M.queue, M.queue + 1, (size_t)--M.nqueue * sizeof *M.queue);
            }
            if ((key != 0 && key != 0x38) || M.next >= k->frames) {
                finish(s);
                return false;
            }
        }
        if (!smk_next_frame(k)) {                          /* SmackDoFrame (a broken frame: stop) */
            finish(s);
            return false;
        }
        if (M.next == 0 && M.sample) mss_start_sample(M.sample);
        M.shown = (int)M.next++;                           /* SmackNextFrame */
    }
    if (s) draw(s, false);
    M.clock_us += STEP_US;
    return true;
}

void movie_intro_stop(void)
{
    if (M.sample) mss_release_sample(M.sample);
    M.sample = NULL;
    bool was = M.smk != NULL;
    smk_close(M.smk);
    M.smk = NULL;
    free(M.pcm);
    M.pcm = NULL;
    M.pcm_len = 0;
    M.playing = false;
    if (was) Snd_CloseDevice();
}

bool movie_intro_playing(void) { return M.playing; }
int movie_intro_frame(void) { return M.shown; }
