/* The Miles Sound System subset GTA uses (see mss.h), modelled on MSS32.DLL V3.6B's software mixer:

   - Per voice, MSS computes an effective volume v = master * volume / 127 (both clamped to 0..127) and a
     pan pair from a 128-entry table T[p] = min(2p, 128) (T[63] = 128): left = T[127 - pan], right = T[pan],
     i.e. full on both sides at the centre and a linear fade towards one side.
       8-bit data goes through a 256-entry table per voice: base = (sample - 128) * 2 * (v + 1) (v + 1 only
       when v > 0), then left = (base * T[127 - pan]) >> 7, right = (base * T[pan]) >> 7.
       16-bit data: scale = T[...] * v / 127, out = (sample * scale) >> 7.
     (MSS32.DLL 0x2000c420 builds these; the inner loops are at 0x20012fdd..0x20013b12.)
   - Resampling: step = rate * 65536 / 22050 (16.16, floor); within DIG_RESAMPLING_TOLERANCE (655 / 65536,
     MSS's default) of 1.0 the voice is not resampled at all and plays one input frame per output frame.
     Otherwise the source is point sampled: read, then add the 16-bit fraction (kept in the top half of a
     32-bit accumulator that starts at 0x80000000) with carry into the position (0x20013c7f).
   - Voices add into a 32-bit build buffer, which is clipped to 16 bits; the 8-bit output keeps the high
     byte (0x20012af9 / 0x20012b52).
   Deviation: MSS restarts the fraction accumulator at 0x80000000 at every mix buffer (its fragment
   size is set by the driver); here it is carried over, so the result does not depend on how the caller
   splits the rendering into calls. */
#include "mss.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_SAMPLES = 32, MAX_STREAMS = 4, MAX_TIMERS = 4 };
enum { RESAMPLE_TOLERANCE = 655 };       /* MSS default for preference 2 */
enum { STREAM_CHUNK = 0x10000 };         /* bytes read per refill */
enum { DEFAULT_VOLUME = 100 };           /* MSS default for preference 1 (DIG_DEFAULT_VOLUME) */

struct MssSample {
    bool allocated;
    bool orphan;                         /* left playing by mss_shutdown(true): mixed until done */
    uint8_t *owned;                      /* its copy of the data */
    int status;
    int format;
    unsigned flags;
    const uint8_t *data;
    uint32_t len;                        /* bytes */
    int32_t rate, volume, pan, loop_count;
    uint32_t pos;                        /* frames */
    uint32_t frac;                       /* 0x80000000 = half a frame */
    MssStream *stream;                   /* the stream feeding this voice, if any */
};

struct MssStream {
    bool used;
    MssSample *s;
    VfsFile *f;
    uint32_t data_off, data_len;         /* the WAV "data" chunk */
    int32_t rate, format;
    unsigned frame_bytes;
    uint32_t pos;                        /* bytes into the data (next byte to play) */
    int32_t loop_count;
    bool started, paused, error;
    bool have;                           /* cur holds the frame at the play position */
    bool ended;
    uint8_t cur[4];
    uint8_t *buf;                        /* STREAM_CHUNK bytes of data starting at buf_start */
    uint32_t buf_start, buf_len;
};

typedef struct {
    bool used, running;
    MssTimerFn fn;
    void *user;
    int32_t hz;
    uint64_t acc;                        /* hz * frames, a tick every MSS_RATE */
} Timer;

static struct {
    bool running;
    int channels;
    int32_t master;
    uint64_t frames;                     /* rendered since start-up: the clock */
    uint32_t drained_ms;                 /* when the last orphan finished */
    MssSample samples[MAX_SAMPLES];
    MssStream streams[MAX_STREAMS];
    Timer timers[MAX_TIMERS];
} M = { .master = 127 };

/* MSS32.DLL's pan table (0x200225d8): 0, 2, 4 .. 124, then 128 from index 63 on. */
static int pan_gain(int p) { return p < 63 ? 2 * p : 128; }

static int32_t asr(int32_t x, int n) { return x >= 0 ? x >> n : ~(~x >> n); }
static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ---- driver ---- */

void mss_startup(int channels)
{
    if (M.running) mss_shutdown(false);
    M.running = true;
    M.channels = channels < 1 ? 1 : channels > MAX_SAMPLES ? MAX_SAMPLES : channels;
    M.master = 127;
}

