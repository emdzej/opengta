/* Mission_Load 0x445800 and the creators it calls for the object lines (Mission_Spawn* 0x43d2c0-
   0x43e1b0). The MisCar_* helpers, Mission_ClearBlock and Mission_ObjCreate they use are in
   mission_obj.c; entity creators of modules not ported yet are stubs (stubs.h). */
#include "car.h"
#include "coll.h"
#include "game.h"
#include "gmath.h"
#include "dummy.h"
#include "mission.h"
#include "mission_obj.h"
#include "obj.h"
#include "ped.h"
#include "player.h"
#include "stubs.h"
#include "trigger.h"
#include <string.h>

/* object types (0x4b0d98) */
enum {
    T_CAR, T_PED, T_OBJECT, T_PLAYER, T_DRIVER, T_PARKED, T_MODEL, T_TELEPHONE, T_TRIGGER, T_DOOR,
    T_TARGET, T_FUTURE, T_COUNTER, T_CRANE, T_CLOCK, T_DUMMY, T_SPRAY, T_BARRIER, T_ESCORT, T_CARBOMB,
    T_BOMBSHOP, T_ESCORTED, T_STOPPED, T_INIT, T_FUTUREPED, T_CARTRIGGER, T_FUTUREDROP, T_HELLS,
    T_DRIVER_PARK, T_FUTURECAR, T_ONETRIGGER, T_MODEL_BARRIER, T_MOVING_TRIG, T_SPECIFIC_BARR, T_MPHONES,
    T_MODEL_DOOR, T_GTA_DEMAND, T_MY_MODEL, T_BOMBSHOP_COST, T_PHONE_TOGG, T_SPECIFIC_DOOR, T_TARGET_SCORE,
    T_DUM_MISSION_TRIG, T_CORRECT_MOD_TRIG, T_CORRECT_CAR_TRIG, T_CLOCK_START, T_CLOCK_STOP,
    T_MIDPOINT_MULTI, T_MID_MULTI_SETUP, T_FINAL_MULTI, T_POWERUP, T_BLOCK_INFO, T_SPECIFIC_DOOR_BOMB,
    T_MOVING_TRIG_HIRED, T_SETUP_SPEED, T_CARBOMB_TRIG, T_DAMAGE_TRIG, T_GUN_TRIG, T_GUN_SCREEN_TRIG,
    T_CARDESTROY_TRIG, T_CARWAIT_TRIG, T_PEDCAR_TRIG, T_CANNON_START, T_DUM_PED_BLOCK_TRIG,
    T_PARKED_PIXELS, T_CHOPPER_ENDPOINT, T_MISSION_COUNTER, T_SECRET_MISSION_COUNTER, T_MISSION_TOTAL,
    T_CARSTUCK_TRIG, T_BASIC_BARRIER, T_ALT_DAMAGE_TRIG,
};
enum { OP_MISSION_END = 0x2d };

#define M (&g_mission)

/* The script object of a line (line_obj). Lines the section doesn't define map to -1, where the
   original reads the record before the table; the port reads a record of -1s instead. */
static MissionObject *line_object(int line)
{
    static MissionObject none;
    int i = line >= 0 && line < MISSION_LINES ? M->line_obj[line] : -1;
    if (i < 0) {
        memset(&none, 0xff, sizeof none);
        return &none;
    }
    return &M->objects[i];
}

/* Car_Get of a handle; a failed creation (-1) would index before the table in the original. */
static Car *handle_car(int h)
{
    static Car none;
    if (h < 0 || h >= CAR_MAX) {
        memset(&none, 0xff, sizeof none);
        return &none;
    }
    return car_get(h);
}

static void cleanup_car(MissionObject *o, const Car *c)
{
    if (!o->persistent && M->ncleanup_cars < MISSION_CLEANUP) M->cleanup_cars[M->ncleanup_cars++] = c->id;
}

/* The MisCar helpers, Mission_ClearBlock and Mission_ObjCreate are in mission_obj.c. */

/* ---- Mission_Spawn* 0x43d2c0-0x43e1b0 (one per object type that needs more than a call) ---- */

/* Mission_SpawnCar 0x43d2c0: CAR: a car with its driver slot ped sitting in it (state 7). */
static void spawn_car(int slot, int line)
{
    MissionObject *o = &M->objects[slot];
    M->scratch_a = mission_clear_block(M->scratch_x, M->scratch_y, M->scratch_z, -1, M->scratch_p1, M->scratch_p2) & 0xff;
    o->handle = (int16_t)mis_car_create(M->scratch_x, M->scratch_y, M->scratch_z, M->scratch_p1 & 0xffff, M->scratch_p2, 0);
    mis_car_make_killer(o->handle);
    if (o->handle >= 0) {
        Car *c = car_get(o->handle);
        c->script_line = (int16_t)line;
        c->unk139 = 0;
        Ped *p = ped_get(c->driver);   /* MisCar_MakeKiller made it car + 200 */
        p->graphic = 0;
        p->state = 7;
        p->objective = 0x19;
        p->car = c->id;
        p->weapon = 0;
        p->health = 100;
        cleanup_car(o, c);
    }
}

