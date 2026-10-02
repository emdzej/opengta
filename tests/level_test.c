/* Level start against the real data: Game_Run's start (Mission_ReadIni, Map_Load, Style_Load, Game_Init
   with Mission_Load) for every section of MISSION.INI, the counts and invariants of the first level of
   each city (NYC 1, San Andreas 102, Vice City 202), determinism of a level start, the event queue,
   the RNGs, then frames of the in-game loop (game_run_step with the subsystems stubbed) rendered around
   the player's start to out/level/<city>_<frame>.png.
     ./build/level_test            (data root: ./game or OPENGTA_DATA) */
#include "exe.h"
#include "game/car.h"
#include "game/coll.h"
#include "game/event.h"
#include "game/game.h"
#include "game/gmath.h"
#include "game/mission.h"
#include "game/obj.h"
#include "game/ped.h"
#include "game/player.h"
#include "game/route.h"
#include "game/stubs.h"
#include "platform.h"
#include "png.h"
#include "render/camera.h"
#include "render/poly.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

enum { W = 640, H = 480 };
static uint32_t fb[W * H];
static int failures, presents;

#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: " __VA_ARGS__); printf("\n"); failures++; } } while (0)

/* platform stubs the core references (the frontend's settings file): no user files here */
uint8_t *plat_load_user_file(const char *name, size_t *size) { (void)name, (void)size; return NULL; }
bool plat_save_user_file(const char *name, const void *data, size_t size) { (void)name, (void)data, (void)size; return true; }
void plat_log(const char *msg) { printf("log: %s\n", msg); }

static void on_present(void *ctx) { (void)ctx; presents++; }

static void options(void)
{
    GameOptions o;
    game_default_options(&o);
    game_set_options(&o);
    g_game.present = on_present;
    g_game.present_ctx = NULL;
}

/* WinMain's part of a level start: Map_ClearName / Map_SetName(0, 2), Mission_SetIniSection. */
static bool start(int section)
{
    options();
    map_clear_name();
    map_set_name(NULL, 2);
    if (!mission_set_ini_section(section)) return false;
    return game_run_begin();
}

/* ---- the sections of MISSION.INI ---- */

static int sections[256], nsections;

static void find_sections(void)
{
    size_t n;
    uint8_t *t = vfs_read_all("GTADATA/MISSION.INI", &n);
    if (!t) return;
    for (size_t i = 0; i + 2 < n; i++)
        if (t[i] == '[' && (i == 0 || t[i - 1] == '\n') && t[i + 1] >= '0' && t[i + 1] <= '9' && nsections < 256)
            sections[nsections++] = atoi((const char *)t + i + 1);
    free(t);
}

/* ---- counts and invariants ---- */

static int count_labels(void)
{
    int k = 0;
    for (int i = 0; i < MISSION_LINES; i++) k += g_mission.labels[i] >= 0;
    return k;
}

static void check_tables(int section)
{
    const Mission *m = &g_mission;
    CHECK(m->nobjects > 0 && m->nobjects <= MISSION_OBJECTS, "[%d] script objects %d", section, m->nobjects);
    CHECK(m->ncommands > 0 && m->ncommands <= MISSION_COMMANDS, "[%d] commands %d", section, m->ncommands);
    int bad_op = 0, bad_label = 0, dangling = 0;
    for (int i = 0; i < m->ncommands; i++) {
        const MissionCommand *c = &m->commands[i];
        if (c->op >= 150) bad_op++;
        if (c->b > 0 && c->b < MISSION_LINES && m->labels[c->b] < 0) dangling++;
        if (c->c > 0 && c->c < MISSION_LINES && m->labels[c->c] < 0) dangling++;
    }
    for (int i = m->ncommands; i < MISSION_COMMANDS; i++) bad_op += m->commands[i].op != MISSION_OPCODE_NONE;
    for (int i = 0; i < MISSION_LINES; i++)
        if (m->labels[i] >= m->ncommands) bad_label++;
    CHECK(!bad_op, "[%d] %d commands with unknown opcodes", section, bad_op);
    CHECK(!bad_label, "[%d] %d labels past the commands", section, bad_label);
    int types_bad = 0;
    for (int i = 0; i < m->nobjects; i++) {
        int t = m->objects[i].type;
        if (!(t >= 0 && t < 72) && t != MISSION_TYPE_SPAWNED_PED) types_bad++;
    }
    CHECK(!types_bad, "[%d] %d script objects without a type", section, types_bad);
    CHECK(event_pending() == 0, "[%d] events queued at level start", section);
    if (dangling) printf("  [%d] %d branch targets name no label (left to the interpreter)\n", section, dangling);
}

