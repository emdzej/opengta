/* The wanted level (wanted.h): the pursuit groups (0x40d640-0x40dc7f) and the criminal records and
   crime reports (0x4131d0-0x41429f). docs/police.md. */
#include "wanted.h"
#include "../audio/audio.h"
#include "car.h"
#include "game.h"
#include "mission.h"
#include "ped.h"
#include "player.h"
#include "police.h"
#include "sentinel.h"
#include "trigger.h"
#include "../hud/hud.h"
#include <string.h>

Criminal g_criminals[CRIMINALS];
Pursuit g_pursuits[PURSUITS];

/* the city of the running MISSION.INI section: 1 / 2 NYC, 0x66 / 0x67 San Andreas, 0xca / 0xcb and
   the rest Vice City (the switch of Police_ReportCrime and Police_UpdatePursuits) */
static void set_wanted_by_city(int ped, int crim)
{
    switch (mission_get_ini_section()) {
    case 1: case 2: police_set_wanted_city1(ped, crim); break;
    case 0x66: case 0x67: police_set_wanted_city2(ped, crim); break;
    default: police_set_wanted_city3(ped, crim); break;   /* 0xca, 0xcb and the rest */
    }
}

/* ---------------------------------------------------------------- pursuits */

/* Police_InitPursuits 0x40d640: four free groups (id, active / lead / criminal -1, no cops) */
void police_init_pursuits(void)
{
    for (int i = 0; i < PURSUITS; i++) {
        Pursuit *p = &g_pursuits[i];
        p->id = (int16_t)i;
        p->active = -1, p->lead = -1, p->criminal = -1;
        p->u34 = 0, p->u06 = 0, p->lined_up = 0, p->ncops = 0;
        for (int k = 0; k < PURSUIT_COPS; k++) p->cops[k] = -1;
    }
}

/* Police_EndPursuit 0x40d690: the roadblocks (junction records of the list 0x50586c) whose first car
   belongs to this group get their timers (+0x10) cleared, then
   the group is freed and its cops sent home: a crew in its car goes to state 4, a crew out of it
   (+0x20) to 0xbe with the return state 4 (the car's +0x244 cleared); destinations and the route
   flag cleared. */
void police_end_pursuit(int n)
{
    n = (int16_t)n;
    for (int k = 0; k < g_junction_nlist; k++) {
        JunctionOvr *j = &g_junction_ovr[g_junction_list[k]];
        int16_t car0;   /* the first roadblock car (+0x12) */
        memcpy(&car0, j->u12, sizeof car0);
        if (car0 == -1) {
            j->u10 = 0;
        } else {
            const Sentinel *s = sentinel_ptr(car_get(car0)->sentinel);
            if (s && s->pursuit == (int16_t)n) j->u10 = 0;
        }
    }
    Pursuit *p = &g_pursuits[n];
    p->active = -1, p->lead = -1, p->criminal = -1;
    p->u06 = 0, p->u34 = 0, p->multi = 0, p->lined_up = 0;
    /* (the pursuit index clears the sightings of the criminal record with that number) */
    for (int j = 0; j < CRIM_SIGHTINGS; j++) police_clear_sighting(n, j);
    for (int k = 0; k < p->ncops; k++) {
        Sentinel *s = sentinel_ptr(p->cops[k]);
        if (!s) continue;
        if (s->u20 == 0) {
            s->state = 4;
        } else {
            sentinel_car(s)->u244 = 0;
            s->state = 0xbe;
            s->sub = 4;
        }
        s->dest[0] = s->dest[1] = s->dest[2] = 0;
        s->u4a = 0;
        p->cops[k] = -1;
    }
    p->ncops = 0;
}

/* Police_UpdatePursuits 0x40d800, per running group (active > 0 with a criminal): unless the
   criminal's ped is in state 9, his wanted level is recomputed from the points (per city, which may
   end the pursuit); then the lead is the nearest cop, the chase / roadblock flags follow the wanted
   level (always in network games; in single player only for levels below 5, i.e. a player exists),
   and when the group has more cops than the record wants, the farthest one drives home. No cop at
   all ends the whole update (the original returns). */
