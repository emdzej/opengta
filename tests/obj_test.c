/* Objects, explosions, fires, power-ups, block animations and map edits against the real data: mission 1
   started as Game_Run does, then the in-game loop (game_run_step, paced by the audio as in the app).
   Checks the map edits (copy-on-write, the type cache, Map_IsFaceSolid / Map_IsCovered), a kicked
   object sliding to rest, a rocket fired at a parked car (the projectile flies, the car is wrecked, the
   explosion's slots animate and free themselves, a fire burns on the wreck), the blast killing a ped,
   a power-up (crate, reveal, collected by walking the player onto it), a door opened through its
   block animation (the face's tile animates, the completion event fires), and determinism (the whole
   script twice gives the same hash). Renders frames to out/obj/.
     ./build/obj_test            (data root: ./game or OPENGTA_DATA) */
#include "audio/audio.h"
#include "exe.h"
#include "game/blockanim.h"
#include "game/car.h"
#include "game/coll.h"
#include "game/event.h"
#include "game/expl.h"
#include "game/fire.h"
#include "game/game.h"
#include "game/gmath.h"
#include "game/input.h"
#include "game/mapedit.h"
#include "game/mission.h"
#include "game/obj.h"
#include "game/ped.h"
#include "game/player.h"
#include "game/powerup.h"
#include "game/proj.h"
#include "game/trigger.h"
#include "game/weapon.h"
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
static bool shots = true;

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
enum { K_UP = 0x148 };

static void frame(void)
{
    for (int calls = 0; calls < 16; calls++) {
        input_feed_held(held);
        audio_render(NULL, 315);
        if (game_run_step() != GAME_STEP_WAIT) return;
    }
    CHECK(0, "game_run_step never ran a frame");
}
static void frames(int n) { while (n-- > 0) frame(); }

static void shot(const char *name)
{
    if (!shots) return;
    char path[128];
    snprintf(path, sizeof path, "out/obj/%s.png", name);
    CHECK(png_write(path, fb, W, H, PNG_XRGB), "write %s", path);
    printf("  %s\n", path);
}

static Ped *me(void) { return ped_get(g_players[0].ped); }

/* the camera straight onto what the player controls */
static void snap_camera(void)
{
    CameraPlayer cp;
    player_camera(0, &cp);
    CameraWorld w = player_camera_world(0);
    camera_snap(&cp, &w);
    player_camera_store(0, &cp);
}

/* the player's ped to (x, y) on the ground there, facing angle (a test helper) */
static void place_me_at(int32_t x, int32_t y, int32_t zstart, int angle)
{
    Ped *p = me();
    coll_remove(p, p->spr.unk20);
    p->spr.x = x, p->spr.y = y;
    p->spr.z = map_get_ground_z(g_game.map, x, y, zstart);
    p->spr.angle = (int16_t)angle;
    p->speed = 0;
    coll_insert(COLL_PED, p->id, p, p->spr.unk20, x, y);
    snap_camera();
}
static void place_me(int32_t x, int32_t y, int angle) { place_me_at(x, y, 0x400000, angle); }

static int count_type(int type, int owner_kind, int owner)
{
    int n = 0;
    for (int i = 0; i < OBJ_MAX; i++) {
        const Obj *o = &g_objs[i];
        if ((o->state & 0xff) && o->type == type && (owner < 0 || (o->owner == owner && o->attach_kind == owner_kind))) n++;
    }
    return n;
}

static int list_len(const Obj *o)
{
    int n = 0;
    for (; o && n < OBJ_MAX; o = o->next) n++;
    return n;
}

