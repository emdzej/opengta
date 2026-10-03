/* Traffic lights 0x47dcf0-0x480e10 (the lights part; Rail_* is rail.c's) and the junction override
   table 0x505f00 (lights.h, docs/traffic.md). The two exe tables, the light phases 0x4b3514 and the
   junction templates 0x4b3520, are read at runtime. */
#include "lights.h"
#include "../exe.h"
#include "../map.h"
#include "coll.h"
#include "game.h"
#include "obj.h"
#include "stubs.h"
#include <string.h>

LightsState g_lights;
JunctionOvr g_junction_ovr[JUNCTION_OVR_MAX];

/* the arm being built (0x77d3d8, 13 bytes each) */
typedef struct {
    uint8_t dir;                /* +0 0 -y, 1 +y, 4 +x, 5 -x (6 ends the list; 3 / 1 forced for crossings) */
    uint8_t u1;
    uint8_t turn;               /* +2 the road's direction bits change along the arm */
    uint8_t sx, sy, sz;         /* +3 the first road block (later: the light's block) */
    uint8_t mx, my, mz;         /* +6 the block where the direction bits changed */
    uint8_t ex, ey, ez;         /* +9 the first block past the road */
    uint8_t len;                /* +0xc */
} LightArm;
static LightArm arms[LIGHTS_ARMS + 1];

#define L g_lights

static const uint8_t *phase_table(void)    /* 6 x {colour state, duration} */
{
    const uint8_t *p = exe_data(0x4b3514, 2 * LIGHTS_PHASES);
    if (!p) game_fatal(-2, 0, 0x4b3514);
    return p;
}
static const uint8_t *template_table(void) /* 11 x {kind, 10 arm directions, 10 orientations} */
{
    const uint8_t *p = exe_data(0x4b3520, 21 * LIGHTS_TEMPLATES);
    if (!p) game_fatal(-2, 0, 0x4b3520);
    return p;
}
static uint8_t phase_state(int phase) { return phase_table()[2 * phase]; }

static bool attr(int what, int x, int y, int z) { return map_test_block_attr(what, (uint8_t)x, (uint8_t)y, (uint8_t)z) != 0; }

/* the probe every function repeats: the first layer of (x, y) with a junction block (its layer to
   cur_z, 0x77d0c3) */
static bool junction_block(uint8_t x, uint8_t y)
{
    for (int z = 0; z < 6; z++)
        if (attr(2, x, y, z)) {
            L.cur_z = (uint8_t)z;
            return true;
        }
    return false;
}
/* the cell table lookup (x and y only); the light of the first match through *light */
static bool cell_known(uint8_t x, uint8_t y, uint8_t *light)
{
    for (int i = 0; i < L.ncells; i++)
        if (L.cells[i].x == x && L.cells[i].y == y) {
            if (light) *light = L.cells[i].light;
            return true;
        }
    return false;
}
static void add_cell(uint8_t x, uint8_t y, uint8_t z, uint8_t light)
{
    /* (Lights_TraceArm adds cells unchecked; the original's limit test is only in Lights_AddJunction) */
    if (L.ncells >= LIGHTS_CELLS) game_fatal(-0xa5, 0xcf, L.ncells);
    LightCell *c = &L.cells[L.ncells];
    c->x = x, c->y = y, c->z = z, c->light = light;
}

/* the group walks: the first light of the selected light's junction. The original compares the
   record before first and only then tests for index 0 (reading in front of the table at 0). */
static int group_first(int sel)
{
    uint8_t g = L.lights[sel].group;
    int i = sel;
    while (i != 0 && L.lights[i - 1].group == g) i--;
    return i;
}

/* Lights_UpdateSprite 0x480e10: the light's frame from its phase's colour state */
void lights_update_sprite(int i)
{
    Light *l = &L.lights[(uint8_t)i];
    switch (phase_state(l->phase)) {
    case 0: sprite_set_frame(&l->light, l->frame_ofs + L.frame_red); break;
    case 1: sprite_set_frame(&l->light, l->frame_ofs + L.frame_amber); break;
    case 2: sprite_set_frame(&l->light, l->frame_ofs + L.frame_flash); break;
    case 3: sprite_set_frame(&l->light, l->frame_ofs + L.frame_green); break;
    default: break;
    }
}

