/* Cars against the real data: mission 1 started as Game_Run does, the player put into a parked car
   (as the end of the enter chain leaves things: driver = the player's ped, engine on, physics
   control, the camera on the car), then the in-game loop (game_run_step, paced by the audio as in the
   app) driven by held keys through the input path. Checks: car info setup, Car_Init of the parked
   cars, accelerating / steering / braking / the handbrake (plausible motion, determinism over two
   runs), a building wall stopping the car, a crash into another car (impulse, damage, deltas), a
   slope (z follows the ground), the camera following, and renders frames to out/car/.
     ./build/car_test            (data root: ./game or OPENGTA_DATA) */
#include "audio/audio.h"
#include "exe.h"
#include "game/car.h"
#include "game/carcoll.h"
#include "game/carinfo.h"
#include "game/coll.h"
#include "game/game.h"
#include "game/gmath.h"
#include "game/input.h"
#include "game/mission.h"
#include "game/ped.h"
#include "game/player.h"
#include "platform.h"
#include "png.h"
#include "render/camera.h"
#include "render/poly.h"
#include "vfs.h"
#include "vfs_host.h"
#include <math.h>
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
enum { K_UP = 0x148, K_DOWN = 0x150, K_LEFT = 0x14b, K_RIGHT = 0x14d, K_SPACE = 0x39 };

static void game_frame_step(void)
{
    for (int calls = 0; calls < 16; calls++) {
        input_feed_held(held);
        audio_render(NULL, 315);
        if (game_run_step() != GAME_STEP_WAIT) return;
    }
    CHECK(0, "game_run_step never ran a frame");
}

static int g_car = -1;
static Car *mycar(void) { return car_get(g_car); }

static void shot(const char *name)
{
    char path[128];
    snprintf(path, sizeof path, "out/car/%s.png", name);
    CHECK(png_write(path, fb, W, H, PNG_XRGB), "write %s", path);
    const Car *c = mycar();
    printf("  %s: car at (%d, %d, %d) heading %#x speed %d damage %d deltas %#x frame %d; camera (%d, %d) h %d\n", path,
           c->spr.x >> 16, c->spr.y >> 16, c->spr.z >> 16, c->spr.angle, c->speed, c->damage, c->spr.deltas, c->spr.frame,
           g_players[0].vp.x, g_players[0].vp.y, g_players[0].vp.height);
}

/* The end of the enter-car chain, done directly: the player's ped drives car n. */
static void enter(int n)
{
    Player *pl = &g_players[0];
    Car *c = car_get(n);
    Ped *p = ped_get(pl->ped);
    c->driver = pl->ped;
    p->car = (int16_t)n;
    p->state = 7;
    c->unk88 = 1;                    /* the engine runs (Car_SetDriverById's delay elapsed) */
    pl->ctl_kind = PLAYER_IN_CAR, pl->ctl_id = n;
    pl->view_kind = CAM_TARGET_CAR, pl->view_id = n;
    car_set_physics_control(n);
    carphys_begin(n);
    g_car = n;
}

/* Move a car to (x, y) facing angle, at rest on the ground (a test helper). */
static void place(int n, int32_t x, int32_t y, int angle)
{
    Car *c = car_get(n);
    coll_remove(c, c->spr.unk20);
    c->spr.x = x, c->spr.y = y;
    c->spr.z = map_get_ground_z(g_game.map, x, y, c->spr.z - 0x400000);
    c->spr.angle = (int16_t)angle;
    c->speed = 0;
    c->body.vx = c->body.vy = c->body.w = 0;
    c->body.x = 0;
    carphys_begin(n);
    car_sync_physics(c);
    car_commit_move(c);
    coll_insert(COLL_CAR, n, c, c->spr.unk20, x, y);
}