/* ---- map edits ---- */
static void check_map_edits(void)
{
    Map *m = g_game.map;
    /* a road block next to the player's start, with a building block somewhere near */
    const Ped *p = me();
    int bx = p->spr.x >> 22, by = p->spr.y >> 22, bz = -1;
    for (int z = 0; z < MAP_Z; z++)
        if ((m->type_cache[z][by][bx] & 0x70) != 0) { bz = z; break; }
    CHECK(bz >= 0, "no block under the player");
    if (bz < 0) return;
    const MapBlock *before = map_get_block(m, bx, by, bz);
    int used_b = m->chg_block_used, used_c = m->chg_column_used;   /* (the doors made edits already) */
    uint16_t t0 = before->type_map;
    int lid0 = before->lid;
    printf("map edits: block (%d, %d, %d) type map %#x lid %d, %d / %d bytes of the change areas used\n", bx, by, bz, t0, lid0,
           m->chg_block_used, m->chg_column_used);
    /* first edit: column and block copied */
    map_set_block_face(bx, by, bz, 4, 7);
    const MapBlock *b = map_get_block(m, bx, by, bz);
    CHECK(b != before && (uint8_t *)b >= m->data_end, "the edited block isn't a copy");
    CHECK(b->lid == 7 && before->lid == lid0 && b->type_map == t0, "face edit: lid %d (original %d)", b->lid, before->lid);
    CHECK(m->chg_block_used == used_b + 8 && m->chg_column_used == used_c + 2 * (7 - map_column(m, bx, by)[0]),
          "change areas %d / %d", m->chg_block_used, m->chg_column_used);
    /* second edit of the same block: in place */
    map_set_block_face(bx, by, bz, 4, lid0);
    CHECK(map_get_block(m, bx, by, bz) == b && m->chg_block_used == used_b + 8, "second edit not in place");
    /* the kind: replaced in a copy made before (the first-edit OR is checked on another block) */
    map_set_block_kind(bx, by, bz, 0x50);
    CHECK((b->type_map & 0x70) == 0x50 && (m->type_cache[bz][by][bx] & 0x70) == 0x50, "kind 0x50: %#x cache %#x", b->type_map,
          m->type_cache[bz][by][bx]);
    map_set_block_type(bx, by, bz, t0 | (uint32_t)before->ext << 16);
    CHECK(b->type_map == t0 && m->type_cache[bz][by][bx] == ((t0 & 0x7f) | (t0 & 0x3f00 ? 0x80 : 0)), "type restored");
    /* first edit of a fresh block with Map_SetBlockKind ORs the bits in */
    int ox = bx + 1;
    const MapBlock *ob = map_get_block(m, ox, by, bz);
    if (ob && (uint8_t *)ob < m->data_end) {
        uint16_t ot = ob->type_map;
        map_set_block_kind(ox, by, bz, 0x10);
        const MapBlock *nb = map_get_block(m, ox, by, bz);
        CHECK(nb->type_map == (ot | 0x10), "first Map_SetBlockKind: %#x, expected %#x | 0x10", nb->type_map, ot);
        printf("  Map_SetBlockKind 0x10 on a fresh block of type map %#x: %#x (ORed)\n", ot, nb->type_map);
        map_set_block_type(ox, by, bz, ot | (uint32_t)ob->ext << 16);
    }
    map_or_block_flags(bx, by, bz, 0x80);
    CHECK(b->type_map == (t0 | 0x80), "or flags");
    map_set_block_type(bx, by, bz, t0 | (uint32_t)before->ext << 16);
    /* covered / face solid around the player and a building */
    int solid = 0, covered = 0;
    for (int y = by - 8; y <= by + 8; y++)
        for (int x = bx - 8; x <= bx + 8; x++) {
            for (int f = 0; f < 4; f++) solid += map_is_face_solid(m, x, y, bz, f);
            covered += map_covered(m, x * 0x400000 + 0x200000, y * 0x400000 + 0x200000, bz * 0x400000 + 0x3f0000);
        }
    printf("  around the player (17 x 17 blocks, layer %d): %d solid side faces, %d covered positions\n", bz, solid, covered);
    CHECK(solid > 0, "no solid faces near the player");
}