/* Mission_SpawnPed 0x43d460: PED of sub-type p1. Most sub-types read the remap and an object line
   (whose handle is passed to the creator) from the line; the created ped's object gets type 99. */
static void spawn_ped(int slot, int line)
{
    MissionObject *o = &M->objects[slot];
    int x = M->scratch_x, y = M->scratch_y, z = M->scratch_z, p1 = M->scratch_p1, p2 = M->scratch_p2;
    M->scratch_a = mission_clear_block(x, y, z, -1, -1, -1) & 0xff;
    if ((p1 > 0x14 && p1 < 0x2f) || p1 == 0xd) {
        M->scratch_a = ini_read_int();
        M->scratch_b = ini_read_int();
        if (M->scratch_b > -1) M->scratch_b = line_object(M->scratch_b)->handle;
        if (p1 == 0xd || (p1 >= 0x15 && p1 <= 0x1a) || (p1 >= 0x29 && p1 <= 0x2e))
            o->handle = (int16_t)mission_ped_create(p1, x, y, z, p2 & 0xffff, M->scratch_b);
        ped_set_appearance(o->handle, 0, M->scratch_a & 0xffff);
    } else {
        switch (p1) {
        case 0: case 4: case 6: case 9: case 10: case 0xe:   /* creator, then the remap (0xe: creator 0) */
            o->handle = (int16_t)mission_ped_create(p1 == 0xe ? 0 : p1, x, y, z, p2, -1);
            ped_set_appearance(o->handle, 0, ini_read_int());
            break;
        case 1: case 5: case 7:                               /* remap, line, creator */
            M->scratch_a = ini_read_int();
            M->scratch_b = ini_read_int();
            if (M->scratch_b > -1) M->scratch_b = line_object(M->scratch_b)->handle;
            o->handle = (int16_t)mission_ped_create(p1, x, y, z, p2, M->scratch_b);
            ped_set_appearance(o->handle, 0, M->scratch_a & 0xffff);
            break;
        case 2:   /* a driver for the car of a line; the creator's result is dropped: the handle is p2 (0x43d692) */
            M->scratch_a = ini_read_int();
            M->scratch_b = ini_read_int();
            mis_car_create_driver(x, y, z, (int16_t)line_object(M->scratch_b)->handle);
            o->handle = M->scratch_p2;
            ped_set_appearance(o->handle, 0, M->scratch_a & 0xffff);
            break;
        case 3:
            game_fatal(-0xfa, 0xc4, line);
        case 8: case 0xb:                                     /* line, creator, then the remap */
            M->scratch_a = ini_read_int();
            M->scratch_b = ini_read_int();
            if (M->scratch_b > -1) M->scratch_b = line_object(M->scratch_b)->handle;
            o->handle = (int16_t)mission_ped_create(p1, x, y, z, p2, M->scratch_b);
            ped_set_appearance(o->handle, 0, ini_read_int());
            break;
        case 0xc:                                             /* line required */
            M->scratch_a = ini_read_int();
            M->scratch_b = ini_read_int();
            o->handle = (int16_t)mission_ped_create(p1, x, y, z, p2, line_object(M->scratch_b)->handle);
            ped_set_appearance(o->handle, 0, M->scratch_a & 0xffff);
            break;
        default:
            game_fatal(-0x118, 0xc4, p1);
        }
    }
    o->x = M->scratch_x;
    o->type = MISSION_TYPE_SPAWNED_PED;
    o->y = M->scratch_y;
    o->z = M->scratch_z;
    if (!o->persistent && M->ncleanup_peds < MISSION_CLEANUP) M->cleanup_peds[M->ncleanup_peds++] = o->handle;
}

/* Mission_SpawnTelephone 0x43d3c0: object type 0x28 at the block's centre (pixels written back to
   the script object); p1 > 0 is kept as the parameter, else -1. */
