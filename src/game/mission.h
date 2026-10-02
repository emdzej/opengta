/* MISSION.INI: the section reader (Mission_ReadIni 0x44ab90 and its tokeniser 0x44ace0-0x44b130) and
   the section loader (Mission_Load 0x445800): the header line, the object lines (script objects and
   the entities they create) and the command lines (the command table and labels the interpreter
   runs). Grammar: docs/game-core.md; the runtime (mission_run.h): docs/missions.md. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum {
    MISSION_SECTION_MAX = 2000,       /* Mission_SetIniSection 0x44ab50 accepts 0..2000 */
    MISSION_TEXT_MAX = 0x37001,       /* the section text buffer 0x6b3e9c */
    MISSION_COMMANDS = 5000,          /* 0x676660 */
    MISSION_OBJECTS = 1400,           /* 0x5f30e8 */
    MISSION_LINES = 0x10000,          /* label / line maps */
    MISSION_PROCESSES = 32,
    MISSION_CLEANUP = 150,            /* RESET cleanup lists of cars, peds, objects */
    MISSION_OPCODE_NONE = 999,        /* unused command records */
    MISSION_TYPE_SPAWNED_PED = 99,    /* script object type of a created PED */
    MISSION_KF_MAX = 90000,           /* the bound Mission_Load checks */
};

/* A command record (24 bytes): opcode (index into the name table 0x4b0ec0; -1 / 0xffff if the word
   is unknown), then five ints: a (usually an object line), b (success label), c (fail label), d, e. */
typedef struct {
    uint16_t op;
    uint8_t pad[2];
    int32_t a, b, c, d, e;
} MissionCommand;
_Static_assert(sizeof(MissionCommand) == 24, "command record");

/* A script object (32 bytes): type (index into 0x4b0d98; 99 = created PED), handle (car, ped,
   object, trigger, door... id, or a parameter), a second parameter, x, y, z (as written: blocks, or
   pixels for some types), and the persistent flag (written with a digit before the coordinates). */
typedef struct {
    int32_t type;               /* +0x00 */
    int32_t handle;             /* +0x04 */
    int32_t param;              /* +0x08 */
    int32_t pad0c;
    int32_t x, y, z;            /* +0x10 */
    uint8_t persistent;         /* +0x1c */
    uint8_t pad1d[3];
} MissionObject;
_Static_assert(sizeof(MissionObject) == 32, "script object");

