/* Traffic against the real data: mission 1 (NYC) started as Game_Run does and run through the in-game
   loop with no input. Checks: the traffic pool primed by the mission header, cars generated around the
   view (Traffic_SpawnAroundView), dummies following the roads (on road blocks, moving along their
   direction), turning at junctions, stopping at red lights and going on green, determinism over two
   runs; the traffic lights (junctions found, phases cycling); a path search between two road blocks
   (Path_Find across frames) and Map_FindNearestRoad. Frames go to out/traffic/.
     ./build/traffic_test            (data root: ./game or OPENGTA_DATA) */
#include "audio/audio.h"
#include "exe.h"
#include "game/ai.h"
#include "game/car.h"
#include "game/carcoll.h"
#include "game/coll.h"
#include "game/dummy.h"
#include "game/game.h"
#include "game/gmath.h"
#include "game/input.h"
#include "game/lights.h"
#include "game/mission.h"
#include "game/path.h"
#include "game/ped.h"
#include "game/player.h"
#include "game/route.h"
#include "game/traffic.h"
#include "map.h"
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

static uint8_t tc(int x, int y, int z) { return (x | y) & ~0xff || z < 0 || z > 5 ? 0 : g_game.map->type_cache[z][y][x]; }
static bool is_traffic(const Car *c) { return c->status != -1 && c->control == CAR_CTL_DUMMY && c->driver != -1 && c->unk139 == 1; }

/* ---- what the run observes ---- */
typedef struct {
    int max_traffic, spawned, moving_frames, off_road, turns, red_stops, green_starts, lights_seen;
    int16_t prev_dirs[CAR_MAX];
    int16_t prev_udc[CAR_MAX];
    int32_t prev_x[CAR_MAX], prev_y[CAR_MAX];
    uint32_t hash;
} Obs;

static uint32_t fnv(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
    return h;
}

static void observe(Obs *o, int frame)
{
    int n = 0;
    for (int i = 0; i < CAR_MAX; i++) {
        const Car *c = &g_cars[i];
        if (!is_traffic(c)) {
            o->prev_x[i] = -1;
            continue;
        }
        n++;
        if (o->prev_x[i] == -1) {
            o->spawned++;
        } else {
            if (c->speed > 0) {
                o->moving_frames++;
                int k = tc(c->spr.x >> 22, c->spr.y >> 22, c->spr.z >> 22) >> 4 & 7;
                if (k != 2 && k != 6 && k != 7 && k != 3) o->off_road++;
            }
            if (c->road_dirs != o->prev_dirs[i] && c->turn_delta == 0 && o->prev_dirs[i] != 0) o->turns++;
            if (c->udc == 1 && o->prev_udc[i] != 1) {
                /* stopped for a light: the junction ahead shows red / amber */
                o->red_stops++;
                if (o->red_stops <= 3)
                    printf("  frame %d: car %d (model %d) stops for the light of junction %d at (%d, %d)\n", frame, i, c->model,
                           c->ua8, c->spr.x >> 22, c->spr.y >> 22);
            }
            if (c->udc == 0 && o->prev_udc[i] == 1) {
                o->green_starts++;
                if (o->green_starts <= 3) printf("  frame %d: car %d goes on green\n", frame, i);
            }
        }
        o->prev_x[i] = c->spr.x, o->prev_y[i] = c->spr.y;
        o->prev_dirs[i] = (int16_t)c->road_dirs;
        o->prev_udc[i] = c->udc;
        o->hash = fnv(o->hash, &c->spr.x, 12);
        o->hash = fnv(o->hash, &c->spr.angle, 2);
        o->hash = fnv(o->hash, &c->speed, 2);
    }
    if (n > o->max_traffic) o->max_traffic = n;
}

static void shot(const char *name)
{
    char path[128];
    snprintf(path, sizeof path, "out/traffic/%s.png", name);
    CHECK(png_write(path, fb, W, H, PNG_XRGB), "write %s", path);
    int n = 0, moving = 0;
    for (int i = 0; i < CAR_MAX; i++)
        if (is_traffic(&g_cars[i])) n++, moving += g_cars[i].speed > 0;
    printf("  %s: %d traffic cars (%d moving), %d active\n", path, n, moving, g_cars_active);
}

