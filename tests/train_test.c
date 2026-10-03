/* Trains against the real data: the rail tracer on the three cities (tracks, stations, train starts),
   then mission 1 (Liberty City) run through the in-game loop as the app does: the three elevated
   trains follow their railways (every bogie on a railway block, carriages coupled), stop at the
   stations with the doors opening and closing, leave for the next free station; the player boards a
   train standing at a station (the end of Ped_TryEnterCar's train part, done directly), rides it,
   takes it over (the fire key: Train_Command 1) and drives it with the door keys (3 / 4), gets off;
   a crash wrecks a train (wreck cars spawned). Two runs of the same frames must agree. Renders to
   out/train/.
     ./build/train_test            (data root: ./game or OPENGTA_DATA) */
#include "audio/audio.h"
#include "exe.h"
#include "game/car.h"
#include "game/coll.h"
#include "game/game.h"
#include "game/gmath.h"
#include "game/input.h"
#include "game/lights.h"
#include "game/mapq.h"
#include "game/mission.h"
#include "game/ped.h"
#include "game/ped_internal.h"
#include "game/player.h"
#include "game/rail.h"
#include "game/stubs.h"
#include "game/train.h"
#include "platform.h"
#include "png.h"
#include "render/camera.h"
#include "render/poly.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum { W = 640, H = 480 };
static uint32_t fb[W * H];
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
    poly_set_screen_rows(fb, W * 4, H);
    poly_set_clip(0, 0, W - 1, H - 1);
    return game_run_begin();
}

static uint8_t held[KEY_COUNT];

static void game_frame_step(void)
{
    for (int calls = 0; calls < 16; calls++) {
        input_feed_held(held);
        audio_render(NULL, 315);
        if (game_run_step() != GAME_STEP_WAIT) return;
    }
    CHECK(0, "game_run_step never ran a frame");
}

/* The camera on a fixed point that the test moves (kind 4): the player module has no train target
   yet (Ref_GetKind1PosRect 0x45fb60 is not ported), so the shots follow a train this way. */
static void view_at(int32_t x, int32_t y, int32_t z, bool snap)
{
    Player *pl = &g_players[0];
    pl->view_kind = CAM_TARGET_POINT4;   /* (kind 3 snaps back when the ped leaves the view) */
    pl->view_x = x, pl->view_y = y, pl->view_z = z;
    if (snap) {
        CameraPlayer cp;
        CameraWorld w = player_camera_world(0);
        player_camera(0, &cp);
        camera_snap(&cp, &w);
        player_camera_store(0, &cp);
    }
}
static void view_train(int t, bool snap)
{
    train_command(7, t);
    const TrainBoardInfo *b = train_get_board_info();
    view_at(b->x, b->y, b->z, snap);
}

static void shot(const char *name, int t)
{
    char path[128];
    snprintf(path, sizeof path, "out/train/%s.png", name);
    CHECK(png_write(path, fb, W, H, PNG_XRGB), "write %s", path);
    const Train *T = &g_trains[t];
    const TrainCar *k = &T->car[T->front_car & 3];
    printf("  %s: train %d at (%d, %d, %d) heading %#x state %d sub %d speed %d doors %d boarded %d\n", path, t,
           k->spr.x >> 16, k->spr.y >> 16, k->spr.z >> 16, k->spr.angle, T->state, T->sub, T->speed, T->door_frame, T->boarded);
}

/* ---- the tracer on each city ---- */