typedef struct {
    /* Mission_ReadIni */
    int section;                /* 0x6b3e28 (Mission_SetIniSection) */
    char name[0x52];            /* 0x6b3e30 */
    int number2;                /* 0x6b3e84 second header value (only passed to the stripped Dbg_Nop) */
    char cmp[0xe];              /* 0x6b3e88 */
    int style;                  /* 0x6b3e98 */
    char *text;                 /* 0x6b3e9c (allocated once, 0x37001 bytes) */
    const char *p;              /* 0x6b3ea0 tokeniser position */
    /* header line */
    int traffic_cars;           /* 0x65617c cars to spawn at start (MisCar_SpawnBatch) */
    int police_on;              /* 0x676600 1: Police_InitForMission */
    int header[6];              /* the six values as read */
    /* tables */
    MissionCommand commands[MISSION_COMMANDS];  /* 0x676660 */
    int ncommands;
    int16_t labels[MISSION_LINES];              /* 0x693b30 label -> command index */
    int16_t line_obj[MISSION_LINES];            /* 0x6561c0 line -> script object */
    MissionObject objects[MISSION_OBJECTS];     /* 0x5f30e8 */
    int nobjects;
    int mission_end_count;      /* 0x5fe008 MISSION_END commands */
    int counter_obj, secret_counter_obj;        /* 0x676388, 0x693b28 */
    int32_t counter_target, secret_target, mission_total, target_score;   /* 0x6765ec, 0x67638c, 0x6b3b78, 0x6b3b84 */
    int16_t bombshop_cost;      /* 0x5fdff8 (5000 unless BOMBSHOP_COST) */
    int16_t player_ped[4];      /* 0x656180 */
    int16_t cur;                /* 0x6b3b70 the process Mission_Update runs (Mission_Load: the next player a PLAYER line binds) */
    /* per-process state (index = process; players own the first ones) */
    int16_t pc[MISSION_PROCESSES];              /* 0x676620 */
    int16_t wait[MISSION_PROCESSES];            /* 0x676280 */
    int16_t step[MISSION_PROCESSES];            /* 0x6560b8 */
    int16_t linked[MISSION_PROCESSES];          /* 0x6b3de8 */
    int16_t active[MISSION_PROCESSES];          /* 0x6761c0 */
    int16_t owner[MISSION_PROCESSES];           /* 0x676348 */
    int32_t kind[MISSION_PROCESSES];            /* 0x676200 */
    int32_t trigger[MISSION_PROCESSES];         /* 0x6762c8 */
    int32_t last_pc[MISSION_PROCESSES];         /* 0x6560f8 the pc a debug build traced (Mission_Update keeps it) */
    int16_t result[MISSION_PROCESSES];          /* 0x6b3b30 the result code a process ended with (1 success, 2 failed, 3 dead...) */
    /* the interpreter's view of the running process (Mission_StepProcess) */
    int cur_pc;                 /* 0x655e58 the command index (the handlers write the next one here) */
    int16_t cur_wait;           /* 0x655e54 copy of wait[cur] */
    int cur_player;             /* 0x676608 the owning player (owner chain of cur) */
    int cur_ped;                /* 0x5f30e0 player_ped[cur] */
    int32_t cur_size;           /* 0x67660c the size Mission_GetObjectPos returns with scratch_x/y/z */
    int16_t respawn[4];         /* 0x5fdffc per player: wasted / respawn countdown (-1 off) */
    uint8_t phone_flag[4];      /* 0x6b3b74 per player (MPHONES answered); 0x6b3b42 + code in Mission_OnBriefDone */
    int32_t brief_flag;         /* 0x6765e8 cleared by Mission_OnBriefDone, 1 after codes 9 / 10 */
    int16_t ended;              /* 0x6b3b7e the end of the level is scheduled */
    int32_t cleanup_cars[MISSION_CLEANUP];      /* 0x6b3b90 */
    int ncleanup_cars;                          /* 0x6b3b88 */
    int32_t cleanup_peds[MISSION_CLEANUP];      /* 0x676390 */
    int ncleanup_peds;                          /* 0x6b3b80 */
    int32_t cleanup_objs[MISSION_CLEANUP];      /* 0x655e60 */
    int ncleanup_objs;                          /* 0x656178 */
    int32_t kf_list[MISSION_KF_MAX];            /* 0x5fe010 objects written with persistence digit 2 */
    int nkf;                                    /* 0x676604 (not reset by Mission_Load) */
    /* other state Mission_Load resets (meaning unknown) */
    int32_t park_car[4];        /* 0x5fdfe8 per player: the car PARK is taking in (-1 none) */
    int32_t park_exit_x, park_exit_y;   /* 0x6765f4, 0x6765f8 where PARK lets the driver out (Mission_GetParkExitPos) */
    int16_t park_exit_angle;    /* 0x6b3b7c */
    int32_t u6762c0, u67661c;
    int32_t scratch_x, scratch_y, scratch_z;    /* 0x676610.. the coordinates of the line */
    int32_t scratch_p1, scratch_p2;             /* 0x5fe004, 0x6765f0 */
    int32_t scratch_a, scratch_b;               /* 0x693b20, 0x693b24 */
} Mission;

extern Mission g_mission;

/* Mission_SetIniSection 0x44ab50; false (fatal -0x58 in the original) out of range */
bool mission_set_ini_section(int n);
static inline int mission_get_ini_section(void) { return g_mission.section; }   /* 0x44ab80 */
void mission_read_ini(void);                 /* Mission_ReadIni 0x44ab90 */
void mission_free_ini(void);                 /* Mission_FreeIni 0x44acc0 */
void mission_load(void);                     /* Mission_Load 0x445800 */

/* the tokeniser over g_mission.p */
bool ini_peek_token(void);                   /* Ini_PeekToken 0x44ace0 (true: 0xff, the low byte test) */
int ini_read_int(void);                      /* Ini_ReadInt 0x44ad50 */
void ini_read_word(char *dst, int cap);      /* Ini_ReadWord 0x44ade0 */
int ini_read_int_checked(int line);          /* Ini_ReadIntChecked 0x44ae50 */
void ini_read_coords(int32_t *x, int32_t *y, int32_t *z);   /* Ini_ReadCoords 0x44aed0 */
int ini_read_opt_digit(void);                /* Ini_ReadOptDigit 0x44b130 */

/* Names from the exe tables (NULL past the end) */
const char *mission_type_name(int t);        /* 0x4b0d98, 72 */
const char *mission_opcode_name(int op);     /* 0x4b0ec0, 150 */
int mission_lookup_type(const char *word);   /* first match, -1 if none */
int mission_lookup_opcode(const char *word);