/* move the player's ped (and so the camera) to pixel (x, y) on the ground */
static void move_player(int32_t x, int32_t y)
{
    Ped *p = ped_get(g_players[0].ped);
    coll_remove(p, p->spr.unk20);
    p->spr.x = x, p->spr.y = y;
    p->spr.z = map_get_ground_z(g_game.map, x, y, 0x10000);
    coll_insert(COLL_PED, p->id, p, p->spr.unk20, x, y);
}

static Obs obs;

static uint32_t run(int frames, bool shots)
{
    memset(&obs, 0, sizeof obs);
    for (int i = 0; i < CAR_MAX; i++) obs.prev_x[i] = -1;
    obs.hash = 2166136261u;
    int pool = 0;
    for (int i = 0; i < CAR_MAX; i++) pool += g_cars[i].unk139 == 1;
    if (shots) printf("  traffic pool: %d reserved slots, %d cars in use, car count %d\n", pool, cars_in_use(), g_cars_count);
    CHECK(pool > 0, "no traffic pool after the level start");
    /* watch junctions with lights near the start, a new one every 300 frames (cars only move near a view) */
    const Ped *pp = ped_get(g_players[0].ped);
    int sx = pp->spr.x >> 22, sy = pp->spr.y >> 22, spot[4], nspot = 0;
    while (nspot < 4) {
        int best = -1, bd = 1 << 30;
        for (int i = 0; i < g_lights.nlights; i++) {
            const Light *l = &g_lights.lights[i];
            bool used = false;
            for (int k = 0; k < nspot; k++) used |= g_lights.lights[spot[k]].group == l->group;
            int d = abs(l->x - sx) + abs(l->y - sy);
            if (!used && d < bd) bd = d, best = i;
        }
        if (best < 0) break;
        spot[nspot++] = best;
    }
    for (int f = 1; f <= frames; f++) {
        if ((f - 1) % 300 == 0 && nspot > 0) {
            const Light *l = &g_lights.lights[spot[(f - 1) / 300 % nspot]];
            if (shots) printf("  frame %d: the player goes to light %d of junction %d at (%d, %d, %d)\n", f, (int)(l - g_lights.lights),
                              l->group, l->x, l->y, l->z);
            move_player(l->x * 0x400000 + 0x200000, l->y * 0x400000 + 0x200000);
        }
        game_frame_step();
        observe(&obs, f);
        if (shots && f % 300 == 150) {
            char name[32];
            snprintf(name, sizeof name, "frame%04d", f);
            shot(name);
        }
    }
    return obs.hash;
}

/* ---- the lights ---- */
static void check_lights(void)
{
    printf("lights: %d junctions, %d lights, %d cells\n", g_lights.njunctions, g_lights.nlights, g_lights.ncells);
    CHECK(g_lights.ready == 1 && g_lights.nlights > 0, "no traffic lights in NYC");
    if (g_lights.nlights == 0) return;
    const Light *l = &g_lights.lights[0];
    int g = l->group;
    CHECK(lights_query(LQ_GROUP, l->x, l->y) == g, "query 0x3a at the light's block");
    CHECK(lights_query(LQ_STATE, 0, 0) == LIGHTS_NONE, "query 0x34 off a junction");
    int last = lights_query(LQ_STATE, l->x, l->y), changes = 0, seen = 1 << last;
    for (int f = 0; f < 640; f++) {
        game_frame_step();
        int st = lights_query(LQ_STATE, l->x, l->y);
        if (st != last) changes++, last = st, seen |= 1 << st;
    }
    printf("  light 0 (junction %d, orientation %d) at (%d, %d): %d changes in 640 frames, states seen %#x\n", g, l->orient, l->x,
           l->y, changes, seen);
    CHECK(changes >= 6 && seen == 0xf, "light 0 doesn't cycle through red, amber, flashing, green");
    lights_command(LQ_FORCE, 0, l->x, l->y);
    CHECK(lights_query(LQ_MODE, l->x, l->y) == 3 && lights_query(LQ_ALL_RED, l->x, l->y) == 1, "forced red");
    int f = 0;
    while (lights_query(LQ_MODE, l->x, l->y) == 3 && f < 1000) game_frame_step(), f++;
    printf("  forced red held %d frames\n", f);
    CHECK(f > 390 && f <= 410, "forced red held %d frames", f);
}