/* The player's start: the first PLAYER line's block centre, standing on its lid. */
static void check_player(int section)
{
    const Mission *m = &g_mission;
    const MissionObject *po = NULL;
    for (int i = 0; i < m->nobjects; i++)
        if (m->objects[i].type == 3) { po = &m->objects[i]; break; }
    const Player *p = &g_players[0];
    CHECK(po != NULL, "[%d] no PLAYER line", section);
    if (!po) return;
    CHECK(p->ped >= PED_DRIVER_FIRST && p->ped < PED_SPECIAL_FIRST, "[%d] player ped %d", section, p->ped);
    if (p->ped < 0 || p->ped >= PED_MAX) return;
    const Ped *d = ped_get(p->ped);
    CHECK(d->anim != 0, "[%d] player ped not spawned", section);
    CHECK(d->spr.x == po->x * 0x400000 + 0x200000 && d->spr.y == po->y * 0x400000 + 0x200000,
          "[%d] player ped at (%d, %d), line says block (%d, %d)", section, d->spr.x >> 16, d->spr.y >> 16, po->x, po->y);
    CHECK(d->spr.zkey == po->z * 0x400000 - 0x10000, "[%d] player z %#x", section, d->spr.zkey);
    CHECK(p->ctl_kind == PLAYER_ON_FOOT && p->ctl_id == p->ped && p->view_kind == CAM_TARGET_PED,
          "[%d] player controls %d/%d, views %d", section, p->ctl_kind, p->ctl_id, p->view_kind);
    CHECK(d->control == 8 && d->player_ctl == 1 && d->objective == 0x25, "[%d] player ped flags", section);
    CHECK(m->player_ped[0] == p->ped, "[%d] script player ped", section);
    CHECK(p->lives == 4 && p->mult == 1 && p->score == 0, "[%d] player record reset", section);
    CHECK(g_players_ready, "[%d] Player_InitAll not done", section);
}

static void check_grid(int section)
{
    int objs = 0, gridded = 0;
    for (int i = 0; i < OBJ_MAX; i++) {
        const Obj *o = &g_objs[i];
        if ((o->state & 0xff) == 0) continue;
        objs++;
        int t = o->type;
        gridded += t != 0x10 && t != 0x41 && t != 0x42 && t != 0x47 && t != 0x48 && t != 0x49;
    }
    int n_obj = coll_count_nodes(COLL_OBJECT), n_car = coll_count_nodes(COLL_CAR), n_ped = coll_count_nodes(COLL_PED);
    CHECK(n_obj == gridded, "[%d] %d object nodes for %d gridded objects", section, n_obj, gridded);
    CHECK(n_car == cars_in_use(), "[%d] %d car nodes for %d cars", section, n_car, cars_in_use());
    CHECK(n_ped == peds_in_use(), "[%d] %d ped nodes for %d peds", section, n_ped, peds_in_use());
    int counted = 0;
    for (int y = 0; y < COLL_GRID; y++)
        for (int x = 0; x < COLL_GRID; x++) counted += g_coll_count[y][x];
    CHECK(counted == n_obj + n_car + n_ped, "[%d] cell counters %d != %d nodes", section, counted, n_obj + n_car + n_ped);
    (void)objs;
}

