/* The game-level sound module 0x402640-0x405760 and the audio mode switch 0x414620-0x4146a0.

   Looping sounds: every game frame Snd_UpdateGame clears the "wanted" list 0x4bdc88 (10 entries of
   {source id, sample, rate, distance, volume, pan, matched, fast}), Snd_GatherLoops fills it with the 10
   nearest looping sources (kept sorted farthest first: an insertion drops entry 0) and
   Snd_UpdateLoopChannels matches it by (source id, sample) against the 10 playing channels 0x4bdb60,
   slewing rate (500, or 3000 per frame for "fast" sources, plus a Doppler term from the change in
   distance), volume and pan (5 per frame), stopping what is no longer wanted and starting what is new on
   a free channel. One-shots go to 3 positional channels (the farthest is stolen), voices to the voice
   channel, the police scanner's words to the menu channel from a 100-entry queue, one per frame. */
#include "snd_internal.h"
#include "exe.h"
#include <string.h>

SndGame sndg = { .music_on = true, .sfx_on = true, .music_vol = 0x7f, .sfx_vol = 0x7f, .scanner_fast = true,
                 .music_usable = true, .opt = { .sound = true } };

/* ---- host ---- */

SndHost snd_host;
SndWorld *snd_world;
static uint32_t crt_seed = 1;

int snd_crt_rand(void)
{
    crt_seed = crt_seed * 214013u + 2531011u;
    return (int)(crt_seed >> 16 & 0x7fff);
}
void snd_crt_srand(uint32_t seed) { crt_seed = seed; }

void Snd_SetHost(const SndHost *host) { snd_host = host ? *host : (SndHost){0}; }
void Snd_SetWorld(SndWorld *w) { snd_world = w; }
void Snd_SetOptions(const SndOptions *o) { if (o) sndg.opt = *o; }

int snd_rand(void) { return snd_host.crt_rand ? snd_host.crt_rand(snd_host.user) : snd_crt_rand(); }

void snd_listener(int32_t *x, int32_t *y, int32_t *z)
{
    *x = *y = *z = 0;
    if (snd_host.listener) snd_host.listener(snd_host.user, x, y, z);
}

bool snd_covered(int32_t x, int32_t y, int32_t z)
{
    return snd_host.covered && snd_host.covered(snd_host.user, x, y, z);
}

bool snd_frozen(void) { return snd_host.frozen && snd_host.frozen(snd_host.user); }

/* 0x4bddac: s = (s * 0x79 + 1) & 0x7ff (the original's sign fix-up for a negative value never applies) */
uint32_t snd_lcg_state;
uint32_t snd_lcg(void)
{
    snd_lcg_state = (snd_lcg_state * 0x79 + 1) & 0x7ff;
    return snd_lcg_state;
}

/* ---- state (0x4bdxxx) ---- */

typedef struct {
    int16_t src;                         /* +0x00 car / train / crane / emitter index (0 for the singletons) */
    int32_t sample;                      /* +0x04 0x84 = none */
    int32_t rate;                        /* +0x08 */
    int32_t dist;                        /* +0x0c Manhattan distance to the listener */
    int32_t vol;                         /* +0x10 */
    int32_t pan;                         /* +0x14 */
    uint8_t matched;                     /* +0x18 */
    uint8_t fast;                        /* +0x19 rate slews by 3000 */
} SndLoop;

static SndLoop wanted[10];               /* 0x4bdc88 */
static SndLoop chan[10];                 /* 0x4bdb60 */
static SndLoop gather_tmp;               /* Snd_GatherLoops' local entry: see there */
static int32_t scanner[100];             /* 0x4bd978 police scanner words (level samples) */
static uint32_t scan_head, scan_tail;    /* 0x4bdc78, 0x4bdc80 */
typedef struct { int32_t x, y, z; bool active; } Emitter;
static Emitter emitter_a[5];             /* 0x4bdb10 loop 0x46 */
static Emitter emitter_b[4];             /* 0x4bddb0 loop 0x1c (only the nearest) */
static int32_t shot_dist[3];             /* 0x4bdda0 */
static bool saved_sfx;                   /* 0x4bdb08 */
static bool once;                        /* 0x4bd970 bit 0 */
static uint8_t music_vol_now;            /* 0x4bdc7c the music volume as last set (halved in tunnels) */
static bool alarm;                       /* 0x4bddf2 */
static int8_t static_level;              /* 0x4bddf4 radio static in tunnels */
static uint8_t static_count;             /* 0x4bddf5 */
static int32_t last_ped_voice;           /* 0x4bddf8 */
static int8_t ped_voice_wait[3];         /* 0x4bddfc */
static uint32_t heli_rate = 22000;       /* 0x4ab254 */
static int8_t scream_wait = 3, voice_b_wait = 2, voice_c_wait = 1;   /* 0x4ab258..0x4ab25a */
static bool drained;                     /* (port) Snd_ShutdownFrontend has run */
static uint32_t drain_start;             /* (port) when */

static void reset_wanted(void)
{
    for (int i = 0; i < 10; i++) {
        wanted[i].src = 0;
        wanted[i].sample = SND_NO_SAMPLE;
        wanted[i].dist = 0x3ffffff;
    }
}

static void reset_channels(void)
{
    for (int i = 0; i < 10; i++) {
        chan[i].src = 1;
        chan[i].sample = SND_NO_SAMPLE;
        chan[i].dist = 0x3ffffff;
    }
}

