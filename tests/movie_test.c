/* The intro movie: decodes every frame of GTADATA/MOVIE.SMK (src/movie/smacker.c), prints the header,
   a hash per frame (FNV-1a of the indices and the palette) and the audio, writes a few frames as PNG and
   the audio track(s) as WAV to out/movie/, and runs the intro player (src/movie/intro.c) as the
   frontend does. */
#include "movie/intro.h"
#include "movie/smacker.h"
#include "audio/mss.h"
#include "png.h"
#include "surface.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static uint32_t fnv(uint32_t h, const uint8_t *p, size_t n)
{
    while (n--) h = (h ^ *p++) * 16777619u;
    return h;
}

static void write_frame_png(const char *path, const Smacker *s)
{
    uint32_t *px = malloc((size_t)s->w * s->h * 4);
    for (uint32_t y = 0; y < s->h; y++)
        for (uint32_t x = 0; x < s->w; x++) {
            const uint8_t *c = s->pal + 3 * s->video[(size_t)y * s->stride + x];
            px[y * s->w + x] = surface_rgb(c[0], c[1], c[2]);
        }
    png_write(path, px, (int)s->w, (int)s->h, PNG_ABGR);
    free(px);
}

static void put16(FILE *f, unsigned v) { fputc((int)(v & 0xff), f); fputc((int)(v >> 8 & 0xff), f); }
static void put32(FILE *f, uint32_t v) { put16(f, v & 0xffff); put16(f, v >> 16); }

static void write_wav(const char *path, const SmkTrack *t, const uint8_t *data, uint32_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    unsigned ch = t->stereo ? 2 : 1, bps = t->bits16 ? 2 : 1;
    fwrite("RIFF", 1, 4, f); put32(f, 36 + len); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, ch); put32(f, t->rate); put32(f, t->rate * ch * bps);
    put16(f, ch * bps); put16(f, bps * 8);
    fwrite("data", 1, 4, f); put32(f, len);
    fwrite(data, 1, len, f);
    fclose(f);
}