/* A hash of the level's state that must not depend on anything but the data. */
static uint32_t state_hash(void)
{
    uint32_t h = crc32(g_rng, sizeof g_rng);
    for (int i = 0; i < OBJ_MAX; i++) {
        const Obj *o = &g_objs[i];
        int32_t v[6] = { o->type, o->state, o->spr.x, o->spr.y, o->spr.z, o->spr.zkey };
        h ^= crc32(v, sizeof v) + (uint32_t)i * 0x9e3779b9u;
    }
    for (int i = 0; i < CAR_MAX; i++) {
        const Car *c = &g_cars[i];
        int32_t v[5] = { c->status, c->model, c->spr.x, c->spr.y, c->spr.z };
        h ^= crc32(v, sizeof v) + (uint32_t)i * 0x85ebca6bu;
    }
    for (int i = 0; i < PED_MAX; i++) {
        const Ped *p = &g_peds[i];
        int32_t v[5] = { p->anim, p->remap, p->spr.x, p->spr.y, p->spr.zkey };
        h ^= crc32(v, sizeof v) + (uint32_t)i * 0xc2b2ae35u;
    }
    h ^= crc32(&g_traffic_models, sizeof g_traffic_models);
    h ^= crc32(g_mission.commands, sizeof g_mission.commands);
    h ^= crc32(g_mission.objects, sizeof g_mission.objects);
    return h;
}

static const char *city_of(const char *cmp)
{
    return !strcasecmp(cmp, "nyc.cmp") ? "nyc" : !strcasecmp(cmp, "sanb.cmp") ? "sanb" : !strcasecmp(cmp, "miami.cmp") ? "miami" : "x";
}

static void print_level(int section)
{
    const Mission *m = &g_mission;
    int fe, ff;
    const uint8_t *end;
    route_get_counts(&fe, &ff, &end);
    const Player *p = &g_players[0];
    const Ped *d = p->ped >= 0 && p->ped < PED_MAX ? ped_get(p->ped) : NULL;
    printf("[%d] \"%s\" %s style %d: header", section, m->name, m->cmp, g_game.style->number);
    for (int i = 0; i < 6; i++) printf(" %d", m->header[i]);
    printf("\n  objects %d (map %d records), script objects %d, commands %d, labels %d, MISSION_END %d\n", objs_in_use(),
           g_obj_map_count, m->nobjects, m->ncommands, count_labels(), m->mission_end_count);
    printf("  routes: %d 0xfe + %d 0xff, %d roadblock vertices; locations: %d police, %d hospitals, %d fire; nav zones %d\n",
           fe, ff, g_roadblock_nverts, g_police_stations, g_hospitals, g_fire_stations, g_nav_count);
    printf("  cars %d (slots used up to %d), peds %d; stub calls: car spawns %d, ped creators %d, triggers %d, doors %d,"
           " cranes %d, car lists %d\n", cars_in_use(), g_cars_count, peds_in_use(), stub_calls[STUB_CAR_SPAWN],
           stub_calls[STUB_PED_CREATE], stub_calls[STUB_TRIGGER], stub_calls[STUB_DOOR], stub_calls[STUB_CRANE],
           stub_calls[STUB_CARLIST]);
    if (d)
        printf("  player 0: ped %d at block (%d, %d, %d) pixels (%d, %d) z %#x angle %d; emergency switch %d; rng %d %d %d %d %d\n",
               p->ped, d->spr.x >> 22, d->spr.y >> 22, (d->spr.zkey + 0x10000) >> 22, d->spr.x >> 16, d->spr.y >> 16,
               d->spr.zkey, d->spr.angle, g_game.opt.emergency, g_rng[0], g_rng[1], g_rng[2], g_rng[3], g_rng[4]);
}

/* ---- the frame loop ---- */