/* back to automatic: in step with the global cycle (orientations 0 / 1 take phase A, 2 / 3 phase B) */
static void set_auto(Light *l, Light *sel_rec)
{
    l->tick = L.tick;
    if (l->kind < 9) l->phase = l->orient == 0 || l->orient == 1 ? L.phase_a : L.phase_b;
    else sel_rec->phase = 0;   /* (crossings: the selected light's phase, not this one's: as in the original) */
    l->frame_ofs = 0;
}
static void set_forced(Light *l)
{
    l->tick = 0xc;
    l->frame_ofs = 0;
    if (l->kind > 9) l->phase = 3;
}

/* Lights_SetMode 0x480ad0: the selected light only (more than 3: 0x1a) */
static int lights_set_mode(int mode)
{
    if ((uint8_t)mode > 3) return LIGHTS_BAD_MODE;
    Light *l = &L.lights[L.sel];
    l->mode = (uint8_t)mode;
    if (mode == 0) {
        l->tick = L.tick;
        if (l->kind < 9) l->phase = l->orient != 0 && l->orient != 1 ? L.phase_b : L.phase_a;
        else l->phase = 0;
        l->frame_ofs = 0;
    } else if (mode == 3) {
        set_forced(l);
    }
    lights_update_sprite(L.sel);
    return LIGHTS_OK;
}

/* Lights_SetGroupMode 0x480970: every light of the selected junction */
static int lights_set_group_mode(int mode)
{
    uint8_t g = L.lights[L.sel].group;
    for (int i = group_first(L.sel); i < LIGHTS_MAX + 2 && L.lights[i].group == g; i++) {
        Light *l = &L.lights[i];
        l->mode = (uint8_t)mode;
        if (mode == 0) set_auto(l, &L.lights[L.sel]);
        else if (mode == 3) set_forced(l);
        lights_update_sprite(i);
    }
    return LIGHTS_OK;
}

/* Lights_AddGroupMode 0x4807e0: the mode of every light of the junction + n, at most 3 */
static int lights_add_group_mode(int n)
{
    uint8_t g = L.lights[L.sel].group;
    n = (uint8_t)n;
    for (int i = group_first(L.sel); i < LIGHTS_MAX + 2 && L.lights[i].group == g; i++) {
        Light *l = &L.lights[i];
        l->mode = l->mode + n < 4 ? (uint8_t)(l->mode + n) : 3;
        if (n == 0) set_auto(l, &L.lights[L.sel]);
        else if (n == 3) set_forced(l);
        lights_update_sprite(i);
    }
    return LIGHTS_OK;
}

/* Lights_SetGroupForced 0x4806a0: every light of the junction forced (mode 3) for 0x32 ticks, in the
   phase of the argument: 0 -> 2 (red), 1 -> 1 (amber), 2 -> 5 (red + amber), 3 -> 0 (green) */
static int lights_set_group_forced(int how)
{
    uint8_t g = L.lights[L.sel].group;
    for (int i = group_first(L.sel); i < LIGHTS_MAX + 2 && L.lights[i].group == g; i++) {
        Light *l = &L.lights[i];
        l->mode = 3;
        l->countdown = 0x32;
        switch ((uint8_t)how) {
        case 0: l->phase = 2; break;
        case 1: l->phase = 1; break;
        case 2: l->phase = 5; break;
        case 3: l->phase = 0; break;
        default: break;
        }
        l->tick = 0xc;
        l->frame_ofs = 0;
        lights_update_sprite(i);
    }
    return LIGHTS_OK;
}

/* the selection the queries and commands make: the light of the first cell at (x, y) (light 0 when
   the cell isn't in the table), and its block */
static void select_at(uint8_t x, uint8_t y)
{
    uint8_t li = 0;
    cell_known(x, y, &li);
    L.sel = li;
    L.sel_x = L.lights[li].x;
    L.sel_y = L.lights[li].y;
}