/* Inserts into the wanted list: before the first entry not farther than e, dropping entry 0 (the
   farthest); nothing if e is not nearer than entry 0. */
static void loop_insert(const SndLoop *e)
{
    int i = 0;
    while (i < 10 && e->dist < wanted[i].dist) i++;
    if (i == 0) return;
    i--;
    if (i > 0) memmove(&wanted[0], &wanted[1], (size_t)i * sizeof *wanted);
    wanted[i] = *e;
}

static int32_t iabs(int32_t x) { return x < 0 ? -x : x; }
static int32_t dist2(int32_t x, int32_t y, int32_t lx, int32_t ly) { return iabs(x - lx) + iabs(y - ly); }
/* __ftol(v * k) for an unsigned 32-bit v and a double constant */
static int32_t ftol_d(uint32_t v, double k) { return (int32_t)((double)v * k); }

static int32_t ftol_float(float f)
{
    double d = f;
    if (!(d > -9.2e18 && d < 9.2e18)) return 0;      /* x87 "integer indefinite": low dword 0 */
    return (int32_t)(uint32_t)(uint64_t)(int64_t)d;
}

/* The horn pattern table 0x4ab091 (rows of 50 bytes, indexed row * 50 - time), from the exe. */
static bool horn_on(int8_t row, int16_t time)
{
    const uint8_t *p = exe_data((uint32_t)(0x4ab091 + (int32_t)row * 0x32 - time), 1);
    return p && *p;
}

static int music_sequential(void);

/* Snd_SetSfxEnabled 0x402640 .. Snd_SuspendSfx 0x402670 */
void Snd_SetSfxEnabled(bool on) { sndg.sfx_on = on; }
void Snd_SetMusicEnabled(bool on) { sndg.music_on = on; }
void Snd_RestoreSfx(void) { sndg.sfx_on = saved_sfx; }
void Snd_SuspendSfx(void) { saved_sfx = sndg.sfx_on; sndg.sfx_on = false; }

/* The radio sample a car type plays (convertibles, or the player's car without CD music). */
static int radio_sample(uint8_t type)
{
    switch (type) {
    case 0: return 0x7e;
    case 1: return 0x7f;
    case 3: return 0x80;
    case 4: return 0x81;
    case 5: return 0x82;
    default: return 0;
    }
}

/* Snd_GatherLoops 0x402690. The entry being built is one local that is reused from source to source and
   never cleared, so fields a source doesn't set carry over (the pan of the tunnel static goes on to the
   damaged-engine entry, the "fast" flag from one car to the next, and at the very start of the frame it
   holds whatever the stack held; here: what the previous call left). */
