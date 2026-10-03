/* Fires and fire engines against the real data: mission 1 (NYC) started as Game_Run does, a fire
   (object 0x12, Obj_CreateAnimated) lit on a road some blocks from the nearest fire station, then the
   in-game loop. Checks: Fire_Register records it and FireEngine_Dispatch sends an engine (a sentinel
   of kind 6 driving car model 0x2a with its hose, type 0x32); the engine drives there (FireEngine_Update
   states 2 / 1), stops (10, 0x14), turns the hose (0x64), sprays a jet of water objects (0x6e, 0x82),
   the fire object goes, the jet is taken down (0x97, 0xa0) and the fire record is dropped; the engine
   heads home (0x1e -> 0x23 / 0x28). Two runs must agree. Then a few tuning entries (Tune_SetCarParam).
   Frames go to out/fire/.
     ./build/fire_test            (data root: ./game or OPENGTA_DATA) */
#include "audio/audio.h"
#include "exe.h"
#include "game/ai.h"
#include "game/car.h"
#include "game/coll.h"
#include "game/fire.h"
#include "game/game.h"
#include "game/gmath.h"
#include "game/input.h"
#include "game/mission.h"
#include "game/obj.h"
#include "game/path.h"
#include "game/ped.h"
#include "game/player.h"
#include "game/route.h"
#include "game/sentinel.h"
#include "game/carinfo.h"
#include "game/tune.h"
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
static bool verbose = true;

#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: " __VA_ARGS__); printf("\n"); failures++; } } while (0)
#define LOG(...) do { if (verbose) printf(__VA_ARGS__); } while (0)

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
static void step(void)
{
    for (int calls = 0; calls < 16; calls++) {
        input_feed_held(held);
        audio_render(NULL, 315);
        if (game_run_step() != GAME_STEP_WAIT) return;
    }
    CHECK(0, "game_run_step never ran a frame");
}

static uint32_t fnv(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
    return h;
}

static void shot(const char *name)
{
    if (!verbose) return;
    char path[128];
    snprintf(path, sizeof path, "out/fire/%s.png", name);
    CHECK(png_write(path, fb, W, H, PNG_XRGB), "write %s", path);
    printf("  -> %s\n", path);
}

static Ped *player_ped(void) { return ped_get(g_players[0].ped); }
static void move_player(int32_t x, int32_t y)
{
    Ped *p = player_ped();
    coll_remove(p, p->spr.unk20);
    p->spr.x = x, p->spr.y = y;
    p->spr.z = map_get_ground_z(g_game.map, x, y, 0x10000);
    coll_insert(COLL_PED, p->id, p, p->spr.unk20, x, y);
}

/* a road block (direction bits in the type cache) on a ring around (cx, cy), at least `d` blocks out */
static bool find_road(int cx, int cy, int d, uint8_t q[8])
{
    for (int r = d; r < d + 8; r++)
        for (int k = -r; k <= r; k++) {
            int c[4][2] = { { cx + k, cy - r }, { cx + k, cy + r }, { cx - r, cy + k }, { cx + r, cy + k } };
            for (int i = 0; i < 4; i++) {
                int x = c[i][0], y = c[i][1];
                if (x < 2 || y < 2 || x > 253 || y > 253) continue;
                for (int z = 5; z >= 0; z--) {
                    if ((g_game.map->type_cache[z][y][x] & 0xf) == 0) continue;
                    q[2] = (uint8_t)x, q[3] = (uint8_t)y, q[4] = (uint8_t)z;
                    return true;
                }
            }
        }
    return false;
}

static const char *state_name(int s)
{
    switch (s) {
    case 1: return "to the fire"; case 2: return "route search"; case 5: return "waiting for the search";
    case 10: return "stopping"; case 0x14: return "stopped"; case 0x1e: return "next fire";
    case 0x23: return "search home"; case 0x27: return "giving up"; case 0x28: return "home";
    case 0x64: return "aiming"; case 0x6e: return "jet"; case 0x78: return "short jet"; case 0x82: return "spraying";
    case 0x97: return "jet off"; case 0xa0: return "hose back"; case 0xaa: return "blocked"; case 0xff: return "dismissed";
    default: return "?";
    }
}

