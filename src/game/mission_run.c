/* The mission script runtime: the processes and script object helpers (0x43cca0-0x43d2b0,
   0x43e280-0x43e870), Mission_OnBriefDone / Mission_GetResultText (0x445580 / 0x445670),
   Mission_Update 0x446fe0 and the interpreter Mission_StepProcess 0x4471a0 with the opcodes it handles
   inline. The opcodes with their own function are in mission_ops.c. docs/missions.md. */
#include "mission_run.h"
#include "../audio/audio.h"
#include "../exe.h"
#include "../text.h"
#include "car.h"
#include "dummy.h"
#include "event.h"
#include "game.h"
#include "mission_obj.h"
#include "mission_ops.h"
#include "obj.h"
#include "ped.h"
#include "player.h"
#include "stubs.h"
#include "trigger.h"
#include <stdio.h>
#include <string.h>

#define M (&g_mission)

void (*mission_trace)(int proc, int pc, int op);

/* ---- script objects ---- */

MissionObject *mission_obj(int line)
{
    static MissionObject none;
    int i = line >= 0 && line < MISSION_LINES ? M->line_obj[line] : -1;
    if (i < 0 || i >= MISSION_OBJECTS) {   /* the original reads the record before the table */
        memset(&none, 0xff, sizeof none);
        return &none;
    }
    return &M->objects[i];
}

/* the command index of a label (0x693b30); labels the section doesn't define give -1 */
static int label_pc(int label)
{
    return label >= 0 && label < MISSION_LINES ? M->labels[label] : -1;
}

/* the original indexes its tables with -1 handles; the port reads a record of -1s instead */
static Car *car_of(int h)
{
    static Car none;
    if ((int16_t)h < 0 || (int16_t)h >= CAR_MAX) {
        memset(&none, 0xff, sizeof none);
        return &none;
    }
    return car_get((int16_t)h);
}
static Ped *ped_of(int h)
{
    static Ped none;
    if ((int16_t)h < 0 || (int16_t)h >= PED_MAX) {
        memset(&none, 0xff, sizeof none);
        return &none;
    }
    return ped_get((int16_t)h);
}
static Obj *obj_of(int h)
{
    static Obj none;
    if ((int16_t)h < 0 || (int16_t)h >= OBJ_MAX) {
        memset(&none, 0xff, sizeof none);
        return &none;
    }
    return obj_get((int16_t)h);
}

/* Mission_RetrieveData 0x43cd50: x, y, z, handle or parameter of script object `obj` */
int mission_retrieve_data(int obj, int field)
{
    const MissionObject *o = obj >= 0 && obj < MISSION_OBJECTS ? &M->objects[obj] : mission_obj(-1);
    switch (field) {
    case 0: return o->x;
    case 1: return o->y;
    case 2: return o->z;
    case 3: return o->handle;
    case 4: return o->param;
    default: game_fatal(-0x106, 0x1ac, field);   /* "Invalid location in a retrieve data" */
    }
}

/* Mission_ForgetObjectHandle 0x43d250 */
void mission_forget_object_handle(int line) { mission_obj(line)->handle = -1; }

/* Mission_PutPedInCar 0x43d270: the ped sits in the car (state 7, objective 0x19, no weapon, health 100) */
void mission_put_ped_in_car(int ped, int car)
{
    Ped *p = ped_of(ped);
    p->graphic = 0;
    p->state = 7;
    p->objective = 0x19;
    p->car = (int16_t)car;
    p->weapon = 0;
    p->health = 100;
}

/* Mission_GetPlayerPhoneFlag 0x43d2b0 */
uint8_t mission_get_player_phone_flag(int n) { return M->phone_flag[n & 3]; }

/* Mission_EnableObject 0x43e4f0: ENABLE re-arms a door, or binds a trigger to the entity of the line
   in its parameter (most kinds) or in its coordinates (PEDCAR_TRIG keeps two lines there). */
void mission_enable_object(MissionObject *o)
{
    switch (o->type) {
    case MT_DOOR: case MT_BARRIER:
        door_unlock(o->handle);
        return;
    case MT_ONETRIGGER: case MT_DUM_MISSION_TRIG: case MT_CORRECT_CAR_TRIG: case MT_CARBOMB_TRIG:
    case MT_GUN_SCREEN_TRIG: case MT_CARDESTROY_TRIG: case MT_CARWAIT_TRIG: case MT_DUM_PED_BLOCK_TRIG:
    case MT_CARSTUCK_TRIG:
        trigger_set_param14(o->handle, mission_obj(o->param)->handle);
        return;
    case MT_MOVING_TRIG: case MT_MOVING_TRIG_HIRED: {
        int car = mission_obj(o->param)->handle;
        trigger_set_param08(o->handle, car);
        car_trig_add(o->handle, car);
        return;
    }
    case MT_SPECIFIC_BARR: case MT_SPECIFIC_DOOR:
        door_set_open_by_car(o->handle, 3, (int16_t)mission_obj(o->param)->handle, -2);
        return;
    case MT_MODEL_DOOR:
        door_set_open_mode3(o->handle, 3, -2);
        return;
    case MT_SPECIFIC_DOOR_BOMB:
        door_set_open_mode5(o->handle, 3, (int16_t)mission_obj(o->param)->handle);
        return;
    case MT_PEDCAR_TRIG:   /* the ped of line y, the car of line z (Mission_SpawnPedCarTrig keeps them there) */
        trigger_set_param14(o->handle, mission_obj(o->y)->handle);
        trigger_set_param08(o->handle, mission_obj(o->z)->handle);
        return;
    default:
        return;
    }
}

/* Mission_GetObjectPos 0x43e680: the pixel position of the object of a line and a size or handle:
   cars and peds where they are now (their sprite), objects too; block-placed types (triggers, doors)
   their block's corner; created PEDs (FUTUREPED...) the coordinates as stored. */
void mission_get_object_pos(int line, int32_t *x, int32_t *y, int32_t *z, int32_t *size)
{
    const MissionObject *o = mission_obj(line);
    switch (o->type) {
    case MT_CAR: case MT_PARKED: case MT_MODEL: case MT_ESCORT: case MT_ESCORTED: case MT_STOPPED: case MT_HELLS: {
        const Car *c = car_of(o->handle);   /* Car_GetCamTarget 0x408220: the sprite */
        *x = (int16_t)(c->spr.x >> 16), *y = (int16_t)(c->spr.y >> 16), *z = (int16_t)(c->spr.z >> 16);
        *size = 0x20;
        return;
    }
    case MT_PED: case MT_DRIVER: case MT_FUTUREPED:
        *x = o->x, *y = o->y, *z = o->z, *size = o->handle;
        return;
    case MT_OBJECT: {
        const Obj *b = obj_of(o->handle);
        *x = (int16_t)(b->spr.x >> 16), *y = (int16_t)(b->spr.y >> 16), *z = (int16_t)(b->spr.z >> 16);
        *size = b->spr.angle;   /* object +0x44: its sprite's angle */
        return;
    }
    case MT_PLAYER: case MISSION_TYPE_SPAWNED_PED: {
        const Ped *p = ped_of(o->handle);   /* Ped_GetPosRect 0x45fb00: the sprite */
        *x = (int16_t)(p->spr.x >> 16), *y = (int16_t)(p->spr.y >> 16), *z = (int16_t)(p->spr.z >> 16);
        *size = 0x10;
        return;
    }
    case MT_TRIGGER: case MT_DOOR: case MT_DUMMY: case MT_SPRAY: case MT_BOMBSHOP: case MT_FUTURECAR:
    case MT_SPECIFIC_DOOR: case MT_DUM_MISSION_TRIG: case MT_CORRECT_MOD_TRIG: case MT_CORRECT_CAR_TRIG:
    case MT_BLOCK_INFO: case MT_GUN_TRIG: case MT_CARWAIT_TRIG: case MT_DUM_PED_BLOCK_TRIG: case MT_ALT_DAMAGE_TRIG:
        *x = o->x << 6, *y = o->y << 6, *z = o->z << 6, *size = o->handle;
        return;
    case MT_TARGET:
        *x = o->x, *y = o->y, *z = o->z, *size = o->param;
        return;
    default:
        *x = o->x, *y = o->y, *z = o->z, *size = 0;
        return;
    }
}

