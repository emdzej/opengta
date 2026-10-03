/* The wanted level: criminal records and crime reports (0x4131d0-0x414280: Police_ReportCrime
   0x4136c0, the per-city thresholds, the scanner reports) and the pursuit groups (0x40d640-0x40dc7f:
   Police_InitPursuits, Police_StartPursuit, Police_UpdatePursuits). See docs/police.md.

   Only criminal record 0 starts pursuits (Police_StartPursuit), so in single player the player's
   record is the one that matters. */
#pragma once
#include "layout.h"
#include <stdint.h>

enum { CRIMINALS = 4, CRIM_SIGHTINGS = 3, PURSUITS = 4, PURSUIT_COPS = 20 };

/* a radio report (6 bytes): the area it happened in and when to say it */
typedef struct {
    uint8_t area;               /* +0 nav zone sample (Area_GetSample) */
    uint8_t dir;                /* +1 compass part of the zone */
    int16_t timer;              /* +2 frames to the report (-1 none) */
    int16_t crime;              /* +4 the scanner's crime sample (-1 free) */
} CrimSighting;
_Static_assert(sizeof(CrimSighting) == 6, "sighting");

/* A criminal record (0x2c bytes at 0x502dd8). */
typedef struct {
    int16_t crime;              /* +0x00 last crime (2..9, -1 free) */
    int16_t crime2;             /* +0x02 the same */
    int16_t kind;               /* +0x04 0 in a car, 1 on foot, 2 on a train (-1) */
    int16_t u06;                /* +0x06 (-1) */
    int16_t ped;                /* +0x08 the criminal (-1) */
    int16_t car;                /* +0x0a his car (-1) */
    int16_t train;              /* +0x0c his train */
    int16_t started;            /* +0x0e pursuit: -1 none, 0 pending, 1 chased, 3 cops recalled, 4 arrested,
                                   5 his car wrecked, 6 shoot on sight */
    int16_t countdown;          /* +0x10 frames until the pursuit starts (-1) */
    CrimSighting seen[CRIM_SIGHTINGS];   /* +0x12 */
    int16_t cops;               /* +0x24 units wanted (per wanted level and city) */
    int16_t u26;
    int16_t u28;                /* +0x28 (police scanner: 0x1e after a crime against a car) */
    uint8_t u2a, u2b;           /* +0x2a, +0x2b block of that crime */
} Criminal;
GAME_OFS(Criminal, kind, 0x04); GAME_OFS(Criminal, ped, 0x08); GAME_OFS(Criminal, train, 0x0c);
GAME_OFS(Criminal, started, 0x0e); GAME_OFS(Criminal, seen, 0x12); GAME_OFS(Criminal, cops, 0x24);
GAME_OFS(Criminal, u28, 0x28); GAME_OFS(Criminal, u2a, 0x2a);
_Static_assert(sizeof(Criminal) == 0x2c, "criminal record");

/* A pursuit group (0x3a bytes at 0x501ac0). */
typedef struct {
    int16_t id;                 /* +0x00 */
    int16_t active;             /* +0x02 > 0 running (-1 free) */
    int16_t lead;               /* +0x04 the cop nearest the target (-1) */
    int16_t u06;                /* +0x06 */
    int16_t u08;
    int16_t criminal;           /* +0x0a criminal record (-1) */
    int16_t cops[PURSUIT_COPS]; /* +0x0c sentinels in the group (-1) */
    uint8_t u34;                /* +0x34 set while a cop fires at the criminal's car */
    uint8_t shoot_on_sight;     /* +0x35 shoot on sight (wanted level 4 and more) */
    uint8_t shoot;              /* +0x36 the cops shoot (from wanted level 3) */
    int8_t ncops;               /* +0x37 */
    uint8_t lined_up;           /* +0x38 cops lined up beside the target car */
    uint8_t multi;              /* +0x39 network game (players > 1) */
} Pursuit;
GAME_OFS(Pursuit, active, 0x02); GAME_OFS(Pursuit, lead, 0x04); GAME_OFS(Pursuit, criminal, 0x0a);
GAME_OFS(Pursuit, cops, 0x0c); GAME_OFS(Pursuit, u34, 0x34); GAME_OFS(Pursuit, ncops, 0x37);
GAME_OFS(Pursuit, multi, 0x39);
_Static_assert(sizeof(Pursuit) == 0x3a, "pursuit record");

extern Criminal g_criminals[CRIMINALS];     /* 0x502dd8 */
extern Pursuit g_pursuits[PURSUITS];        /* 0x501ac0 */

/* ---- pursuits (0x40d640..) ---- */
void police_init_pursuits(void);            /* Police_InitPursuits 0x40d640 */
void police_end_pursuit(int n);             /* Police_EndPursuit 0x40d690 */
void police_update_pursuits(void);          /* Police_UpdatePursuits 0x40d800 */
void police_start_pursuit(void);            /* Police_StartPursuit 0x40da90 (was Police_SpawnRoadblock) */

/* ---- criminal records (0x4131d0..) ---- */
int police_cops_for_wanted(int player);     /* Police_CopsForWanted 0x4131d0 */
void police_clear_sighting(int i, int j);   /* Police_ClearSighting 0x413210 */
void police_init_criminals(void);           /* Police_InitCriminals 0x413250 */
/* Police_UpdateCriminalTarget 0x4132c0: what a criminal moved from (kind a, id) to (kind b, id2) */
void police_update_criminal_target(int a, int id, int b, int id2);
void police_set_wanted_city1(int ped, int crim);   /* Police_SetWantedCity1 0x4133c0 (NYC) */
void police_set_wanted_city2(int ped, int crim);   /* Police_SetWantedCity2 0x4134d0 (San Andreas) */
void police_set_wanted_city3(int ped, int crim);   /* Police_SetWantedCity3 0x4135e0 (Vice City) */
/* Police_ReportCrime 0x4136c0: a crime `crime` by a car (kind 0, id = car) or a ped (kinds 1, 2) at
   (x, y, z) (0: where it is) */
void police_report_crime(int kind, int id, int crime, int32_t x, int32_t y, int32_t z);
void police_clear_criminal(int i);          /* Police_ClearCriminal 0x414190 */
Criminal *police_get_criminal(int i);       /* Police_GetCriminal 0x414230 (NULL if negative) */
int police_find_criminal_by_ped(int ped);   /* Police_FindCriminalByPed 0x414250 (-1 none) */
void police_tick_radio_reports(void);       /* Police_TickRadioReports 0x414280 */