/* ---- a kicked object ---- */
static void check_kick(void)
{
    const Ped *p = me();
    int best = -1;
    int64_t bd = 0;
    for (int i = 0; i < OBJ_MAX; i++) {
        const Obj *o = &g_objs[i];
        if (!(o->state & 0xff) || o->owner != -1 || o->speed) continue;
        const ObjInfo *in = g_obj_infos[o->type];
        if (in->status != 0 || o->weight == 3 || o->type == 0x16 || o->type == 0x21 || o->type == 0x22) continue;
        int64_t dx = (o->spr.x - p->spr.x) >> 16, dy = (o->spr.y - p->spr.y) >> 16, d = dx * dx + dy * dy;
        if (best < 0 || d < bd) best = i, bd = d;
    }
    CHECK(best >= 0, "no object to kick");
    if (best < 0) return;
    Obj *o = &g_objs[best];
    int32_t x0 = o->spr.x, y0 = o->spr.y;
    obj_kick(0, 0, best, 10, 0x100);
    printf("kick: object %d (type %#x, weight %d) at (%d, %d): speed %d heading %#x state %d, moving list %d\n", best, o->type,
           o->weight, x0 >> 16, y0 >> 16, o->speed, o->heading, o->state, list_len(g_obj_moving_list));
    CHECK(o->speed > 0 && list_len(g_obj_moving_list) >= 1, "the kick didn't start it");
    int f = 0;
    while (f < 100 && o->speed > 0) frame(), f++;
    printf("  at rest after %d frames at (%d, %d) (%d pixels), state %d, moving list %d\n", f, o->spr.x >> 16, o->spr.y >> 16,
           (o->spr.x - x0) >> 16, o->state, list_len(g_obj_moving_list));
    CHECK(o->speed == 0 && f < 100, "the object didn't come to rest");
    CHECK(o->spr.x != x0 || o->spr.y != y0 || (o->state & 0xff) == 0, "the object didn't move");
}

/* ---- a rocket at a parked car ---- */
static int nearest_car(int32_t x, int32_t y)
{
    int best = -1;
    int64_t bd = 0;
    for (int i = 0; i < g_cars_count; i++) {
        const Car *c = car_get(i);
        if (c->status == -1 || c->model == 0x2f || c->damage >= 100 || c->driver != -1) continue;
        int64_t dx = (c->spr.x - x) >> 16, dy = (c->spr.y - y) >> 16, d = dx * dx + dy * dy;
        if (best < 0 || d < bd) best = i, bd = d;
    }
    return best;
}