void mss_shutdown(bool drain)
{
    for (int i = 0; i < MAX_STREAMS; i++)
        if (M.streams[i].used) mss_close_stream(&M.streams[i]);
    for (int i = 0; i < MAX_SAMPLES; i++) {
        MssSample *s = &M.samples[i];
        if (drain && s->allocated && s->status == MSS_PLAYING && s->data && !s->stream && !s->orphan) {
            /* the caller's buffer will be reused (the next bank loads into it): keep a copy */
            uint8_t *copy = malloc(s->len);
            if (copy) {
                memcpy(copy, s->data, s->len);
                s->data = s->owned = copy;
                s->orphan = true;
                continue;
            }
        }
        if (!s->orphan) *s = (MssSample){0};
    }
    M.running = false;
}

bool mss_draining(void)
{
    for (int i = 0; i < MAX_SAMPLES; i++)
        if (M.samples[i].orphan) return true;
    return false;
}

uint32_t mss_drained_ms(void) { return M.drained_ms; }

bool mss_running(void) { return M.running; }
void mss_set_master_volume(int32_t vol) { M.master = vol; }
int32_t mss_master_volume(void) { return M.master; }
uint32_t mss_ms_count(void) { return (uint32_t)(M.frames * 1000 / MSS_RATE); }

/* ---- samples ---- */

void mss_init_sample(MssSample *s)
{
    if (!s) return;
    MssStream *st = s->stream;
    *s = (MssSample){ .allocated = true, .status = MSS_DONE, .format = MSS_MONO_8, .rate = 11025,
                      .volume = DEFAULT_VOLUME, .pan = 64, .loop_count = 1, .frac = 0x80000000u, .stream = st };
}

MssSample *mss_allocate_sample(void)
{
    if (!M.running) return NULL;
    int n = 0;
    for (int i = 0; i < MAX_SAMPLES; i++) n += M.samples[i].allocated && !M.samples[i].orphan;
    if (n >= M.channels) return NULL;
    for (int i = 0; i < MAX_SAMPLES; i++)
        if (!M.samples[i].allocated) {
            mss_init_sample(&M.samples[i]);
            M.samples[i].stream = NULL;
            return &M.samples[i];
        }
    return NULL;
}

void mss_release_sample(MssSample *s)
{
    if (s) *s = (MssSample){ .status = MSS_FREE };
}

void mss_set_sample_type(MssSample *s, int format, unsigned flags)
{
    if (s) { s->format = format & 3; s->flags = flags; }
}

void mss_set_sample_address(MssSample *s, const void *data, uint32_t len)
{
    if (!s) return;
    s->data = data;
    s->len = len;
    s->pos = 0;
    s->frac = 0x80000000u;
}

void mss_set_sample_playback_rate(MssSample *s, int32_t hz) { if (s) s->rate = hz; }
void mss_set_sample_volume(MssSample *s, int32_t vol) { if (s) s->volume = vol; }
void mss_set_sample_pan(MssSample *s, int32_t pan) { if (s) s->pan = pan; }
void mss_set_sample_loop_count(MssSample *s, int32_t n) { if (s) s->loop_count = n; }
int32_t mss_sample_volume(const MssSample *s) { return s ? s->volume : 0; }

void mss_start_sample(MssSample *s)
{
    if (!s || !s->allocated) return;
    s->pos = 0;
    s->frac = 0x80000000u;
    s->status = MSS_PLAYING;
}

void mss_end_sample(MssSample *s)
{
    if (s && s->allocated && s->status != MSS_FREE) s->status = MSS_DONE;
}

int mss_sample_status(const MssSample *s) { return s ? s->status : MSS_FREE; }

/* ---- streams ---- */

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