void police_update_pursuits(void)
{
    for (int c = 0; c < PURSUITS; c++) {
        Pursuit *p = &g_pursuits[c];
        if (p->active < 1 || p->criminal < 0) continue;
        Criminal *cr = police_get_criminal(p->criminal);
        if (!cr) return;
        if (ped_get(cr->ped)->state == 9) continue;
        set_wanted_by_city(cr->ped, p->criminal);
        if (p->active < 1 || p->criminal < 0) continue;
        p->lead = (int16_t)pursuit_find_nearest_cop(p->id);
        unsigned lvl = (unsigned)player_get_wanted_level_by_ped(cr->ped);
        if (p->multi == 1 || lvl < 5) {
            switch (lvl) {
            case 0: case 1: case 2: p->shoot = 0, p->shoot_on_sight = 0; break;
            case 3: p->shoot_on_sight = 0, p->shoot = 1; break;
            default: p->shoot_on_sight = 1, p->shoot = 1; break;
            }
        }
        int far = (int16_t)pursuit_find_farthest_cop(c);
        if (far == -1) return;
        Sentinel *s;
        if (cr->cops < p->ncops && (s = sentinel_ptr(far)) != NULL) {
            pursuit_remove_cop(s);
            cop_recall_one(s);
        }
    }
}

/* Police_StartPursuit 0x40da90 (Police_SpawnRoadblock in the first names): only criminal record 0.
   With a crime and the pursuit pending (+0xe = 0) its countdown runs; at 0 the first group that
   isn't running is taken if it is free (-1): the record's number of units, each the nearest
   patrolling police car to the criminal (state 199, this group) queued on the dispatch list with the
   criminal as its target, then the pursuit is started. A criminal on a train only starts it. */
void police_start_pursuit(void)
{
    Criminal *cr = police_get_criminal(0);
    if (!cr || cr->crime == 0 || cr->started != 0) return;   /* (a free record's crime is -1: != 0) */
    if (--cr->countdown >= 1) return;
    int c = 0;
    while (g_pursuits[c].active > 0)
        if (++c > 3) return;
    if (cr->kind < 0) return;
    if (cr->kind == 2) {
        cr->started = 1;
        return;
    }
    if (cr->kind >= 2) return;
    Pursuit *p = &g_pursuits[c];
    if (p->active >= 0) return;   /* (0: neither running nor free, the record stays pending) */
    p->criminal = 0;
    p->active = 1;
    p->multi = g_session_players >= 2;
    int16_t s = -1;
    int target = 0;
    for (int n = cr->cops; n > 0; n--) {
        if (cr->kind == 0) {
            const Car *car = car_get(cr->car);
            target = car->id;
            s = (int16_t)police_find_nearest_car(car->spr.x, car->spr.y);
        } else if (cr->kind == 1) {
            const Ped *pd = ped_get(cr->ped);
            target = pd->id;
            s = (int16_t)police_find_nearest_car(pd->spr.x, pd->spr.y);
        }
        if (s > -1) {
            Sentinel *r = sentinel_ptr(s);
            r->state = 199;
            r->pursuit = (int16_t)c;
        }
        if (g_police_ndispatch >= POLICE_DISPATCH_MAX) game_fatal(-0x4a, 0x1c3, g_police_ndispatch);   /* port: no bound */
        PoliceDispatch *d = &g_police_dispatch[g_police_ndispatch];
        d->active = 1;
        d->sentinel = s;
        d->target = target;
        d->kind = (uint8_t)cr->kind;
        d->pursuit = (int8_t)c;
        g_police_ndispatch++;
    }
    cr->started = 1;
}

/* ---------------------------------------------------------------- criminal records */

/* Police_CopsForWanted 0x4131d0: wanted level 1-2 -> 1, 3-4 -> 2, else 0 */
int police_cops_for_wanted(int player)
{
    switch (player_get_wanted_level(player)) {
    case 1: case 2: return 1;
    case 3: case 4: return 2;
    default: return 0;
    }
}

/* Police_ClearSighting 0x413210 */
void police_clear_sighting(int i, int j)
{
    CrimSighting *s = &g_criminals[(int16_t)i].seen[(int16_t)j];
    s->area = 0, s->dir = 0;
    s->crime = -1, s->timer = -1;
}

/* Police_InitCriminals 0x413250: every id -1, no units, no sightings */
void police_init_criminals(void)
{
    for (int i = 0; i < CRIMINALS; i++) {
        Criminal *c = &g_criminals[i];
        c->countdown = -1, c->started = -1, c->u06 = -1, c->kind = -1, c->crime2 = -1, c->crime = -1;
        c->ped = -1, c->car = -1;
        c->u28 = 0, c->u2a = 0, c->u2b = 0;
        for (int j = 0; j < CRIM_SIGHTINGS; j++) {
            c->seen[j].area = 0, c->seen[j].dir = 0;
            c->seen[j].crime = -1, c->seen[j].timer = -1;
        }
        c->cops = 0;
    }
}