static void test_decode(void)
{
    char err[160];
    Smacker *s = smk_open("GTADATA/MOVIE.SMK", err, sizeof err);
    CHECK(s, "%s", err);
    if (!s) return;
    printf("MOVIE.SMK: SMK%d %ux%u, %u frames (%u physical), frame rate %d (%u us/frame), flags %u\n",
           s->version, s->w, s->h, s->frames, s->nphys, s->frame_rate, s->frame_us, s->flags);
    printf("  trees %u bytes (used %u): nodes mmap %d mclr %d full %d type %d\n", s->trees_size, s->trees_used,
           s->mmap.n, s->mclr.n, s->full.n, s->type.n);
    /* the header's allocation sizes are 4 bytes per node, three more than the tree has */
    const SmkTree *tr4[4] = { &s->mmap, &s->mclr, &s->full, &s->type };
    for (int i = 0; i < 4; i++) {
        uint32_t alloc = s->file[0x38 + 4 * i] | s->file[0x39 + 4 * i] << 8 | (uint32_t)s->file[0x3a + 4 * i] << 16;
        CHECK((uint32_t)tr4[i]->n == alloc / 4 - 3, "tree %d: %d nodes, header size %u", i, tr4[i]->n, alloc);
    }
    CHECK(s->trees_used <= s->trees_size, "trees: %u of %u bytes", s->trees_used, s->trees_size);
    CHECK(s->w == 320 && s->h == 200 && s->frames == 415 && s->frame_us == 40000, "header");
    for (int t = 0; t < SMK_TRACKS; t++) {
        const SmkTrack *tr = &s->track[t];
        if (tr->present)
            printf("  track %d: %u Hz, %d-bit %s, %s, largest chunk %u bytes\n", t, tr->rate, tr->bits16 ? 16 : 8,
                   tr->stereo ? "stereo" : "mono", tr->compressed ? tr->codec ? "Bink audio" : "DPCM" : "raw", tr->largest);
    }
    uint8_t *audio[SMK_TRACKS] = {0};
    uint32_t alen[SMK_TRACKS] = {0}, acap[SMK_TRACKS] = {0};
    uint32_t all = 2166136261u, palettes = 0, keyframes = 0;
    static const uint32_t shots[] = { 0, 30, 60, 100, 150, 200, 250, 300, 350, 414 };
    unsigned shot = 0;
    mkdir("out", 0755);
    mkdir("out/movie", 0755);
    FILE *log = fopen("out/movie/frames.txt", "w");
    for (uint32_t i = 0; i < s->nphys; i++) {
        bool ok = smk_next_frame(s);
        CHECK(ok, "frame %u", i);
        if (!ok) break;
        palettes += s->new_palette;
        keyframes += s->keyframe[i];
        uint32_t h = fnv(fnv(2166136261u, s->video, (size_t)s->stride * s->vh), s->pal, sizeof s->pal);
        all = fnv(all, (const uint8_t *)&h, 4);
        if (log) {
            fprintf(log, "%3u size %6u type %02x%s%s hash %08x", i, s->frame_size[i], s->frame_type[i],
                    s->keyframe[i] ? " key" : "", s->new_palette ? " pal" : "", h);
            for (int t = 0; t < SMK_TRACKS; t++)
                if (s->track[t].len) fprintf(log, " a%d %u", t, s->track[t].len);
            fputc('\n', log);
        }
        for (int t = 0; t < SMK_TRACKS; t++) {
            SmkTrack *tr = &s->track[t];
            if (!tr->len) continue;
            if (alen[t] + tr->len > acap[t]) {
                acap[t] = (alen[t] + tr->len) * 2;
                audio[t] = realloc(audio[t], acap[t]);
            }
            memcpy(audio[t] + alen[t], tr->buf, tr->len);
            alen[t] += tr->len;
        }
        if (shot < sizeof shots / sizeof *shots && shots[shot] == i) {
            char path[64];
            snprintf(path, sizeof path, "out/movie/frame%03u.png", i);
            write_frame_png(path, s);
            shot++;
        }
    }
    if (log) fclose(log);
    printf("  %u palettes, %u keyframes, all frames hash %08x (out/movie/frames.txt)\n", palettes, keyframes, all);
    double movie_s = (double)s->frames * s->frame_us / 1e6;
    for (int t = 0; t < SMK_TRACKS; t++) {
        SmkTrack *tr = &s->track[t];
        if (!tr->present) continue;
        unsigned fb = (tr->stereo ? 2u : 1u) * (tr->bits16 ? 2u : 1u);
        double sec = (double)alen[t] / fb / tr->rate;
        printf("  track %d: %u bytes, %.3f s (movie %.3f s), hash %08x\n", t, alen[t], sec, movie_s,
               fnv(2166136261u, audio[t], alen[t]));
        CHECK(sec > movie_s - 1.0 && sec < movie_s + 1.0, "track %d duration %.3f s", t, sec);
        char path[64];
        snprintf(path, sizeof path, "out/movie/audio%s.wav", t ? "1" : "");
        if (t) path[15] = (char)('0' + t);
        write_wav(t ? path : "out/movie/audio.wav", tr, audio[t], alen[t]);
        free(audio[t]);
    }
    /* determinism: a second pass gives the same hashes */
    smk_rewind(s);
    uint32_t again = 2166136261u;
    for (uint32_t i = 0; i < s->nphys && smk_next_frame(s); i++) {
        uint32_t h = fnv(fnv(2166136261u, s->video, (size_t)s->stride * s->vh), s->pal, sizeof s->pal);
        again = fnv(again, (const uint8_t *)&h, 4);
    }
    CHECK(again == all, "second pass %08x != %08x", again, all);
    smk_close(s);
}

