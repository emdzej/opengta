/* Traffic lights (the lights part of 0x47dcf0-0x480e10) and the junction override table of the
   emergency services module (0x505f00; Junction_InitOverrides 0x419400, Junction_UpdateOverrideTimers
   0x41e140). See docs/traffic.md.

   Lights_Init scans the map for blocks whose type map ext field (bits 16-18) is 1 (Map_TestBlockAttr
   2); each connected patch of them is a junction: its arms (straight runs of such blocks leaving the
   patch) are classified against 11 templates (exe 0x4b3520) and get one light each (88 at most, table
   0x776ae8), with a light sprite (collision kind 7) and a pole sprite (kind 0xe). The lights of a
   junction share a group number (0x58 groups at most; the override table is indexed by it). Every 8
   frames the lights advance through 6 phases (exe 0x4b3514: colour state, duration in 8-frame ticks). */
#pragma once
#include "../render/sprite.h"
#include "layout.h"
#include <stdbool.h>
#include <stdint.h>

enum {
    LIGHTS_MAX = 88,            /* records 0x776ae8 (the 0x57 limit of Lights_AddJunction) */
    LIGHTS_CELLS = 0x2c0,       /* junction cells 0x775fe0 */
    LIGHTS_ARMS = 16,           /* arms of the junction being built 0x77d3d8 (13 bytes each) */
    LIGHTS_TEMPLATES = 11,      /* exe 0x4b3520, 21 bytes each */
    LIGHTS_PHASES = 6,          /* exe 0x4b3514, 2 bytes each */
    JUNCTION_OVR_MAX = 0x58,    /* override table 0x505f00 */
    LIGHTS_RAIL_MAX = 44,
};
/* Lights_Query / Lights_Command codes */
enum {
    LQ_MODE = 0x32,             /* light +0 mode (0 automatic, 3 forced) */
    LQ_ORIENT = 0x33,           /* +1 orientation 0..3 */
    LQ_STATE = 0x34,            /* colour state of the phase: 0 red, 1 amber, 2 flashing amber, 3 green */
    LQ_KIND = 0x35,             /* +3 junction kind (template) */
    LQ_GROUP_MODE = 0x36,       /* command: mode of every light of the junction */
    LQ_ADD_MODE = 0x38,         /* command: add to the mode of every light of the junction */
    LQ_FORCE = 0x39,            /* command: force the junction (mode 3, a phase by the argument) */
    LQ_GROUP = 0x3a,            /* +8 junction (group) number; no junction block here: fatal -0xa9 */
    LQ_ALL_RED = 0x3b,          /* 1 when every light of the junction shows state 0 */
    LQ_RAIL_3C = 0x3c,          /* (x, y) in the rail table 0x77d180 */
    LQ_RAIL_3D = 0x3d,          /* (x, y) in the rail crossing table 0x77cf58 */
};
/* Lights_Query results that are not values */
enum { LIGHTS_OK = 0x14, LIGHTS_BAD = 0x16, LIGHTS_NONE = 0x17, LIGHTS_NOT_INIT = 0x19, LIGHTS_BAD_MODE = 0x1a };

/* A light (0x124 bytes). */
typedef struct Light {
    uint8_t mode;               /* +0x00 0 automatic, 3 forced (1 / 2 make Lights_Update fail) */
    uint8_t orient;             /* +0x01 0..3: which side of the junction; the sprite's place and angle */
    uint8_t phase;              /* +0x02 0..5 */
    uint8_t kind;               /* +0x03 junction kind (template); 10 / 11 are railway crossings */
    uint8_t long_ticks;         /* +0x04 the duration of phases 0 and 3 (12) */
    uint8_t x, y, z;            /* +0x05 the arm's stop block */
    uint8_t group;              /* +0x08 the junction */
    uint8_t tick;               /* +0x09 ticks in the phase */
    int16_t frame_ofs;          /* +0x0a added to the sprite frame (always 0) */
    int16_t countdown;          /* +0x0c ticks left of a forced state (0x32) */
    int16_t u0e;
    Sprite light;               /* +0x10 the light (collision kind 7) */
    Sprite pole;                /* +0x6c the pole (collision kind 0xe) */
    Sprite u_c8;                /* +0xc8 (not used by the lights code) */
} Light;
GAME_OFS(Light, long_ticks, 0x04); GAME_OFS(Light, group, 0x08); GAME_OFS(Light, frame_ofs, 0x0a);
GAME_OFS(Light, countdown, 0x0c); GAME_OFS(Light, light, 0x10);
GAME_OFS32(Light, pole, 0x6c); GAME_SIZE32(Light, 0x124);