/* Lights_Query 0x47df00 */
int lights_query(int what, int bx, int by)
{
    uint8_t q = (uint8_t)what, x = (uint8_t)bx, y = (uint8_t)by;
    int r;
    if (q == LQ_RAIL_3C || q == LQ_RAIL_3D) {
        /* both scans are bounded by 0x77d46b (the crossing table's own count is 0x77d46a) */
        r = 0;
        for (int i = 0; i < L.rail_n46b && i < LIGHTS_RAIL_MAX; i++) {
            const uint8_t *e = q == LQ_RAIL_3C ? L.rail_77d180[i] : L.rail_77cf58[i];
            if (e[0] == x && e[1] == y) { r = 1; break; }
        }
        return r;
    }
    if (q < 0x3c) {
        if (!junction_block(x, y)) {
            if (q == LQ_GROUP) game_fatal(-0xa9, 0x12f, x);   /* (the original then returns 0xff) */
            return LIGHTS_NONE;
        }
        select_at(x, y);
    }
    const Light *s = &L.lights[L.sel];
    switch (q) {
    case LQ_MODE: return s->mode;
    case LQ_ORIENT: return s->orient;
    case LQ_STATE: return phase_state(s->phase);
    case LQ_KIND: return s->kind;
    case LQ_GROUP: return s->group;
    case LQ_ALL_RED: {
        r = 1;
        int i = group_first(L.sel);
        /* the forward walk tests the selected index for 0 (not i): a junction whose first light is
           light 0 always answers 1 */
        while (L.lights[i].group == s->group && L.sel != 0) {
            if (phase_state(L.lights[i].phase) != 0) r = 0;
            if (++i >= LIGHTS_MAX + 2) break;
        }
        return r;
    }
    default: return LIGHTS_BAD;
    }
}

/* Lights_Command 0x47e2a0: on the junction of block (x, y): 0x32 the light's mode, 0x36 the
   junction's mode, 0x38 add to it, 0x39 force it; no junction block there: 0x17 */
int lights_command(int cmd, int arg, int bx, int by)
{
    uint8_t x = (uint8_t)bx, y = (uint8_t)by;
    if (!junction_block(x, y)) return LIGHTS_NONE;
    select_at(x, y);
    switch ((uint8_t)cmd) {
    case LQ_MODE: lights_set_mode(arg); break;
    case LQ_GROUP_MODE: lights_set_group_mode(arg); break;
    case LQ_ADD_MODE: lights_add_group_mode(arg); break;
    case LQ_FORCE: lights_set_group_forced(arg); break;
    default: break;
    }
    return LIGHTS_OK;
}

/* Lights_Update 0x47e420: every 8th frame the global cycle (phases 0 and 3 last 12 ticks, the others
   5) and every light: automatic ones count their ticks (phases 0 / 3: +4, else the phase table's
   duration) and step to the next phase (5 wraps to 0); forced ones count down and then put their
   junction back to automatic. The flashing frame alternates. Crossings (kind 9 and up) are left to
   the rail code. */
int lights_update(void)
{
    int r = LIGHTS_OK;
    if (L.ready != 1) return LIGHTS_NOT_INIT;
    if (L.frame++ != 7) return r;
    L.frame = 0;
    int limit = L.phase_a == 0 || L.phase_a == 3 ? 0xc : 5;
    if (L.tick < limit - 1) {
        L.tick++;
    } else {
        L.tick = 0;
        L.phase_a = L.phase_a > 4 ? 0 : (uint8_t)(L.phase_a + 1);
        L.phase_b = L.phase_b > 4 ? 0 : (uint8_t)(L.phase_b + 1);
    }
    L.frame_flash = L.frame_flash == L.frame_amber ? L.frame_off : L.frame_amber;
    const uint8_t *pt = phase_table();
    for (int i = 0; i < L.nlights; i++) {
        Light *l = &L.lights[i];
        if (l->kind >= 9) continue;
        if (l->mode == 0) {
            uint8_t p = l->phase;
            int dur = p == 0 || p == 3 ? l->long_ticks : pt[2 * p + 1];
            if (l->tick < dur - 1) {
                l->tick++;
            } else {
                p++;
                if (p > 5) p = 0;
                l->phase = p;
                l->tick = 0;
            }
        } else if (l->mode == 3) {
            if (l->countdown == 0) {
                L.sel = (uint8_t)i;
                lights_set_group_mode(0);
            } else {
                l->countdown--;
            }
        } else {
            r = LIGHTS_BAD;   /* (modes 1 / 2: Game_Update stops with a fatal error) */
        }
        lights_update_sprite(i);
    }
    return r;
}