/* Police_UpdateCriminalTarget 0x4132c0: a player's ped changed what he is in: kind a / id (1 the
   ped itself, 0 a car, 2 a train) to kind b / id2. The record is found by the player's ped (id for
   a = 1, else id2), and changes only if it still tracks (a, id). */
void police_update_criminal_target(int a, int id, int b, int id2)
{
    a = (int16_t)a, b = (int16_t)b;
    int16_t sid = (int16_t)id, sid2 = (int16_t)id2;
    int16_t who = a == 1 ? sid : sid2;
    if (ped_get(who)->player_ctl != 1) return;
    int i = 0;
    while (i < CRIMINALS && g_criminals[i].ped != who) i++;
    if (i == CRIMINALS) return;
    Criminal *c = &g_criminals[i];
    if (c->kind != a) return;
    if (a == 1) {
        if (sid != c->ped) return;
    } else if (a == 0) {
        if (sid != c->car) return;
    } else if (a == 2) {
        if (sid != c->train) return;
    } else {
        return;
    }
    c->kind = (int16_t)b;
    if (b == 0) c->car = sid2;
    else if (b == 2) c->train = sid2;
}

/* The three cities' thresholds (Police_SetWantedCity1..3 0x4133c0 / 0x4134d0 / 0x4135e0): the
   wanted points of the ped give the level 0..4 (none with the no-patrols switch 0x503184), the
   player's level is set, a non-zero level makes the record's pursuit countdown 1, and the level
   gives the number of units the record wants (+0x24); level 0 clears the record. */
static void set_wanted(int ped, int crim, const int limits[4], const int16_t units[5])
{
    int16_t pts = (int16_t)player_get_wanted_points(ped);
    int lvl = pts <= limits[0] ? 0 : pts <= limits[1] ? 1 : pts <= limits[2] ? 2 : pts <= limits[3] ? 3 : 4;
    if ((uint8_t)g_police_no_patrols != 0) lvl = 0;
    player_set_wanted_level(ped, lvl);
    Criminal *c = &g_criminals[(int16_t)crim];
    if (lvl != 0) c->countdown = 1;
    c->cops = units[lvl];
    if (lvl == 0) police_clear_criminal(crim);
}

/* NYC: 151 / 251 / 351 / 501 points for levels 1..4, 1..4 units */
void police_set_wanted_city1(int ped, int crim)
{
    static const int lim[4] = { 0x96, 0xfa, 0x15e, 500 };
    static const int16_t units[5] = { 0, 1, 2, 3, 4 };
    set_wanted(ped, crim, lim, units);
}
/* San Andreas: 101 / 201 / 251 / 376, units 1, 2, 4, 6 */
void police_set_wanted_city2(int ped, int crim)
{
    static const int lim[4] = { 0x64, 0xc8, 0xfa, 0x177 };
    static const int16_t units[5] = { 0, 1, 2, 4, 6 };
    set_wanted(ped, crim, lim, units);
}
/* Vice City: 101 / 176 / 251 / 351, one unit at every level */
void police_set_wanted_city3(int ped, int crim)
{
    static const int lim[4] = { 0x64, 0xaf, 0xfa, 0x15e };
    static const int16_t units[5] = { 0, 1, 1, 1, 1 };
    set_wanted(ped, crim, lim, units);
}

/* Police_ReportCrime 0x4136c0. Crimes (the record's +0, with the wanted points, the score bonus kind
   and the scanner's crime sample):
     2 car hit by a player's car (2 points; bonus 0x18; sample 0xe; also +0x28 = 30 and the block),
     3 (50; -; 0x18), 4 (10; 0x1c; 0x1c), 5 car stolen (15; by model 0x1c..0x24; 0x1c),
     6 (50; 0x25; 0x20), 7 (1; -; 0x22), 8 (100; -; 0x23), 9 bank robbery (100; -; 0x2a).
   The reporter must be a player: kind 0 a car (its driver; physics control, not model 0x2f; crime 2
   needs a speed outside -5..11; a matching delayed report of the player's slot in the mission's
   timed table 0x7715d8 is set counting instead), kinds 1 / 2 a player-controlled ped. The record is
   the ped's, else the first free one. A pursuit becomes pending with a countdown of 200 frames (on
   foot), 100 (in a car), or 1 when the car last touched a police car (control 3), which also gives
   a player without a wanted level the points of level 1. The crime's area gets a scanner report
   (at most one per area, direction and crime; three slots) with that delay, and the wanted level
   is recomputed for the city. */
