/* The mission script runtime: Mission_Update 0x446fe0 (per frame), the interpreter Mission_StepProcess
   0x4471a0 and the process / script object helpers around it (0x43cca0-0x43d2b0, 0x43e280-0x43e870,
   0x445580-0x445670). The state is in g_mission (mission.h). The opcode handlers that have their own
   function in the original (MissionOp_* 0x43e980-0x4453c0) are in mission_ops.c. docs/missions.md. */
#pragma once
#include "mission.h"
#include <stdbool.h>
#include <stdint.h>

/* object types (0x4b0d98) and opcodes (0x4b0ec0) by their index */
enum {
    MT_CAR, MT_PED, MT_OBJECT, MT_PLAYER, MT_DRIVER, MT_PARKED, MT_MODEL, MT_TELEPHONE, MT_TRIGGER, MT_DOOR,
    MT_TARGET, MT_FUTURE, MT_COUNTER, MT_CRANE, MT_CLOCK, MT_DUMMY, MT_SPRAY, MT_BARRIER, MT_ESCORT, MT_CARBOMB,
    MT_BOMBSHOP, MT_ESCORTED, MT_STOPPED, MT_INIT, MT_FUTUREPED, MT_CARTRIGGER, MT_FUTUREDROP, MT_HELLS,
    MT_DRIVER_PARK, MT_FUTURECAR, MT_ONETRIGGER, MT_MODEL_BARRIER, MT_MOVING_TRIG, MT_SPECIFIC_BARR, MT_MPHONES,
    MT_MODEL_DOOR, MT_GTA_DEMAND, MT_MY_MODEL, MT_BOMBSHOP_COST, MT_PHONE_TOGG, MT_SPECIFIC_DOOR, MT_TARGET_SCORE,
    MT_DUM_MISSION_TRIG, MT_CORRECT_MOD_TRIG, MT_CORRECT_CAR_TRIG, MT_CLOCK_START, MT_CLOCK_STOP,
    MT_MIDPOINT_MULTI, MT_MID_MULTI_SETUP, MT_FINAL_MULTI, MT_POWERUP, MT_BLOCK_INFO, MT_SPECIFIC_DOOR_BOMB,
    MT_MOVING_TRIG_HIRED, MT_SETUP_SPEED, MT_CARBOMB_TRIG, MT_DAMAGE_TRIG, MT_GUN_TRIG, MT_GUN_SCREEN_TRIG,
    MT_CARDESTROY_TRIG, MT_CARWAIT_TRIG, MT_PEDCAR_TRIG, MT_CANNON_START, MT_DUM_PED_BLOCK_TRIG,
    MT_PARKED_PIXELS, MT_CHOPPER_ENDPOINT, MT_MISSION_COUNTER, MT_SECRET_MISSION_COUNTER, MT_MISSION_TOTAL,
    MT_CARSTUCK_TRIG, MT_BASIC_BARRIER, MT_ALT_DAMAGE_TRIG,
};
enum { MISSION_OPCODES = 150 };

/* ---- script objects ---- */
/* the script object of a line (line_obj); undefined lines give a record of -1s (the original reads
   before the table) */
MissionObject *mission_obj(int line);
static inline int mission_handle(int line) { return mission_obj(line)->handle; }
static inline MissionCommand *mission_cmd(int pc) { return &g_mission.commands[pc]; }
static inline MissionCommand *mission_cur_cmd(void) { return &g_mission.commands[g_mission.cur_pc]; }

int mission_retrieve_data(int obj, int field);      /* Mission_RetrieveData 0x43cd50 (by object index) */
void mission_forget_object_handle(int line);        /* Mission_ForgetObjectHandle 0x43d250 */
void mission_put_ped_in_car(int ped, int car);      /* Mission_PutPedInCar 0x43d270 */
uint8_t mission_get_player_phone_flag(int n);       /* Mission_GetPlayerPhoneFlag 0x43d2b0 */
void mission_enable_object(MissionObject *o);       /* Mission_EnableObject 0x43e4f0 */
/* Mission_GetObjectPos 0x43e680: pixel position and a size / handle of the object of a line */
void mission_get_object_pos(int line, int32_t *x, int32_t *y, int32_t *z, int32_t *size);
/* the usual call: into scratch_x/y/z and cur_size */
void mission_get_object_pos_scratch(int line);
int mission_get_object_health(int line);            /* Mission_GetObjectHealth 0x43e870 */

/* ---- processes ---- */
int mission_start_process(int owner, int trigger, int label);   /* Mission_StartProcess 0x43cca0 (-1 / 0xffff: none free) */
int mission_find_line(int pc);                      /* Mission_FindLine 0x43cd30: the label of a command index */
void mission_kill_processes(int except);            /* Mission_KillProcesses 0x43cdc0 */
void mission_set_process_trigger(int trigger, int proc);   /* Mission_SetProcessTrigger 0x43d1b0 */
void mission_stop_process(int proc);                /* Mission_StopProcess 0x43e280 */
void mission_kill_process(int proc);                /* Mission_KillProcess 0x43e2b0 */
/* Mission_BranchSuccess 0x43e310 / Mission_BranchFail 0x43e3b0: the next pc after command `c` at
   `pc` through its b / c field (0 next, -1 ends the process: result 0, else a label); step = 1 */
int mission_branch_success(const MissionCommand *c, int pc);
int mission_branch_fail(const MissionCommand *c, int pc);
/* the inlined form most handlers use: cur_pc = branch(current command) */
void mission_goto_success(void);
void mission_goto_fail(void);
void mission_award_score(const MissionCommand *c);  /* Mission_AwardScore 0x43e450: e to the owner player */
void mission_end_all(int proc, int result);         /* Mission_EndAll 0x43e4a0 */
int mission_owner_player(int proc);                 /* the owner chain (0x676348) to its root */

/* ---- cleanup lists ---- */
void mission_clear_counter(int car);                /* Mission_ClearCounter 0x43ce60 (car list) */
void mission_remove_from_list(int obj);             /* Mission_RemoveFromList 0x43ce90 (object list) */
void mission_cleanup_cars(void);                    /* Mission_CleanupCars 0x43cef0 */
void mission_cleanup_peds(void);                    /* Mission_CleanupPeds 0x43cfe0 */
void mission_cleanup_objects(void);                 /* Mission_CleanupObjects 0x43d090 */
void mission_kf_process_cars(void);                 /* Mission_KFProcessCars 0x43d150 */

/* ---- counters the frontend's results read ---- */
int mission_get_target_score(void);                 /* Mission_GetTargetScore 0x43d1c0 */
int mission_get_mission_total(void);                /* Mission_GetMissionTotal 0x43d1d0 */
int mission_get_secret_total(void);                 /* Mission_GetSecretTotal 0x43d1e0 */
int mission_get_counter_remaining(void);            /* Mission_GetCounterRemaining 0x43d1f0 */
int mission_get_secret_remaining(void);             /* Mission_GetSecretRemaining 0x43d220 */
int mission_get_result(int player);                 /* 0x6b3b30[player] (read by Mission_GetResultText) */
/* Mission_GetResultText 0x445670: appends the result text to dst, returns the local player's score */
int mission_get_result_text(char *dst, int cap);

/* ---- the frame ---- */
void mission_update(void);                          /* Mission_Update 0x446fe0 */
void mission_step_process(void);                    /* Mission_StepProcess 0x4471a0 */
void mission_on_brief_done(int code);               /* Mission_OnBriefDone 0x445580 (event type 0) */

/* test hook: called before each command the interpreter executes (proc, pc, opcode) */
extern void (*mission_trace)(int proc, int pc, int op);