static void spawn_telephone(int slot)
{
    MissionObject *o = &M->objects[slot];
    int x = M->scratch_x * 0x40 + 0x20, y = M->scratch_y * 0x40 + 0x20, z = M->scratch_z << 6;
    o->x = x, o->y = y, o->z = z;
    int h = (int16_t)mission_obj_create(x, y, z, 0x28, (int16_t)M->scratch_p2);
    o->handle = h;
    o->param = M->scratch_p1 < 1 ? -1 : M->scratch_p1;
    if (!o->persistent && M->ncleanup_objs < MISSION_CLEANUP) M->cleanup_objs[M->ncleanup_objs++] = h;
}

/* Mission_SpawnCarBomb 0x43db30: CARBOMB: a car, then its bomb kind read from the line. */
static void spawn_car_bomb(int slot, int line)
{
    MissionObject *o = &M->objects[slot];
    M->scratch_a = mission_clear_block(M->scratch_x, M->scratch_y, M->scratch_z, -1, M->scratch_p1, M->scratch_p2) & 0xff;
    o->handle = (int16_t)mis_car_create(M->scratch_x, M->scratch_y, M->scratch_z, M->scratch_p1 & 0xffff, M->scratch_p2, 0);
    mis_car_set_flag9c((int16_t)o->handle, ini_read_int_checked(line));
    if (o->handle >= 0) {
        Car *c = car_get(o->handle);
        c->script_line = (int16_t)line;
        c->unk139 = 0;
        cleanup_car(o, c);
    }
}

/* Mission_SpawnHells 0x43dc00: HELLS: a gang car with a driver (remap 0x21), object type 0x1b. */
static void spawn_hells(int slot, int line)
{
    MissionObject *o = &M->objects[slot];
    M->scratch_a = mission_clear_block(M->scratch_x, M->scratch_y, M->scratch_z, -1, M->scratch_p1, M->scratch_p2) & 0xff;
    int c = (int16_t)mis_car_create(M->scratch_x, M->scratch_y, M->scratch_z, M->scratch_p1 & 0xffff, M->scratch_p2, 0);
    o->handle = c;
    if (c >= 0) {
        Car *car = car_get(c);
        car->script_line = (int16_t)line;
        ped_create_car_driver(car);
        o->type = T_HELLS;
        gang_add_car(car->id);
        ped_set_appearance(car->driver, 0, 0x21);
        car->unk139 = 0;
        cleanup_car(o, car);
    }
}

/* Mission_SpawnDriverPark 0x43dce0: DRIVER_PARK: a parked car whose driver slot is bound; models 0x29
   and 3 get a driver created. */
static void spawn_driver_park(int slot, int line)
{
    MissionObject *o = &M->objects[slot];
    M->scratch_a = mission_clear_block(M->scratch_x, M->scratch_y, M->scratch_z, -1, M->scratch_p1, M->scratch_p2) & 0xff;
    int c = (int16_t)mis_car_create(M->scratch_x, M->scratch_y, M->scratch_z, M->scratch_p1 & 0xffff, M->scratch_p2, 0);
    o->handle = c;
    if (c >= 0) {
        Car *car = car_get(c);
        car->unk139 = 0;
        car->driver = (int16_t)(car->id + PED_DRIVER_FIRST);
        car->script_line = (int16_t)line;
        cleanup_car(o, car);
        if (car->model == 0x29 || car->model == 3) ped_create_car_driver(car);
    }
}

/* Mission_SpawnMovingTrig 0x43ddc0: MOVING_TRIG: a trigger (kind 0xb) on the car of line p1. */
static void spawn_moving_trig(int slot, int line)
{
    MissionObject *o = &M->objects[slot];
    MissionObject *ref = line_object(M->scratch_p1);
    o->param = M->scratch_p1;
    M->scratch_a = ini_read_int_checked(line);
    if (ref->type == T_FUTURECAR) {
        o->handle = trigger_create(M->scratch_x, M->scratch_y, M->scratch_z, 0xb, -1, M->scratch_p2, M->scratch_a, o->persistent);
    } else {
        Car *c = handle_car((int16_t)ref->handle);
        o->handle = trigger_create(M->scratch_x, M->scratch_y, M->scratch_z, 0xb, c->id, M->scratch_p2, M->scratch_a, o->persistent);
        if (M->scratch_p2 == 0) car_trig_add(o->handle, c->id);   /* a radius of 0: fires when a player gets in */
    }
}

/* Mission_SpawnSpecificBarrier 0x43dea0 / Mission_SpawnSpecificDoor 0x43df70 /
   Mission_SpawnSpecificDoorBomb 0x43e040: a door opened by the car of a line; they differ in the
   Door_Create arguments and the opening mode. */