static void check_rails(int section, const char *city, int want_trains)
{
    CHECK(start(section), "mission %d start", section);
    printf("%s (mission %d): %d railway blocks traced in %d tracks, %d stations, %d crossings, %d switches, %d train starts\n",
           city, section, g_rail.nvisited, g_rail.ntracks, rail_station_count(), rail_crossing_count(), g_rail.nswitches,
           rail_get_info()->count);
    for (int i = 0; i < rail_station_count(); i++) {
        const uint8_t *s = rail_station(i);
        printf("  station %d at (%d, %d, %d) track %d dir %d occupied %d\n", i, s[RAIL_ST_X], s[RAIL_ST_Y], s[RAIL_ST_Z],
               s[RAIL_ST_TRACK_ID], s[RAIL_ST_DIR], s[RAIL_ST_FLAG]);
    }
    const RailInfo *in = rail_get_info();
    int made = 0;
    for (int t = 0; t < in->count; t++) {
        printf("  start %d: block (%d, %d, %d) dir %d station %d -> kind %d%s\n", t, in->x[t], in->y[t], in->z[t], in->dir[t],
               in->station[t], g_trains[t].kind, g_trains[t].kind == TRAIN_KIND_SINGLE ? " (single unit: not created)" : "");
        if (g_trains[t].kind == TRAIN_KIND_FOUR) made++;
    }
    CHECK(made == want_trains, "%s: %d trains made, expected %d", city, made, want_trains);
    /* every visited block is a railway block, every station a railway block with ext 6 / 7 */
    int bad = 0;
    for (int i = 0; i < g_rail.nvisited; i++)
        if (!map_test_block_attr(1, g_rail.visited[i][0], g_rail.visited[i][1], g_rail.visited[i][2])) bad++;
    for (int i = 0; i < rail_station_count(); i++) {
        const uint8_t *s = rail_station(i);
        int e = map_test_block_attr(6, s[0], s[1], s[2]);
        if (e != 6 && e != 7) bad++;
    }
    CHECK(bad == 0, "%s: %d traced blocks / stations are not what they should be", city, bad);
    game_run_end();
}

/* ---- running ---- */

/* every bogie of every running carriage on a railway block, and the bogies of a carriage 0x7e - 0x28
   pixels apart (give or take the curves) */
static int check_on_rails(void)
{
    int bad = 0;
    for (int t = 0; t < g_train_count; t++) {
        const Train *T = &g_trains[t];
        if (T->kind != TRAIN_KIND_FOUR) continue;
        for (int c = 0; c < 4; c++) {
            const TrainCar *k = &T->car[c];
            if (k->state != TRAIN_CAR_RUNNING) continue;
            for (int b = 0; b < 2; b++) {
                const TrainBogie *g = &k->bogie[b];
                if (!map_test_block_attr(1, g->bx, g->by, g->bz)) bad++;
                if ((g->spr.x >> 22) != g->bx || (g->spr.y >> 22) != g->by) bad++;
            }
            int dx = (k->bogie[1].spr.x - k->bogie[0].spr.x) >> 16, dy = (k->bogie[1].spr.y - k->bogie[0].spr.y) >> 16;
            int d2 = dx * dx + dy * dy;
            if (d2 < 60 * 60 || d2 > 90 * 90) bad++;
        }
    }
    return bad;
}

typedef struct {
    int stations;               /* arrivals (the station sequence entered from braking) */
    int doors_open;             /* frames with the doors fully open */
    int min_frame;
    int max_speed;
    int last_state;
    int to_unload, unloaded;    /* passengers picked at the stops / those that got off */
    int last_sub, last_pass, last_toggle;
    int moved;                  /* pixels the front carriage moved */
    int32_t lx, ly;
} TrainLog;

static uint32_t hash_trains(uint32_t h)
{
    for (int t = 0; t < g_train_count; t++) {
        const Train *T = &g_trains[t];
        const uint8_t *p = (const uint8_t *)T;
        /* (+9, the state after braking, is kept from the last level, as in the original's bss, until set) */
        for (size_t i = 0; i < 0x24; i++)
            if (i != 9) h = (h ^ p[i]) * 16777619u;
        for (int c = 0; c < 4; c++) {
            const TrainCar *k = &T->car[c];
            int32_t v[7] = { k->spr.x, k->spr.y, k->spr.z, k->spr.angle, k->bogie[0].spr.x, k->bogie[1].spr.y, k->bogie[0].sub };
            for (size_t i = 0; i < sizeof v; i++) h = (h ^ ((const uint8_t *)v)[i]) * 16777619u;
        }
    }
    return h;
}

