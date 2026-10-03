/* The police against the real data: mission 1 (NYC) started as Game_Run does, then driven through the
   in-game loop. Checks: the patrol cars of the mission start, crime reports raising the wanted level
   (points, the NYC thresholds, the criminal record), the pursuit (Police_StartPursuit: units sent
   from the dispatch list, cops chasing the player), a roadblock at wanted level 4 in a car, the
   arrest (Player_Busted, the respawn at the police station), an ambulance crew reviving a ped, the
   hunt table and the helicopter; determinism over two runs. Frames go to out/police/.
     ./build/police_test            (data root: ./game or OPENGTA_DATA) */
#include "audio/audio.h"
#include "exe.h"
#include "game/ai.h"
#include "game/ambulance.h"
#include "game/car.h"
#include "game/carphys.h"
#include "game/coll.h"
#include "game/game.h"
#include "game/gang.h"
#include "game/gmath.h"
#include "game/heli.h"
#include "game/input.h"
#include "game/lights.h"
#include "game/mission.h"
#include "game/path.h"
#include "game/ped.h"
#include "game/player.h"
#include "game/police.h"
#include "game/route.h"
#include "game/sentinel.h"
#include "game/stubs.h"
#include "game/wanted.h"
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

enum { K_UP = 0x148 };
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
static uint32_t hash;

static void shot(const char *name)
{
    if (!verbose) return;
    char path[128];
    snprintf(path, sizeof path, "out/police/%s.png", name);
    CHECK(png_write(path, fb, W, H, PNG_XRGB), "write %s", path);
    printf("  -> %s\n", path);
}

static Ped *player_ped(void) { return ped_get(g_players[0].ped); }
static int32_t px(void) { return g_players[0].ctl_kind == PLAYER_IN_CAR ? car_get(g_players[0].ctl_id)->spr.x : player_ped()->spr.x; }
static int32_t py(void) { return g_players[0].ctl_kind == PLAYER_IN_CAR ? car_get(g_players[0].ctl_id)->spr.y : player_ped()->spr.y; }

/* the police cars: cars driven by a kind-2 controller */
static int police_cars(int *nearest, int *chasing)
{
    int n = 0, best = 1 << 30;
    *chasing = 0;
    for (int i = 0; i < CAR_MAX; i++) {
        const Car *c = &g_cars[i];
        if (c->status == -1 || c->sentinel < 0 || c->sentinel >= AI_MAX) continue;
        const AiCtl *r = &g_ai[c->sentinel];
        if (r->kind != AI_KIND_POLICE || r->car != i) continue;
        n++;
        int d = abs((c->spr.x - px()) >> 22) + abs((c->spr.y - py()) >> 22);
        if (d < best) best = d;
        if (r->pursuit >= 0) (*chasing)++;
    }
    *nearest = best;
    return n;
}

static void hash_world(void)
{
    for (int i = 0; i < CAR_MAX; i++) {
        const Car *c = &g_cars[i];
        if (c->status == -1) continue;
        hash = fnv(hash, &c->spr.x, 12);
        hash = fnv(hash, &c->control, 2);
    }
    for (int i = 0; i < AI_MAX; i++) hash = fnv(hash, &g_ai[i], sizeof g_ai[i]);
    hash = fnv(hash, g_criminals, sizeof g_criminals);
    hash = fnv(hash, g_pursuits, sizeof g_pursuits);
    hash = fnv(hash, &g_players[0].wanted_points, 8);
}

/* move the player's ped (and so the camera) to pixel (x, y) on the ground */
static void move_player(int32_t x, int32_t y)
{
    Ped *p = player_ped();
    coll_remove(p, p->spr.unk20);
    p->spr.x = x, p->spr.y = y;
    p->spr.z = map_get_ground_z(g_game.map, x, y, 0x10000);
    coll_insert(COLL_PED, p->id, p, p->spr.unk20, x, y);
}

