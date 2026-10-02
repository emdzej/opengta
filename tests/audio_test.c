/* The sound modules (src/audio) against the data: banks per city, a scripted sequence through the
   game-facing API (frontend sounds and the exit jingle, then in game: one-shots left and right, an
   engine and a siren car driving past, a voice line, a police scanner report, the car radio playing a
   music track for 10 s, a cutscene WAV), paced by the 70 Hz AIL timer as Game_Run is. Renders to
   out/audio/test.wav and checks the result by structure (levels, panning, durations, rates, 8-bit
   output, clipping) and determinism (the sequence runs twice in child processes, rendered in different
   chunk sizes, and must hash the same). */
#include "audio/audio.h"
#include "audio/mss.h"
#include "exe.h"
#include "vfs.h"
#include "vfs_host.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int fail;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fail = 1; } } while (0)

enum { RATE = APP_AUDIO_RATE, FRAME = RATE * GAME_FRAME_TICKS / TIMER_HZ };   /* 945 */
#define BLK (1 << 22)                           /* 4 Mi stereo frames max (190 s) */

/* ---- the host: a listener at a fixed point, a "tunnel" for y above a line ---- */
static const int32_t LX = 100 << 22, LY = 100 << 22, LZ = 4 << 22;
static void listener(void *u, int32_t *x, int32_t *y, int32_t *z) { (void)u; *x = LX; *y = LY; *z = LZ; }
static bool covered(void *u, int32_t x, int32_t y, int32_t z) { (void)u; (void)x; (void)z; return y > (110 << 22); }
static int32_t fixed_time(void *u) { (void)u; return 1000; }

static float *pcm;                              /* the whole render, stereo */
static size_t nframes;
static unsigned chunk = 105;                    /* render granularity: 315 (one tick) must be a multiple */

/* Renders until the timer allows the next game frame (Timer_WaitTicks(3)), as the platform would
   between frames; returns the frames rendered. */
static unsigned wait_frame(void)
{
    unsigned n = 0;
    while (!Timer_WaitTicks(GAME_FRAME_TICKS)) {
        if (nframes + chunk > BLK) { audio_render(NULL, chunk); n += chunk; continue; }
        audio_render(pcm + 2 * nframes, chunk);
        nframes += chunk;
        n += chunk;
    }
    return n;
}

static void render_plain(unsigned frames)
{
    while (frames) {
        unsigned n = frames < chunk ? frames : chunk;
        audio_render(pcm + 2 * nframes, n);
        nframes += n;
        frames -= n;
    }
}

static double rms(size_t from, size_t to, int ch)
{
    double s = 0;
    for (size_t i = from; i < to && i < nframes; i++) s += (double)pcm[2 * i + ch] * pcm[2 * i + ch];
    return to > from ? sqrt(s / (double)(to - from)) : 0;
}

/* The mean square a sound starting at t adds over w frames (against the w frames before). */
static double power(size_t t, size_t w, int ch)
{
    double a = rms(t, t + w, ch), b = rms(t - w, t, ch);
    return a * a - b * b;
}

/* Events measured during the run, compared between and after the runs. */
typedef struct {
    uint32_t hash;
    size_t frames;
    unsigned frame_frames_min, frame_frames_max;
    size_t t_front, t_jingle_end, t_game, t_left, t_right, t_voice, t_voice_end, t_scan, t_scan_end;
    size_t t_music, t_music_vol, t_music_end, t_cut, t_cut_end;
    int music_track, music_state, music_station, music_vol;
    int32_t music_pos0, music_pos1;
    int scanner_words;
    int bank_nyc;
    int clipped, not8bit;
} Result;

static void write_wav(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) { printf("note: can't write %s\n", path); return; }
    uint32_t data = (uint32_t)(nframes * 4);
    uint8_t h[44];
    memcpy(h, "RIFF", 4);
    uint32_t v[] = { 36 + data };
    memcpy(h + 4, v, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    uint32_t fmt[] = { 16, 1 | 2u << 16, RATE, RATE * 4, 4 | 16u << 16 };
    memcpy(h + 16, fmt, 20);
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &data, 4);
    fwrite(h, 1, 44, f);
    for (size_t i = 0; i < 2 * nframes; i++) {
        float x = pcm[i] * 32768.0f;
        int32_t s = (int32_t)lrintf(x);
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        int16_t w = (int16_t)s;
        fwrite(&w, 2, 1, f);
    }
    fclose(f);
}