/* Lights_TraceArm 0x47f240: walk from (x, y) in direction d over junction blocks that aren't in the
   cell table, adding each to it (as the light of the arm), and record on the arm the first road block,
   the first block past the road and where the road's direction bits change. Returns the length. */
static uint8_t lights_trace_arm(int d, uint8_t x, uint8_t y)
{
    const uint8_t x0 = x, y0 = y;
    LightArm *a = &arms[L.narms];
    int road_state = 0;         /* 0 before the road, 1 on it, 2 past it */
    bool seen = false;
    int8_t bits = 0;
    a->turn = 0;
    uint8_t z = L.cur_z;
    for (;;) {
        bool found = junction_block(x, y);
        z = L.cur_z;
        if (!found || cell_known(x, y, NULL)) break;
        add_cell(x, y, z, (uint8_t)(L.nlights + L.narms));
        if (!attr(3, x, y, z)) {
            if (road_state == 1) {
                road_state = 2;
                a->ex = x, a->ey = y, a->ez = L.cur_z;
            }
        } else {
            if (road_state == 0) {
                road_state = 1;
                a->sx = x, a->sy = y, a->sz = L.cur_z;
            }
            if (seen) {
                int8_t b = (int8_t)map_test_block_attr(4, x, y, L.cur_z);
                if (b != bits) {
                    a->turn = 1;
                    a->mx = x, a->my = y, a->mz = L.cur_z;
                    bits = b;
                }
            } else {
                seen = true;
                bits = (int8_t)map_test_block_attr(4, x, y, L.cur_z);
            }
        }
        L.ncells++;
        switch (d) {
        case 0: y--; break;
        case 1: y++; break;
        case 4: x++; break;
        case 5: x--; break;
        default: break;
        }
    }
    uint8_t len = 0;
    switch (d) {
    case 0: len = (uint8_t)(y0 - y); break;
    case 1: len = (uint8_t)(y - y0); break;
    case 4: len = (uint8_t)(x - x0); break;
    case 5: len = (uint8_t)(x0 - x); break;
    default: break;
    }
    if (road_state == 1) a->ex = x, a->ey = y, a->ez = z;
    return len;
}

/* Lights_FloodJunction 0x47e7c0: from (x, y) try +x, +y, -x, -y: a junction block not yet in the cell
   table starts an arm (traced to its end) and the flood goes on from the arm's last block. The arms
   found from this block end up in 0x77d472 (the outermost call writes last). */
static void lights_flood(uint8_t x, uint8_t y)
{
    static const struct { int8_t dx, dy; uint8_t d; } nb[4] = { { 1, 0, 4 }, { 0, 1, 1 }, { -1, 0, 5 }, { 0, -1, 0 } };
    uint8_t found = 0;
    for (int k = 0; k < 4; k++) {
        uint8_t nx = (uint8_t)(x + nb[k].dx), ny = (uint8_t)(y + nb[k].dy);
        if (!junction_block(nx, ny) || cell_known(nx, ny, NULL)) continue;
        if (L.narms >= LIGHTS_ARMS) game_fatal(-0xa5, 0xd0, L.narms);   /* (the original's 10 arms run into its globals) */
        uint8_t len = lights_trace_arm(nb[k].d, nx, ny);
        LightArm *a = &arms[L.narms++];
        a->len = len;
        a->dir = nb[k].d;
        found++;
        switch (nb[k].d) {
        case 4: lights_flood((uint8_t)(x + len), y); break;
        case 1: lights_flood(x, (uint8_t)(y + len)); break;
        case 5: lights_flood((uint8_t)(x - len), y); break;
        default: lights_flood(x, (uint8_t)(y - len)); break;
        }
    }
    L.seed_arms = found;
}