MssStream *mss_open_stream(const char *rel)
{
    if (!M.running || !rel) return NULL;
    MssStream *st = NULL;
    for (int i = 0; i < MAX_STREAMS && !st; i++)
        if (!M.streams[i].used) st = &M.streams[i];
    if (!st) return NULL;
    VfsFile *f = vfs_open(rel);
    if (!f) return NULL;
    /* RIFF/WAVE: walk the chunks for "fmt " and "data" (as MSS's 0x200153a0 does) */
    uint8_t h[12];
    uint64_t size = vfs_file_size(f);
    if (vfs_read_at(f, 0, h, 12) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) {
        vfs_close(f);
        return NULL;
    }
    int channels = 0, bits = 0;
    int32_t rate = 0;
    uint32_t data_off = 0, data_len = 0;
    for (uint64_t o = 12; o + 8 <= size;) {
        uint8_t c[24];
        if (vfs_read_at(f, o, c, 8) != 8) break;
        uint32_t cl = rd32(c + 4);
        if (!memcmp(c, "fmt ", 4) && cl >= 16 && vfs_read_at(f, o + 8, c + 8, 16) == 16) {
            if (rd16(c + 8) != 1) break;                 /* PCM only */
            channels = rd16(c + 10);
            rate = (int32_t)rd32(c + 12);
            bits = rd16(c + 22);
        } else if (!memcmp(c, "data", 4)) {
            data_off = (uint32_t)(o + 8);
            data_len = cl;
            break;
        }
        o += 8 + (uint64_t)cl + (cl & 1);
    }
    if (!data_off || (channels != 1 && channels != 2) || (bits != 8 && bits != 16)) {
        vfs_close(f);
        return NULL;
    }
    MssSample *s = mss_allocate_sample();             /* the stream's voice comes from the same pool */
    if (!s) { vfs_close(f); return NULL; }
    *st = (MssStream){ .used = true, .s = s, .f = f, .data_off = data_off, .data_len = data_len, .rate = rate,
                       .format = (channels == 2 ? 2 : 0) | (bits == 16 ? 1 : 0), .loop_count = 1 };
    st->frame_bytes = (unsigned)(channels * bits / 8);
    st->buf = malloc(STREAM_CHUNK);
    if (!st->buf) { mss_release_sample(s); vfs_close(f); *st = (MssStream){0}; return NULL; }
    s->stream = st;
    mss_set_sample_type(s, st->format, bits == 16 ? MSS_PCM_SIGN : 0);
    mss_set_sample_playback_rate(s, rate);
    return st;
}

void mss_close_stream(MssStream *st)
{
    if (!st || !st->used) return;
    mss_release_sample(st->s);
    vfs_close(st->f);
    free(st->buf);
    *st = (MssStream){0};
}

void mss_start_stream(MssStream *st)
{
    if (!st || !st->used) return;
    st->started = true;
    st->paused = false;
    st->s->status = MSS_PLAYING;
}

void mss_pause_stream(MssStream *st, bool pause)
{
    if (!st || !st->used || !st->started) return;
    st->paused = pause;
    st->s->status = pause ? MSS_STOPPED : MSS_PLAYING;
}

void mss_service_stream(MssStream *st) { (void)st; /* the mixer reads on demand */ }
void mss_set_stream_volume(MssStream *st, int32_t vol) { if (st && st->used) mss_set_sample_volume(st->s, vol); }
int32_t mss_stream_volume(const MssStream *st) { return st && st->used ? st->s->volume : -1; }
void mss_set_stream_loop_count(MssStream *st, int32_t n) { if (st && st->used) st->loop_count = n; }

void mss_set_stream_position(MssStream *st, int32_t pos)
{
    if (!st || !st->used) return;
    uint32_t p = pos < 0 ? 0 : (uint32_t)pos;
    st->pos = p > st->data_len ? st->data_len : p;
    st->s->frac = 0x80000000u;
    st->have = false;
    st->ended = false;
}

/* The byte position of the frame being played (cur was read ahead of it). */
int32_t mss_stream_position(const MssStream *st)
{
    if (!st || !st->used) return -1;
    if (!st->have) return (int32_t)st->pos;
    return (int32_t)(st->pos >= st->frame_bytes ? st->pos - st->frame_bytes : st->pos + st->data_len - st->frame_bytes);
}

int mss_stream_status(const MssStream *st)
{
    if (!st || !st->used || st->error) return -1;
    return st->s->status;
}

void mss_stream_info(const MssStream *st, int32_t *rate, int32_t *format, int32_t *length)
{
    if (!st || !st->used) return;
    if (rate) *rate = st->rate;
    if (format) *format = st->format;
    if (length) *length = (int32_t)st->data_len;
}