/* ---- 1. the wanted level from crime reports ---- */
static void check_wanted(void)
{
    Player *pl = &g_players[0];
    int ped = pl->ped;
    LOG("wanted: player ped %d, points %d level %d, police %s, no patrols %d\n", ped, pl->wanted_points, pl->wanted_level,
        g_mission.police_on ? "on" : "off", g_police_no_patrols);
    /* a shooting (crime 8: 100 points) twice: 200 points, NYC level 1 (151..250) */
    police_report_crime(1, ped, 8, 0, 0, 0);
    police_report_crime(1, ped, 8, 0, 0, 0);
    int c = police_find_criminal_by_ped(ped);
    LOG("  two crimes 8: points %d level %d, criminal record %d (crime %d, kind %d, units %d, pursuit %d in %d frames)\n",
        pl->wanted_points, pl->wanted_level, c, c >= 0 ? g_criminals[c].crime : -1, c >= 0 ? g_criminals[c].kind : -1,
        c >= 0 ? g_criminals[c].cops : -1, c >= 0 ? g_criminals[c].started : -9, c >= 0 ? g_criminals[c].countdown : -9);
    CHECK(pl->wanted_points == 200 && pl->wanted_level == 1, "two shootings: %d points, level %d", pl->wanted_points, pl->wanted_level);
    CHECK(c == 0 && g_criminals[0].ped == ped && g_criminals[0].kind == 1 && g_criminals[0].cops == 1, "criminal record 0");
    CHECK(g_criminals[0].started == 0 && g_criminals[0].countdown == 1, "pursuit pending");
    int reports = 0;
    for (int j = 0; j < CRIM_SIGHTINGS; j++) reports += g_criminals[0].seen[j].crime != -1;
    LOG("  scanner reports pending: %d (area %d dir %d sample %#x in %d)\n", reports, g_criminals[0].seen[0].area, g_criminals[0].seen[0].dir,
        g_criminals[0].seen[0].crime, g_criminals[0].seen[0].timer);
    CHECK(reports == 1, "one scanner report for the area");
    /* the thresholds: 251 level 2, 351 level 3, 501 level 4 */
    player_add_wanted_points(ped, 51);
    police_report_crime(1, ped, 7, 0, 0, 0);   /* +1: 252 */
    CHECK(pl->wanted_level == 2 && g_criminals[0].cops == 2, "level 2 at %d points (level %d)", pl->wanted_points, pl->wanted_level);
    player_add_wanted_points(ped, 100);
    police_report_crime(1, ped, 7, 0, 0, 0);   /* 353 */
    CHECK(pl->wanted_level == 3 && g_criminals[0].cops == 3, "level 3 at %d points (level %d)", pl->wanted_points, pl->wanted_level);
    LOG("  -> %d points: level %d, %d units\n", pl->wanted_points, pl->wanted_level, g_criminals[0].cops);
    CHECK(police_cops_for_wanted(0) == 2, "Police_CopsForWanted at level 3");
}

/* ---- 2. the pursuit: units sent, cops chasing ---- */
static void check_pursuit(int frames)
{
    int near0, chasing;
    int n0 = police_cars(&near0, &chasing);
    LOG("pursuit: %d police cars (%d patrols listed), nearest %d blocks\n", n0, g_police_ncars, near0);
    int best = near0, maxchase = 0, started_at = -1, out_of_car = 0;
    for (int f = 1; f <= frames; f++) {
        step();
        hash_world();
        int nearest, n = police_cars(&nearest, &chasing);
        if (g_pursuits[0].active > 0 && started_at < 0) {
            started_at = f;
            LOG("  frame %d: pursuit 0 running: criminal %d, %d cops, dispatch list %d\n", f, g_pursuits[0].criminal,
                g_pursuits[0].ncops, g_police_ndispatch);
        }
        if (chasing > maxchase) maxchase = chasing;
        if (nearest < best) best = nearest;
        for (int i = 0; i < AI_MAX; i++) out_of_car += g_ai[i].kind == AI_KIND_POLICE && g_ai[i].u20 != 0;
        if (f % 100 == 0) {
            LOG("  frame %d: %d police cars, %d in the pursuit, nearest %d blocks; wanted %d (%d points); group %d cops, lead %d\n", f,
                n, chasing, nearest, g_players[0].wanted_level, g_players[0].wanted_points, g_pursuits[0].ncops, g_pursuits[0].lead);
            for (int i = 0; i < AI_MAX && verbose; i++) {
                const AiCtl *r = &g_ai[i];
                if (r->kind != AI_KIND_POLICE || r->pursuit < 0) continue;
                const Car *c = r->car >= 0 ? &g_cars[r->car] : NULL;
                printf("    cop %d: state %d sub %d car %d at (%d, %d) speed %d dest (%d, %d)\n", i, r->state, r->sub_state, r->car,
                       c ? c->spr.x >> 22 : -1, c ? c->spr.y >> 22 : -1, c ? c->speed : 0, r->dest_x, r->dest_y);
            }
        }
        if (f == 60 || f == frames / 2 || f == frames) {
            char name[32];
            snprintf(name, sizeof name, "pursuit%04d", f);
            shot(name);
        }
    }
    LOG("  pursuit started at frame %d; at most %d cars chasing; nearest %d blocks (from %d); cop-frames out of a car %d\n", started_at,
        maxchase, best, near0, out_of_car);
    CHECK(started_at > 0, "no pursuit started");
    CHECK(maxchase > 0, "no police car joined the pursuit");
    CHECK(best < near0 || best <= 2, "the police didn't come closer (%d -> %d)", near0, best);
}