/* Lights_FindRailCrossing 0x47f120: a crossing (kind 10 / 11) records the first railway block with
   the crossing flag (Map_TestBlockAttr 1 and 8) inside the arms' box */
static void lights_find_rail_crossing(int kind)
{
    if (kind != 11 && kind != 10) return;
    for (int x = arms[0].sx; x <= arms[1].ex; x++)
        for (int y = arms[0].sy; y <= arms[1].ey; y++)
            for (int z = 0; z < 6; z++)
                if (attr(1, x, y, z) && attr(8, x, y, z)) {
                    if (L.ncross < LIGHTS_RAIL_MAX) {
                        uint8_t *c = L.cross[L.ncross];
                        c[0] = (uint8_t)x, c[1] = (uint8_t)y, c[2] = (uint8_t)z, c[3] = L.nlights;
                    }
                    L.ncross++;
                    return;
                }
}

/* Lights_CreateSprites 0x47f560: each new light gets its fields, a light sprite and a pole sprite at
   its stop block, placed and turned by the orientation (0: x + 63, y + 55, angle 0x200; 1: x + 63,
   y + 8, angle 0; 2: x + 55, y + 63, angle 0x300; 3: x + 8, y + 63, angle 0x100, from the exe words
   0x4b3608 / 0x4b360c), the light 0x22 pixels above the block's bottom, the pole at its top; both in
   the collision grid (kinds 7 and 0xe). Over a slope the light's depth key is one layer up. */
static void lights_create_sprites(int kind)
{
    const uint8_t *ang = exe_data(0x4b3608, 8);
    if (!ang) game_fatal(-2, 0, 0x4b3608);
    for (int k = 0; k < L.narms; k++) {
        uint8_t li = (uint8_t)(L.nlights + k);
        Light *l = &L.lights[li];
        l->group = L.njunctions;
        l->kind = (uint8_t)kind;
        l->mode = 0;
        l->long_ticks = 0xc;
        l->tick = 0;
        l->frame_ofs = 0;
        int32_t bx = l->x * 0x400000, by = l->y * 0x400000, bz = l->z * 0x400000;
        int32_t px, py;
        int angle, frame;
        switch (l->orient) {
        case 0: px = bx + 0x3f0000, py = by + 0x370000, angle = ang[0] | ang[1] << 8, frame = L.frame_red; break;
        case 1: px = bx + 0x3f0000, py = by + 0x80000, angle = ang[2] | ang[3] << 8, frame = L.frame_red; break;
        case 2: px = bx + 0x370000, py = by + 0x3f0000, angle = ang[4] | ang[5] << 8, frame = L.frame_green; break;
        case 3: px = bx + 0x80000, py = by + 0x3f0000, angle = ang[6] | ang[7] << 8, frame = L.frame_green; break;
        default: px = py = 0, angle = frame = -1; break;
        }
        if (frame >= 0) {
            sprite_init(&l->light, px, py, bz + 0x220000, angle & 0x3ff, frame);
            coll_insert(COLL_KIND7, li, &l->light, l->light.unk20, px, py);
            sprite_init(&l->pole, px, py, bz + 0x3fffff, angle & 0x3ff, L.frame_pole);
            coll_insert(COLL_KIND14, li, &l->pole, l->pole.unk20, px, py);
        }
        if (g_game.map && (int8_t)g_game.map->type_cache[l->z < MAP_Z ? l->z : MAP_Z - 1][l->y][l->x] < 0)
            l->light.zkey -= 0x400000;
        if (kind > 9) {
            sprite_set_frame(&l->light, L.frame_green);
            l->phase = 0;
        }
    }
}

/* Lights_ClassifyJunction 0x47ebd0: the arm directions against the templates (the first whose first
   narms directions match; none: kind 5 with the previous junction's orientations). Kind 5 whose seed
   block had a single arm becomes kind 6 with the next template's orientations. Per kind the arms'
   recorded blocks move back by one; crossings (10 / 11) look up to 9 blocks along for the other half
   and flood it too. Then each arm's light takes its orientation (0 / 1 start in phase 0, 2 / 3 in
   phase 3) and its block (the road's start, or where its direction changed). */