/* The data byte at p (< data_len), through the chunk buffer. False on a short read (MSS flags the
   stream as failed then: status -1). */
static bool stream_byte(MssStream *st, uint32_t p, uint8_t *b)
{
    if (p < st->buf_start || p >= st->buf_start + st->buf_len) {
        uint32_t n = st->data_len - p < STREAM_CHUNK ? st->data_len - p : STREAM_CHUNK;
        int64_t got = vfs_read_at(st->f, (uint64_t)st->data_off + p, st->buf, n);
        if (got != (int64_t)n) {
            st->error = true;
            st->buf_len = 0;
            return false;
        }
        st->buf_start = p;
        st->buf_len = n;
    }
    *b = st->buf[p - st->buf_start];
    return true;
}

/* Reads one frame of the stream at its position into f[]; false at the end (or on an error). The data
   is one continuous byte sequence that wraps around when looping, as MSS's double-buffered reader sees
   it: a position that is not a multiple of the frame size reads across frames (GTA only rounds the
   positions it sets to even numbers, so a 16-bit stereo track can start with its channels swapped). */
static bool stream_frame(MssStream *st, uint8_t *f)
{
    unsigned fb = st->frame_bytes;
    if (!st->data_len || st->ended) return false;
    if (st->pos + fb > st->data_len && st->loop_count == 1) return false;
    for (unsigned k = 0; k < fb; k++) {
        uint32_t p = st->pos + k;
        if (p >= st->data_len) p -= st->data_len;
        if (!stream_byte(st, p, &f[k])) return false;
    }
    st->pos += fb;
    if (st->pos >= st->data_len) {
        if (st->loop_count == 1) {
            st->pos = st->data_len;
            st->ended = true;
        } else {
            st->pos -= st->data_len;
            st->loop_count--;                       /* 0 counts down below zero: forever */
        }
    }
    return true;
}

/* ---- mixer ---- */

typedef struct {
    int32_t tl[256], tr[256];            /* 8-bit lookup (left, right) */
    int32_t sl, sr;                      /* 16-bit scale */
    uint32_t step;                       /* 16.16; 0x10000 = not resampled */
} Voice;

static void voice_setup(const MssSample *s, Voice *v)
{
    int32_t vol = clampi(s->volume, 0, 127), pan = clampi(s->pan, 0, 127);
    int32_t ev = clampi(clampi(M.master, 0, 127) * vol / 127, 0, 127);
    int gl = pan_gain(127 - pan), gr = pan_gain(pan);
    if (s->format & 1) {
        v->sl = gl * ev / 127;
        v->sr = gr * ev / 127;
    } else {
        int32_t v1 = ev ? ev + 1 : 0;
        for (int k = 0; k < 256; k++) {
            int32_t x = (s->flags & MSS_PCM_SIGN) ? (int8_t)k : k - 128;
            int32_t base = x * 2 * v1;
            v->tl[k] = asr(base * gl, 7);
            v->tr[k] = asr(base * gr, 7);
        }
    }
    uint32_t rate = s->rate > 0 ? (uint32_t)s->rate : 1;
    uint32_t step = (uint32_t)(((uint64_t)rate << 16) / MSS_RATE);
    int32_t d = (int32_t)step - 0x10000;
    v->step = (d < 0 ? -d : d) <= RESAMPLE_TOLERANCE ? 0x10000 : step;
}

/* Adds one source frame (bytes f) to the accumulators. */
static void voice_frame(const MssSample *s, const Voice *v, const uint8_t *f, int32_t *l, int32_t *r)
{
    switch (s->format) {
    case MSS_MONO_8: *l += v->tl[f[0]]; *r += v->tr[f[0]]; break;
    case MSS_STEREO_8: *l += v->tl[f[0]]; *r += v->tr[f[1]]; break;
    case MSS_MONO_16:
    case MSS_STEREO_16: {
        unsigned x = s->flags & MSS_PCM_SIGN ? 0 : 0x8000;
        int32_t a = (int16_t)(rd16(f) ^ x);
        int32_t b = s->format == MSS_STEREO_16 ? (int16_t)(rd16(f + 2) ^ x) : a;
        *l += asr(a * v->sl, 7);
        *r += asr(b * v->sr, 7);
        break;
    }
    }
}

