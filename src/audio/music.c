/* The music module 0x40c300-0x40d620: Music/Track1..10.wav ("CD" music, 22050 Hz 16-bit stereo, 5-58 MB)
   streamed through Miles, as radio stations in cars and one track on foot.

   Radio mode (0x501aa4 = 0, the default): Music_Startup gives tracks 0..8 one random start offset each
   (time() squared, modulo the track's length; track 8, the on-foot music, starts at 0). Getting into a
   car tunes to its station (car_info +0xa8): 3 tracks per station in table 0x4ab270, a rotating index
   per station (Music_NextStation, the radio key), and a track resumes where it was left. On foot it is
   track 8 (station 10, state 4). Sequential mode plays the tracks in turn.

   The tracks loop forever (loop count 0): a track only "ends" when its stream fails. A new track starts
   at volume 0 and gets its volume after 26 service calls (0x4ab2e4).

   The tables are kept as the exe lays them out, because the code reads them out of their bounds:
   0x501578 radio[10] {start, length, position} (entry 9 is a copy of track 2 for the frontend), 0x5015f0
   the track count (10), 0x5015f8 catalog[99] {start, length, position}. Sequential mode reads the
   position of track n from catalog n + 1 (0x50160c + 12 n) but resets catalog n: a quirk, kept. The
   station table ends at 0x4ab2dc, so station 9 reads the variables after it (the frontend flag, the
   current station, the volume countdown) as its tracks. */
#include "snd_internal.h"
#include "exe.h"
#include <stdio.h>

enum { MEM_BASE = 0x501578, MEM_WORDS = 0x150 };
static int32_t mem[MEM_WORDS];           /* 0x501578.. */
static bool initialised;                 /* 0x501aa0 */
static int32_t sequential;               /* 0x501aa4 */
static int32_t track;                    /* 0x501aa8 */
static int32_t state;                    /* 0x501aac */
static MssStream *stream;                /* 0x501a9c */
static uint8_t rot[256];                 /* 0x501ab0 per-station index (16 bytes in the exe) */
static uint8_t frontend_track = 1;       /* 0x4ab2dc */
static int32_t station = 5;              /* 0x4ab2e0 */
static int32_t countdown = 25;           /* 0x4ab2e4 */
static int32_t volume = 50;              /* 0x4abd38 */

static int32_t rd(uint32_t va)
{
    uint32_t i = (va - MEM_BASE) / 4;
    return va >= MEM_BASE && i < MEM_WORDS ? mem[i] : 0;
}
static void wr(uint32_t va, int32_t v)
{
    uint32_t i = (va - MEM_BASE) / 4;
    if (va >= MEM_BASE && i < MEM_WORDS) mem[i] = v;
}
/* radio[n] {start, length, position} at 0x501578, catalog[n] at 0x5015f8 */
static uint32_t radio_va(int32_t n, int k) { return (uint32_t)(MEM_BASE + 12 * n + 4 * k); }
static uint32_t cat_va(int32_t n, int k) { return (uint32_t)(0x5015f8 + 12 * n + 4 * k); }
static int count(void) { return rd(0x5015f0) & 0xff; }   /* a byte */
static int32_t seq_pos(int32_t n) { return rd((uint32_t)(0x50160c + 12 * n)); }
static void set_seq_pos(int32_t n, int32_t v) { wr((uint32_t)(0x50160c + 12 * n), v); }

/* The station table 0x4ab270 (3 tracks a station) and the variables that follow it. */
static int32_t station_track(int32_t i)
{
    uint32_t va = 0x4ab270 + 4u * (uint32_t)i;
    if (va == 0x4ab2dc) return frontend_track;
    if (va == 0x4ab2e0) return station;
    if (va == 0x4ab2e4) return countdown;
    return (int32_t)exe_u32(va);
}

/* "..//music//track1.wav" .. track10.wav (0x4ab2e8, 0x108 bytes each) */
static void track_path(char *buf, size_t cap, int n) { snprintf(buf, cap, "Music/Track%d.wav", n + 1); }

static bool driver(void) { return mss_running() && snd.running; }

