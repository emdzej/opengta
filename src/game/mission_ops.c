/* The opcode handlers that are functions of their own in the original (MissionOp_* 0x43e980-0x4453c0,
   Mission_GetParkExitPos 0x440790). Mission_StepProcess (mission_run.c) calls them with the running
   process in g_mission: cur (0x6b3b70), its command cur_pc (0x655e58), the owning player cur_player
   (0x676608) and that player's ped cur_ped (0x5f30e0), the wait counter cur_wait (0x655e54), the step
   state step[cur] (0x6560b8; 1 on entering a command). A handler leaves the next command in cur_pc.

   Command fields: a (usually an object line), b success label, c fail label, d (timer / parameter),
   e (score / parameter). The original inlines Mission_AwardScore and Mission_BranchSuccess/Fail in
   most handlers; the port calls them (same effect: score e to the owner, then b / c with 0 = next
   command, -1 = end the process). Some handlers write their command's d / e: the original does.

   Timed waits (LOCATE_STOPPED, GOTO, DESTROY, STEAL, IS_POWERUP_DONE): on entry step = d + 5 (0 with
   d < 1: wait forever); each frame without success counts the step down, at 5 the fail branch. */
#include "mission_ops.h"
#include "../audio/audio.h"
#include "../exe.h"
#include "../render/camera.h"
#include "../text.h"
#include "car.h"
#include "dummy.h"
#include "event.h"
#include "game.h"
#include "gmath.h"
#include "mission_obj.h"
#include "mission_run.h"
#include "obj.h"
#include "ped.h"
#include "player.h"
#include "trigger.h"
#include "gang.h"
#include "path.h"
#include "powerup.h"
#include "sentinel.h"
#include "train.h"
#include "../hud/hud.h"
#include <stdio.h>
#include <string.h>

#define M (&g_mission)

/* 0x7710f4 an MPHONE group is ringing: g_mrt.phones_off (trigger.h; Mission_InitCityTables resets it) */

/* ---- small helpers ---- */

static MissionCommand *cmd(void) { return mission_cur_cmd(); }
static int16_t *step(void) { return &M->step[M->cur]; }
static void win(void) { mission_award_score(cmd()); mission_goto_success(); }

/* the step-state timer of the timed waits */
static void timeout_begin(void)
{
    if (*step() == 1) *step() = cmd()->d < 1 ? 0 : (int16_t)(cmd()->d + 5);
}
static void timeout_tick(void)
{
    int16_t *s = step();
    if (*s > 5 && --*s == 5) mission_goto_fail();
}

/* Entities of handles. With a bad handle (-1) the original reads outside its tables; the port reads
   a record of -1s. */
static Car *car_of(int h)
{
    static Car none;
    h = (int16_t)h;
    if (h < 0 || h >= CAR_MAX) {
        memset(&none, 0xff, sizeof none);
        return &none;
    }
    return car_get(h);
}
static Ped *ped_of(int h)
{
    static Ped none;
    h = (int16_t)h;
    if (h < 0 || h >= PED_MAX) {
        memset(&none, 0xff, sizeof none);
        return &none;
    }
    return ped_get(h);
}
static Obj *obj_of(int h)
{
    static Obj none;
    h = (int16_t)h;
    if (h < 0 || h >= OBJ_MAX) {
        memset(&none, 0xff, sizeof none);
        return &none;
    }
    return obj_get(h);
}
/* script object by index (not line) */
static MissionObject *obj_at(int i)
{
    static MissionObject none;
    if (i < 0 || i >= MISSION_OBJECTS) {
        memset(&none, 0xff, sizeof none);
        return &none;
    }
    return &M->objects[i];
}
static int line_index(int line) { return line >= 0 && line < MISSION_LINES ? M->line_obj[line] : -1; }
static int line_handle(int line) { return mission_obj(line)->handle; }
/* the command index of a label (0x693b30); outside the map the original reads past it: -1 here */
static int label_index_pc(int label)
{
    int pc = label >= 0 && label < MISSION_LINES ? M->labels[label] : -1;
    return pc >= 0 && pc < MISSION_COMMANDS ? pc : 0;
}

/* Car fields the car header doesn't name yet (offsets of the original's record, all before the
   sprite, so they hold on any host). */
static int16_t *car16(Car *c, int off) { return (int16_t *)((uint8_t *)c + off); }
static uint8_t *car8(Car *c, int off) { return (uint8_t *)c + off; }
enum { CAR_UNKB6 = 0xb6, CAR_UNK13A = 0x13a, CAR_UNK188 = 0x188, CAR_UNK18A = 0x18a };

static int px(int32_t v) { return (int16_t)((uint32_t)v >> 16); }   /* the pixel (high word) of 16.16 */

static const char *txt(uint32_t key_va)
{
    const char *t = text_get(exe_str(key_va));
    return t ? t : "";
}
static void subtitle(uint32_t key_va) { hud_show_subtitle(1, txt(key_va)); }

static void add_cleanup(int32_t *list, int *n, int id)
{
    if (*n < MISSION_CLEANUP) list[(*n)++] = id;   /* (the original has no bound) */
}

/* Camera_Snap 0x43c5d0 by player index */
static void snap_camera(int n)
{
    CameraPlayer cp;
    player_camera(n, &cp);
    CameraWorld w = player_camera_world(n);
    camera_snap(&cp, &w);
    player_camera_store(n, &cp);
}

static bool ped_in_car_state(const Ped *p) { return p->state == 7 || p->state == 6; }

/* ---- the handlers ---- */

/* MissionOp_LocateStopped 0x43e980 (0x00 LOCATE_STOPPED): the player's ped within e pixels of the
   object of a, standing still and on foot -> b (no score); timed by d. */
void mission_op_locate_stopped(void)
{
    timeout_begin();
    mission_get_object_pos_scratch(cmd()->a);
    Ped *p = ped_of(M->cur_ped);
    if (ped_is_near_point(M->cur_ped, (int16_t)M->scratch_x, (int16_t)M->scratch_y, (int16_t)cmd()->e) &&
        p->speed == 0 && !ped_in_car_state(p))
        mission_goto_success();
    else
        timeout_tick();
}

/* MissionOp_Goto 0x43eb50 (0x15 GOTO): what the player controls is in the block of the object of a. */
void mission_op_goto(void)
{
    timeout_begin();
    mission_get_object_pos_scratch(cmd()->a);
    const int32_t *pos = player_get_controlled_pos(M->cur_player);
    if ((px(pos[0]) & ~0x3f) == (M->scratch_x & ~0x3f) && (px(pos[1]) & ~0x3f) == (M->scratch_y & ~0x3f))
        win();
    else
        timeout_tick();
}

/* MissionOp_Destroy 0x43ed90 (0x01 DESTROY): the car / ped of a has no health left. */
void mission_op_destroy(void)
{
    timeout_begin();
    if ((int16_t)mission_get_object_health(cmd()->a) == 0) win();
    else timeout_tick();
}

/* MissionOp_Answer 0x43ef80 (0x02 ANSWER): the phone of a rings (state 2, alarm sound) for d frames
   (in 50-frame steps the object toggles between states 2 and 3); the player walking up to it on
   foot answers (-> b), the time running out -> c. A phone with a parameter p (TELEPHONE p1) only
   counts while Ped_CheckInCar(ped, p - 1) says 1; e keeps that answer. */