static void spawn_specific(int slot, int line, int kind)
{
    MissionObject *o = &M->objects[slot];
    M->scratch_a = ini_read_int_checked(line);
    if (kind == 0)
        o->handle = door_create(M->scratch_x, M->scratch_y, M->scratch_z, M->scratch_p1, M->scratch_a, M->scratch_p2, 0, 1, o->persistent);
    else
        o->handle = door_create(M->scratch_x, M->scratch_y, M->scratch_z, M->scratch_p1, 8, M->scratch_p2, M->scratch_a, 0, o->persistent);
    M->scratch_a = ini_read_int_checked(line);
    M->scratch_b = ini_read_int_checked(line);
    int car = line_object(M->scratch_a)->type == T_FUTURECAR ? -1 : M->scratch_a;
    if (kind == 0) door_set_open_by_car(o->handle, 2, car, M->scratch_b);
    else if (kind == 1) door_set_open_by_car(o->handle, 3, car, M->scratch_b);
    else door_set_open_mode5(o->handle, 2, car);
    o->param = M->scratch_a;
    door_lock(o->handle);
}

/* Mission_SpawnPedCarTrig 0x43e100: PEDCAR_TRIG: a trigger (kind 0x1e) on a created ped (line p2) and
   a car (line read here); the object keeps p1, p2 and that line in x, y, z. */
static void spawn_ped_car_trig(int slot)
{
    MissionObject *o = &M->objects[slot];
    int l = ini_read_int();
    M->scratch_a = l;
    o->y = M->scratch_p2;
    o->z = l;
    MissionObject *ped = line_object(M->scratch_p2), *car = line_object(l);
    o->x = M->scratch_p1;
    int a = -1, c = -1;
    if (ped->type == MISSION_TYPE_SPAWNED_PED && car->type == T_CAR) {
        M->scratch_a = car->handle;
        M->scratch_p2 = ped->handle;
        a = M->scratch_a;
        c = M->scratch_p2;
    }
    o->handle = trigger_create(0, 0, 0, 0x1e, a, M->scratch_p1, c, o->persistent);
}

/* Mission_SpawnParkedPixels 0x43e1b0: PARKED_PIXELS: a car at pixel coordinates (z in pixels / 64). */
static void spawn_parked_pixels(int slot, int line)
{
    MissionObject *o = &M->objects[slot];
    M->scratch_a = mission_clear_block(M->scratch_x, M->scratch_y, M->scratch_z, -1, M->scratch_p1, M->scratch_p2) & 0xff;
    int c = (int16_t)mis_car_create_at(M->scratch_x << 16, M->scratch_y << 16, M->scratch_z >> 6, M->scratch_p1 & 0xffff,
                                       M->scratch_p2, 0);
    o->handle = c;
    if (c >= 0) {
        Car *car = car_get(c);
        car->script_line = (int16_t)line;
        o->type = T_CAR;
        car->unk139 = 0;
        cleanup_car(o, car);
    }
}

/* ---- Mission_Load 0x445800 ---- */

/* the first part: what the loader resets before reading */
static void reset_tables(void)
{
    Mission *m = M;
    for (int i = 0; i < MISSION_PROCESSES; i++) m->last_pc[i] = -1, m->kind[i] = 0;
    for (int i = 0; i < 4; i++) m->park_car[i] = -1;   /* 0x5fdfe8.. */
    memset(m->respawn, 0, sizeof m->respawn);   /* two dwords: 0x5fdffc, 0x5fe000 */
    memset(m->labels, 0xff, sizeof m->labels);
    memset(m->line_obj, 0xff, sizeof m->line_obj);
    m->u67661c = 0;
    m->mission_end_count = 0;
    m->counter_obj = m->secret_counter_obj = -1;
    m->secret_target = m->counter_target = m->mission_total = -1;
    m->ncleanup_objs = m->ncleanup_peds = m->ncleanup_cars = 0;
    memset(m->phone_flag, 0, sizeof m->phone_flag);
    for (int i = 0; i < MISSION_OBJECTS; i++) {   /* type, handle, param, x, y, z; the flag stays */
        MissionObject *o = &m->objects[i];
        o->x = o->y = o->z = -1;
        o->type = o->handle = o->param = -1;
    }
    for (int i = 0; i < MISSION_COMMANDS; i++) m->commands[i].op = MISSION_OPCODE_NONE;   /* the rest stays */
    for (int i = 0; i < MISSION_CLEANUP; i++) m->cleanup_cars[i] = m->cleanup_objs[i] = m->cleanup_peds[i] = -1;
    for (int i = 0; i < MISSION_PROCESSES; i++) m->trigger[i] = -1;
}

static void check_line(int line)
{
    if (line >= MISSION_LINES) game_fatal(-0xab, 0x130, line);   /* the original indexes past its maps */
}