static uint32_t check_rocket(void)
{
    int n = nearest_car(me()->spr.x, me()->spr.y);
    CHECK(n >= 0, "no parked car");
    if (n < 0) return 0;
    Car *c = car_get(n);
    /* stand 160 pixels from it on the side with the most road, facing it */
    static const int dirs[4][2] = { { 0, 1 }, { 0, -1 }, { 1, 0 }, { -1, 0 } };
    int d = 0;
    for (int k = 0; k < 4; k++) {
        int32_t x = c->spr.x + dirs[k][0] * 0xa00000, y = c->spr.y + dirs[k][1] * 0xa00000;
        int z = c->spr.z >> 22;
        if ((g_game.map->type_cache[z][y >> 22][x >> 22] & 0x70) != 0x50) { d = k; break; }
    }
    int32_t px = c->spr.x + dirs[d][0] * 0xa00000, py = c->spr.y + dirs[d][1] * 0xa00000;
    int a = math_atan2(c->spr.y - py, c->spr.x - px);
    place_me(px, py, a);
    frames(2);
    Ped *p = me();
    printf("rocket: car %d (model %d) at (%d, %d) damage %d; the player at (%d, %d) facing %#x\n", n, c->model, c->spr.x >> 16,
           c->spr.y >> 16, c->damage, p->spr.x >> 16, p->spr.y >> 16, p->spr.angle);
    int before = g_proj.count;
    weapon_fire_rocket(p);
    CHECK(g_proj.count == before + 1, "no rocket (%d projectiles)", g_proj.count);
    int rocket = g_proj.count ? g_proj.id[g_proj.count - 1] : -1;
    if (rocket >= 0) printf("  rocket object %d (type %#x) at (%d, %d)\n", rocket, g_objs[rocket].type, g_objs[rocket].spr.x >> 16,
                            g_objs[rocket].spr.y >> 16);
    int hit = -1, maxexpl = 0;
    for (int f = 0; f < 40; f++) {
        frame();
        int e = expl_active();
        if (e > maxexpl) maxexpl = e;
        if (hit < 0 && c->damage >= 100) {
            hit = f;
            shot("rocket_hit");
        }
        if (f == 3) shot("rocket_03");
        if (hit >= 0 && f == hit + 6) shot("rocket_hit06");
    }
    printf("  car hit on frame %d, damage %d, %d explosion slots at most, %d left; burning %d, %d fires on it\n", hit, c->damage,
           maxexpl, expl_active(), c->burning, count_type(0x12, 1, n));
    CHECK(hit >= 0 && c->damage >= 100, "the rocket didn't wreck the car (damage %d)", c->damage);
    CHECK(maxexpl >= 4, "no explosion (%d slots)", maxexpl);
    CHECK(count_type(0x12, 1, n) + count_type(0x13, 1, n) >= 1 || c->burning > 0, "no fire on the wreck");
    shot("rocket_40");
    frames(60);
    printf("  60 frames later: %d explosion slots, smoke %d, fires %d / %d (dying), burning %d\n", expl_active(),
           count_type(0x33, 0, -1) + count_type(10, 0, -1), count_type(0x12, 0, -1), count_type(0x13, 0, -1), c->burning);
    CHECK(expl_active() == 0, "explosion slots never freed (%d)", expl_active());
    shot("rocket_100");
    return (uint32_t)c->damage * 31 + (uint32_t)hit;
}

/* ---- the blast on a ped ---- */
static void check_blast(void)
{
    /* the nearest live ambient ped */
    const Ped *pl = me();
    int best = -1;
    int64_t bd = 0;
    for (int i = 0; i < PED_DRIVER_FIRST; i++) {
        const Ped *p = ped_get(i);
        if (!p->anim || p->health <= 0 || p->id == pl->id || p->state == 7) continue;
        int64_t dx = (p->spr.x - pl->spr.x) >> 16, dy = (p->spr.y - pl->spr.y) >> 16, dd = dx * dx + dy * dy;
        if (best < 0 || dd < bd) best = i, bd = dd;
    }
    if (best < 0) {
        printf("blast: no ambient ped (skipped)\n");
        return;
    }
    Ped *q = ped_get(best);
    expl_create(q->spr.x + 0x50000, q->spr.y, q->spr.z, 0);
    printf("blast: ped %d at (%d, %d): health %d anim %#x state %d after an explosion 5 pixels away\n", best, q->spr.x >> 16,
           q->spr.y >> 16, q->health, q->anim, q->state);
    CHECK(q->health == 0, "the ped survived the blast (health %d)", q->health);
    frames(30);
}