/* ---- 3. the arrest: cops on foot reach the player and bust him (Player_Busted, the respawn) ---- */
static void check_bust(int frames)
{
    /* A criminal who stands still is hard to reach: the cops chase by greedy steering (the lead is
       always the nearest cop, Police_UpdatePursuits), and the one-way streets around the start keep
       them circling. Running is what brings them: once the criminal is 12 blocks from where a cop
       was sent, the unseen cop is warped there (Sentinel_DriveCar). So the player runs (is moved)
       between two blocks 13 apart, then waits. */
    static const int spot[2][2] = { { 106, 119 }, { 106, 132 } };
    /* from wanted level 3 the cops shoot (pursuit +0x36): at level 2 they come to arrest */
    player_clear_wanted_points(g_players[0].ped);
    player_add_wanted_points(g_players[0].ped, 260);
    LOG("bust: the player on foot at (%d, %d), wanted level %d\n", px() >> 22, py() >> 22, g_players[0].wanted_level);
    int32_t x0 = 0, y0 = 0;
    int busted = -1, respawn = -1, on_foot = 0;
    for (int f = 1; f <= frames; f++) {
        if (busted < 0 && f % 250 == 1) {
            const int *b = spot[(f / 250) & 1];
            move_player(b[0] * 0x400000 + 0x200000, b[1] * 0x400000 + 0x200000);
        }
        step();
        hash_world();
        if (verbose && f % 100 == 0)
            for (int i = 0; i < AI_MAX; i++) {
                const AiCtl *r = &g_ai[i];
                if (r->kind != AI_KIND_POLICE || r->pursuit < 0) continue;
                const Car *c = r->car >= 0 ? &g_cars[r->car] : NULL;
                printf("    f%d cop %d: state %#x sub %#x car (%d, %d) speed %d dist %d foot %d; player (%d, %d)\n", f, i, r->state,
                       r->sub_state, c ? c->spr.x >> 22 : -1, c ? c->spr.y >> 22 : -1, c ? c->speed : 0, r->u46, r->foot_ped,
                       px() >> 22, py() >> 22);
            }
        for (int i = 0; i < AI_MAX; i++) on_foot += g_ai[i].kind == AI_KIND_POLICE && g_ai[i].foot_ped >= 0;
        if (busted < 0 && g_players[0].bust != 0) {
            busted = f;
            x0 = px(), y0 = py();
            LOG("  frame %d: BUSTED at (%d, %d); wanted level %d\n", f, px() >> 22, py() >> 22, g_players[0].wanted_level);
            shot("busted");
        }
        if (busted > 0 && respawn < 0 && g_players[0].bust == 0) {
            respawn = f;
            LOG("  frame %d: respawned at (%d, %d) (police station block %06x), wanted level %d points %d\n", f, px() >> 22,
                py() >> 22, g_player_respawn_block, g_players[0].wanted_level, g_players[0].wanted_points);
        }
        if (respawn > 0 && f > respawn + 40) break;
    }
    LOG("bust: busted at frame %d, respawn at %d; cop-frames on foot %d\n", busted, respawn, on_foot);
    CHECK(busted > 0, "the player wasn't busted");
    CHECK(respawn > 0 && g_players[0].wanted_level == 0 && (px() != x0 || py() != y0), "no respawn after the arrest");
    if (respawn > 0) shot("respawn");
}