void mission_op_answer(void)
{
    MissionCommand *c = cmd();
    MissionObject *o = mission_obj(c->a);
    Ped *p = ped_of(M->cur_ped);
    if (*step() == 1) {
        *step() = (int16_t)(c->d + 10);
        mission_obj_set_state(o->handle, 2);
        mission_alarm_sound_add(o->handle);
        c->e = 1;
    }
    mission_get_object_pos_scratch(c->a);
    int ok = 1;
    if (o->param >= 1 && c->e == 1) {
        ok = ped_check_in_car(M->cur_ped, o->param - 1);
        c->e = ok == 1;
    }
    if (M->cur_wait == 0) {
        obj_toggle_state23(o->handle);
        M->cur_wait = 50;
        if (--*step() == 10) {   /* rang out */
            mission_obj_set_state(o->handle, 1);
            mission_alarm_sound_remove(o->handle);
            mission_goto_fail();
            return;
        }
        if (ok != 1 || !ped_is_near_point16(M->cur_ped, (int16_t)M->scratch_x, (int16_t)M->scratch_y) ||
            ped_in_car_state(p))
            return;
        mission_obj_set_state(o->handle, 1);
        mission_alarm_sound_remove(o->handle);
        if (c->e != 1) mission_goto_fail();
        else mission_goto_success();
        return;
    }
    M->cur_wait--;
    if (ok != 1 || !ped_is_near_point16(M->cur_ped, (int16_t)M->scratch_x, (int16_t)M->scratch_y) || ped_in_car_state(p))
        return;
    mission_obj_set_state(o->handle, 1);
    mission_alarm_sound_remove(o->handle);
    mission_goto_success();   /* quirk: between the 50-frame toggles e isn't looked at */
}

/* MissionOp_Steal 0x43f360 (0x03 STEAL): the player's ped drives the car of a (voice 0x11). */
void mission_op_steal(void)
{
    timeout_begin();
    if (car_has_live_driver(M->cur_ped, line_handle(cmd()->a))) {
        Snd_PlayVoice(0x11);
        win();
    } else {
        timeout_tick();
    }
}

/* MissionOp_DummyDriveOn 0x43f580 (0x53 DUMMY_DRIVE_ON): the car of a turns a parked car; once its
   driver sits in it (anim 0 / 0x7f / 0x80, state 7, AI 0x11) it drives off (speed 5) -> b. */
void mission_op_dummy_drive_on(void)
{
    int h = line_handle(cmd()->a);
    if (h < 0) return;
    mis_car_park(h & 0xffff);
    Car *c = car_of(h);
    if (c->driver < 0) return;
    Ped *p = ped_of(c->driver);
    if ((p->anim == 0 || p->anim == 0x7f || p->anim == 0x80) && p->car == c->id && p->state == 7 && p->u7c == 0x11) {
        c->speed = 5;
        p->graphic = 0;
        mission_put_ped_in_car(c->driver, c->id);
        win();
    }
}

/* MissionOp_SendTo 0x43f760 (0x09 SENDTO): the dummy car of a drives to the object of d (its
   controller in the object's parameter); continues once no controller holds the path search
   (0x4b3094: the dummy's search done). */
void mission_op_send_to(void)
{
    if (*step() != 0) {
        MissionObject *o = mission_obj(cmd()->a);
        mission_get_object_pos_scratch(cmd()->d);
        o->param = (int16_t)dummy_start_drive(o->handle, M->scratch_x, M->scratch_y, M->scratch_z);
        *step() = 0;
    }
    if (g_path_owner != -1) return;
    win();
}

/* MissionOp_MakeObj 0x43f950 (0x0b MAKEOBJ): creates the object of a (FUTURE: handle = object type)
   at its position with angle d; type 0x58 gets +0x12 = 1. The script object turns OBJECT. */
void mission_op_make_obj(void)
{
    MissionObject *o = mission_obj(cmd()->a);
    mission_get_object_pos_scratch(cmd()->a);
    M->scratch_a = o->handle == 0x58;
    int h = (int16_t)mission_obj_create(M->scratch_x, M->scratch_y, M->scratch_z, o->handle & 0xffff, (int16_t)cmd()->d);
    o->handle = h;
    if (!o->persistent) add_cleanup(M->cleanup_objs, &M->ncleanup_objs, h);
    if (M->scratch_a == 1 && h >= 0) obj_of(h)->u12 = 1;
    o->type = MT_OBJECT;
    win();
}

/* MissionOp_MPhone 0x43fb20 (0x10 MPHONE): c phones on the consecutive lines a, a + 1, ... ring
   together (states 3 / 2 every 25 frames, through d) while the player is within 15 blocks of the
   first; walking out of that range stops them and this process. Answering phone i stops every other
   mission process, clears the mission (cleanup lists, temporary triggers and doors, HUD), branches
   to b + i, forgets that phone and sets the player's phone flag (cleared 250 frames later by the
   event Mission_OnBriefDone(player + 0x32)). */
void mission_op_m_phone(void)
{
    MissionCommand *c = cmd();
    int a = c->a, cp = M->cur_player & 3;
    int ring = 0;
    Ped *p = ped_of(M->cur_ped);
    if (M->phone_flag[cp]) return;
    if (*step() == 1) {
        *step() = 0;
        if (!g_mrt.phones_off) {
            g_mrt.phones_off = 1;
            c->d = 25;
            for (int i = 0; i < c->c; i++) {
                int h = line_handle(a + i);
                if (h >= 0) {
                    mission_obj_set_state(h, 3);
                    mission_alarm_sound_add(h);
                }
            }
        }
    }
    ped_check_in_car(M->cur_ped, -1);   /* (result unused) */
    c->d--;
    mission_get_object_pos_scratch(a);
    if (!ped_is_in_block_rect(M->cur_ped, M->scratch_x >> 6, M->scratch_y >> 6, M->scratch_z >> 6, 0xf)) {
        for (int i = 0; i < c->c; i++) {
            int h = line_handle(a + i);
            if (h >= 0) {
                mission_obj_set_state(h & 0xffff, 1);
                mission_alarm_sound_remove(h);
            }
        }
        g_mrt.phones_off = 0;
        M->active[M->cur] = 0;
        return;
    }
    for (int i = 0; i < c->c; i++) {
        int h = line_handle(a + i);
        if (c->d == 0 && h >= 0) obj_toggle_state23(h & 0xffff), ring = 1;
        if (c->d == -10 && h >= 0) obj_toggle_state23(h), ring = 2;
        mission_get_object_pos_scratch(a + i);
        if (ped_is_near_point16(M->cur_ped, (int16_t)M->scratch_x, (int16_t)M->scratch_y) && h >= 0 &&
            M->phone_flag[cp] == 0 && !ped_in_car_state(p)) {
            for (int j = 0; j < c->c; j++) {
                int hj = line_handle(a + j);
                if (hj >= 0) {
                    mission_obj_set_state(hj & 0xffff, 1);
                    mission_alarm_sound_remove(hj);
                }
            }
            mission_kill_processes(M->cur);
            mission_cleanup_peds();
            mission_cleanup_cars();
            mission_cleanup_objects();
            trigger_disable_all_temp();
            door_reset_all_temp();
            hud_skip_subtitle();
            pager_reset();
            hud_arrow_off();
            mission_remove_all_sound_sources();
            M->cur_pc = mission_branch_success(c, M->cur_pc) + i;   /* (0 + i when b ends the process) */
            g_mrt.phones_off = 0;
            mission_obj(a + i)->handle = -1;
            event_schedule(250, 0, M->cur_player + 0x32);
            M->phone_flag[cp] = 1;
            return;
        }
    }
    if (ring == 1) c->d = -1;
    else if (ring == 2) c->d = 25;
}