enum { RUN_FRAMES = 2400 };
static TrainLog logs[TRAIN_MAX];

static uint32_t run(bool shots)
{
    uint32_t h = 2166136261u;
    memset(logs, 0, sizeof logs);
    for (int t = 0; t < g_train_count; t++) {
        const TrainCar *k = &g_trains[t].car[g_trains[t].front_car & 3];
        logs[t].min_frame = 8;
        logs[t].last_state = g_trains[t].state;
        logs[t].lx = k->spr.x, logs[t].ly = k->spr.y;
    }
    bool shot_station = false, shot_moving = false, shot_curve = false;
    int off_rails = 0;
    view_train(0, true);
    if (shots) shot("start", 0);
    for (int f = 0; f < RUN_FRAMES; f++) {
        view_train(0, false);
        game_frame_step();
        off_rails += check_on_rails();
        for (int t = 0; t < g_train_count; t++) {
            const Train *T = &g_trains[t];
            TrainLog *l = &logs[t];
            if (T->state == TRAIN_ST_STATION && l->last_state == TRAIN_ST_BRAKE) l->stations++;
            l->last_state = T->state;
            if (T->door_frame == 7) l->doors_open++;
            if (T->door_frame < l->min_frame) l->min_frame = T->door_frame;
            if (T->speed > l->max_speed) l->max_speed = T->speed;
            if (T->state == TRAIN_ST_STATION && T->sub == 3 && T->toggle == 0) {
                if (l->last_sub != 3 || l->last_toggle == 1) l->to_unload += T->passengers;
                else if (T->passengers < l->last_pass) l->unloaded += l->last_pass - T->passengers;
            }
            l->last_sub = T->state == TRAIN_ST_STATION ? T->sub : -1;
            l->last_pass = T->passengers;
            l->last_toggle = T->toggle;
            const TrainCar *k = &T->car[T->front_car & 3];
            l->moved += abs((k->spr.x - l->lx) >> 16) + abs((k->spr.y - l->ly) >> 16);
            l->lx = k->spr.x, l->ly = k->spr.y;
        }
        h = hash_trains(h);
        const Train *T0 = &g_trains[0];
        if (shots && !shot_station && T0->door_frame == 7 && logs[0].stations > 0) shot_station = true, shot("station_doors_open", 0);
        if (shots && !shot_moving && T0->speed == T0->max_speed && f > 200) shot_moving = true, shot("running", 0);
        if (shots && !shot_curve && T0->speed > 0 && T0->car[1].bogie[0].in_bend) shot_curve = true, shot("curve", 0);
    }
    CHECK(off_rails == 0, "%d bogie / carriage checks off the railway", off_rails);
    for (int t = 0; t < g_train_count; t++)
        printf("  train %d: %d station stops, doors open %d frames (frame down to %d), %d of %d passengers off, top speed %d, front moved %d px, now at station %d state %d\n",
               t, logs[t].stations, logs[t].doors_open, logs[t].min_frame, logs[t].unloaded, logs[t].to_unload, logs[t].max_speed,
               logs[t].moved, g_trains[t].station, g_trains[t].state);
    return h;
}

/* ---- riding ---- */