void mission_get_object_pos_scratch(int line)
{
    mission_get_object_pos(line, &M->scratch_x, &M->scratch_y, &M->scratch_z, &M->cur_size);
}

/* Mission_GetObjectHealth 0x43e870: 100 - damage of a car (0 if its slot is free), the health of a
   ped (0 if it's carried, or in state 0x17, 5 or 0x18), 100 for anything else */
int mission_get_object_health(int line)
{
    const MissionObject *o = mission_obj(line);
    int h;
    switch (o->type) {
    case MT_CAR: case MT_PARKED: case MT_MODEL: case MT_ESCORT: case MT_ESCORTED: case MT_STOPPED: case MT_HELLS: {
        const Car *c = car_of(o->handle);
        if (c->status == -1) return 0;
        h = 100 - c->damage;
        break;
    }
    case MT_PED: case MT_PLAYER: case MISSION_TYPE_SPAWNED_PED: {
        const Ped *p = ped_of(o->handle);
        if (p->carried > -1) return 0;
        if (p->state == 0x17 || p->state == 5 || p->state == 0x18) return 0;
        h = p->health;
        break;
    }
    default:
        return 100;
    }
    if (h < 0) h = 0;   /* the original clears the low word only: the callers test the short */
    return h;
}

/* ---- processes ---- */

/* Mission_StartProcess 0x43cca0: the first free process from 4 runs from `label` for `owner`, linked
   to `trigger` (which goes to state 3) */
int mission_start_process(int owner, int trigger, int label)
{
    Mission *m = M;
    if ((int16_t)trigger > -1) trigger_set_state3((int16_t)trigger);
    int i = 4;
    while (i < MISSION_PROCESSES && m->active[i]) i++;
    if (i == MISSION_PROCESSES) return 0xffff;
    m->linked[i] = (int16_t)trigger;
    m->step[i] = 1;
    m->active[i] = 1;
    m->pc[i] = (int16_t)label_pc(label);
    m->owner[i] = (int16_t)owner;
    /* The original copies the owner's ped into 0x656180[i] for i up to 31: 0x656180 has only four
       entries, the rest overlaps 0x656188..0x6561bf (bss after it, unused). */
    return i;
}

/* Mission_FindLine 0x43cd30: the first label whose command index is pc */
int mission_find_line(int pc)
{
    for (int i = 0; i < MISSION_LINES; i++)
        if (M->labels[i] == pc) return i;
    return -1;
}

/* Mission_KillProcesses 0x43cdc0: every process from 3 but `except`, KEEP_THIS_PROC (kind 1),
   KF_PROCESS (kind 2) and the ones waiting in an MPHONE */
void mission_kill_processes(int except)
{
    Mission *m = M;
    for (int i = 3; i < MISSION_PROCESSES; i++) {
        if (i == except || !m->active[i] || m->kind[i] == 1 || m->kind[i] == 2) continue;
        if (m->pc[i] >= 0 && m->pc[i] < MISSION_COMMANDS && m->commands[m->pc[i]].op == 0x10) continue;
        if (m->linked[i] != -1) trigger_disarm(m->linked[i]);
        m->active[i] = 0;
        m->pc[i] = 0;
        m->wait[i] = 0;
        m->linked[i] = 0;
        m->trigger[i] = -1;
        m->kind[i] = 0;
    }
}

/* Mission_SetProcessTrigger 0x43d1b0 */
void mission_set_process_trigger(int trigger, int proc) { M->trigger[proc] = trigger; }

/* Mission_StopProcess 0x43e280 */
void mission_stop_process(int proc)
{
    if (M->linked[proc] != -1) trigger_disarm(M->linked[proc]);
    M->active[proc] = 0;
}

/* Mission_KillProcess 0x43e2b0 */
void mission_kill_process(int proc)
{
    Mission *m = M;
    if (m->linked[proc] != -1) trigger_disarm(m->linked[proc]);
    m->trigger[proc] = -1;
    m->active[proc] = 0;
    m->pc[proc] = 0;
    m->wait[proc] = 0;
    m->linked[proc] = 0;
    m->kind[proc] = 0;
}

static int branch(int target, int pc)
{
    Mission *m = M;
    int p = m->cur;
    if (target < 0) m->active[p] = 0;
    m->step[p] = 1;
    if (target == -1) {
        mission_kill_process(p);
        return 0;
    }
    if (target != 0) {
        int t = label_pc(target);
        /* labels below -1 index before the label table in the original (the process is stopped) */
        return t;
    }
    return pc + 1;
}

/* Mission_BranchSuccess 0x43e310 / Mission_BranchFail 0x43e3b0 */
int mission_branch_success(const MissionCommand *c, int pc) { return branch(c->b, pc); }
int mission_branch_fail(const MissionCommand *c, int pc) { return branch(c->c, pc); }
void mission_goto_success(void) { M->cur_pc = mission_branch_success(mission_cur_cmd(), M->cur_pc); }
void mission_goto_fail(void) { M->cur_pc = mission_branch_fail(mission_cur_cmd(), M->cur_pc); }

int mission_owner_player(int proc)
{
    int p = (int16_t)proc;
    for (int i = 0; M->owner[p] >= 0 && i < MISSION_PROCESSES; i++) p = M->owner[p];
    return p;
}

/* Mission_AwardScore 0x43e450: a positive e goes to the owner player's score */
void mission_award_score(const MissionCommand *c)
{
    int n = mission_owner_player(M->cur);
    if (c->e > 0) player_add_score(n, c->e, 100, 100, 100, 0);
}

/* Mission_EndAll 0x43e4a0: every process stops with result r */
void mission_end_all(int proc, int r)
{
    M->active[proc] = 0;
    M->result[proc] = (int16_t)r;
    for (int i = 0; i < MISSION_PROCESSES; i++) M->active[i] = 0, M->result[i] = (int16_t)r;
}

/* ---- cleanup lists (RESET) ---- */

/* Mission_ClearCounter 0x43ce60: forgets a car in the cleanup list */
void mission_clear_counter(int car)
{
    for (int i = 0; i < M->ncleanup_cars; i++)
        if (M->cleanup_cars[i] == car) {
            M->cleanup_cars[i] = -1;
            return;
        }
}

/* Mission_RemoveFromList 0x43ce90: removes an object from the cleanup list, closing the gap */
void mission_remove_from_list(int obj)
{
    Mission *m = M;
    for (int i = 0; i < m->ncleanup_objs; i++) {
        if (m->cleanup_objs[i] != obj) continue;
        for (int j = i; j < m->ncleanup_objs; j++)   /* copies the entry after the count too */
            m->cleanup_objs[j] = j + 1 < MISSION_CLEANUP ? m->cleanup_objs[j + 1] : -1;
        m->ncleanup_objs--;
        m->cleanup_objs[m->ncleanup_objs + 1 < MISSION_CLEANUP ? m->ncleanup_objs + 1 : MISSION_CLEANUP - 1] = -1;
        return;
    }
}

/* The compaction the car and object cleanups share. Quirk kept: an entry that is already in place is
   moved past itself to the next free slot, so the list moves up by one each time (and the count is
   the last filled slot + 1). */
static int compact(int32_t *list, int n)
{
    int i = 0;
    for (int j = 1; j < n; j++) {
        if (list[j] == -1) continue;
        while (i < MISSION_CLEANUP - 1 && list[i] != -1) i++;
        list[i] = list[j];
        list[j] = -1;
    }
    return i + 1;
}

/* Mission_CleanupCars 0x43cef0: the listed cars that aren't on screen go (a bike's or convertible's
   visible driver first) */
void mission_cleanup_cars(void)
{
    Mission *m = M;
    bool all_gone = true;
    for (int i = 0; i < m->ncleanup_cars; i++) {
        if (m->cleanup_cars[i] < 0) continue;
        const Car *c = car_of(m->cleanup_cars[i]);
        if (!car_is_on_screen(c)) {
            if ((c->vtype == 3 || car_info_is_convertible(c->id)) && c->driver > -1) ped_kill_if_possible(c->driver);
            if (car_remove_if_unused((int16_t)m->cleanup_cars[i])) {
                m->cleanup_cars[i] = -1;
                continue;
            }
        }
        all_gone = false;
    }
    m->ncleanup_cars = all_gone || m->ncleanup_cars <= 0 ? 0 : compact(m->cleanup_cars, m->ncleanup_cars);
}