void police_report_crime(int kind, int id, int crime, int32_t x, int32_t y, int32_t z)
{
    kind = (int16_t)kind, crime = (int16_t)crime;
    int16_t sid = (int16_t)id;
    if (sid < 0) return;
    const Ped *ped = NULL;
    const Car *car = NULL;
    int player;
    int32_t pz;
    if (kind == 0) {
        car = car_get(sid);
        if (car->driver < 0) return;
        player = (int16_t)player_find_by_ped(car->driver);
        if (player == -1 || car->control != CAR_CTL_PHYSICS || car->model == 0x2f) return;
        if (x == 0) pz = car->spr.z, x = car->spr.x, y = car->spr.y;
        else pz = z;
        if (crime == 2 && car->speed > -6 && car->speed < 0xc) return;
        if (car->driver == -1) return;
        ped = ped_get(car->driver);
        /* (the timed table is indexed by the player number here) */
        for (int n = (int16_t)player_first(); n > -1; n = (int16_t)player_next(n)) {
            TimedBomb *b = &g_mrt.bombs[n];
            if (b->id == sid && b->who == crime && b->kind == kind) {
                if (b->state == 0) {
                    b->state = 1;
                    return;
                }
                if (b->state == 1) return;
            }
        }
    } else if (kind < 1 || kind > 2) {
        game_fatal(-0x4a, 0x1c7, kind);
    } else {
        ped = ped_get(sid);
        player = (int16_t)player_find_by_ped(sid);
        if (player == -1) return;
        if (x == 0) pz = ped->spr.z, x = ped->spr.x, y = ped->spr.y;
        else pz = z;
        if (ped->player_ctl != 1) return;
    }

    int i = 0;
    while (i < CRIMINALS && g_criminals[i].ped != ped->id) i++;
    if (i < CRIMINALS) {
        g_criminals[i].crime = (int16_t)crime;
    } else {
        i = 0;
        while (i < CRIMINALS && g_criminals[i].crime != -1) i++;
        if (i == CRIMINALS) return;
    }
    Criminal *c = &g_criminals[i];
    c->crime = (int16_t)crime;
    c->kind = (int16_t)kind;
    c->crime2 = (int16_t)crime;
    int16_t delay = 100;
    if (kind == 1 || kind == 2) {
        c->ped = sid;
        if (c->started == -1) c->started = 0, c->countdown = 200;
    } else {
        c->car = sid;
        c->ped = ped->id;
        if (car->hit_car < 0 || car_get(car->hit_car)->control != CAR_CTL_AI3) {
            if (c->started == -1) c->started = 0, c->countdown = 100;
        } else {
            delay = 1;
            if (c->started == -1) {
                c->countdown = 1;
                c->started = 0;
                int16_t pid = ped_get(c->ped)->id;
                if (player_get_wanted_level((int16_t)player_find_by_ped(c->ped)) == 0) {
                    int16_t wp = (int16_t)player_get_wanted_points_idx((int16_t)player_find_by_ped(c->ped));
                    int lim = 0x65, give = 0x66;
                    if (mission_get_ini_section() == 1 || mission_get_ini_section() == 2) lim = 0x97, give = 0x98;
                    if (wp <= lim) {
                        player_set_wanted_level(pid, 1);
                        player_clear_wanted_points(pid);
                        player_add_wanted_points(pid, give);
                    }
                }
            }
        }
    }

    /* the crime: points, bonus, the counters; the "at least 3 / 6 units" are overwritten by the
       city's level at the end (as in the original) */
    int16_t sample;
    switch (c->crime) {
    case 2:
        c->u28 = 0x1e;
        c->u2a = (uint8_t)(x >> 22), c->u2b = (uint8_t)(y >> 22);
        sample = 0xe;
        player_add_wanted_points(ped->id, 2);
        if (c->cops < 3) c->cops = 3;
        player_award_bonus(player, 0x18, x, y, pz, 1, 0);
        player_inc_kills(player, 2);
        break;
    case 3:
        sample = 0x18;
        player_add_wanted_points(ped->id, 0x32);
        if (c->cops < 3) c->cops = 3;
        player_inc_kills(player, 3);
        break;
    case 4:
        player_add_wanted_points(ped->id, 10);
        sample = 0x1c;
        if (c->cops < 3) c->cops = 3;
        player_award_bonus(player, 0x1c, x, y, pz, 1, 0);
        player_inc_kills(player, 4);
        break;
    case 5: {
        int b;
        switch (car_get(sid)->model) {
        case 3: case 0x29: b = 0x1e; break;
        case 4: case 0x20: b = 0x23; break;
        case 5: case 0xf: case 0x10: b = 0x22; break;
        case 9: case 0xb: case 0xc: b = 0x21; break;
        case 0x23: b = 0x20; break;
        case 0x2a: b = 0x24; break;
        case 0x2b: b = 0x1f; break;
        default: b = 0x1c; break;
        }
        player_award_bonus(player, b, x, y, pz, 1, 0);
        player_add_wanted_points(ped->id, 0xf);
        sample = 0x1c;
        if (c->cops < 3) c->cops = 3;
        player_inc_kills(player, 5);
        break;
    }
    case 6:
        player_add_wanted_points(ped->id, 0x32);
        sample = 0x20;
        if (c->cops < 3) c->cops = 3;
        player_award_bonus(player, 0x25, x, y, pz, 1, 0);
        player_inc_kills(player, 6);
        break;
    case 7:
        sample = 0x22;
        player_add_wanted_points(ped->id, 1);
        if (c->cops < 6) c->cops = 6;
        player_inc_kills(player, 7);
        break;
    case 8:
        sample = 0x23;
        player_add_wanted_points(ped->id, 100);
        if (c->cops < 6) c->cops = 6;
        player_inc_kills(player, 8);
        break;
    case 9:
        player_add_wanted_points(ped->id, 100);
        sample = 0x2a;
        if (c->cops < 6) c->cops = 6;
        player_inc_kills(player, 9);
        break;
    default:
        sample = 0;
        break;
    }

    int32_t ax, ay;
    if (c->kind == 0) {
        const Car *cc = car_get(c->car);
        ax = cc->spr.x, ay = cc->spr.y;
    } else {
        const Ped *pp = ped_get(c->ped);
        ax = pp->spr.x, ay = pp->spr.y;
    }
    uint8_t area, dir;
    area_get_sample((uint8_t)(ax >> 22), (uint8_t)(ay >> 22), &area, &dir);
    int j;
    for (j = 0; j < CRIM_SIGHTINGS; j++)
        if (c->seen[j].area == area && c->seen[j].dir == dir && c->seen[j].crime == sample) break;
    if (j == CRIM_SIGHTINGS) {
        for (j = 0; j < CRIM_SIGHTINGS; j++) {
            if (c->seen[j].crime != -1) continue;
            c->seen[j].area = area, c->seen[j].dir = dir;
            c->seen[j].crime = sample, c->seen[j].timer = delay;
            break;
        }
    }
    set_wanted_by_city(c->ped, i);
}