/* the crane texts (FXT keys in the exe) */
enum {
    K_CRANE_NOPOLICE = 0x4b1c54, K_CRANE_LONG = 0x4b1ca4, K_CRANE0 = 0x4b1c84, K_CRANE5 = 0x4b1c8c,
    K_CRANE1 = 0x4b1c7c, K_CRANE2 = 0x4b1c74, K_CRANE3 = 0x4b1c6c, K_CRANE4 = 0x4b1c64,
    K_CRANE_NOBOMB = 0x4b1c94, K_LIST_DONE = 0x4b1cec, K_LIST_ONE_LEFT = 0x4b1ce4, K_DUP_MODEL = 0x4b1cd4,
    K_DUP_MODELS = 0x4b1cc0, K_CRANE_EXCELLENT = 0x4b1cb0, K_CRANE_WRECK = 0x4b1cf4,
    K_CRANECAR_TABLE = 0x4b0d88,   /* 4 key pointers by damage / 25 */
};

/* MissionOp_Crane 0x440040 (0x16 CRANE): the crane of a takes the player's car when it stands in the
   block of the object of d. Step 1 checks the car (no police cars, not too long, no bomb, room on the
   drop side, Crane_RequestCar) and shows why not (-> c), else step 3 waits for the crane: state 7
   (gave up) -> c; state 3 (delivered) scores the car (car list delivery, or its value less damage
   shared among the same models already stacked, at least 1000) -> b. Other steps -> b. */
void mission_op_crane(void)
{
    static char buf[256];   /* 0x502f78 */
    MissionCommand *c = cmd();
    MissionObject *o = mission_obj(c->a);
    int cp = M->cur_player;
    if (*step() == 1) {
        if (player_get_controlled_kind(cp) == 0) {
            mission_get_object_pos_scratch(c->d);
            int car = (int16_t)player_get_controlled_id(cp);
            if (car_is_at_block(car, M->scratch_x >> 6, M->scratch_y >> 6, M->scratch_z >> 6)) {
                Car *k = car_of(car);
                if (k->model == 4 || k->model == 0x20) subtitle(K_CRANE_NOPOLICE);
                else if (k->length < -0x40) subtitle(K_CRANE_LONG);
                else if (k->bomb != 0) subtitle(K_CRANE_NOBOMB);
                else {
                    Crane *cr = crane_get(o->handle);
                    M->scratch_a = -2;
                    if (cr->slot > -1) {
                        car_list_get(cr->slot);
                        const Ped *pp = ped_of(M->cur_ped);
                        if (pp->car > -1) {
                            M->scratch_a = car_list_matches(pp->car, cr->slot);
                            if (M->scratch_a == 1) M->scratch_a = -2;
                        }
                    }
                    if (cr->state != 7 && cr->state != 8 && !crane_check_space(o->handle, k->id)) {
                        cr->state = 10;
                        *step() = 4;
                        return;
                    }
                    switch (crane_request_car(o->handle, car)) {
                    case 0:
                        subtitle(M->scratch_a == -2 || M->scratch_a == 1 ? K_CRANE0 : K_CRANE5);
                        *step() = 3;
                        return;
                    case 1: subtitle(K_CRANE1); break;
                    /* 2-4: shown once; while the subtitle still holds it the command waits (strstr) */
                    case 2:
                        if (strstr(txt(K_CRANE2), hud_get_subtitle_buf())) return;
                        subtitle(K_CRANE2);
                        break;
                    case 3:
                        if (strstr(txt(K_CRANE3), hud_get_subtitle_buf())) return;
                        subtitle(K_CRANE3);
                        break;
                    case 4:   /* (the arguments the other way round) */
                        if (strstr(hud_get_subtitle_buf(), txt(K_CRANE4))) return;
                        subtitle(K_CRANE4);
                        break;
                    default: return;
                    }
                }
            }
        }
        mission_goto_fail();
        return;
    }
    if (*step() != 3) {
        mission_goto_success();
        return;
    }
    Crane *cr = crane_get(o->handle);
    if (cr->state == 7) {
        mission_goto_fail();
        return;
    }
    if (cr->state != 3) return;
    int dmg = (int16_t)car_get_damage(cr->car);
    if (dmg >= 100) {
        subtitle(K_CRANE_WRECK);
        mission_goto_success();
        return;
    }
    if (cr->slot > -1) {
        CarListEntry *e = car_list_get(cr->slot);
        ++*step();
        if (car_list_matches(cr->car, cr->slot) != -1) {
            int t = e->target;
            if (t > 0 && e->count == t) {
                e->count++;
                subtitle(K_LIST_DONE);
                /* the entry's +0xc (as an int) is a command index whose e is the score */
                int pc = (int32_t)((uint32_t)(uint16_t)e->uc | (uint32_t)(uint16_t)e->ue << 16);
                int n = mission_owner_player(M->cur);
                if (pc >= 0 && pc < MISSION_COMMANDS && M->commands[pc].e > 0)
                    player_add_score(n, M->commands[pc].e, 100, 100, 100, 0);
                cr->slot = -1;
                return;
            }
            e->count++;
            if (e->count == t - 1) subtitle(K_LIST_ONE_LEFT);
            return;
        }
    }
    int car = cr->car;
    int same = car_list_count_same_model(car, cr->stack);
    if (same > 0) subtitle(same == 1 ? K_DUP_MODEL : K_DUP_MODELS);
    int v = car_get_model_value(car, (int16_t)o->handle);
    v = v * (100 - dmg) / 100 / (same + 1);
    if (v < 1000) v = 1000;
    player_add_score(cp, v, 100, 100, 100, 0);
    mission_goto_success();
    uint32_t key = dmg == 0 ? K_CRANE_EXCELLENT : exe_u32(K_CRANECAR_TABLE + 4u * (unsigned)(dmg / 25));
    snprintf(buf, sizeof buf, txt(key), v);
    hud_show_subtitle(1, buf);
}

/* Mission_GetParkExitPos 0x440790 */
void mission_get_park_exit_pos(int32_t *x, int32_t *y, int16_t *angle)
{
    *x = M->park_exit_x;
    *y = M->park_exit_y;
    *angle = M->park_exit_angle;
}

/* MissionOp_Park 0x4407c0 (0x17 PARK): the player drives a car into the garage of the door object a
   from direction d (0 from +y, 1 from -x, 2 from -y, 3 from +x as the checks read). Step 1 takes the
   car over (owner 99, slow forward) and fixes the camera when the car is in the trigger's area (c < 1)
   or in the block of the object of c; step 2 waits until the car is inside and lets the driver out
   (the exit point Mission_GetParkExitPos gives); step 3 waits for the car to reach the back, closes
   the door, stops the car, schedules its deletion (event 0 with car + 200 after 10 frames) -> b and
   gives the camera back. */