/* Mission_CleanupPeds 0x43cfe0: the listed peds go if nobody saw them recently, else they wander off */
void mission_cleanup_peds(void)
{
    Mission *m = M;
    for (int i = 0; i < m->ncleanup_peds; i++) {
        if (m->cleanup_peds[i] < 0) continue;
        Ped *p = ped_of(m->cleanup_peds[i]);
        p->u8b = 0;
        if (!ped_is_visible_recent(p)) {
            ped_kill_if_possible((int16_t)m->cleanup_peds[i]);
        } else {
            p->objective = 0x19;
            p->state = 1;
            p->target_x = p->spr.x;
            p->target_y = p->spr.y;
            p->u7c = 2;
            p->u78 = 8;
            p->u8b = 0;
        }
        m->cleanup_peds[i] = -1;
    }
    m->ncleanup_peds = 0;
}

/* Mission_CleanupObjects 0x43d090: the listed objects that aren't on screen go */
void mission_cleanup_objects(void)
{
    Mission *m = M;
    bool all_gone = true;
    for (int i = 0; i < m->ncleanup_objs; i++) {
        if (m->cleanup_objs[i] < 0) continue;
        if (!obj_is_on_screen(obj_of(m->cleanup_objs[i]))) {
            obj_remove_if_active(m->cleanup_objs[i]);
            m->cleanup_objs[i] = -1;
        } else {
            all_gone = false;
        }
    }
    m->ncleanup_objs = all_gone || m->ncleanup_objs <= 0 ? 0 : compact(m->cleanup_objs, m->ncleanup_objs);
}

/* Mission_KFProcessCars 0x43d150: the kill frenzy's CAR objects with a driver are held */
void mission_kf_process_cars(void)
{
    for (int i = 0; i < M->nkf; i++) {
        int k = M->kf_list[i];
        if (k < 0 || k >= MISSION_OBJECTS) continue;
        const MissionObject *o = &M->objects[k];
        if (o->type == MT_CAR && o->handle > -1 && car_of(o->handle)->script_line > -1)
            mis_car_set_held(car_of(o->handle)->id);
    }
}

/* ---- counters ---- */

int mission_get_target_score(void) { return M->target_score; }                              /* 0x43d1c0 */
int mission_get_mission_total(void) { return M->mission_total != -1 ? M->mission_total : 0; }   /* 0x43d1d0 */
int mission_get_secret_total(void) { return M->secret_target != -1 ? M->secret_target : 0; }    /* 0x43d1e0 */
/* 0x43d1f0 / 0x43d220: the counter's parameter (its target) minus its handle (the count so far) */
int mission_get_counter_remaining(void)
{
    if (M->counter_obj == -1) return 0;
    return M->objects[M->counter_obj].param - M->objects[M->counter_obj].handle;
}
int mission_get_secret_remaining(void)
{
    if (M->secret_counter_obj == -1) return 0;
    return M->objects[M->secret_counter_obj].param - M->objects[M->secret_counter_obj].handle;
}
int mission_get_result(int player) { return M->result[player & 31]; }

/* Mission_GetResultText 0x445670: the result of the local player's processes (0x513230, the game's
   result) as text, appended to dst; returns the local player's score. The best and second best
   scores (and their players) fill the multiplayer texts. */
int mission_get_result_text(char *dst, int cap)
{
    int best = -1, best_n = -1, second = -1, second_n = -1;
    for (int n = 0; n < 4; n++) {
        int s = player_get_score(n);
        if (s > best) {
            second = best, second_n = best_n;
            best = s, best_n = n;
        } else if (s > second) {
            second = s, second_n = n;
        }
    }
    /* (the original's ties: a score equal to the best goes to the second place test) */
    const char *name = player_get_name(best_n < 0 ? 0 : best_n);
    int local = player_get_score(g_player_local);
    static char buf[256];   /* 0x502f78 */
    const char *key, *s;
    uint32_t va;
    switch (g_game.result) {
    case 1: va = 0x4b1d74; break;   /* m22success */
    case 2: va = 0x4b1d68; break;   /* m22failed */
    case 3: va = 0x4b1d60; break;   /* m22dead */
    case 4: va = 0x4b1d54; break;   /* m22arrest */
    case 5: va = 0x4b1d48; break;   /* m22timeout */
    case 0xb: va = 0x4b1d80; break; /* m22demo */
    case 6: case 8: case 10: va = 0; break;
    default: va = 0x4b1d14; break;  /* m22incomplete */
    }
    if (va) {
        key = exe_str(va);
        s = text_get(key);
    } else {
        /* the formatted ones: Text_Get with printf arguments, copied to 0x502f78 */
        if (g_game.result == 6)
            snprintf(buf, sizeof buf, text_get(exe_str(0x4b1d3c)), name, best,
                     player_get_name(second_n < 0 ? 0 : second_n), second);   /* m22timeover */
        else if (g_game.result == 8)
            snprintf(buf, sizeof buf, text_get(exe_str(0x4b1d30)), name, best);   /* m22score */
        else
            snprintf(buf, sizeof buf, text_get(exe_str(0x4b1d24)), name);   /* m22cannon */
        s = buf;
    }
    size_t l = strlen(dst);
    if (s && l < (size_t)cap) snprintf(dst + l, (size_t)cap - l, "%s", s);
    return local;
}

/* Mission_OnBriefDone 0x445580 (delayed event type 0): code <= 0 is a car whose bomb (kind 3) goes
   off; 1 ends the game (success), 9 / 10 set the brief flag, 0x14 plays voice 0; 0x32..0x35 clear
   player (code - 0x32)'s phone flag; 200..600 delete car code - 200. */
void mission_on_brief_done(int code)
{
    M->brief_flag = 0;
    if (code < 1) {
        Car *c = car_of(-code);
        if (c->bomb == 3 && c->damage < 100) {
            mis_car_destroy(-code);
            c->bomb = 0;
        }
    } else if (code == 1) {
        game_request_end(1);
    } else if (code == 9 || code == 10) {
        M->brief_flag = 1;
    } else if (code == 0x14) {
        Snd_PlayVoice(0);
    }
    if (code > 0x31 && code < 0x36) M->phone_flag[code - 0x32] = 0;
    if (code > 199 && code < 0x259 && car_of(code - 200)->status != -1) car_delete(code - 200);
}

/* ---- Mission_Update 0x446fe0 ---- */

void mission_update(void)
{
    Mission *m = M;
    mission_update_triggers();
    if (g_game.opt.opt50317c) {   /* a debug trace of the pc changes (the label lookup is unused) */
        for (int i = 0; i < MISSION_PROCESSES; i++)
            if (m->active[i] && m->pc[i] != m->last_pc[i]) {
                (void)mission_find_line(m->pc[i]);
                m->last_pc[i] = m->pc[i];
            }
    }
    for (m->cur = 0; m->cur < MISSION_PROCESSES; m->cur++)
        if (m->active[m->cur]) mission_step_process();
    int active = 0;
    for (int i = 0; i < 4; i++) active += m->active[i];   /* only the four players' processes count */
    for (int n = player_first(); n > -1; n = player_next(n)) {
        if (!mission_check_player_car_ok(n) || !ped_is_dead(m->player_ped[n]) || m->respawn[n] == -1) continue;
        if (m->respawn[n] == 0) {
            if (!player_wasted(n)) {   /* no life left: the player's process ends, dead */
                m->active[n] = 0;
                m->result[n] = 3;
                m->respawn[n] = -1;
            } else {
                m->respawn[n] = 1;
            }
        } else {
            m->respawn[n]++;
            /* 0x57 frames to the respawn on foot, 0x64 otherwise */
            if (m->respawn[n] >= (player_get_controlled_kind(n) != 2 ? 0xd : 0) + 0x57) {
                player_respawn(n);
                m->respawn[n] = 0;
            }
        }
    }
    if (active == 0 && m->ended == 0) {
        int r = m->result[g_player_local];
        if (r == 1) event_schedule_exit(1, 1);
        else event_schedule_exit(0x5a, r);
        m->ended = 1;
    }
}