static int nearest_car(int32_t x, int32_t y, int skip)
{
    int best = -1;
    int64_t bd = 0;
    for (int i = 0; i < g_cars_count; i++) {
        const Car *c = car_get(i);
        if (c->status == -1 || i == skip || c->model == 0x2f) continue;
        int64_t dx = (c->spr.x - x) >> 16, dy = (c->spr.y - y) >> 16, d = dx * dx + dy * dy;
        if (best < 0 || d < bd) best = i, bd = d;
    }
    return best;
}

typedef struct { int32_t x, y, z; int16_t h, speed; } Pose;
static Pose pose(void) { const Car *c = mycar(); return (Pose){ c->spr.x, c->spr.y, c->spr.z, c->spr.angle, c->speed }; }
static double dist(Pose a, Pose b) { double dx = (a.x - b.x) / 65536.0, dy = (a.y - b.y) / 65536.0; return sqrt(dx * dx + dy * dy); }
static int hdiff(int a, int b) { int d = (a - b) & 0x3ff; return d > 0x200 ? d - 0x400 : d; }

/* The driving script: idle 5, accelerate 40, accelerate + left 25, accelerate 10, brake (Down) 15,
   (one frame), accelerate + right + handbrake 20, coast 14. Returns a hash of the poses; poses[] gets them. */
enum { SCRIPT_FRAMES = 130 };
static Pose poses[SCRIPT_FRAMES];
static uint32_t run_script(bool shots)
{
    uint32_t h = 2166136261u;
    for (int f = 0; f < SCRIPT_FRAMES; f++) {
        memset(held, 0, sizeof held);
        if (f >= 5 && f < 80) held[K_UP] = 1;
        if (f >= 45 && f < 70) held[K_LEFT] = 1;
        if (f >= 80 && f < 95) held[K_DOWN] = 1;
        if (f >= 96 && f < 116) held[K_UP] = 1, held[K_RIGHT] = 1, held[K_SPACE] = 1;   /* (a frame between: a release and a press in one frame leave the gear of the release) */
        game_frame_step();
        poses[f] = pose();
        if (getenv("CAR_TRACE")) {
            const Car *k = mycar();
            printf("    f%3d pos (%d, %d) h %#x speed %d steer %.4f angle %.4f w %.5f v (%.3f, %.3f) thrust %.3f gear %d brake %d hb %d ctl %d %d %d %d %d\n", f,
                   k->spr.x >> 16, k->spr.y >> 16, k->spr.angle, k->speed, k->steer, k->body.angle, k->body.w, k->body.vx,
                   k->body.vy, k->thrust_in, k->gear, k->brake_in, k->handbrake_in, g_players[0].ctl[0], g_players[0].ctl[1],
                   g_players[0].ctl[2], g_players[0].ctl[3], g_players[0].ctl[4]);
        }
        const uint8_t *b = (const uint8_t *)&poses[f];
        for (size_t i = 0; i < sizeof poses[f]; i++) h = (h ^ b[i]) * 16777619u;
        const PhysBody *bd = &mycar()->body;
        for (size_t i = 0; i < sizeof *bd; i++) h = (h ^ ((const uint8_t *)bd)[i]) * 16777619u;
        if (shots && (f == 4 || f == 44 || f == 69 || f == 94 || f == 115)) {
            char name[32];
            snprintf(name, sizeof name, "drive_%03d", f);
            shot(name);
        }
    }
    memset(held, 0, sizeof held);
    return h;
}

static void check_drive(void)
{
    double d40 = dist(poses[4], poses[44]);
    printf("  accelerate 40 frames: %.1f px, speed %d -> %d, heading %#x -> %#x\n", d40, poses[4].speed, poses[44].speed,
           poses[4].h, poses[44].h);
    CHECK(dist(poses[0], poses[4]) < 1.0, "the car moved while idle");
    CHECK(d40 > 40 && d40 < 2000, "accelerating 40 frames moved %.1f px", d40);
    CHECK(poses[44].speed > poses[10].speed, "speed did not grow (%d -> %d)", poses[10].speed, poses[44].speed);
    int turn = hdiff(poses[69].h, poses[44].h);
    printf("  left 25 frames: heading %#x -> %#x (%+d)\n", poses[44].h, poses[69].h, turn);
    CHECK(turn > 0x20, "steering left turned the heading by %d", turn);
    printf("  brake 15 frames: speed %d -> %d\n", poses[80].speed, poses[94].speed);
    CHECK(poses[94].speed < poses[80].speed, "braking did not slow the car (%d -> %d)", poses[80].speed, poses[94].speed);
    int turn2 = hdiff(poses[115].h, poses[96].h);
    printf("  right + handbrake 20 frames: heading %#x -> %#x (%+d)\n", poses[96].h, poses[115].h, turn2);
    CHECK(turn2 < 0, "steering right turned the heading by %d", turn2);
}