static int next_seq(int32_t t) { int c = count() - 1; return c ? (t + 1) % c : 0; }

/* Music_CatalogTracks 0x40c300 ("CATALOG: Track %d, Length = %d, SampleRate = %d"). A track that can't be
   opened keeps the previous track's length (the locals aren't reset), so only a missing Track1 fails. */
int Music_CatalogTracks(void)
{
    int32_t length = 0, type = 0;
    mem[(0x5015f0 - MEM_BASE) / 4] = (mem[(0x5015f0 - MEM_BASE) / 4] & ~0xff) | 10;
    for (int i = 0;;) {
        if (!driver()) return -51;
        char path[32];
        track_path(path, sizeof path, i);
        stream = mss_open_stream(path);
        if (stream) mss_stream_info(stream, NULL, &type, &length);
        if (length < 1) {
            initialised = false;
            state = 0;
            mss_close_stream(stream);
            stream = NULL;
            return -51;
        }
        mss_close_stream(stream);
        stream = NULL;
        wr(cat_va(i, 0), 0);
        wr(cat_va(i, 1), length);
        wr(cat_va(i, 2), 0);
        i = (uint8_t)(i + 1);
        if (i >= count()) break;
    }
    for (int i = count(); i < 99; i++)
        for (int k = 0; k < 3; k++) wr(cat_va(i, k), 0);
    return 0;
}

/* Music_Init 0x40c470 */
void Music_Init(void)
{
    if (!initialised && Music_Startup()) initialised = true;
}

/* Saves the playing track's position (radio: unless it is the on-foot track 8) and closes the stream:
   the body Music_Stop 0x40c910, Music_StopSavePos 0x40cb90, Music_Shutdown, Music_UpdateRadio and
   Music_NextStation share ("CD STOP - TRACK = %d, track pos = %d"). */
static void stop_and_save(void)
{
    if (state == 0) return;
    if (state == 2 || state == 4) {
        int32_t pos = mss_stream_position(stream);
        if (!sequential) {
            if (track != 8) wr(radio_va(track, 2), pos);
        } else {
            set_seq_pos(track, pos);
        }
        if (track < 0 || track > 9) track = 5;
        mss_pause_stream(stream, true);
        mss_close_stream(stream);
        stream = NULL;
    }
    state = 1;
}

/* Music_Shutdown 0x40c490 */
void Music_Shutdown(void)
{
    if (!initialised) return;
    initialised = false;
    stop_and_save();
    if (stream) {
        mss_close_stream(stream);
        stream = NULL;
    }
    state = 0;
    if (frontend_track) frontend_track = 0;
}

static int32_t crt_time(void) { return snd_host.crt_time ? snd_host.crt_time(snd_host.user) : 0; }

/* Music_Startup 0x40c5c0 ("CD INIT: Track = %d , Offset = %d, Track_pos = %d") */
bool Music_Startup(void)
{
    int32_t a = crt_time(), b = crt_time();
    uint32_t seed = (uint32_t)a * (uint32_t)b;
    track = 0;
    station = 5;
    for (int i = 0; i < 9; i++)
        for (int k = 0; k < 3; k++) wr(radio_va(i, k), 0);
    /* AIL_startup (again) */
    Snd_SetMusicAvailable(true);
    if (Music_CatalogTracks() != 0) {
        Snd_SetMusicAvailable(false);
        return false;
    }
    wr(radio_va(9, 0), rd(cat_va(1, 0)));
    wr(radio_va(9, 1), rd(cat_va(1, 1)));
    wr(radio_va(9, 2), rd(cat_va(1, 0)));
    for (int i = 0; i < 9; i++) {
        uint32_t start = (uint32_t)rd(cat_va(i, 0)), len = (uint32_t)rd(cat_va(i, 1));
        wr(radio_va(i, 0), (int32_t)start);
        wr(radio_va(i, 1), (int32_t)len);
        if (len <= start) {
            Snd_SetMusicAvailable(false);
            return false;
        }
        uint32_t off = seed % (len - start);
        wr(radio_va(i, 2), (int32_t)(start + off));
    }
    wr(radio_va(8, 2), rd(radio_va(8, 0)));
    Snd_SetMusicAvailable(true);
    state = 1;
    return true;
}

