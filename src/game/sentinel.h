/* Emergency services, "sentinels" (0x419000-0x4227a0; the error text 0x4b0b2c calls them so): the AI
   driver records that drive police cars, ambulances, fire engines and mission dummy cars (table
   0x507ea0, 129 x 0x98 bytes, Sentinel_Get 0x41ad60), their per-frame driving (Sentinel_DriveCar
   0x41aed0, called from Cars_UpdateAll for control modes 2, 3, 9 and 10), the per-frame dispatcher
   Emergency_UpdateAll 0x419880 (ported from the disassembly: the decompiler fails on it), the
   ambulance call queue, the police dispatch list, the junction light overrides and the chasers.
   See docs/police.md.

   The record mirrors the original's 0x98 bytes; field widths are those Sentinel_ClearTable 0x41adb0
   writes. The car pointer at +0x40 is kept as a car id + 1 (0 = none) so that the record has the
   original's layout on every host (dummy.c and car.c reach it as raw bytes through sentinel_get). */
#pragma once
#include "ai.h"
#include "car.h"
#include "layout.h"
#include "lights.h"
#include "path.h"
#include <stdbool.h>
#include <stdint.h>

enum {
    SENTINEL_MAX = 0x81,        /* Sentinel_Get accepts 0..0x80; the clear loops do 0x80 */
    SENTINEL_FREE_SCAN = 0x32,  /* Sentinel_FindFree looks at the first 50 */
    SENT_REQUESTS = 0x3fc,      /* 0x50cab0: peds 0..619, then cars + 0x26c */
    SENT_REQ_CAR_BASE = 0x26c,
    AMBU_CALLS_MAX = 0x3fc,     /* 0x505048 */
    POLICE_DISPATCH_MAX = 100,  /* 0x5058a8 */
    JUNCTION_LIST_MAX = 0x16,   /* 0x50586c */
    POLICE_CHASERS = 4,         /* 0x505eec */
};

/* kinds (+0x02) */
enum { SENT_FREE = 0, SENT_AMBULANCE = 1, SENT_POLICE = 2, SENT_ROUTE = 5, SENT_FIRE = 6, SENT_DUMMY = 9 };