/* ---- the path finder ---- */
static bool find_road(int *x, int *y, int z)
{
    for (int r = 0; r < 40; r++)
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++) {
                int bx = *x + dx, by = *y + dy;
                if (bx < 1 || by < 1 || bx > 254 || by > 254) continue;
                uint8_t t = tc(bx, by, z);
                if ((t & 0x70) == 0x20 && (t & 0xf) && !(t & 0x80)) { *x = bx, *y = by; return true; }
            }
    return false;
}

/* the route of path slot n: nodes, printed; consecutive nodes in a row or column (the route keeps
   the turning points), start first */
static int route_len(int n, bool print)
{
    const uint8_t *s = g_path_slots[n];
    int k = 0;
    if (print) printf("  route:");
    for (; k < 85 && (s[k * 3] | s[k * 3 + 1] | s[k * 3 + 2]); k++)
        if (print && k < 16) printf(" (%d,%d,%d)", s[k * 3], s[k * 3 + 1], s[k * 3 + 2]);
    if (print) printf("%s (%d nodes)\n", k > 16 ? " ..." : "", k);
    return k;
}

static void check_path(void)
{
    /* from the start of the mission's player to a road block 30 / 20 blocks away */
    const Ped *p = ped_get(g_players[0].ped);
    int z = p->spr.z >> 22;
    int sx = p->spr.x >> 22, sy = p->spr.y >> 22, dx = sx + 30, dy = sy + 20;
    if (!find_road(&sx, &sy, z) || !find_road(&dx, &dy, z)) { CHECK(0, "no road blocks near the player"); return; }
    for (int mode = 3; mode <= 5; mode += 2) {
        int ctrl = 3, r = 3, calls = 0;
        ai_get(ctrl)->path_progress = 0;
        while (r == 3 && calls < 400) {
            r = path_find(sx, sy, z, dx, dy, z, mode, ctrl), calls++;
            if (r == 3 && calls == 1) {
                CHECK(path_find(sx, sy, z, dx, dy, z, mode, 4) == PATH_BUSY, "a second controller isn't refused");
            }
        }
        printf("path mode %d: (%d, %d, %d) -> (%d, %d, %d): result %d after %d calls (frames)\n", mode, sx, sy, z, dx, dy, z, r, calls);
        CHECK(r == PATH_FOUND && g_path_owner == -1, "path search failed (%d, owner %d)", r, g_path_owner);
        int n = route_len(ctrl, true);
        const uint8_t *s = g_path_slots[ctrl];
        CHECK(n > 1 && s[0] == sx && s[1] == sy, "route doesn't start at the start");
        bool goal = false;
        for (int k = n - 2; k < n; k++) goal |= k >= 0 && s[k * 3] == dx && s[k * 3 + 1] == dy;
        CHECK(goal, "route doesn't reach the goal");
    }
    CHECK(path_find(sx, sy, z, sx, sy, z, 5, 3) == PATH_FOUND && g_path_slots[3][0] == 0, "start = goal");
    uint8_t q[8] = { 0 };
    q[2] = (uint8_t)(p->spr.x >> 22), q[3] = (uint8_t)(p->spr.y >> 22), q[4] = (uint8_t)z;
    int ok = map_find_nearest_road(q);
    printf("  nearest road from the player's block (%d, %d, %d) type %#x: %d -> (%d, %d, %d) type %#x\n", p->spr.x >> 22,
           p->spr.y >> 22, z, tc(p->spr.x >> 22, p->spr.y >> 22, z), ok, q[2], q[3], q[4], tc(q[2], q[3], q[4]));
    CHECK(ok && (tc(q[2], q[3], q[4]) & 0xf), "Map_FindNearestRoad");
}