static void run_frames(const char *city)
{
    char path[128];
    poly_set_screen_rows(fb, W * 4, H);
    poly_set_clip(0, 0, W - 1, H - 1);
    static const int shots[] = { 1, 20, 60 };
    int frames = 0, waits = 0, shot = 0, calls = 0;
    presents = 0;
    while (frames < 60 && calls++ < 1000) {
        memset(fb, 0xff, sizeof fb);
        int r = game_run_step(25000);   /* 25 ms: 1.75 ticks, so about every other call waits */
        if (r == GAME_STEP_WAIT) { waits++; continue; }
        if (r == GAME_STEP_DONE) break;
        frames++;
        if (shot < 3 && frames == shots[shot]) {
            int undrawn = 0;
            for (int i = 0; i < W * H; i++) undrawn += fb[i] == 0xffffffffu;
            snprintf(path, sizeof path, "out/level/%s_%02d.png", city, frames);
            CHECK(png_write(path, fb, W, H, PNG_XRGB), "write %s", path);
            printf("  frame %d: %s (crc %08x, %d undrawn pixels; camera at (%d, %d) height %d)\n", frames, path,
                   crc32(fb, sizeof fb), undrawn, g_players[0].vp.x, g_players[0].vp.y, g_players[0].vp.height);
            CHECK(undrawn < W * H / 50, "%s: %d pixels not drawn", path, undrawn);
            shot++;
        }
    }
    printf("  %d frames, %d waits for the 70 Hz timer, %d presents, frame counter %u\n", frames, waits, presents, g_frame);
    CHECK(frames == 60 && g_frame == 60, "frame loop: %d frames, counter %u", frames, g_frame);
    CHECK(waits > 0 && presents == frames, "frame pacing: %d waits, %d presents", waits, presents);
}

/* ---- the event queue and the RNGs ---- */

static int fired[8], nfired;
static void check_events(void)
{
    event_init();
    event_schedule(2, EVENT_NONE, 1);
    event_schedule(1, EVENT_NONE, 2);
    event_schedule(2, EVENT_NONE, 3);
    CHECK(event_pending() == 3, "3 events queued");
    const EventNode *n = event_head();
    for (nfired = 0; n && n->time != 0xffffffffu; n = n->next) fired[nfired++] = (int)n->arg;
    /* sorted by due frame; on the same frame the later one first */
    CHECK(nfired == 3 && fired[0] == 2 && fired[1] == 3 && fired[2] == 1, "queue order %d %d %d", fired[0], fired[1], fired[2]);
    event_tick();   /* frame 0: nothing due */
    event_tick();   /* frame 1: arg 2 */
    CHECK(event_pending() == 2 && g_frame == 2, "after frame 1: %d pending", event_pending());
    event_tick();
    CHECK(event_pending() == 0 && g_frame == 3, "after frame 2: %d pending", event_pending());
    for (int i = 0; i < EVENT_NODES; i++) event_schedule(5, EVENT_NONE, i);
    CHECK(event_pending() == EVENT_NODES, "pool of %d", EVENT_NODES);
    printf("events: queue order and dispatch ok\n");
}

static void check_rng(void)
{
    CHECK(math_random_reset(), "RNG seeds");
    int32_t a[5];
    for (int i = 0; i < 5; i++) a[i] = math_random();
    CHECK(math_random_reset(), "RNG seeds");
    bool same = true;
    for (int i = 0; i < 5; i++) same &= math_random() == a[i];
    CHECK(same, "Math_Random not deterministic after the reset");
    g_crt_rand_seed = 1;
    int r0 = crt_rand(), r1 = crt_rand();
    CHECK(r0 == 41 && r1 == 18467, "MSVC rand: %d %d", r0, r1);   /* the well-known first values for seed 1 */
    static const int32_t at[][3] = {
        { 0x10000, 0x10000, 0x80 }, { 0x10000, 0, 0 }, { -0x10000, 0, 0x200 }, { 0, 0x10000, 0x100 }, { 0, -0x10000, 0x300 },
        { -0x10000, -0x10000, 0x280 }, { 0x10000, -0x10000, 0x380 }, { -0x10000, 0x10000, 0x180 },
    };
    for (size_t i = 0; i < sizeof at / sizeof *at; i++) {
        int a = math_atan2(at[i][0], at[i][1]);
        CHECK(a == at[i][2], "atan2(%d, %d) = %#x, want %#x", at[i][0] >> 16, at[i][1] >> 16, a, at[i][2]);
    }
    printf("rng: Math_Random after reset %d %d %d %d %d; MSVC rand %d %d\n", a[0], a[1], a[2], a[3], a[4], r0, r1);
}