void mission_op_park(void)
{
    static const int16_t exit_angle[4] = { 0, 0x300, 0x200, 0x100 };
    MissionCommand *c = cmd();
    int cp = M->cur_player;
    int32_t *park = &M->park_car[cp & 3];
    Car *k;
    if (*park == -1) {
        if (player_get_controlled_kind(cp) != 0) return;
        int id = (int16_t)player_get_controlled_id(cp);
        k = car_of(id);
        *park = id;
        mis_car_set_flag128_99(id);
    } else {
        if (*park < -1) return;
        /* quirk: "kind != 3 || kind != 0" always holds, so only the car is compared */
        if (*step() < 3 && (int16_t)player_get_controlled_id(cp) != *park) return;
        k = car_of(*park);
    }
    Ped *p = ped_of(M->cur_ped);
    mission_get_object_pos_scratch(c->a);
    int s = *step();
    int door_obj = line_index(c->a);
    int32_t x = M->scratch_x, y = M->scratch_y;
    int cx = px(k->spr.x), cy = px(k->spr.y), bx = k->spr.x >> 22, by = k->spr.y >> 22;
    if (s == 1) {
        k->unkc0 = 1;
        k->speed = 5;
        *car16(k, CAR_UNKB6) = 0;
        if (player_get_view_kind(cp) != 3) {
            if (c->c < 1) {
                if (M->trigger[M->cur] != -1) {
                    const Trigger *t = trigger_get(M->trigger[M->cur]);
                    int r = t->b;
                    if (t->x - r <= bx && bx <= t->x + r && t->y - r <= by && by <= t->y + r)
                        player_set_view_fixed(k->spr.x, k->spr.y, k->spr.z, cp);
                }
            } else {
                mission_get_object_pos_scratch(c->c);
                if (bx == M->scratch_x && by == M->scratch_y) player_set_view_fixed(k->spr.x, k->spr.y, k->spr.z, cp);
                mission_get_object_pos_scratch(c->a);
            }
        }
        ++*step();
        return;
    }
    if (s == 2) {
        int depth = k->length < -0x40 ? 0x80 : 0x40;
        if ((uint32_t)c->d > 3) return;
        int want, ex = 0, ey = 0;
        switch (c->d) {
        case 0:
            if (y - depth / 2 + 0x40 <= cy || by <= (y >> 6) - 3 || cx < x - 0x40 || x + 0x40 < cx) return;
            want = 2, ex = x + 0x20, ey = y + 0x4a;
            break;
        case 1:
            if (cx <= depth / 2 + x || (x >> 6) + 3 <= bx || cy < y - 0x40 || y + 0x40 < cy) return;
            want = 4, ex = x - 10, ey = y + 0x20;
            break;
        case 2:
            if (cy <= depth / 2 + y || (y >> 6) + 3 <= by || cx < x - 0x40 || x + 0x40 < cx) return;
            want = 1, ex = x + 0x20, ey = y - 10;
            break;
        default:
            if (x - depth / 2 + 0x40 <= cx || bx <= (x >> 6) - 3 || cy < y - 0x40 || y + 0x40 < cy) return;
            want = 8, ex = x + 0x4a, ey = y + 0x20;
            break;
        }
        if (car_get_cardinal_dir(k->id) == want) {   /* facing out: reverse in */
            k->speed = -5;
            if (c->d == 2) *car16(k, CAR_UNK188) = 0, *car16(k, CAR_UNK18A) = 0;
            k->unkc0 = 1;
        } else {
            if (k->speed < 5) k->speed = 5;
            k->unkc0 = 1;
            *car8(k, CAR_UNK13A) = 1;
        }
        if (c->d == 1) *car16(k, CAR_UNK188) = 0, *car16(k, CAR_UNK18A) = 0;
        *car16(k, CAR_UNKB6) = 0;
        k->input = 0;
        if (k->driver == M->cur_ped || p->state == 7) {
            M->park_exit_x = ex, M->park_exit_y = ey;
            M->park_exit_angle = exit_angle[c->d];
            M->scratch_x = ex, M->scratch_y = ey;
            player_exit_train(cp);
        } else if (c->d == 1) {
            return;   /* only this direction waits for the driver before the next step */
        }
        ++*step();
        *park = k->id;
        return;
    }
    if (s != 3) return;
    k = car_of(*park);
    cx = px(k->spr.x), cy = px(k->spr.y), bx = k->spr.x >> 22, by = k->spr.y >> 22;
    int dir = car_get_cardinal_dir(k->id);
    switch (c->d) {
    case 0:
        if (y + 0x40 < cy && (cx & ~0x3f) == (x & ~0x3f)) {
            if (dir != 1) k->speed = 10, *car8(k, CAR_UNK13A) = 1;
            else k->speed = -10;
        }
        M->scratch_y -= 10;
        M->scratch_x += 0x20;
        break;
    case 1:
        if (cx < x && (x >> 6) - 3 < bx && (y >> 6) - 1 <= by && by <= (y >> 6) + 1) {
            if (dir == 8) k->speed = -10;
            else k->speed = 10, *car8(k, CAR_UNK13A) = 1;
        }
        M->scratch_x += 0x4a;
        M->scratch_y += 0x20;
        break;
    case 2:
        if (cy < y && (cx & ~0x3f) == (x & ~0x3f)) {
            if (dir == 2) k->speed = -10;
            else k->speed = 10, *car8(k, CAR_UNK13A) = 1;
        }
        M->scratch_y += 0x4a;
        M->scratch_x += 0x20;
        break;
    case 3:
        if (x + 0x40 < cx && (cy & ~0x3f) == (y & ~0x3f)) {
            if (dir == 4) k->speed = -10;
            else k->speed = 10, *car8(k, CAR_UNK13A) = 1, *car16(k, CAR_UNKB6) = 0, k->input = 0;
        }
        M->scratch_x -= 10;
        M->scratch_y += 0x20;
        break;
    default: break;
    }
    if (p->state == 7) return;   /* the driver isn't out yet */
    door_close(obj_at(door_obj)->handle);
    k->speed = 0;
    *car16(k, CAR_UNKB6) = 1;
    k->input = 0;
    *car8(k, CAR_UNK13A) = 0;
    ++*step();
    k->script_line = -1;
    event_schedule(10, 0, k->id + 200);
    *park = -1;
    win();
    player_reset_view(cp);
    player_camera_start_transition(cp);
}

/* MissionOp_RedArrow 0x441320 (0x90 RED_ARROW): (local player) the red arrow points at the object
   of a (a block corner moves to its centre); a = -1 turns the normal arrow off. */
void mission_op_red_arrow(void)
{
    if (player_is_local(M->cur_player)) {
        if (cmd()->a == -1) {
            hud_arrow_off();   /* quirk: not the red one */
        } else {
            mission_get_object_pos_scratch(cmd()->a);
            if (M->scratch_x % 64 == 0 && M->scratch_y % 64 == 0) M->scratch_x += 0x20, M->scratch_y += 0x20;
            hud_red_arrow_to_pos((int16_t)M->scratch_x, (int16_t)M->scratch_y, (int16_t)M->scratch_z);
        }
    }
    win();
}

/* MissionOp_RedArrowOff 0x4414e0 (0x92 RED_ARROW_OFF) */
void mission_op_red_arrow_off(void)
{
    hud_red_arrow_off();
    win();
}

/* MissionOp_PedOn 0x441600 (0x1f PED_ON): creates the FUTUREPED of a (handle = its sub-type, param =
   the angle) at its position with the preset of the sub-type (e: the line of a target, d: the remap).
   Created -> b (no score) as a SPAWNED_PED, else -> c. */