/* ---- a mission dummy: its controller gets a route (the driving itself is Sentinel_DriveCar's) ---- */
static void check_dummy(void)
{
    const Ped *p = ped_get(g_players[0].ped);
    int z = p->spr.z >> 22, bx = (p->spr.x >> 22) + 25, by = (p->spr.y >> 22) - 15;
    if (!find_road(&bx, &by, z)) { CHECK(0, "no target road"); return; }
    int car = -1;
    for (int i = 0; i < CAR_MAX && car < 0; i++)
        if (g_cars[i].status != -1 && g_cars[i].control == 0 && g_cars[i].unk139 == 0 && (tc(g_cars[i].spr.x >> 22, g_cars[i].spr.y >> 22, g_cars[i].spr.z >> 22) & 0xf))
            car = i;
    if (car < 0) { printf("dummy: no parked car on a road\n"); return; }
    g_path_owner = -1;
    int ctrl = dummy_start_drive(car, bx << 6, by << 6, z << 6);
    CHECK(ctrl >= 0, "no controller for the dummy");
    if (ctrl < 0) return;
    AiCtl *r = ai_get(ctrl);
    int f = 0;
    while (f < 100 && (r->state == DUMMY_S_START || r->state == DUMMY_S_PATH_WAIT)) {
        dummy_update((uint8_t *)r);
        /* what the controller's driver does each frame while its search runs (Sentinel_DriveCar) */
        const Car *c = &g_cars[car];
        if (r->state == DUMMY_S_PATH_WAIT && g_path_owner == ctrl)
            g_path_result = (int16_t)path_find(c->spr.x >> 22, c->spr.y >> 22, c->spr.z >> 22, r->dest_x, r->dest_y, r->dest_z, 5, ctrl);
        f++;
    }
    printf("dummy: car %d -> block (%d, %d, %d): controller %d state %#x after %d steps, control %d\n", car, bx, by, z, ctrl, r->state, f,
           g_cars[car].control);
    route_len(ctrl, true);
    CHECK(r->state == DUMMY_S_ARRIVED && route_len(ctrl, false) > 1, "the dummy got no route");
}

int main(void)
{
    char err[256];
    if (!vfs_mount_default()) { printf("SKIP: no data root\n"); return 0; }
    if (!exe_init(err, sizeof err)) { printf("SKIP: %s\n", err); return 0; }
    math_init_tables();
    if (!camera_init_tables()) { printf("FAIL: camera tables\n"); return 1; }
    mkdir("out", 0755);
    mkdir("out/traffic", 0755);
    game_set_screen(W, H);

    enum { FRAMES = 1200 };
    CHECK(start(1), "mission 1 start");
    uint32_t h1 = run(FRAMES, true);
    printf("traffic over %d frames: at most %d cars, %d spawned, %d moving car-frames (%d off road), %d turns, %d red-light "
           "stops, %d green starts\n",
           FRAMES, obs.max_traffic, obs.spawned, obs.moving_frames, obs.off_road, obs.turns, obs.red_stops, obs.green_starts);
    CHECK(obs.max_traffic >= 3, "too little traffic (%d)", obs.max_traffic);
    CHECK(obs.moving_frames > 100, "traffic doesn't move");
    CHECK(obs.off_road * 50 < obs.moving_frames, "traffic leaves the road (%d of %d)", obs.off_road, obs.moving_frames);
    CHECK(obs.turns > 0, "no car turned");
    check_lights();
    game_run_end();

    CHECK(start(1), "mission 1 restart");
    uint32_t h2 = run(FRAMES, false);
    printf("traffic hash %08x / %08x\n", h1, h2);
    CHECK(h1 == h2, "two runs differ");
    check_path();
    check_dummy();
    game_run_end();

    printf(failures ? "traffic_test: %d FAILURES\n" : "traffic_test: ok\n", failures);
    return failures != 0;
}