/* ---- Mission_StepProcess 0x4471a0 ---- */

static MissionCommand *cmd(void) { return mission_cur_cmd(); }
static void award_success(void) { mission_award_score(cmd()); mission_goto_success(); }
static int line_handle(int line) { return mission_obj(line)->handle; }

static void cleanup_car_add(int id)
{
    if (M->ncleanup_cars < MISSION_CLEANUP) M->cleanup_cars[M->ncleanup_cars++] = id;
}
static void cleanup_obj_add(int id)
{
    if (M->ncleanup_objs < MISSION_CLEANUP) M->cleanup_objs[M->ncleanup_objs++] = id;
}

/* the brief's c: >= 0 the time, < 0 its negation into the other scratch value */
static void brief_split(int c)
{
    if (c < 0) M->scratch_a = 0, M->scratch_b = -c;
    else M->scratch_b = -1, M->scratch_a = c;
}

/* the processes started by a KICKSTART are kept in its process-list object (type 100): x, y, z,
   handle; KILL_PROCESS / KILL_SIDE_PROC / KILL_SPEC_PROC stop them */
static int32_t *kick_field(MissionObject *o, int i)
{
    switch (i) {
    case 0: return &o->x;
    case 1: return &o->y;
    case 2: return &o->z;
    default: return &o->handle;
    }
}
static MissionObject *kick_object(int kick_pc)
{
    int k = M->commands[kick_pc].d;
    return k >= 0 && k < MISSION_OBJECTS ? &M->objects[k] : mission_obj(-1);
}
static void kick_stop(int kick_pc, int i)
{
    Mission *m = M;
    int p = *kick_field(kick_object(kick_pc), i);
    if (p == -1) return;
    if (p >= 0 && p < MISSION_PROCESSES) {
        mission_stop_process(p);
        m->pc[p] = 0;
        m->wait[p] = 0;
        m->linked[p] = 0;
        m->trigger[p] = -1;
        m->kind[p] = 0;
    }
    MissionObject *o = kick_object(kick_pc);
    o->type = 100;
    *kick_field(o, i) = -1;
}

/* a new process for KICKSTART / NEXT_KICK: the first free from 4, owned by the current player */
static int new_process(int label)
{
    Mission *m = M;
    int i = 4;
    while (i < MISSION_PROCESSES && m->active[i]) i++;
    if (i == MISSION_PROCESSES) return -1;
    m->pc[i] = (int16_t)label_pc(label);
    m->step[i] = 1;
    m->active[i] = 1;
    m->linked[i] = -1;
    m->owner[i] = (int16_t)m->cur_player;
    /* (0x656180[i] = 0x656180[owner]: past the four players it's bss nobody reads) */
    return i;
}

/* KICKSTART 0x59: a process from label a (0: the next command) that keeps this process's kind; the
   list object (type 100, the last free record from 1399 down) gets it in x and is written to this
   command's d, this process to its e. This process continues through b. */
static void op_kickstart(void)
{
    Mission *m = M;
    int pc = m->cur_pc, p = m->cur;
    MissionCommand *c = cmd();
    int a = c->a;
    int n = new_process(a);
    int npc = a == 0 ? pc + 1 : label_pc(a);
    m->scratch_b = n;
    if (n >= 0) {
        m->pc[n] = (int16_t)npc;
        m->kind[n] = m->kind[p];
        m->step[n] = 1;
        m->active[n] = 1;
    }
    /* (with no free process the original writes these into the slot before each table) */
    int b = c->b, next = 0;
    m->scratch_a = pc;
    if (b < 0) m->active[p] = 0;
    m->step[p] = 1;
    if (b == -1) mission_kill_process(p);
    else if (b == 0) next = pc + 1;
    else next = label_pc(b);
    int k = MISSION_OBJECTS - 1;
    for (; k >= 0; k--) {
        const MissionObject *o = &m->objects[k];
        if (o->x == -1 && o->y == -1 && o->z == -1 && o->type == -1) break;
    }
    m->cur_pc = next;
    c->d = k;
    c->e = m->cur;
    if (k >= 0) {
        m->objects[k].type = 100;
        m->objects[k].x = n;
    }
}

/* NEXT_KICK 0x61: like KICKSTART, but the process goes into the first free field of the list object
   of the KICKSTART at label d (which then keeps type 100). */
static void op_next_kick(void)
{
    Mission *m = M;
    int pc = m->cur_pc, p = m->cur;
    const MissionCommand *c = cmd();
    int n = new_process(c->a);
    m->scratch_a = c->a == 0 ? pc + 1 : label_pc(c->a);
    int kick = label_pc(c->d);
    m->scratch_b = kick;
    if (n >= 0) {
        m->pc[n] = (int16_t)m->scratch_a;
        m->kind[n] = m->kind[p];
        m->step[n] = 1;
        m->active[n] = 1;
    }
    if (kick >= 0) {
        MissionObject *o = kick_object(kick);
        for (int i = 0; i < 4; i++)
            if (*kick_field(o, i) == -1) {
                *kick_field(o, i) = n;
                o->type = 100;
                break;
            }
    }
    mission_goto_success();
}

/* the car creators of PARKED_ON 0x58, PIXEL_CAR_ON 0x70 and PARKED_PIXELS_ON 0x75: the object of line
   a holds the model (handle) and the heading (param); d is the remap */
static void op_car_on(int kind)
{
    Mission *m = M;
    const MissionCommand *c = cmd();
    int line = c->a;
    mission_get_object_pos_scratch(line);
    MissionObject *o = mission_obj(line);
    int x = m->scratch_x, y = m->scratch_y, z = m->scratch_z;
    int car;
    if (kind == 0) car = (int16_t)mis_car_create(x >> 6, y >> 6, z >> 6, (int16_t)o->handle, (int16_t)o->param, (int16_t)c->d);
    else car = (int16_t)mis_car_create_at(x << 16, y << 16, z >> 6, (int16_t)o->handle, (int16_t)o->param, (int16_t)c->d);
    if (car < 0) {
        if (kind == 2) return;   /* PARKED_PIXELS_ON waits until a car slot is free */
        mission_goto_fail();
        return;
    }
    if (kind == 0) mission_clear_block(x + 0x20, y + 0x20, z, car, o->handle, (int16_t)o->param);
    else mission_clear_block(x, y, z, car, o->handle, (int16_t)o->param);
    o->handle = car;
    Car *cr = car_get(car);
    cr->script_line = (int16_t)line;
    if (kind == 1) {   /* with its driver sitting in it */
        cr->driver = (int16_t)(cr->id + PED_DRIVER_FIRST);
        Ped *p = ped_get(cr->driver);
        p->graphic = 0;
        mission_put_ped_in_car(p->id, cr->id);
    }
    cr->unk139 = 0;
    if (kind == 1) {
        if (cr->model == 0x29 || cr->model == 3) ped_create_car_driver(cr);
        if (cr->model == 0x2f) mis_car_set_flag9c(car, 3);
    }
    o->type = MT_CAR;
    if (!o->persistent) cleanup_car_add(cr->id);
    award_success();
}