/* ---- 4. an ambulance crew revives a dead ped ---- */
static void check_ambulance(int frames)
{
    int victim = -1, best = 1 << 30;
    for (int i = 0; i < 200; i++) {
        const Ped *p = &g_peds[i];
        if (p->anim == 0 || p->control != 0 || p->health <= 0 || p->car != -1 || p->u8b) continue;
        int d = abs((p->spr.x - px()) >> 16) + abs((p->spr.y - py()) >> 16);
        if (d < best) best = d, victim = i;
    }
    CHECK(victim >= 0, "no ped to kill");
    if (victim < 0) return;
    Ped *v = &g_peds[victim];
    LOG("ambulance: ped %d at (%d, %d) dies (%d pixels from the player)\n", victim, v->spr.x >> 22, v->spr.y >> 22, best);
    v->health = 0, v->state = 0x17, v->walk_x = 0, v->speed = 0;
    ambulance_request_for_ped(victim);
    CHECK(g_ambu_ncalls > 0, "no ambulance call queued");
    int crew = -1, medic = -1, revived = -1, maxstate = 0;
    for (int f = 1; f <= frames; f++) {
        step();
        hash_world();
        for (int k = 0; k < g_ambu_ncrews; k++) {
            const AiCtl *r = &g_ai[g_ambu_crews[k]];
            if (crew < 0) {
                crew = f;
                LOG("  frame %d: crew %d sent, ambulance car %d at (%d, %d)\n", f, g_ambu_crews[k], r->car,
                    g_cars[r->car].spr.x >> 22, g_cars[r->car].spr.y >> 22);
            }
            if (medic < 0 && r->foot_ped >= 0) {
                medic = f;
                LOG("  frame %d: the medic (ped %d) gets out\n", f, r->foot_ped);
                shot("medic");
            }
            if (r->state > maxstate) maxstate = r->state;
            if (verbose && f == 2000) shot("ambu2000");
            if (verbose && f % 200 == 0) {
                const Car *c = &g_cars[r->car];
                printf("  frame %d: crew %d state %#x car at (%d, %d) speed %d dest (%d, %d)\n", f, g_ambu_crews[k], r->state,
                       c->spr.x >> 22, c->spr.y >> 22, c->speed, r->dest_x, r->dest_y);
            }
        }
        if (revived < 0 && medic > 0 && v->health == 100) {
            revived = f;
            LOG("  frame %d: ped %d revived (state %d)\n", f, victim, v->state);
            shot("revived");
        }
        if (revived > 0 && f > revived + 200) break;
    }
    LOG("ambulance: crew at frame %d, medic at %d, revived at %d\n", crew, medic, revived);
    CHECK(crew > 0, "no ambulance crew");
    CHECK(revived > 0, "the ped wasn't revived");
}

/* ---- 5. a roadblock ahead of the player's car at wanted level 4 ---- */
static int roadblocks(void)
{
    int n = 0;
    for (int i = 0; i < JUNCTION_OVR_MAX; i++) n += g_junction_ovr[i].u0e == 1;
    return n;
}
static void enter_car(int n)
{
    Player *pl = &g_players[0];
    Car *c = car_get(n);
    Ped *p = ped_get(pl->ped);
    coll_remove(p, p->spr.unk20);
    c->driver = pl->ped;
    p->car = (int16_t)n;
    p->state = 7;
    c->unk88 = 1;
    pl->ctl_kind = PLAYER_IN_CAR, pl->ctl_id = n;
    pl->view_kind = CAM_TARGET_CAR, pl->view_id = n;
    car_set_physics_control(n);
    carphys_begin(n);
}
/* the nearest parked or traffic car becomes the player's (its driver removed) */
static int take_nearest_car(void)
{
    /* the nearest parked or traffic car becomes the player's */
    int car = -1, best = 1 << 30;
    for (int i = 0; i < CAR_MAX; i++) {
        const Car *c = &g_cars[i];
        if (c->status == -1 || c->model == 0x2f || c->sentinel >= 0 || c->control != 0) continue;
        int d = abs((c->spr.x - px()) >> 16) + abs((c->spr.y - py()) >> 16);
        if (d < best) best = d, car = i;
    }
    CHECK(car >= 0, "no car for the player");
    if (car < 0) return -1;
    if (g_cars[car].driver >= 0 && g_cars[car].driver != g_players[0].ped) {
        ped_get(g_cars[car].driver)->car = -1;
        ped_remove(g_cars[car].driver);
    }
    enter_car(car);
    return car;
}
static void check_roadblock(int frames)
{
    int car = take_nearest_car();
    if (car < 0) return;
    int ped = g_players[0].ped;
    player_add_wanted_points(ped, 600);
    police_report_crime(0, car, 5, 0, 0, 0);
    LOG("roadblock: the player takes car %d (model %d) at (%d, %d): wanted level %d (%d points)\n", car, g_cars[car].model,
        px() >> 22, py() >> 22, g_players[0].wanted_level, g_players[0].wanted_points);
    CHECK(g_players[0].wanted_level == 4, "wanted level 4");
    int first = -1, most = 0;
    for (int f = 1; f <= frames; f++) {
        held[K_UP] = (f / 120) % 4 != 3;   /* drive, with a pause now and then */
        step();
        hash_world();
        int n = roadblocks();
        if (n > most) most = n;
        if (n > 0 && first < 0) {
            first = f;
            for (int i = 0; i < JUNCTION_OVR_MAX; i++) {
                const JunctionOvr *j = &g_junction_ovr[i];
                if (j->u0e != 1) continue;
                LOG("  frame %d: roadblock at junction %d block (%d, %d), %d cars; the player at (%d, %d)\n", f, i, j->x, j->y, j->u5a,
                    px() >> 22, py() >> 22);
            }
        }
        if (first > 0 && f == first + 20) shot("roadblock");
        if (f == 600) shot("incar0600");
        if (verbose && f <= 3) { const Car *pc = car_get(car); printf("  car %d: status %d control %d driver %d (player ped %d) spr (%d,%d,%d) frame %d physics %d unk88 %d speed %d\n", car, pc->status, pc->control, pc->driver, g_players[0].ped, pc->spr.x>>16, pc->spr.y>>16, pc->spr.z>>16, pc->spr.frame, pc->physics, pc->unk88, pc->speed); }
        if (verbose && f % 200 == 0) {
            int nearest, chasing, nc = police_cars(&nearest, &chasing);
            printf("  frame %d: player car at (%d, %d) speed %d; %d police cars, %d chasing, nearest %d; roadblocks %d\n", f,
                   px() >> 22, py() >> 22, car_get(car)->speed, nc, chasing, nearest, n);
        }
    }
    memset(held, 0, sizeof held);
    LOG("roadblock: first at frame %d, at most %d at once\n", first, most);
    CHECK(first > 0, "no roadblock");
}