static uint32_t run_fire(int frames)
{
    uint32_t h = 2166136261u;
    /* the fire station the dispatcher will choose and a road block 8+ blocks from it */
    const BlockXYZ *st = &g_fire_engine_bases[0];
    LOG("fire stations: (%d, %d, %d) (%d, %d, %d) (%d, %d, %d) (%d, %d, %d)\n", st[0].x, st[0].y, st[0].z,
        st[1].x, st[1].y, st[1].z, st[2].x, st[2].y, st[2].z, st[3].x, st[3].y, st[3].z);
    uint8_t q[8] = { 0 };
    CHECK(find_road(st->x, st->y, 8, q), "no road near the fire station");
    int32_t fx = q[2] * 0x400000 + 0x200000, fy = q[3] * 0x400000 + 0x200000;
    int32_t fz = map_get_ground_z(g_game.map, fx, fy, q[4] * 0x400000);
    move_player(fx + 0x800000, fy - 0x600000);   /* the camera follows the player: between the fire and where the engine stops */
    for (int f = 0; f < 10; f++) step();
    int fire = obj_create_animated(fx, fy, fz, 0x12, -1, 0);
    LOG("fire: object %d at block (%d, %d, %d) z %d; recorded fires %d, engines out %d\n", fire, q[2], q[3], q[4],
        fz >> 16, g_fire.count, g_fire.engines_out);
    CHECK(fire >= 0, "no fire object");
    if (fire < 0) return h;
    CHECK(g_fire.count == 1 && g_fire.fires[0].obj == fire, "the fire isn't recorded (count %d)", g_fire.count);
    int car = g_fire.fires[0].engine;
    CHECK(car >= 0, "no fire engine dispatched");
    if (car < 0) return h;
    const Car *c = car_get(car);
    int sid = c->sentinel;
    const Sentinel *s = sentinel_ptr(sid);
    CHECK(s && s->kind == SENT_FIRE && c->model == 0x2a, "engine car %d: model %d, sentinel %d kind %d", car,
          c->model, sid, s ? s->kind : -1);
    CHECK(s && s->u50 >= 0 && obj_get(s->u50)->type == 0x32, "no hose");
    LOG("engine: car %d (model 0x%x) at (%d, %d, %d), sentinel %d state 0x%x, hose object %d, destination (%d, %d, %d)\n",
        car, c->model, c->spr.x >> 22, c->spr.y >> 22, c->spr.z >> 22, sid, s->state, s->u50, s->dest[0], s->dest[1], s->dest[2]);

    int last = -1, arrived = -1, sprayed = -1, jet = 0, put_out = -1, home = -1;
    bool seen[256] = { false };
    for (int f = 1; f <= frames; f++) {
        step();
        s = sentinel_ptr(sid);
        c = car_get(car);
        if (s->kind != SENT_FIRE) {
            LOG("  frame %d: the engine record is gone (kind %d)\n", f, s->kind);
            break;
        }
        h = fnv(h, &c->spr.x, 12);
        h = fnv(h, &s->state, 1);
        h = fnv(h, &g_fire, sizeof g_fire);
        if (f == 4) shot("00_fire");
        if (s->state != last) {
            last = s->state;
            seen[last] = true;
            int d = abs((c->spr.x >> 22) - q[2]) + abs((c->spr.y >> 22) - q[3]);
            LOG("  frame %4d: state 0x%02x %-22s car (%d, %d, %d) speed %d, %d blocks from the fire\n", f, last, state_name(last),
                c->spr.x >> 22, c->spr.y >> 22, c->spr.z >> 22, c->speed, d);
        }
        if (arrived < 0 && s->state == 0x14) {
            arrived = f;
            shot("01_arrived");
        }
        if (s->state == 0x82) {
            int n = 0;
            const Fire *fr = &g_fire.fires[0];
            for (int k = 0; k < 9 && fr->objs[k] >= 0; k++) n++;
            if (sprayed < 0) {
                sprayed = f;
                jet = n;
                LOG("  frame %d: jet of %d objects (types", f, n);
                for (int k = 0; k < n; k++) LOG(" 0x%x", obj_get(fr->objs[k])->type);
                LOG("), the fire object %s\n", obj_get(fire)->state ? "still burning" : "deleted");
            }
            if (f == sprayed + 60) shot("02_spraying");
        }
        if (put_out < 0 && sprayed > 0 && g_fire.count == 0) {
            put_out = f;
            LOG("  frame %d: fire record dropped (count %d), engine state 0x%x\n", f, g_fire.count, s->state);
            shot("03_out");
        }
        if (home < 0 && put_out > 0 && (s->state == 0x28 || s->state == 0x23)) home = f;
        if (home > 0 && f >= home + 150) {
            shot("04_home");
            break;
        }
    }
    LOG("fire: arrived at frame %d, spraying from %d (%d jet objects), out at %d, heading home at %d\n", arrived, sprayed,
        jet, put_out, home);
    CHECK(arrived > 0, "the engine never stopped at the fire");
    CHECK(seen[0x64] && sprayed > 0, "the engine never sprayed");
    CHECK(jet > 0, "no jet objects");
    CHECK(obj_get(fire)->state == 0, "the fire object is still there");
    CHECK(put_out > 0 && put_out > sprayed, "the fire wasn't put out");
    CHECK(home > 0, "the engine didn't head home");
    return h;
}