/* the first building block (type 5 at the ground layer) ahead of (x, y) along +y or -y */
static bool find_wall(int32_t x, int32_t y, int32_t z, int dir, int32_t *wy)
{
    for (int k = 1; k < 12; k++) {
        int32_t yy = y + dir * k * 0x400000;
        if ((car_type_cache(x, yy, z - 0x10000) & 0x70) == 0x50) { *wy = yy; return true; }
    }
    return false;
}

static void check_wall(void)
{
    /* look for a street running north-south next to a building: a road block whose next block in y
       is a building at the same layer */
    const Map *m = g_game.map;
    int32_t sx = 0, sy = 0, wy = 0;
    int dir = 0;
    const Car *c = mycar();
    for (int r = 0; r < 40 && !dir; r++)
        for (int bx = (c->spr.x >> 22) - r; bx <= (c->spr.x >> 22) + r && !dir; bx++)
            for (int by = (c->spr.y >> 22) - r; by <= (c->spr.y >> 22) + r && !dir; by++) {
                if (bx < 1 || by < 2 || bx > 254 || by > 253) continue;
                int32_t x = bx * 0x400000 + 0x200000, y = by * 0x400000 + 0x200000;
                int32_t z = map_get_ground_z(m, x, y, 0);
                if ((car_type_cache(x, y, z - 0x10000) & 0x70) != 0x20) continue;
                for (int d = -1; d <= 1 && !dir; d += 2) {
                    int32_t y1 = y + d * 0x400000, y2 = y + 2 * d * 0x400000;
                    if ((car_type_cache(x, y1, z - 0x10000) & 0x70) == 0x20 && map_get_ground_z(m, x, y1, 0) == z &&
                        find_wall(x, y1, z, d, &wy) && wy == y2)
                        sx = x, sy = y, dir = d;
                }
            }
    CHECK(dir != 0, "no road next to a building found");
    if (!dir) return;
    place(g_car, sx, sy, dir > 0 ? 0 : 0x200);   /* heading 0 = +y */
    int32_t face = dir > 0 ? (wy >> 22) * 0x400000 : (wy >> 22) * 0x400000 + 0x400000;
    printf("  wall: car at block (%d, %d) heading %#x, building face at y %d\n", sx >> 22, sy >> 22, mycar()->spr.angle, face >> 16);
    int16_t dmg0 = mycar()->damage;
    memset(held, 0, sizeof held);
    int stopped = -1;
    for (int f = 0; f < 70; f++) {
        held[K_UP] = 1;
        game_frame_step();
        const Car *k = mycar();
        int32_t front = k->spr.y + dir * k->half_l * 0x10000;
        CHECK((dir > 0 ? front <= face + 0x40000 : front >= face - 0x40000), "frame %d: the car's front %d is through the wall at %d",
              f, front >> 16, face >> 16);
        if (stopped < 0 && k->map_hit) stopped = f;
        if (f == 30 || f == 69) shot(f == 30 ? "wall_030" : "wall_069");
    }
    memset(held, 0, sizeof held);
    const Car *k = mycar();
    printf("  wall: first contact frame %d, front at y %d, damage %d -> %d, deltas %#x\n", stopped,
           (k->spr.y + dir * k->half_l * 0x10000) >> 16, dmg0, k->damage, k->spr.deltas);
    CHECK(stopped >= 0, "the car never touched the wall");
}