void Snd_GatherLoops(void)
{
    static const SndWorld empty;
    const SndWorld *w = snd_world ? snd_world : &empty;
    SndLoop *e = &gather_tmp;
    if (!once) {
        once = true;
        music_vol_now = sndg.music_vol;
    }
    uint32_t r = snd_lcg();
    int ncars = w->ncars;
    int32_t lx, ly, lz;
    snd_listener(&lx, &ly, &lz);
    if (w->player_kind == 2) static_level = 0;

    for (int i = 0; i < ncars && w->cars; i++) {
        SndCar *c = &w->cars[i];
        if (c->status == -1) continue;
        int32_t d = dist2(c->x, c->y, lx, ly);
        if (d >= 0x3ffffff) continue;
        bool player = w->player_kind == 0 && (int16_t)w->player_id == c->id;
        e->src = (int16_t)i;
        e->dist = d;
        uint32_t vol = (uint32_t)Snd_DistToVolume(d);
        bool covered = snd_covered(c->x, c->y, c->z);
        if (covered) vol >>= 1;
        e->pan = Snd_DistToPan(lx - c->x);
        if (c->burning) {                               /* on fire */
            e->vol = ftol_d(vol, 0.3);
            e->sample = 0x4b;
            e->rate = Snd_GetSampleRate(0x4b) + (int32_t)(r % 2000);
            loop_insert(e);
        }
        uint8_t horn = c->horn;
        int horn_sample = horn / 10 + 0x3a;
        int32_t horn_rate = Snd_GetSampleRate(horn_sample) + (horn % 10) * 0x200;
        if (c->damage > 100) continue;
        if (c->siren == 2) {
            if (horn < 0x3c) {
                e->sample = horn_sample;
                e->rate = horn_rate;
                e->vol = ftol_d(vol, 0.5);
                loop_insert(e);
                e->sample = 0x43;
                e->rate = Snd_GetSampleRate(0x43);
                loop_insert(e);
            } else {
                e->sample = 0x42;
                e->rate = Snd_GetSampleRate(0x42);
                e->vol = ftol_d(vol, 0.5);
                loop_insert(e);
            }
        } else if (c->siren == 1) {
            if (horn == 0x21 || horn >= 0x3c) {
                e->sample = 0x43;
                e->rate = Snd_GetSampleRate(0x43);
                e->vol = ftol_d(vol, 0.5);
            } else {
                e->vol = ftol_d(vol, 0.5);
                switch ((c->id % 3) & 0xff) {
                case 0:
                    e->sample = 0x40;
                    e->rate = (int32_t)((double)Snd_GetSampleRate(0x40) * 1.7);
                    break;
                case 1:
                    e->rate = horn_rate;
                    e->sample = horn_sample;
                    break;
                case 2:
                    e->sample = 0x40;
                    e->rate = Snd_GetSampleRate(0x40);
                    break;
                }
            }
            loop_insert(e);
        }

        if (c->engine_on != 1 || c->engine_off != 0) continue;
        int16_t sp = c->speed;
        e->fast = c->fast_change;
        e->sample = c->engine + 0x2d;
        int32_t rate;
        uint32_t cv;
        switch (c->sound_fn) {
        case 0:
            rate = Snd_GearRate(sp);
            if (e->sample == 0x36) rate *= 2;
            e->fast = 1;
            goto engine_vol_player;
        case 1:
            rate = Snd_GearRate(sp) >> 1;
        engine_vol_player:
            if (player) {
                e->vol = (int32_t)((double)Snd_CalcVolume(sp) * 0.8);
                if (covered) e->vol = (int32_t)((uint32_t)e->vol >> 1);
                break;
            }
            cv = (uint32_t)(Snd_CalcVolume(sp) * (int32_t)vol) / 100;
            e->vol = ftol_d(cv, 0.6);
            break;
        case 2:
        case 3:
            rate = Snd_CalcEngineRate(sp);
            if (c->sound_fn == 3) rate *= 3;
            cv = (uint32_t)(Snd_CalcVolume(sp) * (int32_t)vol) / 100;
            e->vol = ftol_d(cv, 0.6);
            break;
        case 4:
            rate = Snd_CalcRateC(sp);
            if (player) {
                e->vol = Snd_CalcVolume(sp);
                if (covered) e->vol = (int32_t)((uint32_t)e->vol >> 1);
            } else {
                cv = (uint32_t)(Snd_CalcVolume(sp) * (int32_t)vol) / 100;
                e->vol = ftol_d(cv, 0.6);
            }
            break;
        case 5:
            rate = Snd_CalcRateB(sp);
            goto engine_vol_player;
        default:
            rate = 4000;
            e->sample = SND_NO_SAMPLE;           /* still takes a place in the list */
            e->vol = 0;
            break;
        }
        e->rate = rate;
        if (c->fast_pitch) e->rate = rate * 3;
        loop_insert(e);
        e->fast = 0;

        if (c->b147 == -1 && c->speed > 0 && c->engine == 6) {
            e->vol = ftol_d(vol, 0.4);
            e->sample = 0x45;
            e->rate = Snd_GetSampleRate(0x45);
            loop_insert(e);
        }
        if (c->sound_fn == 5) {                         /* bus: air brake once stopped after >= 5 */
            int16_t s = c->speed;
            if (s > c->brake_peak) c->brake_peak = s;
            if (s == 0 && c->brake_peak >= 5) {
                if (sndg.sfx_on && sndg.running) Snd_Play3D(c->x, c->y, 0, 0x29);
                c->brake_peak = 0;
            }
        }
        if (c->sound_fn != 4) {                         /* skids (not the tank) */
            uint32_t sk = (uint32_t)ftol_float(c->skid);
            bool play;
            if (c->sound_fn == 5) {
                play = (sp > 14 && (double)sk > 40.0) || (c->skid_flag == 1 && c->speed >= 2);
                rate = (int32_t)((sk >> 1) * 150 + 8000);
            } else {
                play = sk > 12 || (c->skid_flag == 1 && c->speed >= 2);
                rate = (int32_t)(sk * 150 + 8000);
            }
            if (play) {
                e->sample = 0x41;
                e->rate = rate;
                if (player) {
                    e->vol = Snd_CalcVolume(sp);
                    if (covered) e->vol = (int32_t)((uint32_t)e->vol >> 1);
                } else {
                    cv = (uint32_t)(Snd_CalcVolume(sp) * (int32_t)vol) / 100;
                    e->vol = ftol_d(cv, 0.6);
                }
                loop_insert(e);
            }
        }
        /* horns */
        if (c->control != 1) {
            if (c->horn_time > 0 && horn < 0x3c && c->horn_pattern != -1 && horn_on(c->horn_pattern, c->horn_time)) {
                e->sample = horn_sample;
                e->rate = horn_rate;
                e->vol = ftol_d(vol, 0.4);
                loop_insert(e);
            }
        } else if (!player && c->horn_time > 0 && horn < 0x3c) {
            e->sample = horn_sample;
            e->rate = horn_rate;
            e->vol = ftol_d(vol, 0.4);
            loop_insert(e);
        }
        if (!player) {
            /* other cars: a convertible's radio */
            if (c->convertible & 1) {
                e->vol = ftol_d(vol, 0.8);
                int s = radio_sample(c->radio);
                if (s) {
                    e->sample = s;
                    e->rate = Snd_GetSampleRate(s);
                    loop_insert(e);
                }
            }
        } else {
            /* the player's car: the radio sample if there is no CD music, the horn, the tunnel static */
            if (!(sndg.music_usable && sndg.music_on) && (c->convertible & 1)) {
                e->vol = ftol_d(vol, 0.8);
                int s = radio_sample(c->radio);
                if (s) {
                    e->sample = s;
                    e->rate = Snd_GetSampleRate(s);
                    loop_insert(e);
                }
            }
            if (c->horn_time > 0 && horn < 0x3c) {
                e->sample = horn_sample;
                e->rate = horn_rate;
                e->vol = covered ? 0x28 : 0x50;
                loop_insert(e);
            }
            e->fast = 1;
            if (sndg.music_on && sndg.music_usable && !music_sequential() && c->radio < 8) {
                if (covered) {
                    e->sample = 0x4a;
                    if (static_level < 100) static_level = (int8_t)(static_level + 10);
                    else static_level = (int8_t)(r % 40 + 60);
                    int8_t h = (int8_t)(static_level >> 1);
                    e->vol = h;
                    e->rate = Snd_GetSampleRate(0x4a) + h * 0x80;
                    e->pan = 0x40;
                    loop_insert(e);
                    if (music_vol_now == sndg.music_vol) {
                        music_vol_now = sndg.music_vol >> 1;
                        Music_SetVolume(music_vol_now);
                    }
                } else {
                    if (static_level > 0) {
                        static_level = (int8_t)(static_level - 10);
                        e->sample = 0x4a;
                        e->vol = static_level;
                        e->rate = Snd_GetSampleRate(0x4a) + static_level * 0x80;
                        e->pan = 0x40;
                        loop_insert(e);
                    }
                    if (static_level < 0) static_level = 0;
                    if (music_vol_now != sndg.music_vol) {
                        music_vol_now = sndg.music_vol;
                        Music_SetVolume(music_vol_now);
                    }
                }
            }
        }
        if (c->engine_off == 0 && c->damage > 50 && c->damage < 100) {   /* a damaged engine */
            e->sample = 0x35;
            e->rate = Snd_GearRate(sp);
            uint32_t dv = (uint32_t)((c->damage - 50) * (int32_t)vol) / 50;
            e->vol = ftol_d(dv, 0.5);
            loop_insert(e);
        }
    }
    e->fast = 0;

    for (int i = 0; i < w->ntrains && w->trains; i++) {
        const SndTrain *t = &w->trains[i];
        if (!t->speed) continue;
        int32_t d = dist2(t->x, t->y, lx, ly);
        if (d >= 0x3ffffff) continue;
        e->src = (int16_t)i;
        e->dist = d;
        e->vol = ftol_d((uint32_t)Snd_DistToVolume(d), 0.7);
        if (snd_covered(t->x, t->y, t->z)) e->vol = (int32_t)((uint32_t)e->vol >> 1);
        e->pan = Snd_DistToPan(lx - t->pan_x);
        e->sample = 0x37;
        e->rate = Snd_CalcRateA(t->speed);
        loop_insert(e);
    }
    for (int i = 0; i < 4; i++) {
        const SndCrane *k = &w->cranes[i];
        if (k->state == 9 || k->state == 8) continue;
        int32_t d = dist2(k->x, k->y, lx, ly);
        if (d >= 0x3ffffff) continue;
        e->src = (int16_t)i;
        e->dist = d;
        e->vol = ftol_d((uint32_t)Snd_DistToVolume(d), 0.7);
        e->pan = Snd_DistToPan(lx - k->x);
        e->sample = 0x44;
        if (k->state == 0 || k->state == 6) {
            e->rate = 12000;
            e->vol = (int32_t)((uint32_t)e->vol >> 1);
        } else {
            e->rate = 14000;
        }
        loop_insert(e);
    }
    for (int i = 0; i < 5; i++) {
        const Emitter *m = &emitter_a[i];
        if (!m->active) continue;
        int32_t d = dist2(m->x, m->y, lx, ly);
        if (d >= 0x3ffffff) continue;
        e->src = (int16_t)i;
        e->dist = d;
        e->vol = ftol_d((uint32_t)Snd_DistToVolume(d), 0.7);
        e->pan = Snd_DistToPan(lx - m->x);
        e->sample = 0x46;
        e->rate = Snd_GetSampleRate(0x46);
        loop_insert(e);
    }
    if (alarm) {
        e->src = 0;
        e->dist = 0;
        e->vol = 0x50;
        e->pan = 0x3f;
        e->sample = 0x47;
        e->rate = Snd_GetSampleRate(0x47);
        loop_insert(e);
    }
    /* emitter B: only the nearest (with none active: distance 0x3ffffff, which never gets in) */
    int32_t best = 0x3ffffff;
    int bi = 0;
    for (int i = 0; i < 4; i++) {
        if (!emitter_b[i].active) continue;
        int32_t d = dist2(emitter_b[i].x, emitter_b[i].y, lx, ly);
        if (d < best) { best = d; bi = i; }
    }
    e->src = 0;
    e->dist = best;
    e->vol = ftol_d((uint32_t)Snd_DistToVolume(best), 0.2);
    e->pan = Snd_DistToPan(lx - emitter_b[bi].x);
    e->sample = 0x1c;
    e->rate = Snd_GetSampleRate(0x1c);
    loop_insert(e);

    if (w->heli_state != 0 && w->heli_state != 1) {
        int32_t d = dist2(w->heli_x, w->heli_y, lx, ly);
        if (d < 0x3ffffff) {
            e->src = 0;
            e->dist = d;
            e->vol = ftol_d((uint32_t)Snd_DistToVolume(d), 0.7);
            e->pan = Snd_DistToPan(lx - w->heli_x);
            e->sample = 0x48;
            switch (w->heli_state) {
            case 2: heli_rate = 22000; break;                       /* rotor up to speed */
            case 3: case 4: case 7: if (heli_rate > 16000) heli_rate -= 100; break;
            case 5: case 6: if (heli_rate < 22000) heli_rate += 100; break;
            }
            e->rate = (int32_t)heli_rate;
            loop_insert(e);
        }
    }
    if (w->leader) {
        int32_t d = dist2(w->leader_x, w->leader_y, lx, ly);
        if (d < 0x3ffffff) {
            e->src = 0;
            e->dist = d;
            e->vol = ftol_d((uint32_t)Snd_DistToVolume(d), 0.7);
            e->pan = Snd_DistToPan(lx - w->leader_x);
            e->sample = 0x49;
            e->rate = Snd_GetSampleRate(0x49);
            loop_insert(e);
        }
    }
}