/* ---- a power-up ---- */
static void check_powerup(void)
{
    Player *pl = &g_players[0];
    Ped *p = me();
    /* a crate 2 blocks ahead of a clear spot: the player's position, offset along +y */
    int32_t x = p->spr.x, y = p->spr.y + 0x200000, z = p->spr.z - 1;
    int k0 = powerups_in_use();
    int ok = powerup_add(3, 0, x, y, z);   /* rocket launcher, default ammo */
    CHECK(ok == 1 && powerups_in_use() == k0 + 1, "PowerUp_Add");
    int slot = -1;
    for (int i = 0; i < POWERUP_MAX; i++)
        if (g_powerups[i].x == x && g_powerups[i].y == y && g_powerups[i].obj != -1) slot = i;
    if (slot < 0) return;
    PowerUp *u = &g_powerups[slot];
    printf("power-up %d: type %d, crate object %d (type %#x), visible %d\n", slot, u->type, u->obj, g_objs[u->obj].type, u->visible);
    CHECK(g_objs[u->obj].type == 0x54 && u->visible == 1, "not a crate");
    CHECK(powerup_exists_at(x, y), "PowerUp_ExistsAt");
    powerup_reveal(x, y);
    printf("  revealed: object %d (type %#x), visible %d\n", u->obj, u->obj >= 0 ? g_objs[u->obj].type : -1, u->visible);
    CHECK(u->obj >= 0 && g_objs[u->obj].type == 0x50 && u->visible == 2, "the reveal");
    int ammo0 = pl->ammo[2];
    /* walk onto it */
    place_me(x, y - 0x180000, 0);
    frame();
    shot("powerup_before");
    held[K_UP] = 1;
    int f = 0;
    while (f < 60 && powerup_exists_at(x, y)) frame(), f++;
    held[K_UP] = 0;
    printf("  collected after %d frames walking: rocket ammo %d -> %d, record obj %d\n", f, ammo0, pl->ammo[2], u->obj);
    CHECK(!powerup_exists_at(x, y) && pl->ammo[2] > ammo0, "not collected (ammo %d)", pl->ammo[2]);
    shot("powerup_after");
    /* the others, collected directly */
    int mult = pl->mult, lives = pl->lives;
    powerup_add(11, 0, x, y, z);
    powerup_reveal(x, y);
    powerup_collect(0, x, y, 2);
    powerup_add(10, 0, x, y, z);
    powerup_reveal(x, y);
    powerup_collect(0, x, y, 2);
    powerup_add(12, 0, x, y, z);
    powerup_reveal(x, y);
    powerup_collect(0, x, y, 2);
    printf("  multiplier %d -> %d, armour %d, jail free %d, lives %d\n", mult, pl->mult, pl->armour, pl->jail_free, lives);
    CHECK(pl->mult == mult + 1 && pl->armour == 3 && pl->jail_free == 1, "multiplier / armour / jail free");
    powerup_add(10, 0, x, y, z);
    powerup_reveal(x, y);
    powerup_collect(0, x, y, 2);
    CHECK(powerup_exists_at(x, y), "full armour still took the armour");
    CHECK(powerup_remove_at(x, y) && !powerup_exists_at(x, y), "PowerUp_RemoveAt");
}

/* ---- a door ---- */
static void check_door(void)
{
    const Style *s = g_game.style;
    int door = -1;
    for (int i = 0; i < g_mrt.ndoors; i++)
        if (door_get(i)->state == DOOR_CLOSED || door_get(i)->state == DOOR_OPEN) { door = i; break; }
    CHECK(door >= 0, "no door in mission 1 (%d doors)", g_mrt.ndoors);
    if (door < 0) return;
    Door *d = door_get(door);
    const BlockAnim *a = &g_blockanims[d->anim];
    int tile = a->tile;
    const int16_t *remap = a->which ? s->lid_remap : s->side_remap;
    printf("door %d at block (%d, %d, %d) orient %d, %d frames from tile %d, state %d; animation %d on %s tile %d shows %d\n", door,
           d->x, d->y, d->z, d->orient, d->frames, d->tile, d->state, d->anim, a->which ? "lid" : "side", tile, remap[tile]);
    /* stand in front of it (2 blocks out on the side its face looks at: +x for orient 1, +y for 3, on
       the door's layer) and open it */
    int ox = d->orient == 1 ? 2 : 0, oy = d->orient == 3 ? 2 : d->orient == 1 ? 0 : 1;
    place_me_at((d->x + ox) * 0x400000 + 0x200000, (d->y + oy) * 0x400000 + 0x200000, d->z * 0x400000, 0x300);
    frame();
    shot("door_closed");
    int r0 = remap[tile];
    door_open(door);
    CHECK(a->active == 1, "the door's animation didn't start");
    int f = 0, changes = 0, last = remap[tile];
    while (f < 400 && a->active) {
        frame(), f++;
        if (remap[tile] != last) changes++, last = remap[tile];
        if (f == 30) shot("door_opening");
    }
    printf("  opened: %d frames, the tile changed %d times (%d -> %d), door state %d, animation index %d of %d\n", f, changes, r0,
           remap[tile], d->state, a->index, a->nframes);
    CHECK(!a->active && d->state == DOOR_OPEN, "the door didn't finish opening (state %d)", d->state);
    CHECK(changes >= 2 && remap[tile] != r0, "the face didn't animate");
    shot("door_open");
    door_close(door);
    f = 0;
    while (f < 400 && a->active) frame(), f++;
    printf("  closed again after %d frames, state %d, tile shows %d\n", f, d->state, remap[tile]);
    CHECK(d->state == DOOR_CLOSED, "the door didn't close");
}