void mission_op_ped_on(void)
{
    MissionCommand *c = cmd();
    mission_get_object_pos_scratch(c->a);
    M->scratch_a = mission_clear_block(M->scratch_x, M->scratch_y, M->scratch_z, -1, -1, -1) & 0xff;
    MissionObject *o = mission_obj(c->a);
    int kind = o->handle, x = M->scratch_x, y = M->scratch_y, z = M->scratch_z, ang = (int16_t)o->param;
    if (kind > 0x14 && kind < 0x2f) {
        M->scratch_b = c->e;
        if (M->scratch_b > -1) M->scratch_b = line_handle(M->scratch_b);
        /* 0x1b-0x28 create nothing: the handle keeps the sub-type number */
        if ((kind >= 0x15 && kind <= 0x1a) || kind >= 0x29)
            o->handle = (int16_t)mission_ped_create_typed(x, y, z, ang, M->scratch_b, kind);
    } else {
        switch (kind) {
        case 0: o->handle = (int16_t)mission_ped_create_0(x, y, z, ang); break;
        case 1: o->handle = (int16_t)mission_ped_create_1(x, y, z, ang, line_handle(c->e)); break;
        case 2: o->handle = (int16_t)mis_car_create_driver(x, y, z, (int16_t)line_handle(c->e)); break;
        case 3: game_fatal(-0xfa, 0xff, mission_find_line(M->cur_pc)); break;
        case 4: o->handle = (int16_t)mission_ped_create_4(x, y, z, ang); break;
        case 5: o->handle = (int16_t)mission_ped_create_5(x, y, z, ang, line_handle(c->e)); break;
        case 6: o->handle = (int16_t)mission_ped_create_6(x, y, z, ang); break;
        case 7: o->handle = (int16_t)mission_ped_create_7(x, y, z, ang, line_handle(c->e)); break;
        case 8: o->handle = (int16_t)mission_ped_create_8(x, y, z, ang, line_handle(c->e)); break;
        case 9: o->handle = (int16_t)mission_ped_create_9(x, y, z, ang); break;
        case 10: o->handle = (int16_t)mission_ped_create_10(x, y, z, ang); break;
        case 0xb: o->handle = (int16_t)mission_ped_create_11(x, y, z, ang, line_handle(c->e)); break;
        case 0xc: o->handle = (int16_t)mission_ped_create_12(x, y, z, ang, line_handle(c->e)); break;
        case 0xe: o->handle = (int16_t)mission_ped_create_guard(x, y, z, ang); break;
        default: break;   /* 0xd and others: nothing created, the handle stays */
        }
    }
    if (o->handle == -1) {
        mission_goto_fail();
        return;
    }
    mis_ped_set_remap(o->handle, (int16_t)c->d);
    if (!o->persistent) add_cleanup(M->cleanup_peds, &M->ncleanup_peds, o->handle);
    o->type = MISSION_TYPE_SPAWNED_PED;
    mission_goto_success();
}

/* MissionOp_CarOn 0x441cd0 (0x21 CAR_ON): creates the FUTURECAR of a (handle = model, param = angle,
   remap d) on its block with its driver slot ped sitting in it; models 0x29 / 3 get a visible driver,
   0x2f a bomb flag 3. The block is cleared around the new car. -> b, or c if no car. */
void mission_op_car_on(void)
{
    MissionCommand *c = cmd();
    int line = c->a;
    mission_get_object_pos_scratch(line);
    MissionObject *o = mission_obj(line);
    int h = (int16_t)mis_car_create(M->scratch_x >> 6, M->scratch_y >> 6, M->scratch_z >> 6, (int16_t)o->handle,
                                    (int16_t)o->param, (int16_t)c->d);
    if (h < 0) {
        mission_goto_fail();
        return;
    }
    mission_clear_block(M->scratch_x, M->scratch_y, M->scratch_z, h, o->handle, (int16_t)o->param);
    o->handle = h;
    Car *k = car_get(h);
    k->script_line = (int16_t)line;
    k->driver = (int16_t)(k->id + PED_DRIVER_FIRST);
    k->unk139 = 0;
    Ped *p = ped_get(k->id + PED_DRIVER_FIRST);
    p->health = 100;
    p->graphic = 0;
    mission_put_ped_in_car(p->id, k->id);
    if (!o->persistent) add_cleanup(M->cleanup_cars, &M->ncleanup_cars, k->id);
    if (k->model == 0x29 || k->model == 3) ped_create_car_driver(k);
    if (k->model == 0x2f) mis_car_set_flag9c(h, 3);
    o->type = MT_CAR;
    win();
}

/* MissionOp_PedSendTo 0x441fd0 (0x26 PED_SENDTO): the ped of d walks (state 4, AI 8 / 2) to the
   object of a, turned towards it. */
void mission_op_ped_send_to(void)
{
    mission_get_object_pos_scratch(cmd()->a);
    Ped *p = ped_of(line_handle(cmd()->d));
    p->objective = 0x19;
    p->state = 4;
    p->u78 = 8;
    p->u7c = 2;
    p->spr.angle = (int16_t)math_atan2(M->scratch_y * 0x10000 - p->spr.y, M->scratch_x * 0x10000 - p->spr.x);
    p->target_x = M->scratch_x << 16;
    p->target_y = M->scratch_y << 16;
    win();
}

/* MissionOp_DoRepo 0x4421a0 (0x2a DO_REPO): waits until the player drives the car of a; after d
   frames without that the car's driver comes back (a ped at the object of the car object's
   parameter line, sent to the car; step 2), and once that ped has an anim below 0x1e the car stops
   (control 0). */
void mission_op_do_repo(void)
{
    MissionCommand *c = cmd();
    MissionObject *o = mission_obj(c->a);
    if (car_has_live_driver(M->cur_ped, o->handle)) {
        win();
        return;
    }
    if (c->d > 0 && --c->d == 0) {
        int l2 = o->param;
        mission_get_object_pos_scratch(l2);
        int d = (int16_t)mis_car_create_driver(M->scratch_x, M->scratch_y, M->scratch_z, (int16_t)o->handle);
        *step() = 2;
        mission_obj(l2)->handle = d;
        return;
    }
    if (*step() == 2) {
        Ped *p = ped_of(line_handle(o->param));
        if (p->anim < 0x1e) {
            Car *k = car_of(p->car);
            k->control = 0;
            k->unkc0 = 0;
        }
    }
}

/* the free script object START_MODEL keeps the player's car in: the last one whose type and
   coordinates are all -1 (searched from the end) */
static int free_script_object(void)
{
    for (int i = MISSION_OBJECTS - 1; i >= 0; i--) {
        const MissionObject *o = &M->objects[i];
        if (o->x == -1 && o->y == -1 && o->z == -1 && o->type == -1) return i;
    }
    return -1;
}

/* MissionOp_StartModel 0x4423f0 (0x2f START_MODEL): e > 0: the player, in a car, swaps it for a
   model car (model 0x2f, at the car's place, z of the object of a, angle = its size field): the real
   car is kept (held, owner 99) in a free script object (type 100: x = controlled kind, y = car,
   z = wanted points) whose index goes to the parameter of a's object; the player drives the model;
   e counts down -> b. e = 0 -> c. */