/* the inline opcodes */
static void step_inline(int op)
{
    Mission *m = M;
    MissionCommand *c = cmd();
    int p = m->cur, pc = m->cur_pc;
    MissionObject *o;
    int h, v;

    switch (op) {
    case 0x04:   /* SURVIVE: wait d frames (counted down in the command's c) */
        if (m->step[p] == 1) {
            m->step[p] = 2;
            c->c = c->d;
            return;
        }
        if (--c->c > 0) return;
        award_success();
        return;
    case 0x05:   /* HUNTON */
        car_unk_004318e0_wrap((int16_t)line_handle(c->a), (int16_t)line_handle(c->d), (int16_t)c->c);
        award_success();
        return;
    case 0x06:   /* HUNTOFF */
        hunt_remove((int16_t)line_handle(c->a));
        award_success();
        return;
    case 0x08: { /* DUMMYON: the car's driver takes it over as a traffic dummy */
        Car *cr = car_of(line_handle(c->a));
        mis_car_make_killer(cr->id);
        car_clear_field0ec(cr->id);
        mission_put_ped_in_car(cr->driver, cr->id);
        award_success();
        return;
    }
    case 0x0a: { /* END: the process and its owners end with result a */
        int r = (int16_t)c->a;
        m->active[p] = 0;
        m->result[p] = (int16_t)r;
        /* Quirk kept: the owner walk moves the current process (0x6b3b70) up the chain, so the pc
           is written back to the root's slot and Mission_Update continues after the root. */
        for (int i = 0; m->owner[m->cur] >= 0 && i < MISSION_PROCESSES; i++) {
            m->cur = m->owner[m->cur];
            m->result[m->cur] = (int16_t)r;
            m->active[m->cur] = 0;
        }
        return;
    }
    case 0x0c:   /* EXPLODE */
        mission_get_object_pos_scratch(c->a);
        mis_obj_create425520(m->scratch_x >> 6, m->scratch_y >> 6, m->scratch_z >> 6, c->d, m->cur_player);
        award_success();
        return;
    case 0x0d:   /* OBTAIN: the object of line a (type in its handle) is carried by the player's ped */
        o = mission_obj(c->a);
        h = (int16_t)obj_create_at_ped(m->cur_ped, (int16_t)o->handle, (int16_t)c->d);
        o->handle = h;
        if (h > -1) {
            Obj *b = obj_get(h);
            b->attach_kind = 4;
            b->owner = (int16_t)m->cur_ped;
            if (!o->persistent) cleanup_obj_add(o->handle);
        }
        o->type = MT_OBJECT;
        award_success();
        return;
    case 0x0e:   /* THROW */
        obj_throw_to_car(line_handle(c->a), line_handle(c->d));
        award_success();
        return;
    case 0x0f:   /* BRIEF */
        mission_get_object_pos_scratch(c->a);
        brief_split(c->c);
        mission_brief_timed1(c->d, (int16_t)c->e, m->cur_player & 0xff);
        mission_goto_success();
        return;
    case 0x12:   /* DISABLE */
        trigger_kill(line_handle(c->a));
        m->linked[p] = -1;
        award_success();
        return;
    case 0x13:   /* ENABLE */
        o = mission_obj(c->a);
        mission_enable_object(o);
        if (o->type != MT_DOOR && o->type != MT_BARRIER && o->type != MT_MODEL_BARRIER && o->type != MT_SPECIFIC_BARR &&
            o->type != MT_MODEL_DOOR && o->type != MT_SPECIFIC_DOOR && o->type != MT_SPECIFIC_DOOR_BOMB)
            trigger_disarm(o->handle);
        award_success();
        return;
    case 0x14:   /* DECCOUNT: down to 0 succeeds */
        o = mission_obj(c->a);
        if (--o->handle > 0) mission_goto_fail();
        else award_success();
        return;
    case 0x18:   /* SETBOMB */
        mis_car_set_flag9c((int16_t)line_handle(c->a), c->d);
        award_success();
        return;
    case 0x19:   /* ESCORT */
        m->scratch_a = line_handle(c->a);
        if ((int16_t)dummy_add_follower(m->scratch_a, (int16_t)line_handle(c->d)) > -1)
            car_of(m->scratch_a)->script_line = (int16_t)c->a;
        award_success();
        return;
    case 0x1a:   /* ARROW: to the object of line a (a block corner points to its centre); -1 off */
        if (c->a == -1) {
            hud_arrow_off();
        } else {
            mission_get_object_pos_scratch(c->a);
            if (m->scratch_x % 64 == 0 && m->scratch_y % 64 == 0) m->scratch_x += 0x20, m->scratch_y += 0x20;
            hud_arrow_to_pos(m->scratch_x, m->scratch_y & 0xffff, (int16_t)m->scratch_z);
        }
        award_success();
        return;
    case 0x1b:   /* LOCK_DOOR: the car's doors */
        mis_car_set_held(line_handle(c->a));
        award_success();
        return;
    case 0x1c:   /* UNLOCK_DOOR */
        mis_car_clear_held(line_handle(c->a));
        award_success();
        return;
    case 0x1d: { /* EXPL_LAST: the car the player's ped was last in */
        const Ped *pp = ped_of(m->cur_ped);
        if (pp->car > -1 && player_get_controlled_kind(m->cur_player) != 1) mis_car_destroy(pp->car);
        award_success();
        return;
    }
    case 0x1e: { /* DROP_ON: an object (type in the handle) at the block's centre */
        mission_get_object_pos_scratch(c->a);
        o = mission_obj(c->a);
        h = (int16_t)mission_obj_create(m->scratch_x + 0x20, m->scratch_y + 0x20, m->scratch_z, (int16_t)o->handle, (int16_t)c->d);
        o->handle = h;
        if (!o->persistent) cleanup_obj_add(h);
        o->type = MT_OBJECT;
        award_success();
        return;
    }
    case 0x20:   /* KILL_DROP */
        o = mission_obj(c->a);
        if (o->handle < 0) {
            mission_goto_fail();
            return;
        }
        obj_delete(o->handle);
        o->type = 0x65;
        mission_remove_from_list(o->handle);
        award_success();
        return;
    case 0x22:   /* ARMEDMESS */
        hud_show_zone_text(text_get(exe_str(0x4b1d00)), 0);   /* "bomb_on" */
        award_success();
        return;
    case 0x23:   /* DISARMMESS */
        hud_show_zone_text(text_get(exe_str(0x4b1d08)), 0);   /* "bomb_off" */
        award_success();
        return;
    case 0x24:   /* ARROW_OFF */
        hud_arrow_off();
        award_success();
        return;
    case 0x25:   /* P_BRIEF */
        mission_get_object_pos_scratch(c->a);
        brief_split(c->c);
        mission_brief_timed0(c->d, (int16_t)c->e, m->cur_player & 0xff);
        mission_goto_success();
        return;
    case 0x27:   /* WAIT_FOR_PED: until the ped of line d is on the block of line a */
        mission_get_object_pos_scratch(c->a);
        if (ped_is_near_block(ped_of(line_handle(c->d))->id, m->scratch_x >> 6, m->scratch_y >> 6, m->scratch_z >> 6, 0))
            award_success();
        return;
    case 0x28:   /* PED_BACK: the ped of line d goes back to the car of line a */
        if ((int16_t)mission_get_object_health(c->d) == 0) {
            mission_goto_fail();
            return;
        }
        ped_send_to_car_door2(ped_of(line_handle(c->d)), (int16_t)line_handle(c->a));
        award_success();
        return;
    case 0x29:   /* SETUP_REPO */
        mission_obj(c->a)->param = c->d;
        mission_goto_success();
        return;
    case 0x2b:   /* KILL_OBJ */
        o = mission_obj(c->a);
        o->type = 0x65;
        obj_remove_if_active(o->handle);
        mission_remove_from_list(o->handle);
        award_success();
        return;
    case 0x2c: { /* DO_GTA: the car list of line a; done when complete */
        o = mission_obj(c->a);
        if (m->step[p] == 1) {
            CarListEntry *e = car_list_get(o->handle);
            e->uc = (int16_t)pc, e->ue = (int16_t)(pc >> 16);   /* +0xc: the pc, as an int */
            mission_set_slot773168(e->a, o->handle);
            m->step[p]++;
        } else if (m->step[p] == 2 && car_list_is_complete((uint8_t)o->handle)) {
            mission_goto_success();
        }
        return;
    }
    case 0x2d:   /* MISSION_END */
        mission_award_score(c);
        player_add_multiplier((int16_t)m->cur_player, 1);
        mission_goto_success();
        return;
    case 0x2e:   /* EXPLODE_CAR */
        if (line_handle(c->a) > -1) mis_car_destroy(line_handle(c->a));
        award_success();
        return;
    case 0x32:   /* KILL_CAR */
        h = line_handle(c->a);
        if (h > -1) {
            if (car_is_marked_for_removal(h)) ambulance_clear_request(h + 0x26c);
            mission_clear_counter(h);
            car_remove_if_unused(h);
        }
        award_success();
        return;
    case 0x33:   /* ARROWPED */
        if (line_handle(c->a) > -1) hud_arrow_to_obj(line_handle(c->a));
        award_success();
        return;
    case 0x34:   /* ARROWCAR */
        if (line_handle(c->a) > -1) hud_arrow_to_car(line_handle(c->a));
        award_success();
        return;
    case 0x35:   /* REMAP_PED */
        if (line_handle(c->a) < 0) {
            mission_goto_fail();
            return;
        }
        mis_ped_set_remap(line_handle(c->a), (uint8_t)c->d);
        award_success();
        return;
    case 0x36:   /* REMAP_CAR: the sprite's remap byte (+0x260) */
        if (line_handle(c->a) < 0) {
            mission_goto_fail();
            return;
        }
        car_of(line_handle(c->a))->spr.remap = (uint8_t)c->d;
        award_success();
        return;
    case 0x37:   /* ARE_BOTH_ONSCREEN */
        h = line_handle(c->d);
        if (h > -1 && line_handle(c->a) > -1 && car_is_on_screen(car_of(line_handle(c->a))) && car_is_on_screen(car_of(h)))
            award_success();
        else
            mission_goto_fail();
        return;
    case 0x38:   /* THROW_TO_POINT */
        mission_get_object_pos_scratch(c->d);
        obj_throw_to_point(line_handle(c->a), m->scratch_x << 16, m->scratch_y << 16);
        award_success();
        return;
    case 0x3c:   /* STARTUP: once per level, voice event 0x14 in 10 frames */
        if (m->u67661c == 0) {
            event_schedule(10, 0, 0x14);
            trigger_set_state3(line_handle(c->a));
            mission_goto_success();
            m->u67661c = 1;
        }
        return;
    case 0x3d:   /* CHECK_PEDBACK: until the ped of line a is in state 6 */
        if ((int16_t)mission_get_object_health(c->a) == 0) {
            mission_goto_fail();
            return;
        }
        if (ped_of(line_handle(c->a))->state != 6) return;
        award_success();
        return;
    case 0x3e:   /* START_HIRED_ESC */
        mission_get_object_pos_scratch(c->d);
        if ((int16_t)hunt_add_block_target(line_handle(c->a), m->scratch_x >> 6, m->scratch_y >> 6, m->scratch_z >> 6) == -1) return;
        award_success();
        return;
    case 0x3f:   /* DUMMY_OFF */
        if (line_handle(c->a) < 0) {
            mission_goto_fail();
            return;
        }
        car_of(line_handle(c->a))->unkc0 = 1;
        award_success();
        return;
    case 0x40:   /* POWERUP_ON: type in the handle, value in the parameter */
        o = mission_obj(c->a);
        mission_get_object_pos_scratch(c->a);
        powerup_add(o->handle, o->param, m->scratch_x << 16, m->scratch_y << 16, m->scratch_z * 0x10000 - 1);
        mission_goto_success();
        return;
    case 0x41:   /* PLAIN_EXPL_BUILDING */
        mission_get_object_pos_scratch(c->a);
        mis_obj_create425780(m->scratch_x >> 6, m->scratch_y >> 6, m->scratch_z >> 6, c->d, m->cur_player);
        award_success();
        return;
    case 0x42:   /* CHANGE_BLOCK: the block takes the info in the BLOCK_INFO's handle */
        mission_get_object_pos_scratch(c->a);
        map_set_block_thunk(m->scratch_x >> 6, m->scratch_y >> 6, m->scratch_z >> 6, (uint32_t)line_handle(c->a));
        award_success();
        return;
    case 0x43:   /* CHANGE_TYPE: face handle, tile param */
        mission_get_object_pos_scratch(c->a);
        o = mission_obj(c->a);
        mission_call438020(m->scratch_x >> 6, m->scratch_y >> 6, m->scratch_z >> 6, (uint8_t)o->handle, (uint8_t)o->param);
        award_success();
        return;
    case 0x44:   /* P_BRIEF_TIMED */
        mission_get_object_pos_scratch(c->a);
        mission_brief_countdown(c->d, (int16_t)c->e, m->cur_player & 0xff);
        mission_goto_success();
        return;
    case 0x46:   /* DOOR_ON: waits until the door can be unlocked */
        if (door_unlock(line_handle(c->a))) award_success();
        return;
    case 0x47:   /* DOOR_OFF */
        if (door_lock(line_handle(c->a))) award_success();
        return;
    case 0x48:   /* WRECK_CURR_TRAIN */
        m->scratch_a = (int8_t)player_find_train_slot(m->cur_player);
        if (m->scratch_a == -1) {
            mission_goto_fail();
            return;
        }
        mission_call46d650(m->scratch_a);
        award_success();
        return;
    case 0x49:   /* OPEN_DOOR */
        if (line_handle(c->a) == -1) {
            mission_goto_fail();
            return;
        }
        door_open(line_handle(c->a));
        award_success();
        return;
    case 0x4a:   /* CLOSE_DOOR */
        if (line_handle(c->a) == -1) {
            mission_goto_fail();
            return;
        }
        door_close(line_handle(c->a));
        award_success();
        return;
    case 0x4c:   /* BANK_ROBBERY: wanted level 4 and a crime report at the object of line a */
        player_add_wanted_points(m->cur_ped, 1000);
        player_set_wanted_level((int16_t)m->cur_ped, 4);
        if (c->a > -1) {
            h = mission_obj(c->a)->handle;
            mission_get_object_pos_scratch(c->a);
            police_report_crime(0, (int16_t)h, 9, m->scratch_x << 16, m->scratch_y << 16, m->scratch_z << 16);
        }
        award_success();
        return;
    case 0x4d:   /* DELAY_CRIME: a timed bomb on a car (kind 0) or ped (kind 1 for a created PED) */
        m->scratch_a = 0;
        o = mission_obj(c->a);
        if (o->type != MT_CAR && o->type != MT_PARKED) m->scratch_a = o->type == MISSION_TYPE_SPAWNED_PED;
        mission_add_timed_bomb(m->cur_player, m->scratch_a, (int16_t)o->handle, (int16_t)c->c, (int16_t)c->d);
        award_success();
        return;
    case 0x50:   /* BANK_ALARM_ON: a sound source at the block of line d, its slot in that object's param */
        o = mission_obj(c->d);
        mission_get_object_pos_scratch(c->d);
        o->param = (int8_t)mission_add_sound_source((m->scratch_x + 0x20) * 0x10000, (m->scratch_y + 0x20) * 0x10000,
                                                    m->scratch_z << 16);
        mission_goto_success();
        return;
    case 0x51:   /* BANK_ALARM_OFF */
        mission_remove_sound_source((uint8_t)mission_obj(c->a)->param);
        award_success();
        return;
    case 0x52:   /* GARAGE_SEND */
        mission_get_object_pos_scratch(c->d);
        if ((int16_t)hunt_add_block_target2(line_handle(c->a), m->scratch_x >> 6, m->scratch_y >> 6, m->scratch_z >> 6) == -1) return;
        award_success();
        return;
    case 0x54: { /* PLAYER_ARE_BOTH_ONSCREEN: the player (in a car or on foot) and the car of line a */
        int car = (int16_t)line_handle(c->a);
        int kind = player_get_controlled_kind(m->cur_player);
        if (kind == 0) m->scratch_a = car_is_on_screen(car_of(player_get_controlled_id(m->cur_player)));
        else if (kind == 2) m->scratch_a = ped_is_visible_recent(ped_of(player_get_controlled_id(m->cur_player)));
        else m->scratch_a = 0;
        if (m->scratch_a && car_is_on_screen(car_of(car))) award_success();
        else mission_goto_fail();
        return;
    }
    case 0x55:   /* CHECK_CAR: the player drives the car of line a (with remap d if d >= 0) */
        h = line_handle(c->a);
        if (h > -1 && player_get_controlled_kind(m->cur_player) == 0 && (int16_t)player_get_controlled_id(m->cur_player) == h &&
            (c->d < 0 || car_of(player_get_controlled_id(m->cur_player))->remap == (uint32_t)c->d))
            award_success();
        else
            mission_goto_fail();
        return;
    case 0x56: { /* WAIT_FOR_PLAYER: waits while the player is within d blocks of line a */
        mission_get_object_pos_scratch(c->a);
        const int32_t *pos = player_get_controlled_pos(m->cur_player);
        int bx = m->scratch_x >> 6, by = m->scratch_y >> 6, r = c->d;
        int px = pos[0] >> 22, py = pos[1] >> 22;
        /* Quirk kept: the original's test is x >= bx - r && (x <= bx + r || y >= by - r) && y <= by + r */
        if (px >= bx - r && (px <= bx + r || py >= by - r) && py <= by + r) return;
        award_success();
        return;
    }
    case 0x57:   /* EXPL_PED */
        mission_explode_ped((int16_t)line_handle(c->a));
        award_success();
        return;
    case 0x58:   /* PARKED_ON */
        op_car_on(0);
        return;
    case 0x59:   /* KICKSTART */
        op_kickstart();
        return;
    case 0x5a: { /* IS_PED_IN_CAR: the ped of line a in a car (of model d if d >= 0) */
        h = line_handle(c->a);
        if (h > -1) {
            const Ped *pp = ped_of(h);
            if (pp->state == 7 || pp->state == 6) {
                if (c->d < 0 || c->d == car_of(pp->car)->model) award_success();
                else mission_goto_fail();
                return;
            }
        }
        mission_goto_fail();
        return;
    }
    case 0x5b:   /* CANCEL_BRIEFING: the countdown of the brief at label a */
        v = label_pc(c->a);
        Snd_DisableAlarmLoop();
        pager_remove_countdown(v >= 0 ? m->commands[v].e : -1);
        award_success();
        return;
    case 0x5c:   /* FREEZE_TIMED */
        player_set_timer18c(m->cur_player, c->a);
        award_success();
        return;
    case 0x5d:   /* FREEZE_ENTER */
        player_set_flag189(m->cur_player, 1);
        award_success();
        return;
    case 0x5e:   /* UNFREEZE_ENTER */
        player_set_flag189(m->cur_player, 0);
        award_success();
        return;
    case 0x5f:   /* EXPL_NO_FIRE */
        mission_get_object_pos_scratch(c->a);
        mis_obj_create4258d0(m->scratch_x >> 6, m->scratch_y >> 6, m->scratch_z >> 6, c->d, m->cur_player);
        award_success();
        return;
    case 0x60:   /* KILL_PROCESS: every process the KICKSTART at label a started */
        v = m->scratch_a = label_pc(c->a);
        if (v >= 0) {
            for (int i = 0; i < 4; i++) kick_stop(v, i);
            if (m->commands[v].e > -1) m->commands[v].e = -1;
        }
        award_success();
        return;
    case 0x61:   /* NEXT_KICK */
        op_next_kick();
        return;
    case 0x62:   /* KILL_SIDE_PROC: the same, keeping the KICKSTART's e */
        v = m->scratch_a = label_pc(c->a);
        if (v >= 0)
            for (int i = 0; i < 4; i++) kick_stop(v, i);
        award_success();
        return;
    case 0x63: { /* SET_KILLTRIG: the trigger of line a gets field e - 1 of the KICKSTART at label d's list */
        h = line_handle(c->a);
        v = m->scratch_a = label_pc(c->d);
        m->scratch_b = mission_retrieve_data(v >= 0 ? m->commands[v].d : -1, c->e - 1);
        trigger_set_flag1c(h, m->scratch_b);
        mission_goto_success();
        return;
    }
    case 0x64:   /* POWERUP_OFF */
        mission_get_object_pos_scratch(c->a);
        powerup_remove_at(m->scratch_x << 16, m->scratch_y << 16);
        award_success();
        return;
    case 0x65:   /* KILL_SPEC_PROC: field d (1..4) of the list, anything else clears its e */
        v = m->scratch_a = label_pc(c->a);
        if (v >= 0) {
            if (c->d >= 1 && c->d <= 4) kick_stop(v, c->d - 1);
            else if (m->commands[v].e > -1) m->commands[v].e = -1;
        }
        award_success();
        return;
    case 0x66:   /* SCORE_CHECK */
        if (player_get_score(m->cur_player) < c->a) {
            mission_goto_fail();
            return;
        }
        player_set_flag001(m->cur_player);
        award_success();
        return;
    case 0x67:   /* FRENZY_SET: the score now into d of the FRENZY_CHECK at label a */
        v = m->scratch_a = label_pc(c->a);
        if (v >= 0) m->commands[v].d = player_get_score(m->cur_player);
        award_success();
        return;
    case 0x68:   /* FRENZY_CHECK: a points scored since */
        m->scratch_a = player_get_score(m->cur_player);
        if (m->scratch_a - c->d < c->a) {
            mission_goto_fail();
            return;
        }
        Snd_PlayVoice(4);
        award_success();
        return;
    case 0x69:   /* IS_GOAL_DEAD: the object of line a is dead (killed by the ped of line d, if d > 0) */
        if ((int16_t)mission_get_object_health(c->a) == 0) {
            o = mission_obj(c->a);
            if (c->d < 1 || o->type != MISSION_TYPE_SPAWNED_PED || ped_of(o->handle)->u5a == line_handle(c->d)) {
                award_success();
                return;
            }
        }
        mission_goto_fail();
        return;
    case 0x6a: { /* GENERAL_ONSCREEN: line a inside the player's view rectangle */
        mission_get_object_pos_scratch(c->a);
        const int32_t *r = player_get_view_rect(m->cur_player);
        if (m->scratch_x < r[0] || m->scratch_y < r[2] || r[1] < m->scratch_x || r[3] < m->scratch_y) mission_goto_fail();
        else award_success();
        return;
    }
    case 0x6b: { /* GET_CAR_INFO: the car the ped of line a is in becomes the CAR of line d */
        const Ped *pp = ped_of(line_handle(c->a));
        o = mission_obj(c->d);
        if (pp->car == -1) {
            mission_goto_fail();
            return;
        }
        o->handle = pp->car;
        o->type = MT_CAR;
        Car *cr = car_of(pp->car);
        cr->unk139 = 0;
        cr->script_line = (int16_t)c->d;
        if (o->persistent == 1) cleanup_car_add(cr->id);   /* quirk kept: the test is inverted here */
        award_success();
        return;
    }
    case 0x6c: { /* START_CHOPPER: from line a to line d */
        int32_t x, y, z, size;
        mission_get_object_pos_scratch(c->d);
        mission_get_object_pos(c->a, &x, &y, &z, &size);
        heli_spawn(x << 16, y << 16, z << 16, size, m->scratch_x << 16, m->scratch_y << 16, m->scratch_z << 16);
        mission_goto_success();
        return;
    }
    case 0x6d: { /* LOCATE: the player's ped on foot within e pixels of line a; d frames to fail */
        mission_get_object_pos_scratch(c->a);
        const Ped *pp = ped_of(m->cur_ped);
        if (m->step[p] == 1) m->step[p] = c->d < 1 ? 2 : (int16_t)(c->d + 5);
        bool near = ped_is_near_point((int16_t)m->cur_ped, (int16_t)m->scratch_x, (int16_t)m->scratch_y, (int16_t)c->e);
        if (!near || pp->state == 7 || pp->state == 6) {
            if (m->step[p] >= 6 && --m->step[p] == 5) mission_goto_fail();
            return;
        }
        mission_goto_success();
        return;
    }
    case 0x6e:   /* SPEECH_BRIEF */
        mission_brief4((int16_t)c->e, m->cur_player);
        mission_goto_success();
        return;
    case 0x6f:   /* MOBILE_BRIEF */
        mission_brief3((int16_t)c->e, m->cur_player);
        mission_goto_success();
        return;
    case 0x70:   /* PIXEL_CAR_ON */
        op_car_on(1);
        return;
    case 0x71:   /* SET_NO_COLLIDE */
        mis_car_set_flag128_99((int16_t)line_handle(c->a));
        award_success();
        return;
    case 0x72:   /* PED_WEAPON */
        ped_set_weapon_raw((int16_t)line_handle(c->a), c->d);
        award_success();
        return;
    case 0x73: { /* FREEUP_CAR: no longer a script car */
        Car *cr = car_of(line_handle(c->a));
        cr->script_line = -1;
        cr->unk139 = 1;
        award_success();
        return;
    }
    case 0x74: { /* MESSAGE_BRIEF: the text whose key is the number e */
        static char key[16];   /* 0x502f78 */
        snprintf(key, sizeof key, "%d", c->e);
        hud_show_big_message_hi(text_get(key));
        mission_goto_success();
        return;
    }
    case 0x75:   /* PARKED_PIXELS_ON */
        op_car_on(2);
        return;
    case 0x76:   /* PED_POLICE */
        ped_make_police(ped_of(line_handle(c->a)));
        award_success();
        return;
    case 0x77: { /* DROP_WANTED_LEVEL */
        const Ped *pp = ped_of(m->cur_ped);
        if (pp->state != 9 && pp->ufc == 0) {
            player_clear_wanted_points(m->cur_ped & 0xffff);
            player_clear_wanted_level((int16_t)m->cur_ped);
        }
        award_success();
        return;
    }
    case 0x78:   /* IS_PED_ARRESTED */
        if (ped_of(line_handle(c->a))->state != 9) mission_goto_fail();
        else award_success();
        return;
    case 0x79: { /* HELL_ON: a gang car (model 3) with its driver at line a */
        o = mission_obj(c->a);
        mission_get_object_pos_scratch(c->a);
        h = (int16_t)mis_car_create(m->scratch_x >> 6, m->scratch_y >> 6, m->scratch_z >> 6, 3, (int16_t)o->param, 0);
        o->handle = h;
        if (h < 0) {
            mission_goto_fail();
            return;
        }
        m->scratch_a = mission_clear_block(m->scratch_x, m->scratch_y, m->scratch_z, h, 3, (int16_t)o->param) & 0xff;
        Car *cr = car_get(h);
        cr->script_line = (int16_t)c->a;
        ped_create_car_driver(cr);
        o->type = MT_HELLS;
        gang_add_car(cr->id);
        Ped *pp = ped_of(cr->driver);
        mis_ped_set_remap(cr->driver, 0x21);
        pp->graphic = 0;
        mission_put_ped_in_car(pp->id, cr->id);
        if (!o->persistent) cleanup_car_add(cr->id);
        award_success();
        cr->unk139 = 0;
        return;
    }
    case 0x7c:   /* SET_PED_SPEED */
        *(uint8_t *)&ped_of(line_handle(c->a))->move_speed = (uint8_t)c->d;
        award_success();
        return;
    case 0x7d:   /* IS_PLAYER_ON_TRAIN: the train into the handle of line a */
        if (player_get_controlled_kind(m->cur_player) != 1) {
            mission_goto_fail();
            return;
        }
        mission_obj(c->a)->handle = (int16_t)player_get_controlled_id(m->cur_player);
        award_success();
        return;
    case 0x7e:   /* WRECK_A_TRAIN */
        mission_call46d650((uint8_t)line_handle(c->a));
        award_success();
        return;
    case 0x7f:   /* INCCOUNT: up to d succeeds (d < 0: never) */
        o = mission_obj(c->a);
        o->handle++;
        if (c->d < 0 || o->handle < c->d) mission_goto_fail();
        else award_success();
        return;
    case 0x80:   /* COMPARE */
        if (line_handle(c->a) != c->d) mission_goto_fail();
        else award_success();
        return;
    case 0x81:   /* RESET */
        mission_kill_processes(p);
        mission_cleanup_peds();
        mission_cleanup_cars();
        mission_cleanup_objects();
        trigger_disable_all_temp();
        door_reset_all_temp();
        hud_arrow_off();
        mission_remove_all_sound_sources();
        mission_alarm_sound_stop_all();
        award_success();
        return;
    case 0x82:   /* KEEP_THIS_PROC */
        m->kind[p] = 1;
        award_success();
        return;
    case 0x83:   /* SET_COLLIDE */
        mis_car_set_flag128_1((int16_t)line_handle(c->a));
        award_success();
        return;
    case 0x84:   /* DEAD_ARRESTED */
        if (ped_of(line_handle(c->a))->state != 9 && (int16_t)mission_get_object_health(c->a) != 0) mission_goto_fail();
        else award_success();
        return;
    case 0x86:   /* STOP_FRENZY */
        player_end_frenzy_weapon(m->cur_player);
        award_success();
        return;
    case 0x8d:   /* KF_PROCESS */
        mission_kf_process_cars();
        m->kind[m->cur] = 2;
        award_success();
        return;
    case 0x91:   /* RESET_WITH_BRIEFS */
        mission_kill_processes(p);
        mission_cleanup_peds();
        mission_cleanup_cars();
        mission_cleanup_objects();
        trigger_disable_all_temp();
        door_reset_all_temp();
        hud_skip_subtitle();
        pager_reset();
        hud_arrow_off();
        mission_remove_all_sound_sources();
        award_success();
        return;
    default:     /* DRIVEON (7), DONOWT (0x11), XXXX (0x7a, 0x7b): no case, the process stays */
        return;
    }
}