static SndCar cars[3];
static SndWorld world;

/* One game frame: wait for the timer, then the game's sound update (Game_Run calls it after the
   frame's logic). */
static unsigned game_frame(Result *r)
{
    unsigned n = wait_frame();
    if (n < r->frame_frames_min) r->frame_frames_min = n;
    if (n > r->frame_frames_max) r->frame_frames_max = n;
    Audio_Update();
    return n;
}

static void run(Result *r)
{
    memset(r, 0, sizeof *r);
    r->frame_frames_min = ~0u;
    SndHost host = { .listener = listener, .covered = covered, .crt_time = fixed_time };
    Snd_SetHost(&host);
    SndOptions opt = { .sound = true };
    Snd_SetOptions(&opt);

    /* frontend: LEVEL000 (16-bit), the 3 stereo loops and a mono click, then the exit jingle */
    Audio_EnterFrontend();
    Snd_SetSfxVolume(7);
    r->t_front = nframes;
    Snd_PlaySampleN(0);
    Snd_PlaySampleN(5);
    render_plain(RATE);
    Snd_PlaySampleN(12);
    render_plain(RATE / 2);
    Audio_EnterGame();                          /* the jingle (0xf) drains while the game starts */
    while (Snd_FrontendDraining()) render_plain(315);   /* (a multiple of both chunk sizes) */
    r->t_jingle_end = nframes;
    Snd_Reset(1);                               /* Game_Init: Snd_Reset(Style_GetNumber()), NYC = 1 */
    r->bank_nyc = Snd_LevelBank();
    Snd_SetSfxVolume(7);
    Snd_SetMusicVolume(7);
    Timer_SetEnabled(true);                     /* Game_Run */
    Timer_Start();
    r->t_game = nframes;

    /* the world: the player in car 0 (engine, radio station 1), a police car with its siren driving
       past from left to right, slot 2 free */
    memset(cars, 0, sizeof cars);
    cars[0] = (SndCar){ .id = 0, .control = 1, .status = 0, .engine_on = 1, .x = LX, .y = LY, .z = LZ,
                        .engine = 0, .radio = 1, .horn = 0x14, .sound_fn = 0, .horn_pattern = -1, .b147 = 0 };
    cars[1] = (SndCar){ .id = 1, .control = 2, .status = 0, .engine_on = 1, .siren = 2, .speed = 30,
                        .x = LX - (8 << 22), .y = LY + (2 << 22), .z = LZ, .engine = 3, .horn = 0x3c, .radio = 2,
                        .horn_pattern = -1, .sound_fn = 0 };
    cars[2] = (SndCar){ .status = -1 };
    world = (SndWorld){ .ncars = 3, .cars = cars, .player_kind = 0, .player_id = 0, .view_kind = 0, .view_id = 0 };
    Snd_SetWorld(&world);

    for (int f = 0; f < 23 * 16; f++) {         /* 16 s of game */
        if (f < 69) {                           /* the police car crosses 16 blocks in 3 s */
            cars[1].x = LX - (8 << 22) + (int32_t)((int64_t)f * (16 << 22) / 69);
        } else {
            cars[1].status = -1;
        }
        cars[0].speed = (int16_t)(f < 60 ? f / 3 : 20);   /* the player's car accelerates */
        if (f == 10) { r->t_left = nframes; Snd_PlayAt(LX - (12 << 22), LY, LZ, 10); }
        if (f == 30) { r->t_right = nframes; Snd_PlayAt(LX + (12 << 22), LY, LZ, 0x12); }
        if (f == 50) { Snd_PlayVoice(6); r->t_voice = nframes; }
        if (f >= 50 && !r->t_voice_end && !Snd_VoiceIsPlaying()) r->t_voice_end = nframes;
        if (f == 90) { Snd_PoliceRadio(9, 0, 2, 3); r->t_scan = nframes; }
        if (f > 90 && !r->t_scan_end && !Snd_MenuIsPlaying()) r->t_scan_end = nframes;
        if (Music_State() == 2 && !r->t_music) {
            r->t_music = nframes;
            r->music_track = Music_Track();
            r->music_station = Music_Station();
        }
        game_frame(r);
        if (r->t_music && !r->t_music_vol && Music_StreamVolume() > 0) {
            r->t_music_vol = nframes;
            r->music_vol = Music_StreamVolume();
        }
        if (r->t_music && !r->music_pos0 && nframes - r->t_music >= (size_t)RATE)
            r->music_pos0 = Snd_MusicGetPos(), r->music_pos1 = (int32_t)nframes;
    }
    r->music_state = Music_State();
    r->t_music_end = nframes;

    /* a cutscene WAV (closes the radio) for 2 s */
    Snd_PlayCutsceneVoice(0);
    r->t_cut = nframes;
    for (int f = 0; f < 46; f++) game_frame(r);
    int ms = Snd_GetCutsceneVoiceStatus();
    printf("cutscene voice after %zu frames: %d ms\n", nframes - r->t_cut, ms);
    CHECK(ms >= 1900 && ms <= 2100, "cutscene position %d ms after 2 s", ms);
    Snd_StopCutsceneVoice();
    r->t_cut_end = nframes;
    render_plain(RATE / 4);
    Timer_Stop();
    Audio_Shutdown();
    r->frames = nframes;

    for (size_t i = 0; i < 2 * nframes; i++) {
        float x = pcm[i] * 128.0f;
        if (x != floorf(x)) r->not8bit++;
        if (pcm[i] >= 127.0f / 128 || pcm[i] <= -1.0f) r->clipped++;
    }
    r->hash = crc32(pcm, nframes * 2 * sizeof *pcm);
}