void mission_op_start_model(void)
{
    MissionCommand *c = cmd();
    int oi = line_index(c->a);
    MissionObject *o = obj_at(oi);
    mission_get_object_pos_scratch(c->a);
    int cp = M->cur_player;
    if (c->e == 0) {
        mission_goto_fail();
        return;
    }
    o->param = free_script_object();
    Ped *p = ped_of(player_get_ped(cp));
    if (p->state == 9 || p->state != 7) return;
    if (p->car != (int16_t)player_get_controlled_id(cp) || p->ufc == 1) return;
    MissionObject *keep = obj_at(o->param);
    keep->type = 100;
    keep->y = (int16_t)player_get_controlled_id(cp);
    keep->x = player_get_controlled_kind(cp);
    Car *k = car_of(p->car);
    M->scratch_a = (int16_t)k->road_dirs;
    k->control = 0;
    k->unk139 = 0;
    M->scratch_b = k->spr.angle;
    mis_car_set_held(k->id);
    mis_car_set_flag128_99(k->id);
    keep->z = (int16_t)player_get_wanted_points_idx(cp);
    int h = (int16_t)mis_car_create_at(k->spr.x, k->spr.y, M->scratch_z >> 6, 0x2f, M->cur_size >> 6, 0);
    o->handle = h;
    if (h == -1) {
        mission_goto_fail();
        return;
    }
    o->x = px(k->spr.x);
    o->y = px(k->spr.y);
    Car *m = car_get(h);
    m->road_dirs = (uint16_t)M->scratch_a;
    m->spr.angle = (int16_t)M->scratch_b;
    car_reset_from_info(h);
    mis_car_set_flag9c(h, 3);
    m->unk139 = 0;
    player_set_controlled(cp, PLAYER_IN_CAR, h);
    player_set_view_target(cp, 0, h);
    mis_car_give_player(h, cp & 0xff);
    mission_set_player_slot(cp & 0xff, obj_at(o->param)->y);
    c->e--;
    mission_goto_success();
}

/* MissionOp_DoModel 0x4427c0 (0x30 DO_MODEL): runs the model car of the START_MODEL at label a.
   The player dead (or state 9) gets the real car back and the process ends. While the target (the
   object of d; d < 0 none) lives: with the model wrecked, after 100 frames the next model car (the
   START_MODEL's e counts them) at the real car's block, or -> c when none is left; without a player
   slot the model is deleted -> c. Target dead: after 100 frames -> b. */
void mission_op_do_model(void)
{
    MissionCommand *c = cmd();
    int spc = label_index_pc(c->a);
    MissionCommand *sm = mission_cmd(spc);
    Ped *pp = ped_of(M->cur_ped);
    int cp = M->cur_player;
    if (pp->health == 0 || pp->state == 9) {
        MissionObject *o = mission_obj(sm->a);
        M->scratch_a = obj_at(o->param)->y;
        player_set_controlled(cp, PLAYER_IN_CAR, M->scratch_a);
        player_retarget_camera(0, (int16_t)o->handle, 0, (int16_t)M->scratch_a);
        mission_kill_process(M->cur);   /* (cur_pc is left as it is) */
        return;
    }
    if (c->d < 0 || (int16_t)mission_get_object_health((int16_t)c->d) != 0) {
        MissionObject *o = mission_obj(sm->a);
        int h = o->handle;
        Car *k = car_of(h & 0xffff);
        if (mission_has_player_slot(cp & 0xff)) {
            if (k->damage < 100) return;
            if ((*step())++ < 100) return;
            *step() = 0;
            if (sm->e > 0) {
                sm->e--;
                mission_get_object_pos_scratch(sm->a);
                M->scratch_a = obj_at(o->param)->y;
                Car *real = car_of(M->scratch_a);
                int n = (int16_t)mis_car_create(real->spr.x >> 22, real->spr.y >> 22, M->scratch_z >> 6, 0x2f,
                                                (int16_t)M->cur_size, 0);
                o->handle = n;
                car_reset_from_info(n);
                mis_car_set_flag9c(n & 0xffff, 3);
                car_of(n)->unk139 = 0;
                real = car_of(obj_at(o->param)->y);
                M->scratch_a = (int16_t)real->road_dirs;
                M->scratch_b = real->spr.angle;
                Car *m = car_of(n);
                m->spr.angle = (int16_t)M->scratch_b;
                m->road_dirs = (uint16_t)M->scratch_a;
                player_set_controlled(cp, PLAYER_IN_CAR, n);
                player_set_view_target(cp, 0, n);
                mis_car_give_player(n & 0xffff, cp & 0xff);
                return;
            }
            mission_goto_fail();
            return;
        }
        if (k->status != -1 && pp->health != 0) car_delete(h);
        mis_car_clear_held(k->id);
        mission_goto_fail();
        return;
    }
    if ((*step())++ < 100) return;
    mission_goto_success();
}

/* MissionOp_ReturnControl 0x442ca0 (0x31 RETURN_CONTROL): ends the model of the START_MODEL at label
   a: in a car the player gets the real car back (camera, driver, the saved wanted points, slot
   cleared, camera snapped, the model removed if unused); else only the model's hold is released. */
void mission_op_return_control(void)
{
    MissionCommand *sm = mission_cmd(label_index_pc(cmd()->a));
    Ped *p = ped_of(M->cur_ped);
    int cp = M->cur_player;
    MissionObject *o = mission_obj(sm->a);
    if (p->state == 7) {
        Car *k = car_of(o->handle);
        k->control = 0;
        k->unkc0 = 1;
        *car16(k, CAR_UNKB6) = 1;
        M->scratch_a = obj_at(o->param)->y;
        if (M->scratch_a != -1) {
            player_set_controlled(cp, PLAYER_IN_CAR, M->scratch_a);
            player_retarget_camera(0, (int16_t)o->handle, 0, (int16_t)M->scratch_a);
            mis_car_give_player((int16_t)M->scratch_a, cp & 0xff);
            player_add_wanted_points(player_get_ped(cp), obj_at(o->param)->z);
            mission_clear_player_slot(cp & 0xff);
            mis_car_clear_held(M->scratch_a);
            snap_camera(cp);
            car_remove_if_unused((int16_t)o->handle);
            p->state = 7;
        }
    } else {
        mis_car_clear_held(o->handle);
    }
    win();
}

/* MissionOp_GotoDropoff 0x442fc0 (0x39 GOTO_DROPOFF): every 8th frame (wait 7) d counts down: at 0
   -> c; the player's ped near the object of a -> b. */
void mission_op_goto_dropoff(void)
{
    if (*step() != 0) *step() = 0;
    mission_get_object_pos_scratch(cmd()->a);
    ped_check_in_car(M->cur_ped, -1);
    if (M->cur_wait != 0) {
        M->cur_wait--;
        return;
    }
    M->cur_wait = 7;
    if (--cmd()->d == 0) mission_goto_fail();
    else if (ped_is_near_point16(M->cur_ped, (int16_t)M->scratch_x, (int16_t)M->scratch_y)) win();
}

/* MissionOp_ModelHunt 0x4431d0 (0x3a MODEL_HUNT): d parked cars on the lines a, a + 1, ... hunt the
   player's ped; this process then stays in step 5 (removing wrecked hunters from the hunt) and a new
   process (owner = the player) continues at the next command (b = 0) or here (b > 0). */