/* the opcodes with a function of their own (mission_ops.c) */
static void (*const op_fn[MISSION_OPCODES])(void) = {
    [0x00] = mission_op_locate_stopped, [0x01] = mission_op_destroy, [0x02] = mission_op_answer,
    [0x03] = mission_op_steal, [0x09] = mission_op_send_to, [0x0b] = mission_op_make_obj,
    [0x10] = mission_op_m_phone, [0x15] = mission_op_goto, [0x16] = mission_op_crane, [0x17] = mission_op_park,
    [0x1f] = mission_op_ped_on, [0x21] = mission_op_car_on, [0x26] = mission_op_ped_send_to,
    [0x2a] = mission_op_do_repo, [0x2f] = mission_op_start_model, [0x30] = mission_op_do_model,
    [0x31] = mission_op_return_control, [0x39] = mission_op_goto_dropoff, [0x3a] = mission_op_model_hunt,
    [0x3b] = mission_op_model_future, [0x45] = mission_op_change_ped_type, [0x4b] = mission_op_ped_out_of_car,
    [0x4e] = mission_op_get_driver_info, [0x4f] = mission_op_kill_ped, [0x53] = mission_op_dummy_drive_on,
    [0x85] = mission_op_is_powerup_done, [0x87] = mission_op_frenzy_brief, [0x88] = mission_op_add_a_life,
    [0x89] = mission_op_kf_brief_timed, [0x8a] = mission_op_kf_cancel_briefing, [0x8b] = mission_op_kf_brief_general,
    [0x8c] = mission_op_kf_cancel_general, [0x8e] = mission_op_reset_kf, [0x8f] = mission_op_wait_for_players,
    [0x90] = mission_op_red_arrow, [0x92] = mission_op_red_arrow_off, [0x93] = mission_op_is_a_train_wrecked,
    [0x94] = mission_op_inc_heads, [0x95] = mission_op_is_ped_stunned,
};