static void ride(void)
{
    /* wait until train 0 stands at a station with its doors open (Ped_TryEnterCar boards then) */
    int t = 0, f = 0;
    for (; f < 3000; f++) {
        const Train *T = &g_trains[t];
        if (T->state == TRAIN_ST_STATION && T->door_frame == 7 && T->speed == 0) break;
        view_train(t, false);
        game_frame_step();
    }
    CHECK(f < 3000, "train %d never stood at a station with the doors open", t);
    Train *T = &g_trains[t];
    Player *pl = &g_players[0];
    Ped *p = ped_get(pl->ped);
    /* the end of the train part of Ped_TryEnterCar (ped_car.c): the ped steps in and boards */
    const TrainCar *k = &T->car[1];
    coll_remove(p, p->spr.unk20);
    p->spr.x = k->spr.x, p->spr.y = k->spr.y, p->spr.z = k->spr.z;
    p->mode = 0;
    g_ped_74f0f8 = p->id;
    train_command(9, t);
    p->car = -1;
    p->anim = 0x3b;
    p->state = 0x12;
    p->train = (uint8_t)t;
    player_board_train(p->id);
    printf("ride: player ped %d boards train %d at station %d (frame %d): player kind %d id %d, rider %d\n", p->id, t, T->station, f,
           pl->ctl_kind, pl->ctl_id, T->rider);
    CHECK(pl->ctl_kind == PLAYER_ON_TRAIN && pl->ctl_id == t, "the player isn't on the train");
    CHECK(T->rider == p->id, "the train's rider is %d", T->rider);
    view_train(t, true);
    shot("boarded", t);
    /* riding along (not in charge): the train keeps its timetable */
    int st0 = T->station;
    for (f = 0; f < 1500 && T->station == st0; f++) view_train(t, false), game_frame_step();
    for (; f < 3000 && !(T->state == TRAIN_ST_STATION && T->speed == 0); f++) view_train(t, false), game_frame_step();
    printf("ride: left station %d, at station %d after %d frames (state %d)\n", st0, T->station, f, T->state);
    CHECK(T->station != st0, "the ridden train never left its station");
    shot("ride_next_station", t);
    /* take it over: the fire key on a train (Player_ApplyInput: Train_Command 1) */
    train_command(1, t);
    pl->train_door = 1;
    for (f = 0; f < 200 && T->state != TRAIN_ST_RIDDEN; f++) view_train(t, false), game_frame_step();
    printf("ride: taken over: state %d boarded %d top speed %d after %d frames\n", T->state, T->boarded, T->max_speed, f);
    CHECK(T->state == TRAIN_ST_RIDDEN && T->boarded == 2 && T->max_speed == 0x50, "taking the train over");
    CHECK(train_is_boarded(t), "Train_IsBoarded");
    /* go (accelerate = door key 3), run 300 frames past the stations, then stop (brake = door key 4) */
    train_command(3, t);
    int32_t x0 = T->car[T->front_car].spr.x, y0 = T->car[T->front_car].spr.y;
    for (f = 0; f < 300; f++) {
        view_train(t, false);
        game_frame_step();
        if (f == 150) shot("driving", t);
    }
    int dist = abs((T->car[T->front_car].spr.x - x0) >> 16) + abs((T->car[T->front_car].spr.y - y0) >> 16);
    printf("ride: driven 300 frames: speed %d (top %d), %d px from where it started, state %d\n", T->speed, T->max_speed, dist, T->state);
    CHECK(T->speed == 0x50 && dist > 300, "the driven train didn't go");
    train_command(4, t);
    for (f = 0; f < 200 && T->state != TRAIN_ST_RIDDEN; f++) view_train(t, false), game_frame_step();
    printf("ride: stopped: state %d speed %d after %d frames\n", T->state, T->speed, f);
    CHECK(T->state == TRAIN_ST_RIDDEN && T->speed == 0, "the train didn't stop");
    /* backwards: door key 4 from the ridden state turns it round */
    train_command(4, t);
    int dir = T->dir;
    for (f = 0; f < 120; f++) view_train(t, false), game_frame_step();
    printf("ride: reversed: direction %d, speed %d, state %d\n", dir, T->speed, T->state);
    CHECK(dir == 7 && T->speed > 0, "the train didn't reverse");
    CHECK(check_on_rails() == 0, "a reversed train left the railway");
    shot("reversed", t);
    train_command(3, t);   /* (direction 7: door key 3 brakes into the ridden state) */
    for (f = 0; f < 200 && T->state != TRAIN_ST_RIDDEN; f++) view_train(t, false), game_frame_step();
    /* leave: Train_Command 2, back to the station sequence (doors open where it stands) */
    train_command(2, t);
    pl->train_door = 0;
    for (f = 0; f < 100; f++) view_train(t, false), game_frame_step();
    printf("ride: left the train: state %d sub %d boarded %d top speed %d direction %d doors %d\n", T->state, T->sub, T->boarded,
           T->max_speed, T->dir, T->door_frame);
    CHECK(T->boarded == 1 && T->max_speed == 0x3c && T->dir == 6, "leaving the train");
    CHECK(check_on_rails() == 0, "off the railway after the ride");
}