/* Music_PlayTrack 0x40c700 ("STREAMPLAY: Track = %d", "Offset Play %d"): starts at volume 0 (the
   countdown restores it), seeks to pos made even (one byte up, or down at the end) if it is inside
   the track, loops forever. */
bool Music_PlayTrack(int n, int32_t pos)
{
    if (n < 0 || n > 9) n = 0;
    char path[32];
    track_path(path, sizeof path, n);
    stream = mss_open_stream(path);
    if (!stream) return false;
    mss_set_stream_volume(stream, 0);
    mss_start_stream(stream);
    countdown = 25;
    uint32_t len = (uint32_t)rd(radio_va(n, 1));
    if ((uint32_t)pos < len && pos > 0) {
        if (pos & 1) {
            if ((uint32_t)pos + 1 < len) pos++;
            else pos--;
        }
        mss_set_stream_position(stream, pos);
    }
    mss_set_stream_loop_count(stream, 0);
    return true;
}

static void playing(void)
{
    state = station == 10 ? 4 : 2;
}

/* Music_PlayCurrent 0x40c830 */
int Music_PlayCurrent(int n)
{
    int r = 0;
    if (state == 0) return 0;
    track = n;
    if (Music_PlayTrack(n, sequential ? seq_pos(n) : rd(radio_va(n, 2)))) {
        playing();
        r = sequential;
        if (sequential == 1) {
            int c = count() - 1;
            r = c ? (track + 1) / c : 0;
            track = next_seq(track);
        }
    }
    return r;
}

/* Music_SetVolume 0x40c8b0 ("CD SET VOL") */
void Music_SetVolume(int vol)
{
    if (stream) mss_set_stream_volume(stream, (uint8_t)vol);
    volume = (uint8_t)vol;
}

void Music_Stop(void) { stop_and_save(); }          /* 0x40c910 */
void Music_StopSavePos(void) { stop_and_save(); }   /* 0x40cb90 */

/* The restart of a failed stream shared by Music_ServiceSequential and Music_NextStation. */
static void restart_failed(void)
{
    if (!sequential) {
        wr(radio_va(track, 2), rd(radio_va(track, 0)));
    } else {
        wr(cat_va(track, 2), rd(cat_va(track, 0)));
        track = next_seq(track);
    }
}

/* Music_ServiceSequential 0x40ca00: services the stream; every 26th call (re)sets the volume and
   restarts the track if its stream failed ("Offset %d"). */
int Music_ServiceSequential(void)
{
    mss_service_stream(stream);
    int32_t old = countdown--;
    if (old >= 0) return 0;
    countdown = 25;
    if (state != 2 && state != 4) return 0;
    mss_set_stream_volume(stream, volume);
    if (!driver() || !stream) return 0;
    if (mss_stream_status(stream) != -1) return 0;
    restart_failed();
    if (state == 0) return 0;
    if (Music_PlayTrack(track, sequential ? seq_pos(track) : rd(radio_va(track, 2)))) {
        playing();
        if (sequential == 1) track = next_seq(track);
    }
    return sequential;
}

/* Music_ServiceFrontend 0x40cc80: restarts the frontend track if its stream failed. (Nothing in the exe
   starts a stream in the frontend, so this only acts on a stream left from before.) */
void Music_ServiceFrontend(void)
{
    if (state == 0 || !driver() || !stream) return;
    if (mss_stream_status(stream) != -1) return;
    if (frontend_track) {
        if (!Music_PlayTrack(0, rd(radio_va(0, 0)))) { state = 0; return; }
    } else {
        bool ok = Music_PlayTrack(1, rd(radio_va(9, 0)));
        state = 0;
        if (!ok) return;
    }
    state = 2;
}

/* Music_ServicePaused 0x40cd40 */
void Music_ServicePaused(void)
{
    if (state == 0 || !driver() || !stream) return;
    if (mss_stream_status(stream) != -1) return;
    if (!Music_PlayTrack(0, frontend_track ? rd(radio_va(0, 0)) : rd(radio_va(9, 0)))) state = 0;
}