/* One object line after its line number: [digit] (x, y, z) TYPE p1 p2 [more]. */
static void load_object(int slot, int line)
{
    Mission *m = M;
    if (slot >= MISSION_OBJECTS) game_fatal(-0xab, 0x130, line);   /* the original has no check */
    check_line(line);
    MissionObject *o = &m->objects[slot];
    bool kf = false;
    int d = ini_read_opt_digit();
    if (d < 0) o->persistent = 0;
    else {
        o->persistent = 1;
        if (d == 2) kf = true;
    }
    ini_read_coords(&m->scratch_x, &m->scratch_y, &m->scratch_z);
    o->x = m->scratch_x;
    m->line_obj[line] = (int16_t)slot;
    o->y = m->scratch_y;
    o->z = m->scratch_z;
    o->handle = -1;
    char word[64];
    ini_read_word(word, sizeof word);
    o->type = mission_lookup_type(word);
    m->scratch_p1 = ini_read_int_checked(line);
    m->scratch_p2 = ini_read_int_checked(line);
    int x = m->scratch_x, y = m->scratch_y, z = m->scratch_z, p1 = m->scratch_p1, p2 = m->scratch_p2;
    int persist = o->persistent, h;
    MissionObject *r;

    switch (o->type) {
    case T_CAR: spawn_car(slot, line); break;
    case T_PED: spawn_ped(slot, line); break;
    case T_OBJECT:
        m->scratch_a = mission_clear_block(x, y, z, -1, -1, -1) & 0xff;
        h = (int16_t)mission_obj_create(x, y, z, p1 & 0xffff, (int16_t)p2);
        o->handle = h;
        if (!persist && m->ncleanup_objs < MISSION_CLEANUP) m->cleanup_objs[m->ncleanup_objs++] = h;
        break;
    case T_PLAYER:   /* binds the next player; p1 is the line of the car whose driver slot it takes */
        if (m->cur != -1) {
            m->scratch_a = mission_clear_block(x, y, z, -1, -1, -1) & 0xff;
            Car *c = handle_car((int16_t)line_object(p1)->handle);
            int n = m->cur;
            h = (int16_t)mis_car_create_player_ped(n, x, y, z, c->id, p2, player_get_remap(n));
            o->handle = h;
            m->player_ped[n] = (int16_t)h;
            m->cur = player_next(n);
        }
        break;
    case T_DRIVER:   /* the next player drives the car of line p2 */
        if (m->cur != -1) {
            m->scratch_a = mission_clear_block(x, y, z, -1, -1, -1) & 0xff;
            r = line_object(p2);
            int n = m->cur;
            mis_car_put_player_in((int16_t)r->handle, n);
            m->player_ped[n] = handle_car((int16_t)r->handle)->driver;   /* Car_GetDriver 0x475fd0 */
            ped_set_appearance(m->player_ped[n], 0, player_get_remap(n) & 0xff);
            player_set_controlled(n, PLAYER_IN_CAR, r->handle);
            m->cur = player_next(n);
        }
        break;
    case T_PARKED:
        m->scratch_a = mission_clear_block(x, y, z, -1, p1, p2) & 0xff;
        h = (int16_t)mis_car_create(x, y, z, p1 & 0xffff, p2, 0);
        o->handle = h;
        if (h >= 0) {
            Car *c = car_get(h);
            c->script_line = (int16_t)line;
            o->type = T_CAR;
            c->unk139 = 0;
            cleanup_car(o, c);
        }
        break;
    case T_MODEL:
        m->scratch_a = mission_clear_block(x, y, z, -1, p1, p2) & 0xff;
        o->handle = (int16_t)car_spawn_parked(x, y, z);
        mis_car_set_flag9c((int16_t)o->handle, 3);
        if (o->handle >= 0) {
            Car *c = car_get(o->handle);
            c->script_line = (int16_t)line;
            cleanup_car(o, c);
        }
        break;
    case T_TELEPHONE: spawn_telephone(slot); break;
    case T_TRIGGER: o->handle = trigger_create(x, y, z, 4, p1, p2, 0, persist); break;
    case T_DOOR: {   /* Door_Create(x, y, z, Mission_MapDoorType(p1 & 0xff), 8, p2, value, 0, flag) */
        int v = ini_read_int_checked(line);
        o->handle = door_create(x, y, z, mission_map_door_type(p1 & 0xff), 8, p2, v, 0, persist);
        if (o->handle == -1) game_fatal(-1, 1, 1);
        door_set_open_any(o->handle, 2);
        door_lock(o->handle);
        break;
    }
    case T_TARGET: case T_FUTURE: case T_COUNTER: case T_FUTUREPED: case T_BLOCK_INFO:
        o->param = p2;
        o->handle = p1;
        break;
    case T_CRANE: o->handle = crane_create(x, y, z, p1); break;
    case T_CLOCK: case T_INIT: case T_MY_MODEL: case T_SETUP_SPEED: break;
    case T_DUMMY: o->handle = p2; break;
    case T_SPRAY: o->handle = trigger_create(x, y, z, 3, p1, p2, 0, persist); break;
    case T_BARRIER:
        m->scratch_a = ini_read_int_checked(line);
        o->handle = door_create(x, y, z, p1, p2, m->scratch_a, 0, 1, persist);
        door_set_open_any(o->handle, 1);
        door_lock(o->handle);
        break;
    case T_ESCORT:
        m->scratch_a = mission_clear_block(x, y, z, -1, p1, p2) & 0xff;
        h = mis_car_create_type1(x, y, z, (int16_t)p1, p2 & 0xffff, 0);
        o->handle = (int16_t)h;
        mis_car_set_drive_mode1(h);
        if (o->handle >= 0) {
            Car *c = car_get(o->handle);
            c->script_line = (int16_t)line;
            cleanup_car(o, c);
        }
        break;
    case T_CARBOMB: spawn_car_bomb(slot, line); break;
    case T_BOMBSHOP: o->handle = trigger_create(x, y, z, 8, p1, p2, 0, persist); break;
    case T_ESCORTED: case T_STOPPED:
        m->scratch_a = mission_clear_block(x, y, z, -1, p1, p2) & 0xff;
        h = (int16_t)mis_car_create_type1(x, y, z, (int16_t)p1, p2, 0);
        o->handle = h;
        if (h >= 0) {
            Car *c = car_get(h);
            c->script_line = (int16_t)line;
            cleanup_car(o, c);
            if (o->type == T_ESCORTED) mis_car_set_drive_mode2((int16_t)o->handle);
        }
        break;
    case T_CARTRIGGER: o->handle = trigger_create(x, y, z, 9, p1, p2, 0, persist); break;
    case T_FUTUREDROP: o->handle = p1; break;
    case T_HELLS: spawn_hells(slot, line); break;
    case T_DRIVER_PARK: spawn_driver_park(slot, line); break;
    case T_FUTURECAR: o->param = p2, o->handle = p1; break;
    case T_ONETRIGGER: {
        int a = m->scratch_a = ini_read_int_checked(line);
        int v = line_object(a)->type == T_FUTURECAR ? -1 : a;
        o->handle = trigger_create(x, y, z, 10, p1, p2, v, persist);
        o->param = a;
        break;
    }
    case T_MODEL_BARRIER:
        m->scratch_a = ini_read_int_checked(line);
        o->handle = door_create(x, y, z, p1, m->scratch_a, p2, 0, 1, persist);
        door_set_open_mode3(o->handle, 1, ini_read_int_checked(line));
        door_lock(o->handle);
        break;
    case T_MOVING_TRIG: spawn_moving_trig(slot, line); break;
    case T_SPECIFIC_BARR: spawn_specific(slot, line, 0); break;
    case T_MPHONES: o->handle = trigger_create(x, y, z, 0xc, p1, p2, 0, persist); break;
    case T_MODEL_DOOR:
        m->scratch_a = ini_read_int_checked(line);
        m->scratch_b = ini_read_int_checked(line);
        o->handle = door_create(x, y, z, p1, m->scratch_a, p2, m->scratch_b, 0, persist);
        m->scratch_a = ini_read_int_checked(line);
        door_set_open_mode3(o->handle, 2, m->scratch_a);
        door_lock(o->handle);
        break;
    case T_GTA_DEMAND:   /* two more values, read into the coordinate scratch */
        m->scratch_x = ini_read_int_checked(line);
        m->scratch_y = ini_read_int_checked(line);
        o->handle = car_list_add(p1, p2, m->scratch_x, m->scratch_y);
        break;
    case T_BOMBSHOP_COST: m->bombshop_cost = (int16_t)p1; break;
    case T_PHONE_TOGG:
        m->scratch_a = line_object(p1)->handle;
        o->handle = trigger_create(x, y, z, 0xd, m->scratch_a, p2, 0, persist);
        break;
    case T_SPECIFIC_DOOR: spawn_specific(slot, line, 1); break;
    case T_TARGET_SCORE: m->target_score = p1; break;
    case T_DUM_MISSION_TRIG: {
        int a = m->scratch_a = ini_read_int_checked(line);
        r = line_object(a);
        o->param = a;
        o->handle = trigger_create(x, y, z, 0xe, p1, p2, r->type == T_FUTURECAR ? -1 : r->handle, persist);
        break;
    }
    case T_CORRECT_MOD_TRIG:
        m->scratch_a = ini_read_int_checked(line);
        o->handle = trigger_create(x, y, z, 0xf, p1, p2, m->scratch_a, persist);
        break;
    case T_CORRECT_CAR_TRIG:
        m->scratch_a = ini_read_int_checked(line);
        o->param = m->scratch_a;
        o->handle = trigger_create(x, y, z, 0x10, p1, p2, -1, persist);
        break;
    case T_CLOCK_START: o->handle = trigger_create(x, y, z, 0x11, p1, p2, 0, persist); break;
    case T_CLOCK_STOP:   /* the CLOCK_START of line p1 gets this object as its parameter */
        r = line_object(p1);
        r->param = slot;
        o->handle = trigger_create(x, y, z, 0x12, r->handle, p2, 0, persist);
        trigger_set_param14(r->handle, o->handle);
        break;
    case T_MIDPOINT_MULTI:
        o->param = p1;
        o->handle = trigger_create(x, y, z, 0x13, -1, p2, 0, persist);
        mission_set_target_order(o->handle & 0xff);
        break;
    case T_MID_MULTI_SETUP: {   /* links the chain of MIDPOINT_MULTI triggers starting at line p1 */
        int a = p1;
        MissionObject *nx;
        do {
            r = line_object(a);
            a = r->param;
            nx = line_object(a);
            trigger_set_param14(r->handle, nx->handle);
        } while (nx->type == T_MIDPOINT_MULTI);
        m->scratch_a = a;
        break;
    }
    case T_FINAL_MULTI:
        o->handle = trigger_create(x, y, z, 0x14, -1, p1, 0, persist);
        mission_set_target_order(o->handle & 0xff);
        break;
    case T_POWERUP:   /* block -> pixels (the block's centre), then like FUTURECAR */
        o->x = o->x * 0x40 + 0x20;
        o->y = o->y * 0x40 + 0x20;
        o->z = o->z << 6;
        o->param = p2, o->handle = p1;
        break;
    case T_SPECIFIC_DOOR_BOMB: spawn_specific(slot, line, 2); break;
    case T_MOVING_TRIG_HIRED:
        o->param = p1;
        r = line_object(p1);
        if (r->type == T_FUTURECAR) o->handle = trigger_create(x, y, z, 0x16, -1, p2, 0, persist);
        else o->handle = trigger_create(x, y, z, 0x16, handle_car((int16_t)r->handle)->id, p2, 0, persist);
        break;
    case T_CARBOMB_TRIG:
        o->param = p1;
        r = line_object(p1);
        if (r->type == T_FUTURECAR) o->handle = trigger_create(0, 0, 0, 0x18, p2, 0, -1, persist);
        else o->handle = trigger_create(0, 0, 0, 0x18, p2, 0, handle_car((int16_t)r->handle)->id, persist);
        break;
    case T_DAMAGE_TRIG: o->handle = trigger_create(x, y, z, 0x19, p1, p2, 0, persist); break;
    case T_GUN_TRIG: o->handle = trigger_create(x, y, z, 0x1a, p1, p2, 0, persist); break;
    case T_GUN_SCREEN_TRIG:
        r = line_object(p2);
        if (r->type == MISSION_TYPE_SPAWNED_PED) o->handle = trigger_create(x, y, z, 0x1b, p1, 0, r->handle, persist);
        else {
            o->handle = trigger_create(x, y, z, 0x1b, p1, 0, -1, persist);
            o->param = p2;
        }
        break;
    case T_CARDESTROY_TRIG: {
        int a = m->scratch_a = ini_read_int_checked(line);
        o->param = a;
        r = line_object(a);
        if (r->type == T_FUTURECAR) o->handle = trigger_create(x, y, z, 0x1c, p1, p2, -1, persist);
        else o->handle = trigger_create(x, y, z, 0x1c, p1, p2, handle_car((int16_t)r->handle)->id, persist);
        break;
    }
    case T_CARWAIT_TRIG:
        r = line_object(p2);
        o->param = p2;
        if (r->type == T_FUTURECAR) o->handle = trigger_create(0, 0, 0, 0x1d, p1, 0, -1, persist);
        else o->handle = trigger_create(0, 0, 0, 0x1d, p1, 0, handle_car((int16_t)r->handle)->id, persist);
        break;
    case T_PEDCAR_TRIG: spawn_ped_car_trig(slot); break;
    case T_CANNON_START: o->handle = trigger_create(x, y, z, 0x1f, p1, p2, 0, persist); break;
    case T_DUM_PED_BLOCK_TRIG: {   /* reads its line with the unchecked reader */
        int a = m->scratch_a = ini_read_int();
        r = line_object(a);
        int v = -1;
        if (r->type != MISSION_TYPE_SPAWNED_PED) {
            o->param = a;
            v = r->handle;
        }
        o->handle = trigger_create(x, y, z, 0x15, p1, p2, v, persist);
        break;
    }
    case T_PARKED_PIXELS: spawn_parked_pixels(slot, line); break;
    case T_CHOPPER_ENDPOINT: heli_set_exit_target(x << 16, y << 16); break;
    case T_MISSION_COUNTER:
        m->counter_obj = slot;
        o->handle = p1, o->param = p1;
        m->counter_target = p1;
        break;
    case T_SECRET_MISSION_COUNTER:
        m->secret_counter_obj = slot;
        o->handle = p1, o->param = p1;
        m->secret_target = p1;
        break;
    case T_MISSION_TOTAL:
        o->handle = p1, o->param = p1;
        m->mission_total = p1;
        break;
    case T_CARSTUCK_TRIG:
        r = line_object(p2);
        o->param = p2;
        if (r->type != T_FUTURECAR) o->handle = trigger_create(0, 0, 0, 0x20, p1, 0, handle_car((int16_t)r->handle)->id, persist);
        else o->handle = trigger_create(0, 0, 0, 0x20, p1, 0, -1, persist);
        break;
    case T_BASIC_BARRIER:
        m->scratch_a = ini_read_int_checked(line);
        o->handle = door_create(x, y, z, p1, p2, m->scratch_a, 0, 0, persist);
        door_set_open_any(o->handle, 1);
        door_lock(o->handle);
        break;
    case T_ALT_DAMAGE_TRIG: o->handle = trigger_create(x, y, z, 0x21, p1, p2, 0, persist); break;
    default: game_fatal(-0xab, 0x130, line);   /* unknown object type */
    }
    if (kf && m->nkf < MISSION_KF_MAX) m->kf_list[m->nkf++] = slot;
}