void mission_op_model_hunt(void)
{
    MissionCommand *c = cmd();
    int n = c->d;   /* 0x655e50 */
    if (*step() == 5) {
        int removed = 0;
        for (int i = 0; i < n; i++) {
            /* quirk: indexed by the count removed so far, not by i */
            int h = line_handle(c->a + removed);
            if (h >= 0 && car_is_wrecked(h & 0xffff)) {
                hunt_remove((int16_t)h);
                removed++;
            }
        }
        return;
    }
    for (int i = 0; i < n; i++) {
        M->scratch_a = c->a + i;
        mission_get_object_pos_scratch(M->scratch_a);
        MissionObject *o = mission_obj(M->scratch_a);
        int h = (int16_t)car_spawn_parked(M->scratch_x >> 6, M->scratch_y >> 6, M->scratch_z >> 6);
        o->handle = h;
        if (h >= 0) {
            mis_car_set_flag9c(h, 3);
            Car *k = car_get(h);
            k->script_line = (int16_t)o->handle;   /* quirk: the car's own id, not the line */
            o->type = MT_CAR;
            add_cleanup(M->cleanup_cars, &M->ncleanup_cars, k->id);
        }
        car_unk_004318e0_wrap((int16_t)o->handle, M->cur_ped, 3);
    }
    M->scratch_a = c->b;
    int t;
    if (c->b == 0) t = M->cur_pc + 1;
    else if (c->b == -1) {
        *step() = 5;
        return;
    } else t = M->cur_pc;   /* (with b > 0 the new process starts on this command again) */
    M->scratch_a = t;
    int np = (int16_t)mission_start_process(M->cur_player, -1, mission_find_line(t));
    M->scratch_b = np;
    if (np >= 0) M->pc[np] = (int16_t)t;   /* (none free: the original writes pc[-1]) */
    *step() = 5;
}

/* MissionOp_ModelFuture 0x443440 (0x3b MODEL_FUTURE): a parked model car at the object of a. */
void mission_op_model_future(void)
{
    MissionCommand *c = cmd();
    mission_get_object_pos_scratch(c->a);
    int line = c->a;
    MissionObject *o = mission_obj(line);
    M->scratch_a = mission_clear_block(M->scratch_x, M->scratch_y, M->scratch_z, -1, -1, -1) & 0xff;
    /* quirk: pixel coordinates where Car_SpawnParked takes blocks */
    o->handle = (int16_t)car_spawn_parked(M->scratch_x, M->scratch_y, M->scratch_z);
    /* quirk: the test reads the script object whose index is the line number */
    if (obj_at(line)->handle < 0) {
        mission_goto_fail();
        return;
    }
    mis_car_set_flag9c(o->handle & 0xffff, 3);
    car_of(o->handle)->script_line = (int16_t)line;
    win();
}

/* MissionOp_ChangePedType 0x443690 (0x45 CHANGE_PED_TYPE): a live, free ped of a gets the AI preset
   d (target: the object of e). -> b either way (no score). */
void mission_op_change_ped_type(void)
{
    MissionCommand *c = cmd();
    MissionObject *o = mission_obj(c->a);
    Ped *p = ped_of(o->handle);
    if (p->health == 0 || p->state == 0xc || p->state == 0x17 || p->carried >= 0) {
        mission_goto_success();
        return;
    }
    int k = c->d, h = o->handle;
    if (k < 0x15 || k > 0x2e) {
        switch (k) {
        case 0: mission_ped_set_obj_wander(h); break;
        case 1:
            M->scratch_a = c->e;
            M->scratch_b = h;
            if (M->scratch_a > -1) M->scratch_a = line_handle(M->scratch_a);
            mission_ped_set_obj_18(M->scratch_b, (int16_t)M->scratch_a);
            break;
        case 2: M->scratch_a = h; mission_ped_send_to(h, line_handle(c->e)); break;
        case 3: game_fatal(-0xfa, 0x159, mission_find_line(M->cur_pc)); break;
        case 4: mission_ped_set_obj_wander2(h); break;
        case 5: M->scratch_a = h; mission_ped_set_obj_attack(h, (int16_t)line_handle(c->e)); break;
        case 6: case 9: mission_ped_set_obj_24(h); break;
        case 7: M->scratch_a = h; mission_ped_set_obj_follow(h, (int16_t)line_handle(c->e)); break;
        case 8: M->scratch_a = h; mission_ped_set_obj_1c(h, (int16_t)line_handle(c->e)); break;
        case 10: mission_ped_set_obj_23(h); break;
        case 0xb: M->scratch_a = h; mission_ped_set_obj_31(h, (int16_t)line_handle(c->e)); break;
        case 0xc: M->scratch_a = h; mission_ped_set_obj_30(h, (int16_t)line_handle(c->e)); break;
        case 0xd:
            M->scratch_a = c->e;
            M->scratch_b = h;
            if (M->scratch_a > -1) M->scratch_a = line_handle(M->scratch_a);
            mission_ped_set_obj_39(M->scratch_b, (int16_t)M->scratch_a);
            break;
        default: break;
        }
    } else {
        M->scratch_a = h;
        mission_ped_set_obj_typed(h, (int16_t)line_handle(c->e), k);
    }
    mission_goto_success();
}

/* MissionOp_PedOutOfCar 0x443a90 (0x4b PED_OUT_OF_CAR): the driver of the car of a (or the SPAWNED_PED
   a, from its car) leaves the car; step 1 finds the driver (kept in the object's parameter) and asks
   it out (Car_GetDriverInfo: 1 -> step 2, -1 -> step 3), step 2 waits until it is out (not AI 0x13,
   not in a car state), step 3 -> b. A car without a driver: its driver slot ped (id 0: -> c). */
void mission_op_ped_out_of_car(void)
{
    MissionObject *o = mission_obj(cmd()->a);
    int s = *step();
    if (o->type == MT_CAR) {
        if (s == 1) {
            if (o->handle == -1) return;
            Car *k = car_of(o->handle);
            if (k->driver == -1) {
                Ped *d = ped_of(k->id + PED_DRIVER_FIRST);
                if (d->id != 0) {   /* quirk: tests the slot's id field */
                    o->param = d->id;
                    *step() = 3;
                    return;
                }
                mission_goto_fail();
                return;
            }
            o->param = k->driver;
            M->scratch_a = (int8_t)car_get_driver_info(k->id);
            if (M->scratch_a == 1) *step() = 2;
            else if (M->scratch_a == -1) *step() = 3;
            return;
        }
        if (s == 2) {
            if (o->param != 0) {
                const Ped *d = ped_of(o->param);
                if (d->u7c != 0x13 && !ped_in_car_state(d)) *step() = 3;
            }
            return;
        }
        if (s != 3) {
            game_fatal(-0x4a, 0x1e9, s);
            return;
        }
        win();
        return;
    }
    if (o->type != MISSION_TYPE_SPAWNED_PED) return;
    if (s == 1) {
        if (o->handle == -1) return;
        const Ped *p = ped_of(o->handle);
        if (p->car == -1) {
            *step() = 3;
            return;
        }
        o->param = p->car;
        M->scratch_a = (int8_t)car_get_driver_info(p->car);
        if (M->scratch_a == 1) *step() = 2;
        else if (M->scratch_a == -1) *step() = 3;
    } else if (s == 2) {
        if (o->param != -1) {
            const Ped *p = ped_of(o->handle);
            if (p->u7c != 0x13 && !ped_in_car_state(p)) *step() = 3;
        }
    } else if (s == 3) {
        win();
    }
}