/* Music_UpdateRadio 0x40cde0. In a car (view kind 0, car +0x80 == 0) and not already playing a station
   (state 2), tune to the car's station; on foot, switch to track 8 unless it is already playing (4).
   Leaving a car keeps nothing: re-entering any car resumes the station's track where it was left. */
void Music_UpdateRadio(void)
{
    const SndWorld *w = snd_world;
    if (!sequential) {
        int view_kind = w ? w->view_kind : 1;
        if (view_kind == 0) {
            const SndCar *c = w && w->cars && w->view_id >= 0 && w->view_id < w->ncars ? &w->cars[w->view_id] : NULL;
            if (state == 2 || !c || c->engine_off != 0) goto service;
            station = c->radio;
            int32_t t = station_track(station * 3 + rot[station & 0xff]);
            if (state == 0) goto service;
            stop_and_save();
            state = 1;
            track = t;
            if (!Music_PlayTrack(t, sequential ? seq_pos(t) : rd(radio_va(t, 2)))) goto service;
            playing();
        } else {
            if (state == 4) goto service;
            station = 10;
            if (state == 0) goto service;
            if (state == 2) stop_and_save();
            state = 1;
            track = 8;
            if (!Music_PlayTrack(8, sequential ? rd(0x50166c) : rd(0x5015e0))) goto service;
            playing();
        }
    } else {
        if (state == 2 || state == 4 || state == 0) goto service;
        if (!Music_PlayTrack(track, seq_pos(track))) goto service;
        playing();
    }
    if (sequential == 1) track = next_seq(track);
service:
    Music_ServiceSequential();
}

/* Music_NextStation 0x40d140 (and its thunk 0x404c60), the radio key: next track of the station with a
   burst of static (not on stations 6, 7, 8 or on foot), or the next track in sequential mode. Then the
   same service as Music_ServiceSequential, restarting through Music_PlayCurrent. */
void Music_NextStation(void)
{
    if (!initialised || snd_frozen()) return;
    bool ok;
    if (!sequential) {
        if (station == 6 || station == 7 || station == 10 || station == 8) return;
        stop_and_save();
        Snd_PlayRadioStatic();
        uint8_t i = (uint8_t)((rot[station & 0xff] + 1) % 3);
        rot[station & 0xff] = i;
        int32_t t = station_track(station * 3 + i);
        if (state == 0) goto service;
        track = t;
        ok = Music_PlayTrack(t, rd(radio_va(t, 2)));
    } else {
        wr(cat_va(track, 2), rd(cat_va(track, 0)));
        stop_and_save();
        track = next_seq(track);
        if (state == 0) goto service;
        ok = Music_PlayTrack(track, seq_pos(track));
    }
    if (ok) {
        playing();
        if (sequential == 1) track = next_seq(track);
    }
service:
    mss_service_stream(stream);
    int32_t old = countdown--;
    if (old >= 0) return;
    countdown = 25;
    if (state != 2 && state != 4) return;
    mss_set_stream_volume(stream, volume);
    if (Music_GetStatus() == 3) {
        restart_failed();
        Music_PlayCurrent(track);
    }
}

/* Music_GetStatus 0x40d550: 0 no stream, 1 playing, 3 failed */
int Music_GetStatus(void)
{
    if (!driver() || !stream) return 0;
    return mss_stream_status(stream) == -1 ? 3 : 1;
}

/* Music_Pause 0x40d5c0: closes the stream without saving the position */
void Music_Pause(void)
{
    if (state != 2 && state != 4) return;
    if (track < 0 || track > 9) track = 5;
    mss_pause_stream(stream, true);
    mss_close_stream(stream);
    stream = NULL;
    state = 3;
}

void Music_SetSequential(bool on) { sequential = on ? 1 : 0; }   /* 0x40d620 */
bool Music_Sequential(void) { return sequential != 0; }
int Music_State(void) { return state; }
int Music_Track(void) { return track; }
int Music_Station(void) { return station; }
int Music_StreamVolume(void) { return mss_stream_volume(stream); }