/* The command lines: label OPCODE a b c d e, until a negative label. */
static void load_commands(void)
{
    Mission *m = M;
    int n = 0;
    for (int label = ini_read_int(); label > -1; label = ini_read_int()) {
        check_line(label);
        if (n >= MISSION_COMMANDS) game_fatal(-0xab, 0x130, label);   /* the original has no check */
        m->labels[label] = (int16_t)n;
        char word[64];
        ini_read_word(word, sizeof word);
        MissionCommand *c = &m->commands[n];
        c->op = (uint16_t)mission_lookup_opcode(word);
        c->a = ini_read_int();
        c->b = ini_read_int();
        c->c = ini_read_int();
        c->d = ini_read_int();
        c->e = ini_read_int();
        if (c->op == OP_MISSION_END) m->mission_end_count++;
        n++;
    }
    m->ncommands = n;
}

/* Mission_Load 0x445800: parses the section text Mission_ReadIni kept. Header: six values (cars to
   spawn, police on, -, 0x505efa, the emergency services switch, -). Object lines until a negative
   number, then command lines until a negative label. Then the player processes start at command 0
   and the level's traffic and police are created. */
void mission_load(void)
{
    Mission *m = M;
    mission_init_city_tables();
    for (int i = 0; i < 6; i++) {
        int v = ini_read_int();
        m->header[i] = v;
        if (i == 0) m->traffic_cars = v;
        if (i == 1) m->police_on = v;
        if (i == 3) mission_set_var505efa(v);
        if (i == 4) g_game.opt.emergency = (int16_t)v;   /* Mission_SetVar5031cc 0x478810 */
    }
    m->cur = (int16_t)player_first();
    dummy_init_groups();
    m->bombshop_cost = 5000;
    m->u6762c0 = 5;
    mission_reset_lists();
    reset_tables();

    int slot = -1;
    for (int line = ini_read_int(); line >= 0; line = ini_read_int()) load_object(++slot, line);
    m->nobjects = slot + 1;
    load_commands();

    for (int n = player_first(); n > -1; n = player_next(n)) {
        m->pc[n] = 0;
        m->wait[n] = 0;
        m->step[n] = 1;
        m->linked[n] = 0;
        m->active[n] = 1;
        m->owner[n] = -1;
    }
    m->ended = 0;
    for (int i = 4; i < MISSION_PROCESSES; i++) m->active[i] = 0;
    if (m->traffic_cars) mis_car_spawn_batch(m->traffic_cars);
    if (m->police_on == 1) police_init_for_mission();
}