static void check_car_crash(void)
{
    /* the nearest other car, our car placed 3 lengths behind it on its axis, driving into it */
    Car *me = mycar();
    int o = nearest_car(me->spr.x, me->spr.y, g_car);
    CHECK(o >= 0, "no other car");
    if (o < 0) return;
    Car *other = car_get(o);
    int16_t a = other->spr.angle;
    int back = other->half_l + me->half_l + 450;
    int32_t x = other->spr.x - math_sin(a) * back, y = other->spr.y - math_cos(a) * back;
    place(g_car, x, y, a);
    Pose o0 = { other->spr.x, other->spr.y, other->spr.z, other->spr.angle, other->speed };
    int16_t dmg_me = me->damage, dmg_o = other->damage;
    printf("  crash: car %d (model %d) behind car %d (model %d) at (%d, %d) heading %#x\n", g_car, me->model, o,
           other->model, other->spr.x >> 16, other->spr.y >> 16, a);
    int hit = -1;
    for (int f = 0; f < 60; f++) {
        held[K_UP] = f < 45;
        game_frame_step();
        if (hit < 0 && me->hit_car == o) hit = f;
        if (f == 20 || f == 59) shot(f == 20 ? "crash_020" : "crash_059");
    }
    memset(held, 0, sizeof held);
    Pose o1 = { other->spr.x, other->spr.y, other->spr.z, other->spr.angle, other->speed };
    printf("  crash: first hit frame %d; other car moved %.1f px, heading %#x -> %#x, physics %d; damage %d -> %d / %d -> %d, deltas %#x / %#x\n",
           hit, dist(o0, o1), o0.h, o1.h, other->physics, dmg_me, me->damage, dmg_o, other->damage, me->spr.deltas,
           other->spr.deltas);
    CHECK(hit >= 0, "the cars never touched");
    CHECK(dist(o0, o1) > 1.0, "the other car wasn't pushed");
    CHECK(other->physics == 1, "the other car didn't become a physics body");
}

static void check_slope(void)
{
    /* the nearest slope block (bit 7 of the type cache) to the car, then drive across it */
    const Map *m = g_game.map;
    const Car *c = mycar();
    int fx = -1, fy = -1, fz = -1;
    for (int r = 0; r < 60 && fx < 0; r++)
        for (int bx = (c->spr.x >> 22) - r; bx <= (c->spr.x >> 22) + r && fx < 0; bx++)
            for (int by = (c->spr.y >> 22) - r; by <= (c->spr.y >> 22) + r && fx < 0; by++)
                for (int z = 1; z < 6 && fx < 0; z++) {
                    if (bx < 0 || by < 0 || bx > 255 || by > 255) continue;
                    uint8_t t = m->type_cache[z][by][bx];
                    unsigned s = map_get_type_map(m, bx, by, z) >> 8 & 0x3f;
                    if ((t & 0x80) && (t & 0x70) == 0x20 && s >= 9 && s <= 0x28) fx = bx, fy = by, fz = z;
                }
    CHECK(fx >= 0, "no road slope found");
    if (fx < 0) return;
    unsigned s = map_get_type_map(m, fx, fy, fz) >> 8 & 0x3f;
    /* 8-block slopes 9-0x10 rise along -y, 0x11-0x18 +y, 0x19-0x20 -x, 0x21-0x28 +x (Map_GetGroundZ) */
    int ang = s <= 0x10 ? 0x200 : s <= 0x18 ? 0 : s <= 0x20 ? 0x300 : 0x100;
    int32_t x = fx * 0x400000 + 0x200000, y = fy * 0x400000 + 0x200000;
    place(g_car, x - math_sin(ang) * 40, y - math_cos(ang) * 40, ang);
    printf("  slope: type %#x at block (%d, %d, %d), car at (%d, %d, z %#x) heading %#x\n", s, fx, fy, fz, mycar()->spr.x >> 16,
           mycar()->spr.y >> 16, mycar()->spr.z, ang);
    int32_t zmin = mycar()->spr.z, zmax = zmin;
    int bad = 0;
    for (int f = 0; f < 40; f++) {
        held[K_UP] = f < 25;
        game_frame_step();
        const Car *k = mycar();
        int32_t g = map_get_ground_z(m, k->spr.x, k->spr.y, k->spr.z - 0x200000);
        if (k->falling == 0 && (k->spr.z - g > 0x40000 || g - k->spr.z > 0x40000)) bad++;
        if (k->spr.z < zmin) zmin = k->spr.z;
        if (k->spr.z > zmax) zmax = k->spr.z;
        if (f == 15) shot("slope_015");
    }
    memset(held, 0, sizeof held);
    printf("  slope: z range %#x .. %#x (%d pixels), %d frames off the ground\n", zmin, zmax, (zmax - zmin) >> 16, bad);
    CHECK(zmax - zmin >= 0x40000, "z didn't follow the slope (%#x .. %#x)", zmin, zmax);
    CHECK(bad == 0, "%d frames with z away from the ground", bad);
}