/* Snd_PlayTalk 0x404410 */
void Snd_PlayTalk(const char *text)
{
    if (sndg.sfx_on && sndg.running) Snd_PlayMumble(text);
}

static int32_t slew(int32_t cur, int32_t target, int32_t step)
{
    if (target == cur) return cur;
    if (target > cur) return cur + step < target ? cur + step : target;
    return cur - step > target ? cur - step : target;
}

/* Snd_UpdateLoopChannels 0x404430. A wanted entry matches every channel with its (source, sample), so
   duplicates in the list update the same channel twice. */
void Snd_UpdateLoopChannels(void)
{
    for (int i = 0; i < 10; i++) wanted[i].matched = chan[i].matched = 0;
    for (int i = 0; i < 10; i++) {
        SndLoop *w = &wanted[i];
        if (w->sample == SND_NO_SAMPLE) continue;
        for (int c = 0; c < 10; c++) {
            SndLoop *ch = &chan[c];
            if (w->src != ch->src || w->sample != ch->sample) continue;
            int32_t old = ch->dist;
            w->matched = ch->matched = 1;
            ch->dist = w->dist;
            w->rate += Snd_CalcDelta(old, w->dist);     /* Doppler: approaching raises the pitch */
            int32_t rate = slew(ch->rate, w->rate, ch->fast ? 3000 : 500);
            Snd_LoopSetRate(c, rate);
            ch->rate = rate;
            int32_t vol = slew(ch->vol, w->vol, 5);
            Snd_LoopSetVolume(c, vol);
            ch->vol = vol;
            int32_t pan = slew(ch->pan, w->pan, 5);
            Snd_LoopSetPan(c, pan);
            ch->pan = pan;
        }
    }
    for (int c = 0; c < 10; c++)
        if (!chan[c].matched) {
            Snd_LoopStop(c);
            chan[c].sample = SND_NO_SAMPLE;
            chan[c].src = 0;
        }
    for (int i = 0; i < 10; i++) {
        if (wanted[i].matched || wanted[i].sample == SND_NO_SAMPLE) continue;
        for (int c = 0; c < 10; c++) {
            if (chan[c].matched) continue;
            chan[c] = wanted[i];
            Snd_LoopSetRate(c, chan[c].rate);
            Snd_LoopSetPan(c, chan[c].pan);
            Snd_LoopSetVolume(c, chan[c].vol);
            Snd_LoopSetSample(c, chan[c].sample);
            Snd_LoopStart(c);
            chan[c].matched = 1;
            wanted[i].matched = 1;
            break;
        }
    }
}