/* MissionOp_GetDriverInfo 0x443f20 (0x4e GET_DRIVER_INFO): the object of d becomes the driver of the
   car of a (SPAWNED_PED, cleanup unless persistent; the ped's +0x8b = 1). No driver: the driver slot
   ped if it sits in that car alive (else -> c). */
void mission_op_get_driver_info(void)
{
    MissionCommand *c = cmd();
    Car *k = car_of(line_handle(c->a));
    MissionObject *od = mission_obj(c->d);
    int id;
    if (k->driver == -1) {
        Ped *p = ped_of(k->id + PED_DRIVER_FIRST);
        if (p->car != k->id || p->state == 5 || p->health < 1) {
            mission_goto_fail();
            return;
        }
        id = p->id;
        p->u8b = 1;
    } else {
        ped_of(k->driver)->u8b = 1;
        id = k->driver;
    }
    od->handle = id;
    od->type = MISSION_TYPE_SPAWNED_PED;
    if (!od->persistent) add_cleanup(M->cleanup_peds, &M->ncleanup_peds, id);
    win();
}

/* MissionOp_FrenzyBrief 0x444180 (0x87 FRENZY_BRIEF): the kill frenzy brief e (Mission_Brief5) for
   the player; c < 0 is a countdown of -c (scratch, unused here). -> b, no score. */
void mission_op_frenzy_brief(void)
{
    MissionCommand *c = cmd();
    mission_get_object_pos_scratch(c->a);
    M->scratch_a = c->c;
    if (M->scratch_a < 0) M->scratch_b = -M->scratch_a, M->scratch_a = 0;
    else M->scratch_b = -1;
    mission_brief5((int16_t)c->e, M->cur_player & 0xff);
    mission_goto_success();
}

/* MissionOp_AddALife 0x4442b0 (0x88 ADD_A_LIFE) */
void mission_op_add_a_life(void)
{
    player_add_life(M->cur_player & 0xff);
    win();
}

/* MissionOp_KFBriefTimed 0x4443d0 (0x89 KF_BRIEF_TIMED): the player's timer +0x1ae = d seconds
   (x 25 frames), voice 3. -> b, no score. */
void mission_op_kf_brief_timed(void)
{
    player_set_timer1ae((int16_t)M->cur_player, (int16_t)((int16_t)cmd()->d * 25));
    Snd_PlayVoice(3);
    mission_goto_success();
}

/* MissionOp_KFCancelBriefing 0x4444b0 (0x8a KF_CANCEL_BRIEFING): timer +0x1ae off. */
void mission_op_kf_cancel_briefing(void)
{
    player_set_timer1ae((int16_t)M->cur_player, -1);
    win();
}

/* MissionOp_KillPed 0x4445d0 (0x4f KILL_PED): removes the SPAWNED_PED of a (unless dead, state 0xc:
   -> c), cancels its ambulance and takes it off the ped cleanup list. -> b. */
void mission_op_kill_ped(void)
{
    MissionObject *o = mission_obj(cmd()->a);
    if (o->type != MISSION_TYPE_SPAWNED_PED || o->handle < 0 || ped_of(o->handle)->state == 0xc) {
        mission_goto_fail();
        return;
    }
    ped_kill_if_possible((int16_t)o->handle);
    ambulance_cancel_for_ped((int16_t)o->handle);
    int n = M->ncleanup_peds;
    for (int i = 0; i < n; i++) {
        if (M->cleanup_peds[i] != o->handle) continue;
        /* compacted over the entry: the original moves n - i entries (one past the end too) */
        for (int j = i; j < n; j++) M->cleanup_peds[j] = j + 1 < MISSION_CLEANUP ? M->cleanup_peds[j + 1] : -1;
        M->ncleanup_peds--;
        if (n < MISSION_CLEANUP) M->cleanup_peds[n] = -1;
        break;
    }
    win();
}

/* MissionOp_IsPowerupDone 0x4448a0 (0x85 IS_POWERUP_DONE): the power-up at the object of a has been
   taken -> b; still there -> c (at once, or when the d-frame timer runs out); type 0xf: voice 0xc on
   the fail. */
void mission_op_is_powerup_done(void)
{
    MissionCommand *c = cmd();
    MissionObject *o = mission_obj(c->a);
    timeout_begin();
    mission_get_object_pos_scratch(c->a);
    if (!powerup_exists_at(M->scratch_x << 16, M->scratch_y << 16)) {
        win();
        return;
    }
    if (c->d >= 1) {
        int16_t *s = step();
        if (*s < 6 || --*s != 5) return;
    }
    if (o->handle == 0xf) Snd_PlayVoice(0xc);
    mission_goto_fail();
}

/* MissionOp_KFBriefGeneral 0x444b60 (0x8b KF_BRIEF_GENERAL): timer +0x1b0 = d x 25. -> b, no score. */
void mission_op_kf_brief_general(void)
{
    player_set_timer1b0((int16_t)M->cur_player, (int16_t)((int16_t)cmd()->d * 25));
    mission_goto_success();
}

/* MissionOp_KFCancelGeneral 0x444c40 (0x8c KF_CANCEL_GENERAL) */
void mission_op_kf_cancel_general(void)
{
    player_set_timer1b0((int16_t)M->cur_player, -1);
    win();
}

/* MissionOp_ResetKF 0x444d60 (0x8e RESET_KF): releases the cars of the kill frenzy list and stops
   every other process of kind 2 (from process 3 on). */
void mission_op_reset_kf(void)
{
    for (int i = 0; i < M->nkf; i++) {
        MissionObject *o = obj_at(M->kf_list[i]);
        if (M->kf_list[i] < 0 || o->type != MT_CAR || o->handle < 0) continue;
        Car *k = car_of(o->handle);
        if (k->script_line >= 0) mis_car_clear_held(k->id);
    }
    for (int i = 3; i < MISSION_PROCESSES; i++)
        if (i != M->cur && M->active[i] && M->kind[i] == 2) mission_kill_process(i);
    win();
}

/* MissionOp_WaitForPlayers 0x444f30 (0x8f WAIT_FOR_PLAYERS): no player's race progress exceeds the
   handle of a -> b, else -> c (no score). */
void mission_op_wait_for_players(void)
{
    if (mission_all_players_below(line_handle(cmd()->a))) win();
    else mission_goto_fail();
}

/* MissionOp_IsATrainWrecked 0x4450f0 (0x93 IS_A_TRAIN_WRECKED) */
void mission_op_is_a_train_wrecked(void)
{
    if (train_any_wrecked()) win();
    else mission_goto_fail();
}

/* MissionOp_IncHeads 0x445290 (0x94 INC_HEADS): Player_SetStatByCity(player, a). */
void mission_op_inc_heads(void)
{
    player_set_stat_by_city(M->cur_player, cmd()->a);
    win();
}

/* MissionOp_IsPedStunned 0x4453c0 (0x95 IS_PED_STUNNED): the ped of a in anim 0x2b -> b, else c. */
void mission_op_is_ped_stunned(void)
{
    if (ped_of(line_handle(cmd()->a))->anim == 0x2b) win();
    else mission_goto_fail();
}