static void check_setup(void)
{
    int n = car_info_count();
    printf("car info: %d records\n", n);
    CHECK(n > 0, "no car info records");
    const uint8_t *in = car_info_of_model(0);
    if (in) printf("  model 0: %dx%d, sprite %d, mass %.3f, thrust %.4f, adhesion %.3f / %.3f, turn ratio %d\n",
                   carinfo_s16(in, 0), carinfo_s16(in, 2), carinfo_s16(in, 6), carinfo_float(in, 0x7c), carinfo_float(in, 0x80),
                   carinfo_float(in, 0x84), carinfo_float(in, 0x88), carinfo_s16(in, 0x98));
    int cars = 0, bad = 0;
    for (int i = 0; i < g_cars_count; i++) {
        const Car *c = car_get(i);
        if (c->status == -1) continue;
        cars++;
        int32_t g = map_get_ground_z(g_game.map, c->spr.x, c->spr.y, c->spr.z - 0x200000);
        if (c->spr.z != g || c->info == NULL || c->box.cx != c->spr.x || c->falling != 0) bad++;
    }
    printf("  %d cars at the level start, %d not resting on the ground\n", cars, bad);
    CHECK(cars > 0 && bad == 0, "%d of %d cars badly placed", bad, cars);
}

int main(void)
{
    char err[256];
    if (!vfs_mount_default()) { printf("SKIP: no data root\n"); return 0; }
    if (!exe_init(err, sizeof err)) { printf("SKIP: %s\n", err); return 0; }
    math_init_tables();
    if (!camera_init_tables()) { printf("FAIL: camera tables\n"); return 1; }
    mkdir("out", 0755);
    mkdir("out/car", 0755);
    game_set_screen(W, H);

    CHECK(start(1), "mission 1 start");
    check_setup();
    const Ped *p = ped_get(g_players[0].ped);
    int n = nearest_car(p->spr.x, p->spr.y, -1);
    CHECK(n >= 0, "no parked car");
    if (n < 0) return 1;
    enter(n);
    printf("player ped %d at (%d, %d) enters car %d (model %d, vtype %d) at (%d, %d) heading %#x\n", p->id, p->spr.x >> 16,
           p->spr.y >> 16, n, mycar()->model, mycar()->vtype, mycar()->spr.x >> 16, mycar()->spr.y >> 16, mycar()->spr.angle);
    uint32_t h1 = run_script(true);
    check_drive();
    game_run_end();

    CHECK(start(1), "mission 1 restart");
    enter(n);
    uint32_t h2 = run_script(false);
    printf("script pose hash %08x / %08x\n", h1, h2);
    CHECK(h1 == h2, "the same script gave different poses");
    check_wall();
    check_car_crash();
    check_slope();
    game_run_end();

    printf(failures ? "car_test: %d FAILURES\n" : "car_test: ok\n", failures);
    return failures != 0;
}