/* ---- a crash ---- */

static void crash(void)
{
    int t = 1;
    Train *T = &g_trains[t];
    int cars0 = cars_in_use();
    view_train(t, true);
    train_crash(t, 2);
    int wrecked = 0;
    for (int c = 0; c < 4; c++) wrecked += T->car[c].state == TRAIN_CAR_WRECKED;
    int cars1 = cars_in_use();
    printf("crash: train %d: %d carriages wrecked, state %d next %d, cars %d -> %d, Train_AnyWrecked %d\n", t, wrecked, T->state,
           T->next_state, cars0, cars1, train_any_wrecked());
    CHECK(wrecked == 4 && train_any_wrecked(), "Train_Crash didn't wreck the train");
    CHECK(cars1 == cars0 + 4, "the wreck cars (model 0xb) weren't spawned");
    for (int f = 0; f < 30; f++) game_frame_step();
    shot("crashed", t);
}

int main(void)
{
    char err[256];
    if (!vfs_mount_default()) { printf("SKIP: no data root\n"); return 0; }
    if (!exe_init(err, sizeof err)) { printf("SKIP: %s\n", err); return 0; }
    math_init_tables();
    if (!camera_init_tables()) { printf("FAIL: camera tables\n"); return 1; }
    mkdir("out", 0755);
    mkdir("out/train", 0755);
    game_set_screen(W, H);

    /* the curve table: straight row 0, the corners end on the next block's edge */
    uint8_t a, b;
    int an = train_curve_entry(1, 0x30, &a, &b);
    printf("curve table: row 1 step 0x30 (%d, %d) angle %d; row 3 step 0x45 angle %d; row 7 step 9 angle %d\n", a, b, an,
           train_curve_entry(3, 0x45, NULL, NULL), train_curve_entry(7, 9, NULL, NULL));

    check_rails(1, "NYC", 3);
    check_rails(102, "SANB", 0);
    check_rails(202, "MIAMI", 2);
    an = train_curve_entry(1, 0x30, &a, &b);
    printf("curve table: row 1 step 0x30 (%d, %d) angle %d; row 3 step 0x45 angle %d; row 7 step 9 angle %d\n", a, b, an,
           train_curve_entry(3, 0x45, NULL, NULL), train_curve_entry(7, 9, NULL, NULL));
    CHECK(a == 31 && b == 63 && an == 0x100, "curve row 1 doesn't end a quarter turn on the edge");

    CHECK(start(1), "mission 1 start");
    printf("mission 1: %d trains\n", g_train_count);
    CHECK(check_on_rails() == 0, "trains off the railway at the start");
    uint32_t h1 = run(true);
    for (int t = 0; t < g_train_count; t++) {
        CHECK(logs[t].stations >= 1, "train %d never stopped at a station", t);
        CHECK(logs[t].doors_open > 0 && logs[t].min_frame == 1, "train %d never opened its doors", t);
        CHECK(logs[t].max_speed == 0x3c, "train %d top speed %d", t, logs[t].max_speed);
        CHECK(logs[t].unloaded > 0 && logs[t].unloaded <= logs[t].to_unload, "train %d: %d of %d passengers off", t, logs[t].unloaded,
              logs[t].to_unload);
    }
    game_run_end();

    CHECK(start(1), "mission 1 restart");
    uint32_t h2 = run(false);
    printf("train hash %08x / %08x\n", h1, h2);
    CHECK(h1 == h2, "the same frames gave different trains");
    ride();
    crash();
    game_run_end();

    printf(failures ? "train_test: %d FAILURES\n" : "train_test: ok\n", failures);
    return failures != 0;
}