/* Runs the sequence in a child process (fresh module state) and gets the result back through a pipe;
   the first run also keeps its output for the checks. */
static bool run_child(Result *r, unsigned ch, bool keep)
{
    int fd[2];
    if (pipe(fd)) return false;
    pid_t p = fork();
    if (p < 0) return false;
    if (p == 0) {
        close(fd[0]);
        chunk = ch;
        Result res;
        run(&res);
        if (keep) {
            mkdir("out", 0777);
            mkdir("out/audio", 0777);
            write_wav("out/audio/test.wav");
            FILE *f = fopen("out/audio/test.f32", "wb");
            if (f) { fwrite(pcm, sizeof *pcm, 2 * nframes, f); fclose(f); }
        }
        ssize_t w = write(fd[1], &res, sizeof res);
        _exit(w == (ssize_t)sizeof res ? (fail ? 2 : 0) : 1);
    }
    close(fd[1]);
    ssize_t n = read(fd[0], r, sizeof *r);
    close(fd[0]);
    int st = 0;
    waitpid(p, &st, 0);
    if (WIFEXITED(st) && WEXITSTATUS(st) == 2) fail = 1;
    return n == (ssize_t)sizeof *r && WIFEXITED(st) && WEXITSTATUS(st) != 1;
}

static void show_bank(const char *name)
{
    char sdt[64], raw[64];
    snprintf(sdt, sizeof sdt, "GTADATA/AUDIO/%s.SDT", name);
    snprintf(raw, sizeof raw, "GTADATA/AUDIO/%s.RAW", name);
    printf("  %s: %lld records, RAW %lld bytes\n", name, (long long)(vfs_size(sdt) / 12), (long long)vfs_size(raw));
}