typedef struct Sentinel {
    int16_t id;                 /* +0x00 own index */
    int8_t kind;                /* +0x02 SENT_* (0 free) */
    uint8_t pad03;
    int32_t u04;                /* +0x04 (car.c: 1 = an ambulance on a call) */
    int16_t u08;                /* +0x08 */
    int16_t group;              /* +0x0a dummy convoy group */
    uint8_t u0c;                /* +0x0c current route node x, 0 = none (dummy: "keep the group") */
    uint8_t u0d, u0e;           /* +0x0d, +0x0e route node y, z */
    uint8_t u0f;                /* +0x0f route follower: laps over a repeated node (4: state 2) */
    uint8_t u10, u11;           /* +0x10, +0x11 cleared when the node distance restarts */
    uint8_t u12;                /* +0x12 stuck recovery step (Sentinel_HandleStuck; > 0 active) */
    uint8_t u13;                /* +0x13 the road direction the recovery started on */
    int16_t u14, u16, u18;      /* +0x14 Chebyshev blocks to the route node, +0x16 its minimum */
    uint8_t u1a;                /* +0x1a passed the node on the wrong lane (side probes) */
    uint8_t state;              /* +0x1b state machine (per kind) */
    uint8_t sub;                /* +0x1c sub state / the state to return to */
    uint8_t pad1d;
    int16_t car_id;             /* +0x1e the car it drives (-1) */
    uint8_t u20;                /* +0x20 (police: the crew is out of the car) */
    uint8_t u21, u22;           /* +0x21 ambulance: current victim, +0x22 victims */
    uint8_t pad23;
    int16_t victims[10];        /* +0x24 ambulance victims (-1) */
    int32_t u38;                /* +0x38 cleared when a path search starts */
    uint8_t u3c;
    uint8_t pad3d[3];
    int32_t car_ref;            /* +0x40 Car * in the original: here car id + 1, 0 = none */
    int16_t route;              /* +0x44 path slot it follows (-1) */
    uint8_t dist;               /* +0x46 Chebyshev distance in blocks to the target */
    uint8_t pad47;
    int16_t u48;                /* +0x48 the route slot before the last search (-1) */
    int8_t u4a;                 /* +0x4a > 0: no route, steered directly (Sentinel_Steer); 0: route nodes */
    uint8_t u4b;
    int16_t u4c;
    uint8_t u4e;                /* +0x4e turn round: 2 turning, 3 aligning, 1 backing out, 0 none */
    uint8_t pad4f;
    int16_t u50, u52, u54, u56; /* +0x50.. (-1) */
    uint8_t u58;                /* +0x58 traffic-light override step 0..4 (Sentinel_OverrideLights) */
    uint8_t u59;                /* +0x59 the junction's saved mode (0xff: it was red already) */
    uint8_t u5a;                /* +0x5a forced (the saved mode, non-zero) */
    uint8_t u5b, u5c;           /* +0x5b, +0x5c the junction block */
    uint8_t pad5d;
    int16_t u5e;                /* +0x5e the override record (lights.h JunctionOvr) / roadblock (-1) */
    int16_t u60, u62;           /* (-1) */
    uint8_t dest[3];            /* +0x64 destination block x, y, z */
    uint8_t pad67;
    int16_t u68, u6a;           /* (-1) */
    int16_t pursuit;            /* +0x6c pursuit group (-1) */
    uint8_t u6e, u6f, u70;
    uint8_t pad71;
    int16_t u72, u74;
    uint8_t u76;
    uint8_t u77[30];            /* +0x77 10 x (x, y, z) */
    uint8_t u95, u96;
    uint8_t pad97;
} Sentinel;
GAME_OFS(Sentinel, kind, 0x02); GAME_OFS(Sentinel, u04, 0x04); GAME_OFS(Sentinel, group, 0x0a);
GAME_OFS(Sentinel, u0c, 0x0c); GAME_OFS(Sentinel, u14, 0x14); GAME_OFS(Sentinel, state, 0x1b);
GAME_OFS(Sentinel, car_id, 0x1e); GAME_OFS(Sentinel, u20, 0x20); GAME_OFS(Sentinel, victims, 0x24);
GAME_OFS(Sentinel, u38, 0x38); GAME_OFS(Sentinel, u3c, 0x3c); GAME_OFS(Sentinel, car_ref, 0x40);
GAME_OFS(Sentinel, route, 0x44); GAME_OFS(Sentinel, dist, 0x46); GAME_OFS(Sentinel, u48, 0x48);
GAME_OFS(Sentinel, u4a, 0x4a); GAME_OFS(Sentinel, u4c, 0x4c); GAME_OFS(Sentinel, u4e, 0x4e);
GAME_OFS(Sentinel, u50, 0x50); GAME_OFS(Sentinel, u58, 0x58); GAME_OFS(Sentinel, u5e, 0x5e);
GAME_OFS(Sentinel, dest, 0x64); GAME_OFS(Sentinel, u68, 0x68); GAME_OFS(Sentinel, pursuit, 0x6c);
GAME_OFS(Sentinel, u6e, 0x6e); GAME_OFS(Sentinel, u72, 0x72); GAME_OFS(Sentinel, u76, 0x76);
GAME_OFS(Sentinel, u77, 0x77); GAME_OFS(Sentinel, u95, 0x95);
_Static_assert(sizeof(Sentinel) == 0x98, "sentinel record");

/* An ambulance / wreck request (10 bytes at 0x50cab0, indexed by ped id, or car id + 0x26c). */
typedef struct {
    int16_t id;                 /* +0 own index */
    uint8_t x, y, z;            /* +2 block (moved onto the nearest road) */
    uint8_t state;              /* +5 0 none, 1 being set up, 2 queued (cars: marked for removal) */
    uint8_t u6;                 /* +6 */
    uint8_t pad7;
    int16_t sentinel;           /* +8 the crew sent (-1) */
} SentRequest;
_Static_assert(sizeof(SentRequest) == 10, "sentinel request");

/* A police dispatch entry (16 bytes at 0x5058a8): a unit to send at a criminal. */
typedef struct {
    int16_t active;             /* +0x0 1, -1 when done */
    int16_t pad2;
    int32_t target;             /* +0x4 Car * / Ped * in the original: here the car / ped id */
    uint8_t kind;               /* +0x8 0 car, 1 ped */
    uint8_t pad9;
    int16_t sentinel;           /* +0xa the police car found or spawned (-1) */
    int8_t pursuit;             /* +0xc pursuit group */
    uint8_t padd[3];
} PoliceDispatch;
_Static_assert(sizeof(PoliceDispatch) == 16, "police dispatch");

/* The junction overrides (0x505f00, JunctionOvr) are lights.h's; Map_FindNearestRoad and Path_Find
   are path.h's. */

/* The table itself (0x507ea0) and its accessors Sentinel_Get / FindFree / ClearTable / Reset are
   ai.c's (the traffic port, struct AiCtl: the same 0x98 bytes); a Sentinel is that record. */