/* the state of the simulation: objects in use, cars, peds, power-ups, explosions, RNG */
static uint32_t state_hash(void)
{
    uint32_t h = crc32(g_rng, sizeof g_rng);
    for (int i = 0; i < OBJ_MAX; i++) {
        const Obj *o = &g_objs[i];
        if (!(o->state & 0xff)) continue;
        int32_t v[7] = { i, o->type, o->state, o->spr.x, o->spr.y, o->spr.z, o->speed };
        h = crc32(v, sizeof v) ^ (h * 31);
    }
    for (int i = 0; i < CAR_MAX; i++) {
        const Car *c = &g_cars[i];
        int32_t v[6] = { c->status, c->model, c->spr.x, c->spr.y, c->damage, c->burning };
        h = crc32(v, sizeof v) ^ (h * 31);
    }
    for (int i = 0; i < PED_MAX; i++) {
        const Ped *p = &g_peds[i];
        int32_t v[4] = { p->anim, p->spr.x, p->spr.y, p->health };
        h = crc32(v, sizeof v) ^ (h * 31);
    }
    h ^= crc32(g_powerups, sizeof g_powerups);
    for (int i = 0; i < EXPL_SLOTS; i++) h = (h * 31) ^ (uint32_t)g_expl[i].frame;
    return h;
}

static uint32_t run_all(void)
{
    if (!start(1)) {
        CHECK(0, "mission 1 start");
        return 0;
    }
    printf("mission 1: %d objects, %d animated, %d status 7, %d attached, %d power-ups, %d doors, %d block animations\n", objs_in_use(),
           list_len(g_obj_anim_list), list_len(g_obj_status7_list), list_len(g_obj_attached_list), powerups_in_use(), g_mrt.ndoors,
           g_blockanim_count);
    frames(5);
    snap_camera();
    frame();
    shot("start");
    check_map_edits();
    check_kick();
    uint32_t r = check_rocket();
    check_blast();
    check_powerup();
    check_door();
    uint32_t h = state_hash() ^ r;
    printf("state hash %08x\n", h);
    game_run_end();
    return h;
}

int main(void)
{
    char err[256];
    if (!vfs_mount_default()) { printf("SKIP: no data root\n"); return 0; }
    if (!exe_init(err, sizeof err)) { printf("SKIP: %s\n", err); return 0; }
    math_init_tables();
    if (!camera_init_tables()) { printf("FAIL: camera tables\n"); return 1; }
    mkdir("out", 0755);
    mkdir("out/obj", 0755);
    game_set_screen(W, H);

    uint32_t h1 = run_all();
    shots = false;
    uint32_t h2 = run_all();
    CHECK(h1 == h2, "not deterministic: %08x / %08x", h1, h2);
    printf(failures ? "obj_test: %d FAILURES\n" : "obj_test: ok\n", failures);
    return failures != 0;
}