/* Snd_Play3D 0x4046b0: a one-shot at (x, y, z) on a free positional channel, else on the one playing
   the farthest sound if this one is nearer (sample 0x27 always gets in). Its volume (the distance volume,
   halved except for sample 0x27, halved again if covered) is overridden by Snd_ShotStart's. */
void Snd_Play3D(int32_t x, int32_t y, int32_t z, int sample)
{
    int32_t lx, ly, lz;
    snd_listener(&lx, &ly, &lz);
    int32_t d = iabs(y - ly) + iabs(x - lx);
    if (d >= 0x3ffffff) return;
    uint32_t vol = (uint32_t)Snd_DistToVolume(d);
    if (sample != 0x27) vol >>= 1;
    if (snd_covered(x, y, z)) vol >>= 1;
    int32_t pan = Snd_DistToPan(lx - x);
    int ch = 0;
    while (ch < 3 && Snd_ShotIsPlaying(ch)) ch++;
    if (ch == 3) {
        int32_t far = shot_dist[0];
        ch = 0;
        for (int k = 1; k < 3; k++)
            if (shot_dist[k] > far) { far = shot_dist[k]; ch = k; }
        if (sample != 0x27 && d > far) return;
    }
    shot_dist[ch] = d;
    Snd_ShotStop(ch);
    Snd_ShotSetPan(ch, pan);
    Snd_ShotSetVolume(ch, (int32_t)vol);
    Snd_ShotSetSample(ch, sample);
    Snd_ShotStart(ch);
}

void Snd_PlayAt(int32_t x, int32_t y, int32_t z, int sample)        /* 0x4047f0 */
{
    if (sndg.sfx_on && sndg.running) Snd_Play3D(x, y, z, sample);
}

void Snd_PlayAtXY(int32_t x, int32_t y, int sample)                 /* 0x404810 */
{
    if (sndg.sfx_on && sndg.running) Snd_Play3D(x, y, 0, sample);
}

void Snd_PlayUI(int32_t x, int32_t y, int32_t z, int sample)        /* 0x404840 */
{
    (void)z;
    if (sndg.sfx_on && sndg.running) Snd_PlayPositional(x, y, sample);
}

void Snd_PlaySampleN(int sample)                                    /* 0x404860 */
{
    if (sndg.sfx_on && sndg.running) Snd_PlaySfx(sample);
}