/* The player as the frontend runs it: one step per 35 ms frame, the Miles mix rendered in between. */
static void test_intro(void)
{
    static uint32_t px[640 * 480];
    Surface scr = { px, 640, 480, 640 };
    CHECK(!movie_intro_start(), "disabled by default");
    movie_intro_set_enabled(true);
    CHECK(movie_intro_start(), "movie_intro_start");
    size_t cap = 22050 * 20 * 2, nmix = 0;
    float *mix = malloc(cap * sizeof *mix);
    int steps = 0, last = -1, repeats = 0;
    uint32_t frac = 0;
    bool sound_mid = false;
    while (movie_intro_step(NULL, 0, &scr)) {
        int f = movie_intro_frame();
        repeats += f == last;
        last = f;
        if (steps == 120 || steps == 300) {
            char path[64];
            snprintf(path, sizeof path, "out/movie/intro_step%03d.png", steps);
            png_write(path, px, 640, 480, PNG_ABGR);
        }
        /* 35 ms of audio: 771.75 frames at 22050 Hz */
        frac += 22050 * 35;
        unsigned n = frac / 1000;
        frac %= 1000;
        if ((nmix + n) * 2 <= cap) { mss_render(mix + nmix * 2, n); nmix += n; }
        if (steps == 300) sound_mid = mss_running();
        steps++;
    }
    printf("intro: %d steps (%d showed the previous frame again), last frame %d, %.3f s of mix\n", steps, repeats,
           last, nmix / 22050.0);
    CHECK(steps == 475 && last == 414, "steps %d last %d", steps, last);
    CHECK(sound_mid, "the sound device was open mid-movie");
    CHECK(!mss_running(), "Snd_CloseDevice at the end");
    uint32_t black = 1;
    for (int i = 0; i < 640 * 480; i++) black &= px[i] == 0xff000000u;
    CHECK(black, "the screen is black after the movie");
    /* the mix: loud enough while the soundtrack plays (seconds 3-13) */
    double e = 0;
    for (size_t i = 22050 * 3 * 2; i < 22050 * 13 * 2 && i < nmix * 2; i++) e += mix[i] * mix[i];
    e = sqrt(e / (22050 * 10 * 2));
    printf("intro: mix rms %.4f (seconds 3-13)\n", e);
    CHECK(e > 0.01, "the movie's sound is in the mix");
    FILE *f = fopen("out/movie/intro_mix.wav", "wb");
    if (f) {
        uint32_t len = (uint32_t)nmix * 4;
        fwrite("RIFF", 1, 4, f); put32(f, 36 + len); fwrite("WAVEfmt ", 1, 8, f);
        put32(f, 16); put16(f, 1); put16(f, 2); put32(f, 22050); put32(f, 22050 * 4); put16(f, 4); put16(f, 16);
        fwrite("data", 1, 4, f); put32(f, len);
        for (size_t i = 0; i < nmix * 2; i++) {
            float v = mix[i] * 32768.0f;
            put16(f, (unsigned)(int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v));
        }
        fclose(f);
    }
    free(mix);

    /* keys: an Alt press is ignored, its release ends the movie at the next frame's check */
    CHECK(movie_intro_start(), "start again");
    int i = 0;
    for (; i < 50; i++) movie_intro_step(NULL, 0, &scr);
    uint16_t alt = 0x38, alt_up = 0xb8, enter = 0x1c;
    movie_intro_step(&alt, 1, &scr);
    for (i = 0; i < 10; i++) movie_intro_step(NULL, 0, &scr);
    CHECK(movie_intro_playing(), "Alt pressed: still playing");
    bool on = movie_intro_step(&alt_up, 1, &scr);
    int extra = 0;
    while (on && extra < 5) on = movie_intro_step(NULL, 0, &scr), extra++;
    CHECK(!on && extra <= 2, "Alt released: stopped (%d more steps)", extra);
    CHECK(movie_intro_start(), "start a third time");
    movie_intro_step(NULL, 0, &scr);
    movie_intro_step(&enter, 1, &scr);
    on = movie_intro_step(NULL, 0, &scr);
    CHECK(!on, "Enter stops it at the next frame");
    movie_intro_stop();
}

int main(void)
{
    if (!vfs_mount_default()) { printf("SKIP: no data root\n"); return 0; }
    if (!vfs_exists("GTADATA/MOVIE.SMK")) { printf("SKIP: no GTADATA/MOVIE.SMK\n"); return 0; }
    test_decode();
    test_intro();
    printf(fails ? "movie_test: %d FAILED\n" : "movie_test: ok\n", fails);
    return fails != 0;
}