/* A junction cell (0x775fe0): a block of a junction or of one of its arms, and the light it belongs to. */
typedef struct { uint8_t x, y, z, light; } LightCell;

/* The module's state (bss around 0x776ae0-0x77d473). */
typedef struct {
    Light lights[LIGHTS_MAX + 2];   /* 0x776ae8 (the group walks read one record past the last light) */
    LightCell cells[LIGHTS_CELLS];  /* 0x775fe0 */
    uint16_t ncells;            /* 0x77d464 */
    uint8_t nlights;            /* 0x77d463 */
    uint8_t njunctions;         /* 0x77d467 the next group number */
    uint8_t ncross;             /* 0x77d466 railway blocks inside crossing junctions */
    uint8_t cross[LIGHTS_RAIL_MAX][4];   /* 0x77d2b8 {x, y, z, first light} */
    uint8_t ready;              /* 0x77d460 Lights_Init done */
    uint8_t rail_ready;         /* 0x77d461 Rail_Init returned 0x14 */
    uint8_t frame;              /* 0x77d462 0..7 */
    uint8_t tick;               /* 0x77d178 the global tick in the phase */
    uint8_t phase_a;            /* 0x77d2b4 the phase of orientations 0 / 1 */
    uint8_t phase_b;            /* 0x77d0c4 the phase of orientations 2 / 3 (starts 3) */
    int16_t frame_red, frame_green, frame_amber, frame_off, frame_pole;   /* 0x77d45c, 0x77d45e, 0x77d008, 0x77d00a, 0x77d45a */
    int16_t frame_flash;        /* 0x776ae0 toggles amber / unlit every 8 frames */
    uint8_t sel, sel_x, sel_y;  /* 0x4b3510.. the light the last query / command found */
    uint8_t cur_z;              /* 0x77d0c3 the layer of the last junction block found */
    /* the junction being built */
    uint8_t seed_x, seed_y;     /* 0x776ae3, 0x776ae2 */
    uint8_t narms;              /* 0x77d470 */
    uint8_t seed_arms;          /* 0x77d472 arms the outermost flood found */
    uint8_t tmpl;               /* 0x77d0c2 template whose orientations are used */
    /* rail tables Lights_Query 0x3c / 0x3d reads (the rail tracer fills them; Lights_Init zeroes the counts) */
    uint8_t rail_n46a, rail_n46b;   /* 0x77d46a, 0x77d46b */
    uint8_t rail_77d180[LIGHTS_RAIL_MAX][7];
    uint8_t rail_77cf58[LIGHTS_RAIL_MAX][4];   /* {x, y, ?, light} level crossings */
} LightsState;
extern LightsState g_lights;

/* A junction override (0x5c bytes, 0x505f00 + group * 0x5c): an emergency vehicle holding a junction's
   lights (Sentinel_OverrideLights 0x41e1c0 sets owner, timer 0x3c and the saved mode). */
typedef struct {
    uint8_t id;                 /* +0x00 */
    uint8_t u01;
    int16_t obj;                /* +0x02 the traffic-light object (type 0x10) at the junction (-1) */
    uint8_t owner;              /* +0x04 the controller holding it (0xff free) */
    uint8_t u05;
    int16_t timer;              /* +0x06 frames left; at 0 the owner is cleared */
    uint8_t mode;               /* +0x08 the junction's mode when taken */
    uint8_t u09;
    uint8_t x, y;               /* +0x0a the object's block */
    int16_t obj_angle;          /* +0x0c the object's sprite angle (the object's is set to 0) */
    uint8_t u0e, u0f;
    int16_t u10;
    uint8_t u12[0x48];          /* +0x12 (-1) */
    int16_t u5a;
} JunctionOvr;
_Static_assert(sizeof(JunctionOvr) == 0x5c, "junction override");
extern JunctionOvr g_junction_ovr[JUNCTION_OVR_MAX];   /* 0x505f00 */

void lights_init(void);                     /* Lights_Init 0x47dcf0 */
int lights_update(void);                    /* Lights_Update 0x47e420 (0x14 ok, 0x16 a light in mode 1 / 2, 0x19 no init) */
int lights_query(int what, int bx, int by); /* Lights_Query 0x47df00 */
int lights_command(int cmd, int arg, int bx, int by);   /* Lights_Command 0x47e2a0 */
void lights_update_sprite(int i);           /* Lights_UpdateSprite 0x480e10 */
void junction_init_overrides(void);         /* Junction_InitOverrides 0x419400 */
void junction_update_override_timers(void); /* Junction_UpdateOverrideTimers 0x41e140 */
