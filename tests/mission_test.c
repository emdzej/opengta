/* The mission script interpreter against the real MISSION.INI: every section starts (Game_Run's start)
   and runs N frames of Mission_Update (with the delayed events ticking; everything else stubbed)
   without an unknown opcode; mission 1 logs the processes' program counters and the commands executed
   for the first frames (twice: deterministic), then the player's ped is put on the trigger blocks of
   the first phone / triggers to see the processes they start.
     ./build/mission_test [frames]     (data root: ./game or OPENGTA_DATA) */
#include "exe.h"
#include "game/car.h"
#include "game/event.h"
#include "game/game.h"
#include "game/gmath.h"
#include "game/mission.h"
#include "game/mission_run.h"
#include "game/ped.h"
#include "game/player.h"
#include "game/stubs.h"
#include "game/trigger.h"
#include "render/camera.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: " __VA_ARGS__); printf("\n"); failures++; } } while (0)

uint8_t *plat_load_user_file(const char *name, size_t *size) { (void)name, (void)size; return NULL; }
bool plat_save_user_file(const char *name, const void *data, size_t size) { (void)name, (void)data, (void)size; return true; }
void plat_log(const char *msg) { printf("log: %s\n", msg); }
static void on_present(void *ctx) { (void)ctx; }

static bool start(int section)
{
    GameOptions o;
    game_default_options(&o);
    game_set_options(&o);
    g_game.present = on_present;
    map_clear_name();
    map_set_name(NULL, 2);
    if (!mission_set_ini_section(section)) return false;
    return game_run_begin();
}

/* ---- the trace ---- */

static bool tracing;
static int executed, op_hist[MISSION_OPCODES];
static uint32_t trace_hash;
static void trace(int proc, int pc, int op)
{
    executed++;
    if (op >= 0 && op < MISSION_OPCODES) op_hist[op]++;
    trace_hash = (trace_hash ^ (uint32_t)(proc << 24 | pc << 8 | op)) * 16777619u;
    if (tracing) printf("    p%-2d pc %4d label %5d %-24s\n", proc, pc, mission_find_line(pc), mission_opcode_name(op));
}

static void frame(void)
{
    event_tick();
    mission_update();
}

static int active_count(void)
{
    int n = 0;
    for (int i = 0; i < MISSION_PROCESSES; i++) n += g_mission.active[i] != 0;
    return n;
}

static void print_procs(const char *what)
{
    printf("  %s:", what);
    for (int i = 0; i < MISSION_PROCESSES; i++)
        if (g_mission.active[i]) printf(" p%d@%d(label %d, step %d)", i, g_mission.pc[i], mission_find_line(g_mission.pc[i]), g_mission.step[i]);
    printf("\n");
}

/* N frames of mission 1, logged; returns the trace hash */
static uint32_t run_logged(int frames, bool log)
{
    trace_hash = 2166136261u;
    executed = 0;
    if (!start(1)) {
        CHECK(0, "mission 1 start");
        return 0;
    }
    tracing = log;
    for (int f = 0; f < frames; f++) {
        if (log && f < 8) printf("   frame %d\n", f);
        tracing = log && f < 8;
        frame();
    }
    tracing = false;
    if (log) print_procs("after the frames");
    return trace_hash;
}

/* put the player's ped on block (bx, by, bz) */
static void place_player(int bx, int by, int bz)
{
    int ped = g_mission.player_ped[0];
    Ped *p = ped_get(ped);
    p->spr.x = bx * 0x400000 + 0x200000;
    p->spr.y = by * 0x400000 + 0x200000;
    p->spr.z = bz * 0x400000;
    player_set_controlled(0, 2, ped);
}

/* the triggers of mission 1: put the player on each TELEPHONE / TRIGGER / MPHONES block in turn and
   run frames: the processes they start */
static void drive_triggers(void)
{
    if (!start(1)) return;
    for (int f = 0; f < 10; f++) frame();
    print_procs("mission 1 at frame 10");
    int tried = 0, started = 0;
    for (int i = 0; i < g_mission.nobjects && tried < 6; i++) {
        const MissionObject *o = &g_mission.objects[i];
        if (o->type != MT_TRIGGER && o->type != MT_TELEPHONE && o->type != MT_MPHONES) continue;
        int bx = o->x, by = o->y, bz = o->z;
        if (o->type == MT_TELEPHONE) bx >>= 6, by >>= 6, bz >>= 6;   /* the phone's coordinates are pixels */
        int before = active_count();
        place_player(bx, by, bz);
        executed = 0;
        for (int f = 0; f < 30; f++) frame();
        int after = active_count();
        printf("  %s (object %d, handle %d) at block (%d, %d, %d): %d -> %d processes, %d commands\n",
               mission_type_name(o->type), i, o->handle, bx, by, bz, before, after, executed);
        print_procs("   now");
        tried++;
        started += after > before;
        place_player(1, 1, 4);   /* away again */
        for (int f = 0; f < 5; f++) frame();
    }
    CHECK(tried > 0, "no triggers in mission 1");
    printf("  %d of %d triggers started processes\n", started, tried);
}

int main(int argc, char **argv)
{
    char err[256];
    int frames = argc > 1 ? atoi(argv[1]) : 300;
    if (!vfs_mount_default()) { printf("SKIP: no data root\n"); return 0; }
    if (!exe_init(err, sizeof err)) { printf("SKIP: %s\n", err); return 0; }
    math_init_tables();
    if (!camera_init_tables()) { printf("FAIL: camera tables\n"); return 1; }
    game_set_screen(640, 480);
    mission_trace = trace;

    /* mission 1, logged, twice */
    printf("mission 1: the first frames\n");
    uint32_t h1 = run_logged(frames, true);
    int ex1 = executed;
    game_run_end();
    uint32_t h2 = run_logged(frames, false);
    game_run_end();
    printf("  %d frames: %d commands executed, trace %08x / %08x\n", frames, ex1, h1, h2);
    CHECK(h1 == h2 && ex1 == executed, "mission 1 not deterministic");
    CHECK(ex1 > 0, "mission 1 executed nothing");

    printf("mission 1: triggers\n");
    drive_triggers();
    game_run_end();

    /* every section: N frames without an unknown opcode (that would be fatal) */
    static const int secs[] = { 1, 2, 1001, 1002, 1004, 102, 103, 1101, 1102, 1103, 1104, 202, 203, 1201, 1202, 1203, 1204 };
    printf("every section, %d frames:\n", frames);
    memset(op_hist, 0, sizeof op_hist);
    for (size_t i = 0; i < sizeof secs / sizeof *secs; i++) {
        if (!start(secs[i])) {
            printf("  [%d] no such section\n", secs[i]);
            continue;
        }
        executed = 0;
        for (int f = 0; f < frames; f++) frame();
        printf("  [%3d] %-26s %6d commands, %2d processes active\n", secs[i], g_mission.name, executed, active_count());
        game_run_end();
    }
    printf("opcodes executed:");
    for (int i = 0; i < MISSION_OPCODES; i++)
        if (op_hist[i]) printf(" %s %d", mission_opcode_name(i), op_hist[i]);
    printf("\n");
    printf(failures ? "FAILED (%d)\n" : "ok\n", failures);
    return failures != 0;
}