static void lights_classify(void)
{
    const uint8_t *tt = template_table();
    int kind = 5;
    for (int t = 0; t < LIGHTS_TEMPLATES; t++) {
        bool match = true;
        for (int i = 0; i < L.narms; i++) {
            int o = t * 21 + 1 + i;   /* (more than 10 arms read on into the next template, as in the original) */
            if (o >= 21 * LIGHTS_TEMPLATES || arms[i].dir != tt[o]) match = false;
        }
        if (match) {
            kind = tt[t * 21];
            if (kind == 5 && L.seed_arms == 1) kind = 6, t++;
            L.tmpl = (uint8_t)t;
            break;
        }
    }
    LightArm *a0 = &arms[0], *a1 = &arms[1], *a2 = &arms[2];
    switch (kind) {
    case 0: case 4: case 5: case 6:
        a0->mx--, a1->my--, a0->sx--, a1->sy--;
        break;
    case 1:
        a0->mx--, a1->my--, a2->my--, a0->sx--, a1->sy--;
        a2->sy = (uint8_t)(a2->ey - 1);
        break;
    case 2:
        a0->my--, a1->mx--;
        a0->sy = (uint8_t)(a0->ey - 1);
        a1->sx = (uint8_t)(a1->ex - 1);
        break;
    case 3:
        a0->mx--, a1->my--, a2->mx--, a0->sx--;
        a1->sy = (uint8_t)(a1->ey - 1);
        a2->sx = (uint8_t)(a2->ex - 1);
        break;
    case 7:
        a1->mx--;
        /* fall through */
    case 8:
        a0->my--, a0->sy--, a1->sx--;
        break;
    case 10: {
        for (int x = L.seed_x + 1; x < L.seed_x + 10; x++)
            if (junction_block((uint8_t)x, L.seed_y)) {
                lights_flood((uint8_t)x, (uint8_t)(L.seed_y - 1));
                break;
            }
        a0->my--, a0->sy--, a1->my--, a1->sy--;
        a1->dir = 3;
        break;
    }
    case 11: {
        for (int y = L.seed_y + 1; y < L.seed_y + 10; y++)
            if (junction_block(L.seed_x, (uint8_t)y)) {
                lights_flood((uint8_t)(L.seed_x - 1), (uint8_t)y);
                break;
            }
        a0->mx--, a0->sx--, a1->mx--, a1->sx--;
        a1->dir = 1;
        break;
    }
    default:
        game_fatal(-0x6c, 0, 0);
    }
    lights_find_rail_crossing(kind);
    for (int i = 0; i < L.narms; i++) {
        Light *l = &L.lights[L.nlights + i];
        int o = L.tmpl * 21 + 11 + i;
        switch (o < 21 * LIGHTS_TEMPLATES ? tt[o] : 0xff) {
        case 0: l->orient = 0, l->phase = 0; break;
        case 1: l->orient = 1, l->phase = 0; break;
        case 2: l->orient = 2, l->phase = 3; break;
        case 3: l->orient = 3, l->phase = 3; break;
        default: break;
        }
        LightArm *a = &arms[i];
        if (a->turn) a->sx = a->mx, a->sy = a->my, a->sz = a->mz;
        l->x = a->sx, l->y = a->sy, l->z = a->sz;
    }
    lights_create_sprites(kind);
}

/* Lights_AddJunction 0x47e6c0: the seed block into the cell table, the flood, the classification;
   then the new lights and the junction count */
static int lights_add_junction(uint8_t x, uint8_t y)
{
    if (L.ncells > 0x2bf) game_fatal(-0xa5, 0xcf, 0);
    L.seed_x = x, L.seed_y = y;
    add_cell(x, y, L.cur_z, L.nlights);
    L.ncells++;
    L.narms = 0;
    lights_flood(x, y);
    if (L.nlights + L.narms > 0x57) game_fatal(-0xa5, 0xd0, 0);
    arms[L.narms].dir = 6;
    lights_classify();
    L.nlights = (uint8_t)(L.nlights + L.narms);
    L.njunctions++;
    return LIGHTS_OK;
}