/* Mission_StepProcess 0x4471a0: one command of process `cur`. The command's handler leaves the next
   pc in cur_pc; then a reached TARGET_SCORE ends everything (result 8) and the pc and wait counter go
   back to the process. */
void mission_step_process(void)
{
    Mission *m = M;
    int p = m->cur;
    m->cur_wait = m->wait[p];
    m->cur_ped = m->player_ped[p & 3];   /* 0x656180[p]: past the players the original reads bss */
    if (p >= 4) m->cur_ped = m->player_ped[mission_owner_player(p) & 3];
    m->cur_pc = m->pc[p];
    m->cur_player = mission_owner_player(p);
    if (m->cur_pc < 0 || m->cur_pc >= MISSION_COMMANDS) game_fatal(-0xb6, 0xe4, m->cur_pc);   /* (port: outside the table) */
    int op = m->commands[m->cur_pc].op;
    if (op > 0x95) game_fatal(-0xb6, 0xe4, mission_find_line(m->cur_pc));   /* unknown opcode */
    if (mission_trace) mission_trace(p, m->cur_pc, op);
    if (op_fn[op]) op_fn[op]();
    else step_inline(op);
    if (mission_score_target_reached()) mission_end_all(m->cur, 8);
    m->pc[m->cur] = (int16_t)m->cur_pc;
    m->wait[m->cur] = m->cur_wait;
}