/* ---- 6. the mission-end helicopter: lands by the player, takes him, flies off, ends the level ---- */
static void check_heli(int frames)
{
    int32_t x = px(), y = py();
    heli_set_exit_target(x + 0x2800000, y);
    heli_spawn(x - 0x2000000, y - 0x1800000, 0x400000, 0x100, x + 0x400000, y, 0x400000);
    int seen[9] = { 0 }, done = -1;
    for (int f = 1; f <= frames && done < 0; f++) {
        for (int calls = 0; calls < 16; calls++) {
            input_feed_held(held);
            audio_render(NULL, 315);
            int r = game_run_step();
            if (r == GAME_STEP_DONE) { done = f; break; }
            if (r != GAME_STEP_WAIT) break;
        }
        if (g_heli.state >= 0 && g_heli.state <= 8 && !seen[g_heli.state]) {
            seen[g_heli.state] = f;
            LOG("  frame %d: heli state %d at (%d, %d, z %d) heading %d speed %d; camera kind %d; player ped state %d action %d\n", f,
                g_heli.state, g_heli.spr.x >> 22, g_heli.spr.y >> 22, g_heli.spr.z >> 16, g_heli.spr.angle, g_heli.speed,
                g_players[0].view_kind, player_ped()->state, player_ped()->u7c);
        }
        hash = fnv(hash, &g_heli.spr.x, 12);
    }
    LOG("heli: landed (state 4) at frame %d, took the player (5) at %d, at the exit (7) at %d; level end at %d\n", seen[4], seen[5],
        seen[7], done);
    CHECK(seen[4] > 0 && seen[5] > 0 && seen[7] > 0, "the heli didn't go through its states");
    CHECK(done > 0 || g_game.quit, "the level didn't end");
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
    mkdir("out/police", 0755);
    game_set_screen(W, H);

    uint32_t h[2];
    for (int run = 0; run < 2; run++) {
        verbose = run == 0;
        hash = 2166136261u;
        CHECK(start(1), "mission 1 start");
        for (int f = 0; f < 20; f++) step();
        shot("start");
        /* onto the northbound lane beside the start (x 107): police cars coming up from the south reach
           it (on the start block itself the one-way grid leads them past) */
        move_player(107 * 0x400000 + 0x200000, 124 * 0x400000 + 0x200000);
        check_wanted();
        check_pursuit(600);
        check_bust(2400);
        check_ambulance(3000);
        check_roadblock(1200);
        /* the heli: from a fresh start, with the player in a car. (On foot the walking player's ped
           copies the camera target into the controlled kind, Ped_UpdateAll -> Player_ControlViewTarget,
           and kind 5 is fatal in Player_UpdateAll, as in the original: see docs/police.md) */
        game_run_end();
        CHECK(start(1), "mission 1 restart");
        for (int f = 0; f < 20; f++) step();
        take_nearest_car();
        check_heli(1500);
        h[run] = hash;
        game_run_end();
    }
    printf("police hash %08x / %08x\n", h[0], h[1]);
    CHECK(h[0] == h[1], "two runs differ");
    printf(failures ? "police_test: %d FAILURES\n" : "police_test: ok\n", failures);
    return failures != 0;
}