/* Snd_PoliceRadio 0x404880: the scanner report replaces the queue: one of 3 intros (0x4f..0x51), 0x52,
   the car model (0x53 + a few known models), the kind (0x61..0x63), up to two direction words
   (0x79..0x7d), the area name (100 + (area - 1) % 20, from the nav data's sample number), 0x4e. */
void Snd_PoliceRadio(int model, int kind, int direction, int area)
{
    if (!sndg.sfx_on || !sndg.running || !(area & 0xff)) return;
    Snd_MenuStop();
    scan_head = 0;
    sndg.scanner_fast = true;
    uint32_t r = snd_lcg();
    scanner[0] = r % 3 == 0 ? 0x4f : r % 3 == 1 ? 0x50 : 0x51;
    scanner[1] = 0x52;
    int m;
    switch (model & 0xff) {
    case 0x09: m = 0x54; break;
    case 0x0c: m = 0x55; break;
    case 0x0e: m = 0x56; break;
    case 0x18: m = 0x57; break;
    case 0x1c: m = 0x58; break;
    case 0x20: m = 0x59; break;
    case 0x22: m = 0x5a; break;
    case 0x23: m = 0x5b; break;
    case 0x2a: m = 0x5c; break;
    case 0x47: m = 0x5d; break;
    case 0x5a: m = 0x5e; break;
    case 0x5b: m = 0x5f; break;
    case 0x60: m = 0x60; break;
    default: m = 0x53; break;
    }
    scanner[2] = m;
    scanner[3] = kind == 0 ? 0x61 : kind == 2 ? 0x63 : 0x62;
    int n = 4;
    switch (direction & 0xff) {
    case 1: scanner[4] = 0x7b; n = 5; break;
    case 2: scanner[4] = 0x79; n = 5; break;
    case 3: scanner[4] = 0x7a; n = 5; break;
    case 4: scanner[4] = 0x7c; n = 5; break;
    case 5: scanner[4] = 0x7d; n = 5; break;
    case 6: scanner[4] = 0x7b; scanner[5] = 0x7a; n = 6; break;
    case 7: scanner[4] = 0x79; scanner[5] = 0x7a; n = 6; break;
    case 8: scanner[4] = 0x7b; scanner[5] = 0x7c; n = 6; break;
    case 9: scanner[4] = 0x79; scanner[5] = 0x7c; n = 6; break;
    }
    scanner[n] = (int32_t)((uint8_t)(area - 1) % 20) + 100;
    uint32_t k = (uint32_t)(n + 1) % 100;
    scanner[k] = 0x4e;
    scan_tail = (k + 1) % 100;
}

/* Snd_SetMusicVolume 0x404bf0 / Snd_SetSfxVolume 0x404c30: level 0..7 through the 8-byte tables 0x4ab048 /
   0x4ab050 (0, 0x12, .. 0x7f). */
static uint8_t volume_table(uint32_t va, int level)
{
    uint8_t l = (uint8_t)level > 7 ? 7 : (uint8_t)level;
    const uint8_t *t = exe_data(va + l, 1);
    return t ? *t : (uint8_t)(l * 0x7f / 7);
}

void Snd_SetMusicVolume(int level)
{
    sndg.music_vol = volume_table(0x4ab048, level);
    Music_SetVolume(sndg.music_vol);
}

void Snd_SetSfxVolume(int level)
{
    sndg.sfx_vol = volume_table(0x4ab050, level);
    Snd_SetMasterVolume(sndg.sfx_vol);
}

/* Snd_PlayRadioStatic 0x404c70: one of the 4 tuning noises 0x1d..0x20 in turn */
void Snd_PlayRadioStatic(void)
{
    if (sndg.running && sndg.sfx_on) {
        int n = (static_count & 3) + 0x1d;
        static_count++;
        Snd_TalkPlay(n);
    }
}

void Snd_SetEmitterA(int slot, int32_t x, int32_t y, int32_t z)    /* 0x404cb0 */
{
    if ((unsigned)(slot & 0xff) < 5) emitter_a[slot & 0xff] = (Emitter){ x, y, z, true };
}
void Snd_ClearEmitterA(int slot) { if ((unsigned)(slot & 0xff) < 5) emitter_a[slot & 0xff].active = false; }   /* 0x404cf0 */
void Snd_EnableAlarmLoop(void) { alarm = true; }                    /* 0x404d10 */
void Snd_DisableAlarmLoop(void) { alarm = false; }                  /* 0x404d20 */
void Snd_PlaySample28(void) { if (sndg.running && sndg.sfx_on) Snd_TalkPlay(0x28); }   /* 0x404d30 */
void Snd_SetEmitterB(int slot, int32_t x, int32_t y, int32_t z)    /* 0x404d50 */
{
    if ((unsigned)(slot & 0xff) < 4) emitter_b[slot & 0xff] = (Emitter){ x, y, z, true };
}
void Snd_ClearEmitterB(int slot) { if ((unsigned)(slot & 0xff) < 4) emitter_b[slot & 0xff].active = false; }   /* 0x404d90 */

/* Snd_Pause 0x404db0 / Snd_Resume 0x404de0 */
void Snd_Pause(void)
{
    if (!sndg.running) return;
    if (sndg.sfx_on) {
        Snd_TalkPlay(0x2c);
        Snd_MuteAll();
    }
    Music_Stop();
}

void Snd_Resume(void)
{
    if (!sndg.running) return;
    Music_Stop();
    if (sndg.sfx_on) Snd_RestoreVolumes();
}