extern SentRequest g_sent_requests[SENT_REQUESTS];  /* 0x50cab0 */
extern int16_t g_ambu_calls[AMBU_CALLS_MAX];        /* 0x505048 */
extern int16_t g_ambu_ncalls;                       /* 0x50584e */
extern PoliceDispatch g_police_dispatch[POLICE_DISPATCH_MAX];   /* 0x5058a8 */
extern int16_t g_police_ndispatch;                  /* 0x50f28e */
extern int16_t g_junction_list[JUNCTION_LIST_MAX];  /* 0x50586c */
extern int16_t g_junction_nlist;                    /* 0x50584a */
extern int16_t g_police_chasers[POLICE_CHASERS];    /* 0x505eec */
/* what Sentinel_DriveCar computes for the record it is driving (the kind handlers, the steering and
   the probes read them) */
extern int16_t g_sent_bx, g_sent_by;                /* 0x50caa0, 0x50caa2 the car's block */
extern int16_t g_sent_bz;                           /* 0x50caa8 */
extern int16_t g_sent_half_len;                     /* 0x504f4c the look-ahead (0x20 / 0x24 / the car's half length) */
extern int16_t g_sent_node_dx, g_sent_node_dy;      /* 0x504f4a, 0x50584c route node minus the next one (0: last) */
extern int16_t g_sent_dest_dist;                    /* 0x505850 Chebyshev blocks from the car to +0x64 */
extern int32_t g_sent_req;                          /* 0x504f60 the car's +0x120 (a request pointer there) */
/* written and never read by the original (kept as globals so the writes stay) */
extern int32_t g_sent_505848;                       /* 0x505848 cleared by Sentinel_InitAll */
extern int16_t g_car_last_wreck;                    /* 0x4bde00 the last car Car_RegisterWreck queued */
extern int32_t g_sent_route_car;                    /* 0x75cd50 Sentinel_PlanRouteToTarget's car (car id + 1) */

/* the car a record drives (+0x40), NULL if none. Records set up by dummy.c / ai.c only fill +0x1e
   (the car id): then that one is used. */
static inline Car *sentinel_car(const Sentinel *s)
{
    if (s->car_ref) return car_get(s->car_ref - 1);
    return s->car_id >= 0 && s->car_id < CAR_MAX ? car_get(s->car_id) : NULL;
}
static inline void sentinel_set_car(Sentinel *s, const Car *c) { s->car_ref = c ? c->id + 1 : 0; }

/* ---- the table ---- */
/* Sentinel_Get 0x41ad60 (ai.c's sentinel_get: fatal -0x91 if negative, NULL past 0x80) */
static inline Sentinel *sentinel_ptr(int i) { return (Sentinel *)(void *)sentinel_get(i); }
static inline AiCtl *sentinel_ai(Sentinel *s) { return (AiCtl *)(void *)s; }
void sentinel_init_all(void);               /* Sentinel_InitAll 0x41abd0 */
void sentinel_pick_nearest_base(Sentinel *s);   /* Sentinel_PickNearestBase 0x4194e0 */
void sentinel_pick_nearest_base_to(Sentinel *s, const uint8_t *q);   /* 0x4195e0 (q: block bytes at +2..+4) */
void police_add_chaser(int s);              /* Police_AddChaser 0x4196d0 */
void police_update_chasers(void);           /* Police_UpdateChasers 0x419740 */

/* ---- per frame ---- */
void emergency_update_all(void);            /* Emergency_UpdateAll 0x419880 */
void sentinel_drive_car(Car *c);            /* Sentinel_DriveCar 0x41aed0 */
/* Sentinel_OverrideLights 0x41e1c0 (rec: a Sentinel; dummy.c passes the raw record) */
void sentinel_override_lights(void *rec, Car *c);
/* Sentinel_WarpCar 0x419000: the car to (x, y, z) (16.16, a block centre), 0 / 1 / 2 as the original */
int sentinel_warp_car(Car *c, int32_t x, int32_t y, int32_t z);
int sentinel_warp_to_nearest_road(Car *c);  /* Sentinel_WarpToNearestRoad 0x41a750 */
void sentinel_plan_route_to_target(Sentinel *s);   /* Sentinel_PlanRouteToTarget 0x41a9d0 */

/* ---- ambulance requests and wrecks ---- */
void ambulance_request_for_ped(int ped);    /* Ambulance_RequestForPed 0x41a1a0 */
void ambulance_cancel_for_ped(int ped);     /* Ambulance_CancelForPed 0x41a250 */
void ambulance_clear_request(int id);       /* Ambulance_ClearRequest 0x41a3d0 */
void car_mark_for_removal(int car);         /* Car_MarkForRemoval 0x41a360 */
bool car_is_marked_for_removal(int car);    /* Car_IsMarkedForRemoval 0x41a470 */
/* Car_RegisterWreck 0x405790 / Car_TryRemoveWreck 0x405800: car-module functions on the request
   table (here, with the table) */
void car_register_wreck(int car, const Car *c);
int car_try_remove_wreck(SentRequest *r);