/* Police_ClearCriminal 0x414190: unless the criminal's ped is in state 9, his pursuit ends and the
   record is freed (the sightings stay). */
void police_clear_criminal(int i)
{
    i = (int16_t)i;
    if (i < 0) return;   /* port: the original reads record -1 */
    Criminal *c = &g_criminals[i];
    if (c->ped >= 0 && ped_get(c->ped)->state == 9) return;   /* (ped -1 read before the table) */
    for (int k = 0; k < PURSUITS; k++)
        if (g_pursuits[k].criminal == (int16_t)i) {
            police_end_pursuit(k);
            break;
        }
    c->cops = 0;
    c->countdown = -1, c->started = -1;
    c->kind = -1, c->crime = -1, c->ped = -1, c->car = -1;
}

/* Police_GetCriminal 0x414230 */
Criminal *police_get_criminal(int i)
{
    i = (int16_t)i;
    return i < 0 ? NULL : &g_criminals[i];
}

/* Police_FindCriminalByPed 0x414250 */
int police_find_criminal_by_ped(int ped)
{
    for (int i = 0; i < CRIMINALS; i++)
        if (g_criminals[i].ped == (int16_t)ped) return i;
    return -1;
}

/* Police_TickRadioReports 0x414280: every pending report counts down; when it passes 0 the scanner
   says it (not with the no-patrols switch) and the slot is freed. */
void police_tick_radio_reports(void)
{
    for (int i = 0; i < CRIMINALS; i++)
        for (int j = 0; j < CRIM_SIGHTINGS; j++) {
            CrimSighting *s = &g_criminals[i].seen[j];
            if (s->timer < 0 || --s->timer != -1) continue;
            if ((uint8_t)g_police_no_patrols == 0) Snd_PoliceRadio((uint8_t)s->crime, 0, s->dir, s->area);
            s->area = 0, s->dir = 0;
            s->crime = -1, s->timer = -1;
        }
}