static void advance(MssSample *s, const Voice *v, uint32_t *frames)
{
    if (v->step == 0x10000) { (*frames)++; return; }
    uint32_t f = s->frac + ((v->step & 0xffff) << 16);
    *frames = (v->step >> 16) + (f < s->frac);      /* the carry */
    s->frac = f;
}

static void mix_sample(MssSample *s, int32_t *acc, unsigned frames)
{
    Voice v;
    voice_setup(s, &v);
    MssStream *st = s->stream;
    unsigned fb = (s->format & 2 ? 2 : 1) * (s->format & 1 ? 2 : 1);
    for (unsigned i = 0; i < frames; i++) {
        uint32_t n = 0;
        if (st) {
            if (!st->have && !(st->have = stream_frame(st, st->cur))) { s->status = MSS_DONE; return; }
            voice_frame(s, &v, st->cur, &acc[2 * i], &acc[2 * i + 1]);
            advance(s, &v, &n);
            for (uint32_t k = 0; k < n; k++)
                if (!(st->have = stream_frame(st, st->cur))) { s->status = MSS_DONE; return; }
            continue;
        }
        uint32_t nframes = s->len / fb;
        if (s->pos >= nframes) {
            if (s->loop_count == 1 || !nframes) { s->status = MSS_DONE; return; }
            if (s->loop_count > 1) s->loop_count--;
            s->pos = 0;
            s->frac = 0x80000000u;
        }
        voice_frame(s, &v, s->data + (size_t)s->pos * fb, &acc[2 * i], &acc[2 * i + 1]);
        advance(s, &v, &n);
        s->pos += n;
    }
}

static void run_timers(unsigned frames)
{
    for (int t = 0; t < MAX_TIMERS; t++) {
        Timer *tm = &M.timers[t];
        if (!tm->used || !tm->running || tm->hz <= 0) continue;
        tm->acc += (uint64_t)tm->hz * frames;
        while (tm->acc >= MSS_RATE && tm->running) {
            tm->acc -= MSS_RATE;
            tm->fn(tm->user);
        }
    }
}

void mss_render(float *out, unsigned frames)
{
    enum { BLOCK = 256 };
    while (frames) {
        unsigned n = frames < BLOCK ? frames : BLOCK;
        int32_t acc[2 * BLOCK] = {0};
        for (int i = 0; i < MAX_SAMPLES; i++) {
            MssSample *s = &M.samples[i];
            if (!s->allocated || s->status != MSS_PLAYING || (!M.running && !s->orphan)) continue;
            if (s->stream ? !s->stream->started || s->stream->paused : !s->data) continue;
            mix_sample(s, acc, n);
            if (s->orphan && s->status != MSS_PLAYING) {
                free(s->owned);
                *s = (MssSample){0};
                M.drained_ms = (uint32_t)((M.frames + n) * 1000 / MSS_RATE);
            }
        }
        if (out) {
            for (unsigned i = 0; i < 2 * n; i++) {
                int32_t x = clampi(acc[i], -32768, 32767);
#if MSS_OUTPUT_8BIT
                x = asr(x, 8) * 256;
#endif
                out[i] = (float)x / 32768.0f;
            }
            out += 2 * n;
        }
        M.frames += n;
        run_timers(n);
        frames -= n;
    }
}

/* ---- timers ---- */

int mss_register_timer(MssTimerFn fn, void *user)
{
    for (int t = 0; t < MAX_TIMERS; t++)
        if (!M.timers[t].used) {
            M.timers[t] = (Timer){ .used = true, .fn = fn, .user = user, .hz = 100 };
            return t;
        }
    return -1;
}

static Timer *timer(int t) { return t >= 0 && t < MAX_TIMERS && M.timers[t].used ? &M.timers[t] : NULL; }
void mss_set_timer_frequency(int t, int32_t hz) { Timer *tm = timer(t); if (tm) tm->hz = hz; }
void mss_start_timer(int t) { Timer *tm = timer(t); if (tm) tm->running = true; }
void mss_stop_timer(int t) { Timer *tm = timer(t); if (tm) tm->running = false; }
void mss_release_timer(int t) { Timer *tm = timer(t); if (tm) *tm = (Timer){0}; }