int main(void)
{
    char err[256];
    if (!vfs_mount_default()) { printf("SKIP: no data root\n"); return 0; }
    if (!vfs_exists("GTADATA/AUDIO/LEVEL001.SDT")) { printf("SKIP: no GTADATA/AUDIO\n"); return 0; }
    if (!exe_init(err, sizeof err)) printf("note: %s (gear rates and volume tables read as 0)\n", err);

    /* which bank each city uses: Snd_Reset(Style_GetNumber()), the CMP header's style byte */
    static const char *cities[] = { "GTADATA/NYC.CMP", "GTADATA/SANB.CMP", "GTADATA/MIAMI.CMP" };
    for (int i = 0; i < 3; i++) {
        VfsFile *f = vfs_open(cities[i]);
        uint8_t h[8] = {0};
        if (f) { vfs_read_at(f, 0, h, 8); vfs_close(f); }
        printf("%s: style %d, sample byte %d -> LEVEL%03d\n", cities[i], h[4], h[5], h[4]);
        CHECK(h[4] == i + 1, "%s style %d", cities[i], h[4]);
    }
    printf("banks:\n");
    show_bank("LEVEL000");
    show_bank("LEVEL001");
    show_bank("LEVEL002");
    show_bank("LEVEL003");
    show_bank("VOCALCOM");
    show_bank("MISBRIEF");
    CHECK(vfs_size("GTADATA/AUDIO/LEVEL001.SDT") == 131 * 12, "LEVEL001.SDT size");
    CHECK(vfs_size("GTADATA/AUDIO/VOCALCOM.SDT") == 71 * 12, "VOCALCOM.SDT size");
    for (int i = 1; i <= 10; i++) {
        char p[32];
        snprintf(p, sizeof p, "Music/Track%d.wav", i);
        printf("  %s: %lld bytes%s\n", p, (long long)vfs_size(p), vfs_exists(p) ? "" : " (missing)");
    }

    pcm = malloc((size_t)BLK * 2 * sizeof *pcm);
    if (!pcm) { printf("FAIL: out of memory\n"); return 1; }

    Result a, b;
    if (!run_child(&a, 105, true) || !run_child(&b, 63, false)) {
        printf("FAIL: child run\n");
        return 1;
    }
    /* reload the first run's samples for the level checks */
    FILE *f = fopen("out/audio/test.f32", "rb");
    nframes = 0;
    if (f) { nframes = fread(pcm, 2 * sizeof *pcm, BLK, f); fclose(f); }
    remove("out/audio/test.f32");

    printf("rendered %zu frames (%.2f s) at %d Hz; hash %08x / %08x (chunks of 105 / 63 frames)\n",
           a.frames, (double)a.frames / RATE, RATE, a.hash, b.hash);
    CHECK(a.hash == b.hash && a.frames == b.frames, "not deterministic");
    CHECK(nframes == a.frames, "reloaded %zu frames", nframes);

    printf("game frame: %u..%u audio frames (expected %d = 3 ticks of 70 Hz)\n", a.frame_frames_min,
           a.frame_frames_max, FRAME);
    CHECK(a.frame_frames_min == FRAME && a.frame_frames_max == FRAME, "frame pacing");
    printf("level bank in NYC: LEVEL%03d\n", a.bank_nyc);
    CHECK(a.bank_nyc == 1, "NYC bank %d", a.bank_nyc);

    /* frontend sounds */
    double fl = rms(a.t_front, a.t_front + RATE, 0), fr = rms(a.t_front, a.t_front + RATE, 1);
    printf("frontend: rms L %.4f R %.4f; exit jingle and the 1 s wait end %.2f s after the game started\n",
           fl, fr, (double)(a.t_jingle_end - (a.t_front + RATE * 3 / 2)) / RATE);
    CHECK(fl > 0.01 && fr > 0.01, "frontend silent");
    CHECK(a.t_jingle_end - (a.t_front + RATE * 3 / 2) > (size_t)RATE, "no drain");

    /* one-shots 12 blocks left, then right (pan 16 / 111: gains 32/128 and 128/32): the power they add */
    size_t w = RATE / 10;
    double l1 = power(a.t_left, w, 0), r1 = power(a.t_left, w, 1);
    double l2 = power(a.t_right, w, 0), r2 = power(a.t_right, w, 1);
    printf("one-shot left adds power L %.5f R %.5f; right: L %.5f R %.5f\n", l1, r1, l2, r2);
    CHECK(l1 > r1 * 4, "left one-shot not on the left");
    CHECK(r2 > l2 * 4, "right one-shot not on the right");

    /* voice: VOCALCOM 6 at its rate (22050 Hz: one output frame per input byte) */
    VfsFile *vf = vfs_open("GTADATA/AUDIO/VOCALCOM.SDT");
    uint8_t rec[12] = {0};
    if (vf) { vfs_read_at(vf, 6 * 12, rec, 12); vfs_close(vf); }
    uint32_t vlen = rec[4] | rec[5] << 8 | rec[6] << 16 | (uint32_t)rec[7] << 24;
    uint32_t vrate = rec[8] | rec[9] << 8 | rec[10] << 16 | (uint32_t)rec[11] << 24;
    double vexp = (double)vlen / vrate, vgot = (double)(a.t_voice_end - a.t_voice) / RATE;
    printf("voice 6: %u bytes at %u Hz = %.3f s, played %.3f s (to the frame), rms %.4f\n", vlen, vrate, vexp,
           vgot, rms(a.t_voice, a.t_voice_end, 0));
    CHECK(fabs(vgot - vexp) < 0.05, "voice duration %.3f s, expected %.3f s", vgot, vexp);

    /* police scanner: 7 words (intro, 0x52, model 9, kind, "2" direction, area, end) at 6000 Hz */
    printf("scanner report: %.2f s\n", (double)(a.t_scan_end - a.t_scan) / RATE);
    CHECK(a.t_scan_end > a.t_scan + RATE / 2, "scanner too short");

    /* music: the car's station 1 = tracks 4, 0, 1: track 4 (Track5.wav), volume after 26 frames */
    printf("radio: station %d track %d (Track%d.wav) from %.2f s, volume %d after %.2f s (%zu frames), state %d\n",
           a.music_station, a.music_track, a.music_track + 1, (double)(a.t_music - a.t_game) / RATE, a.music_vol,
           (double)(a.t_music_vol - a.t_music) / RATE, (a.t_music_vol - a.t_music) / FRAME, a.music_state);
    CHECK(a.music_vol == 0x7f, "music volume %d", a.music_vol);
    CHECK(a.music_station == 1 && a.music_track == 4, "station %d track %d", a.music_station, a.music_track);
    CHECK(a.music_state == 2, "music state %d", a.music_state);
    size_t mstart = a.t_music_vol, mend = a.t_music_end;
    double ml = rms(mstart, mend, 0), mr = rms(mstart, mend, 1);
    printf("music %.2f s: rms L %.4f R %.4f\n", (double)(mend - mstart) / RATE, ml, mr);
    CHECK(mend - mstart >= (size_t)RATE * 10, "less than 10 s of music");
    CHECK(ml > 0.02 && mr > 0.02, "music silent");
    CHECK(a.t_music_vol - a.t_music >= 25 * FRAME && a.t_music_vol - a.t_music <= 28 * FRAME,
          "music volume came after %zu frames", (a.t_music_vol - a.t_music) / FRAME);

    /* output format */
    printf("output: %d samples off the 8-bit grid, %d at full scale\n", a.not8bit, a.clipped);
    CHECK(a.not8bit == 0, "not 8-bit");
    CHECK(a.clipped < (int)(a.frames / 1000), "clipping %d", a.clipped);

    /* the helpers */
    CHECK(Snd_GearRate(0) == 18000 && Snd_GearRate(15) == 66000 && Snd_GearRate(-3) == 48000 &&
          Snd_GearRate(99) == 77000, "gear rates %d %d %d", Snd_GearRate(0), Snd_GearRate(15), Snd_GearRate(-3));
    CHECK(Snd_CalcVolume(0) == 40 && Snd_CalcVolume(10) == 55 && Snd_CalcVolume(40) == 100, "CalcVolume");
    CHECK(Snd_DistToVolume(0) == 127 && Snd_DistToVolume(0x3ffffff) == 0 && Snd_DistToVolume(-(8 << 22)) == 63,
          "DistToVolume %d", Snd_DistToVolume(-(8 << 22)));
    CHECK(Snd_DistToPan(0) == 63 && Snd_DistToPan(-(12 << 22)) == 111 && Snd_DistToPan(12 << 22) == 15, "DistToPan");
    CHECK(Snd_CalcDelta(10000, 0) == 3 && Snd_CalcDelta(0, 10000) == -3, "CalcDelta");

    /* the mixer against MSS's formulas: 8-bit full scale panned hard left, 16-bit at the centre,
       resampling (11025 Hz: every frame twice but the first) and the 1% tolerance (22200 Hz: not resampled) */
    mss_startup(4);
    static uint8_t loud[100];
    memset(loud, 0xff, sizeof loud);
    MssSample *s8 = mss_allocate_sample();
    mss_set_sample_address(s8, loud, sizeof loud);
    mss_set_sample_playback_rate(s8, 11025);
    mss_set_sample_volume(s8, 127);
    mss_set_sample_pan(s8, 0);
    mss_start_sample(s8);
    float out[2 * 300];
    mss_render(out, 300);
    int n8 = 0;
    for (int i = 0; i < 300 && out[2 * i] != 0; i++) n8++;
    printf("mixer: 8-bit 0xff at volume 127 pan 0: L %.6f R %.6f for %d frames\n", out[0], out[1], n8);
    CHECK(out[0] == 127.0f / 128 && out[1] == 0, "8-bit pan law");
    CHECK(n8 == 199, "11025 Hz played %d frames for 100", n8);   /* the phase starts at 0.5: 1 + 2 * 99 */
    MssSample *s16 = mss_allocate_sample();
    static int16_t half[64];
    for (int i = 0; i < 64; i++) half[i] = 16384;
    mss_set_sample_type(s16, MSS_MONO_16, MSS_PCM_SIGN);
    mss_set_sample_address(s16, half, sizeof half);
    mss_set_sample_playback_rate(s16, 22200);
    mss_set_sample_volume(s16, 100);
    mss_set_sample_pan(s16, 64);
    mss_start_sample(s16);
    mss_set_master_volume(108);
    mss_render(out, 100);
    int n16 = 0;
    for (int i = 0; i < 100 && out[2 * i] != 0; i++) n16++;
    /* v = 108 * 100 / 127 = 85, scale = 128 * 85 / 127 = 85, 16384 * 85 >> 7 = 10880 -> 8-bit 42 */
    printf("mixer: 16-bit 0.5 at volume 100 pan 64 master 108: L %.6f R %.6f for %d frames\n", out[0], out[1], n16);
    CHECK(out[0] == 42.0f / 128 && out[1] == 42.0f / 128, "16-bit volume");
    CHECK(n16 == 64, "22200 Hz played %d frames for 64", n16);
    /* streaming: Track5.wav at unity gain must come out as its own samples (high bytes), across chunk
       reads, and from an offset of 2 mod 4 with the channels swapped */
    mss_set_master_volume(127);
    static const int32_t offs[] = { 1000000, 3000002 };
    for (int k = 0; k < 2; k++) {
        MssStream *st = mss_open_stream("Music/Track5.wav");
        CHECK(st != NULL, "open Track5.wav");
        if (!st) break;
        mss_set_stream_volume(st, 127);
        mss_set_stream_position(st, offs[k]);
        mss_start_stream(st);
        enum { N = 40000 };
        static float sout[2 * N];
        static uint8_t src[4 * N + 8];
        mss_render(sout, N);
        VfsFile *tf = vfs_open("Music/Track5.wav");
        int64_t got = tf ? vfs_read_at(tf, 44 + (uint64_t)offs[k], src, 4 * N) : -1;
        if (tf) vfs_close(tf);
        int bad = 0;
        for (int i = 0; i < 2 * N && got == 4 * N; i++) {
            int16_t v = (int16_t)(src[2 * i] | src[2 * i + 1] << 8);
            float want = (float)((v >= 0 ? v >> 8 : ~(~v >> 8)) * 256) / 32768.0f;
            if (sout[i] != want) bad++;
        }
        int32_t pos = mss_stream_position(st);
        printf("stream from byte %d: %d of %d samples differ from the file; position after %d frames: %d\n",
               offs[k], bad, 2 * N, N, pos);
        CHECK(bad == 0, "stream samples differ");
        CHECK(pos == offs[k] + 4 * N, "stream position %d", pos);
        mss_close_stream(st);
    }
    mss_shutdown(false);

    printf("wrote out/audio/test.wav\n");
    free(pcm);
    printf(fail ? "audio_test: FAILED\n" : "audio_test: ok\n");
    return fail;
}