/* Snd_PlayCutsceneVoice 0x404e00: closes the music stream (Music_Pause) and streams AUDIO/n.WAV at the
   sfx level. */
void Snd_PlayCutsceneVoice(int n)
{
    if (sndg.running && sndg.sfx_on && (uint8_t)n <= 5) {
        sndg.cutscene_voice = true;
        Music_Pause();
        Snd_SetStreamVolume(sndg.sfx_vol);
        Snd_MusicPlay(n);
    }
}

void Snd_StopCutsceneVoice(void)                                    /* 0x404e40 */
{
    if (sndg.running && sndg.sfx_on) {
        sndg.cutscene_voice = false;
        Snd_MusicClose();
    }
}

int Snd_GetCutsceneVoiceStatus(void)                                /* 0x404e60 */
{
    if (sndg.running && sndg.sfx_on && sndg.cutscene_voice) return Snd_MusicGetPos();
    return -2;
}

/* Snd_PlayVoice 0x404e90: pan 0x3f, volume 0x7f (then replaced by Snd_VoicePlay's) */
void Snd_PlayVoice(int n)
{
    if (sndg.running && sndg.sfx_on) {
        Snd_VoiceSetPan(0x3f);
        Snd_VoiceSetVolume(0x7f);
        if (n < 0x47) Snd_VoicePlay(n);
    }
}

/* Snd_PlayRandomVoice 0x404ed0: voice 7..10 if the voice channel is free */
void Snd_PlayRandomVoice(void)
{
    uint32_t r = snd_lcg();
    if (sndg.running && sndg.sfx_on && !Snd_VoiceIsPlaying()) {
        Snd_VoiceSetPan(0x3f);
        Snd_VoiceSetVolume((int32_t)(r % 40 + 60));
        uint32_t n = (r & 3) + 7;
        if (n < 0x47) Snd_VoicePlay((int)n);
    }
}

/* The positional voice helpers: distance volume (lost to Snd_VoicePlay's) and pan. */
static bool voice_at(int32_t x, int32_t y, int32_t z)
{
    int32_t lx, ly, lz;
    snd_listener(&lx, &ly, &lz);
    int32_t d = iabs(y - ly) + iabs(x - lx);
    if (d >= 0x3ffffff) return false;
    uint8_t vol = (uint8_t)Snd_DistToVolume(d);
    if (snd_covered(x, y, z)) vol >>= 1;
    Snd_VoiceSetPan(Snd_DistToPan(lx - x));
    Snd_VoiceSetVolume(vol);
    return true;
}

/* Snd_PlayPedVoice 0x404f50: kind 0x12..0x14 waits 50 calls between plays (counting only calls that find
   the voice channel free and the kind different from the last), 0x15 doesn't wait; the sample is
   kind + 4 * (variant % 3). */
void Snd_PlayPedVoice(int32_t x, int32_t y, int32_t z, int kind, int variant)
{
    if (!sndg.running || !sndg.sfx_on || Snd_VoiceIsPlaying() || last_ped_voice == kind) return;
    uint8_t k = (uint8_t)(kind - 0x12);
    if (kind != 0x15) {
        if (k > 2) return;
        int8_t wait = ped_voice_wait[k];
        ped_voice_wait[k] = (int8_t)(wait - 1);
        if (wait > 0) return;
    }
    if (!voice_at(x, y, z)) return;
    last_ped_voice = kind;
    if (kind != 0x15) ped_voice_wait[k] = 0x32;
    int n = kind + ((int16_t)variant % 3) * 4;
    if (n < 0x47) Snd_VoicePlay(n);
}

/* Snd_PlayScream 0x405090, Snd_PlayVoiceB 0x4051b0, Snd_PlayVoiceC 0x4052c0: a random voice of a range,
   then a wait (in calls with the voice channel free). */
static bool voice_wait(int8_t *w)
{
    int8_t old = *w;
    *w = (int8_t)(old - 1);
    return old < 0;
}

void Snd_PlayScream(int32_t x, int32_t y, int32_t z)
{
    uint32_t r = snd_lcg();
    if (!sndg.running || !sndg.sfx_on || Snd_VoiceIsPlaying() || !voice_wait(&scream_wait)) return;
    if (!voice_at(x, y, z)) return;
    uint32_t n = r % 21 + 0x1e;
    if (n < 0x47) Snd_VoicePlay((int)n);
    scream_wait = (int8_t)(r % 6 + 4);
}

void Snd_PlayVoiceB(int32_t x, int32_t y, int32_t z)
{
    uint32_t r = snd_lcg();
    if (!sndg.running || !sndg.sfx_on || Snd_VoiceIsPlaying() || !voice_wait(&voice_b_wait)) return;
    if (!voice_at(x, y, z)) return;
    uint32_t n = r % 9 + 0x34;
    if (n < 0x47) Snd_VoicePlay((int)n);
    voice_b_wait = 2;
}

void Snd_PlayVoiceC(int32_t x, int32_t y, int32_t z)
{
    uint32_t r = snd_lcg();
    if (!sndg.running || !sndg.sfx_on || Snd_VoiceIsPlaying() || !voice_wait(&voice_c_wait)) return;
    if (!voice_at(x, y, z)) return;
    uint32_t n = (r & 7) + 0x3e;
    if (n < 0x47) Snd_VoicePlay((int)n);
    voice_c_wait = 3;
}