int main(void)
{
    char err[256];
    if (!vfs_mount_default()) { printf("SKIP: no data root\n"); return 0; }
    if (!exe_init(err, sizeof err)) { printf("SKIP: %s\n", err); return 0; }
    math_init_tables();
    if (!camera_init_tables()) { printf("FAIL: camera tables\n"); return 1; }
    mkdir("out", 0755);
    mkdir("out/level", 0755);
    poly_set_screen_rows(fb, W * 4, H);
    poly_set_clip(0, 0, W - 1, H - 1);

    check_rng();
    game_set_screen(W, H);   /* the frontend's mode */

    /* every section of MISSION.INI */
    find_sections();
    CHECK(nsections > 0, "no sections found");
    printf("MISSION.INI: %d sections\n", nsections);
    for (int i = 0; i < nsections; i++) {
        int s = sections[i];
        if (!start(s)) { CHECK(0, "[%d] start failed", s); continue; }
        printf("  [%d] %-26s %-9s objects %4d, script objects %4d, commands %4d, cars %3d, peds %3d\n", s,
               g_mission.name, g_mission.cmp, objs_in_use(), g_mission.nobjects, g_mission.ncommands, cars_in_use(),
               peds_in_use());
        check_tables(s);
        check_grid(s);
        if (s < 1000) check_player(s);
        game_run_end();
    }

    /* the first level of each city, in detail, with frames */
    static const int firsts[] = { 1, 102, 202 };
    for (int i = 0; i < 3; i++) {
        int s = firsts[i];
        if (!start(s)) { CHECK(0, "[%d] start failed", s); continue; }
        print_level(s);
        uint32_t h1 = state_hash();
        if (s == 1) {   /* boxes around the player's start: overlapping, apart */
            const Ped *d = ped_get(g_players[0].ped);
            CollBox a, b, c;
            coll_build_box(d->spr.x, d->spr.y, d->spr.z, 4, 4, d->spr.angle, 10, &a);
            coll_build_box(d->spr.x + 0x30000, d->spr.y, d->spr.z, 8, 16, 0x40, 0, &b);
            coll_build_box(d->spr.x + 0x400000, d->spr.y, d->spr.z, 8, 16, 0x40, 0, &c);
            CHECK(coll_box_vs_box(&a, &b) == 1 && coll_box_vs_box(&a, &c) == -1, "box tests");
            CHECK(a.gz[0] == 0xff0000 - 1, "ground under the player %#x (layer 3's bottom face - 1)", a.gz[0]);
            CollHit *h = coll_query_block_all(d->spr.x, d->spr.y, d->spr.zkey, COLL_PED);
            int peds = 0;
            for (; h; h = h->next) peds += h->owner == d;
            coll_unlock();
            CHECK(peds == 1, "the player's ped not found around its block");
            printf("  boxes: overlap %d, apart %d; ground under the player %#x; block query finds the player\n",
                   coll_box_vs_box(&a, &b), coll_box_vs_box(&a, &c), a.gz[0]);
        }
        run_frames(city_of(g_mission.cmp));
        game_run_end();
        CHECK(start(s), "[%d] restart", s);
        uint32_t h2 = state_hash();
        CHECK(h1 == h2, "[%d] level start not deterministic: %08x %08x", s, h1, h2);
        printf("  level start state hash %08x (same on restart)\n", h2);
        game_run_end();
    }

    check_events();
    printf(failures ? "FAIL (%d)\n" : "PASS\n", failures);
    return failures != 0;
}