/* The tuning entries (Tune_SetCarParam, what config.ini would hold; the data has no config.ini, so
   Tune_LoadFile at the level start changed nothing): a float, a short and a byte field of a model. */
static void check_tuning(void)
{
    int model = 0;
    while (model < CAR_MODELS && g_car_model_index[model] < 0) model++;
    const uint8_t *r = car_info_record(g_car_model_index[model]);
    float mass0 = carinfo_float(r, 0x7c);
    char k1[32], k2[32], k3[32];
    snprintf(k1, sizeof k1, "car %d mass", model);
    snprintf(k2, sizeof k2, "car %d turn ratio", model);
    snprintf(k3, sizeof k3, "car %d centre of mass y", model);
    tune_set_car_param(k1, 0x28000, (float)0x28000 / 65536.0f);
    tune_set_car_param(k2, -3, -3.0f / 65536.0f);
    tune_set_car_param(k3, 0x105, 0x105 / 65536.0f);
    tune_set_car_param((char[]){ "car 101 mass" }, 1, 1.0f);   /* no such model: ignored */
    LOG("tuning: model %d mass %g -> %g, turn ratio %d, centre of mass y %d\n", model, mass0, carinfo_float(r, 0x7c),
        carinfo_s16(r, 0x98), r[0x77]);
    CHECK(carinfo_float(r, 0x7c) == 2.5f, "mass not tuned");
    CHECK(carinfo_s16(r, 0x98) == -3, "turn ratio not tuned");
    CHECK(r[0x77] == 5, "centre of mass y not tuned (a byte)");
}

int main(int argc, char **argv)
{
    (void)argc, (void)argv;
    char err[256];
    if (!vfs_mount_default()) { printf("SKIP: no data root\n"); return 0; }
    if (!exe_init(err, sizeof err)) { printf("SKIP: %s\n", err); return 0; }
    math_init_tables();
    if (!camera_init_tables()) { printf("FAIL: camera tables\n"); return 1; }
    mkdir("out", 0755);
    mkdir("out/fire", 0755);
    game_set_screen(W, H);

    uint32_t h[2];
    for (int run = 0; run < 2; run++) {
        verbose = run == 0;
        CHECK(start(1), "mission 1 start");
        for (int f = 0; f < 20; f++) step();
        h[run] = run_fire(4000);
        if (run == 0) check_tuning();   /* (the next level start copies the style's records again) */
        game_run_end();
    }
    printf("fire hash %08x / %08x\n", h[0], h[1]);
    CHECK(h[0] == h[1], "two runs differ");
    printf(failures ? "fire_test: %d FAILURES\n" : "fire_test: ok\n", failures);
    return failures != 0;
}