/* Snd_PlayAtObject 0x4053d0: sample 0x4c or 0x4d at a player's view target */
void Snd_PlayAtObject(int32_t x, int32_t y, int32_t z)
{
    uint32_t r = snd_lcg();
    if (sndg.running && sndg.sfx_on) Snd_PlayAt(x, y, z, (int)(r & 1) + 0x4c);
}

void Snd_SetMusicAvailable(bool on) { sndg.music_usable = on; }     /* 0x405450 */

static bool music_allowed(void) { return !sndg.opt.demo || sndg.opt.demo_music; }

/* Snd_InitFrontend 0x405460 */
void Snd_InitFrontend(void)
{
    if (sndg.running) return;
    sndg.running = true;
    sndg.cutscene_voice = false;
    Snd_StartupFrontend();
    Snd_LoadLevelBank(0);
    if (music_allowed()) Music_Init();
}

/* Snd_ShutdownFrontend 0x4054a0: unless quiet (and with music on, a quirk: it tests the music flags),
   plays the exit jingle 0xf and waits for it (see Snd_FrontendDraining). */
void Snd_ShutdownFrontend(bool quiet)
{
    if (!sndg.running) return;
    sndg.running = false;
    sndg.cutscene_voice = false;
    Music_Shutdown();
    if (sndg.music_on && sndg.music_usable && !quiet) Snd_PlaySfx(0xf);
    drained = true;
    drain_start = mss_ms_count();
    Snd_Shutdown();
}

bool Snd_FrontendDraining(void)
{
    if (mss_draining()) return true;
    uint32_t end = mss_drained_ms() > drain_start ? mss_drained_ms() : drain_start;
    return drained && mss_ms_count() < end + 1000;
}

/* Snd_UpdateFrontend 0x4054f0 */
void Snd_UpdateFrontend(void)
{
    if (sndg.cutscene_voice) Snd_MusicService();
    else Music_ServiceFrontend();
}

/* Snd_UpdateGame 0x405510: loops and the scanner unless paused, then the radio. */
void Snd_UpdateGame(void)
{
    if (!sndg.running) return;
    bool frozen = snd_frozen();
    if (sndg.sfx_on && !frozen) {
        reset_wanted();
        Snd_GatherLoops();
        Snd_UpdateLoopChannels();
        if (scan_head != scan_tail && !Snd_MenuIsPlaying()) {
            if (sndg.scanner_fast) Snd_PlayMenuSample(scanner[scan_head]);
            else Snd_PlayMenuSampleLow(scanner[scan_head]);
            scan_head = (scan_head + 1) % 100;
        }
    }
    if (sndg.music_on && sndg.music_usable) {
        if (frozen) Music_ServicePaused();
        else Music_UpdateRadio();
    }
}

/* Snd_InitGame 0x405600: LEVEL001 first (Game_Init's Snd_Reset then loads the city's). */
void Snd_InitGame(void)
{
    snd_lcg_state = 0;
    if (sndg.running) return;
    sndg.running = true;
    reset_channels();
    Snd_StartupGame();
    for (int i = 0; i < 5; i++) emitter_a[i].active = false;
    Snd_LoadLevelBank(1);
    Snd_OpenVocalBank();
    if (music_allowed()) Music_Init();
    alarm = false;
    Snd_SetMasterVolume(sndg.sfx_vol);
    if (music_allowed()) Music_SetVolume(sndg.music_vol);
}

/* Snd_ShutdownGame 0x4056c0 */
void Snd_ShutdownGame(void)
{
    if (!sndg.running) return;
    sndg.running = false;
    Snd_CloseVocalBank();
    Music_Shutdown();
    Snd_ShutdownSamples();
}

/* Snd_Reset 0x4056e0 (Game_Init, with the style number) */
void Snd_Reset(int bank)
{
    if (!sndg.running) return;
    reset_wanted();
    Snd_MenuStop();
    scan_head = scan_tail = 0;
    reset_channels();
    Snd_LoadLevelBank(bank);
}

/* Snd_StopAll 0x405760 (Game_Shutdown) */
void Snd_StopAll(void)
{
    if (!sndg.running) return;
    Snd_StopAllLoops();
    Snd_StopAllShots();
    Music_StopSavePos();
    Snd_MenuStop();
    Snd_VoiceStop();
}

/* ---- the audio mode switch (0x414620-0x4146a0), gated by the "sound" launch option 0x502f6c ---- */

void Audio_Update(void)
{
    if (!sndg.opt.sound) return;
    if (sndg.mode == 1) Snd_UpdateGame();
    else if (sndg.mode == 2) Snd_UpdateFrontend();
}

void Audio_EnterGame(void)
{
    if (!sndg.opt.sound) return;
    if (sndg.mode == 2) Snd_ShutdownFrontend(false);
    Snd_InitGame();
    sndg.mode = 1;
}

void Audio_EnterFrontend(void)
{
    if (!sndg.opt.sound) return;
    if (sndg.mode == 1) Snd_ShutdownGame();
    Snd_InitFrontend();
    sndg.mode = 2;
}

void Audio_Shutdown(void)
{
    if (!sndg.opt.sound) return;
    if (sndg.mode == 1) Snd_ShutdownGame();
    else if (sndg.mode == 2) Snd_ShutdownFrontend(true);
}

int Audio_Mode(void) { return sndg.mode; }

void audio_render(float *out, unsigned frames) { mss_render(out, frames); }

static int music_sequential(void) { return Music_Sequential(); }