/* Lights_Init 0x47dcf0: the sprite numbers (traffic-light group + 0 red, 1 green, 2 amber, 3 the
   unlit frame, 4 pole), the counters, the global cycle (A at 0, B at 3), then every block
   column, rows outer: a junction block (not a railway) whose column isn't in the cell table yet seeds
   a junction. Then the rail tracer. */
void lights_init(void)
{
    int16_t base = (int16_t)sprite_group_base(SPRITE_GROUP_TRAFFIC_LIGHTS);
    memset(L.lights, 0, sizeof L.lights);   /* (bss in the original: kept from the last level) */
    L.frame_red = base;
    L.frame_green = (int16_t)(base + 1);
    L.frame_off = (int16_t)(base + 3);
    L.frame_pole = (int16_t)(base + 4);
    L.frame_amber = (int16_t)(base + 2);
    L.ready = L.rail_ready = L.frame = 0;
    L.nlights = 0;
    L.ncells = 0;
    L.ncross = L.njunctions = 0;
    L.rail_n46a = L.rail_n46b = 0;
    L.narms = L.seed_arms = 0;
    L.sel = L.sel_x = L.sel_y = 0xff;
    L.tick = 0;
    L.phase_a = 0;
    L.phase_b = 3;
    int r = LIGHTS_OK;
    for (int y = 0; y < 0x100 && r == LIGHTS_OK; y++)
        for (int x = 0; x < 0x100 && r == LIGHTS_OK; x++) {
            if (!junction_block((uint8_t)x, (uint8_t)y)) continue;
            if (cell_known((uint8_t)x, (uint8_t)y, NULL)) continue;
            if (!attr(1, x, y, L.cur_z)) r = lights_add_junction((uint8_t)x, (uint8_t)y);
        }
    if (r == LIGHTS_OK) {
        L.ready = 1;
        if (rail_init() == LIGHTS_OK) L.rail_ready = 1;
    }
}

/* ---- the junction overrides (emergency services module) ---- */

/* Junction_InitOverrides 0x419400: every record free; then each traffic-light object (type 0x10) of the
   first 2500 binds the record of its junction (Lights_Query 0x3a: fatal if it doesn't stand on one),
   which keeps its block, id and sprite angle (the object's angle becomes 0). */
void junction_init_overrides(void)
{
    for (int i = 0; i < JUNCTION_OVR_MAX; i++) {
        JunctionOvr *j = &g_junction_ovr[i];
        j->id = (uint8_t)i;
        j->owner = 0xff;
        j->timer = 0;
        j->mode = 0, j->u09 = 0;
        j->obj = -1;
        j->x = j->y = 0;
        j->u0e = 0;
        j->u10 = 0;
        j->u5a = 0;
        memset(j->u12, 0xff, sizeof j->u12);
    }
    for (int i = 0; i < 0x9c4; i++) {
        Obj *o = obj_get(i);
        if (o->type != 0x10) continue;
        int g = (uint8_t)lights_query(LQ_GROUP, o->spr.x >> 22, o->spr.y >> 22);
        if (g >= JUNCTION_OVR_MAX) game_fatal(-0xa5, 0x58, g);   /* (port: the original writes past the table) */
        JunctionOvr *j = &g_junction_ovr[g];
        j->x = (uint8_t)(o->spr.x >> 22);
        j->y = (uint8_t)(o->spr.y >> 22);
        j->obj = o->id;
        j->obj_angle = o->spr.angle;
        o->spr.angle = 0;
    }
}

/* Junction_UpdateOverrideTimers 0x41e140: a running timer that reaches 0 frees the record (the debug
   option 0x502f54 also logs "junc no to reset" to a file; not ported) */
void junction_update_override_timers(void)
{
    for (int i = 0; i < JUNCTION_OVR_MAX; i++) {
        JunctionOvr *j = &g_junction_ovr[i];
        if (j->timer > 0 && --j->timer == 0) j->owner = 0xff;
    }
}
