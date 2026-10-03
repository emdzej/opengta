/* The path finder 0x46e9e0-0x471940 and Map_FindNearestRoad 0x41a490 (path.h). A best-first search
   over map blocks: the open and closed nodes share one list sorted by cost, headed by the goal node;
   a node's direction byte is the direction it was reached by and 0 once it has been expanded. The
   road modes (2..5) cost a node by its Chebyshev distance to the goal (greedy) and expand along a
   road in long runs; modes 0 / 1 accumulate step costs on a grid. See docs/traffic.md. */
#include "path.h"
#include "ai.h"
#include "game.h"
#include "route.h"
#include "../map.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* a search node (16 bytes in the original) */
typedef struct PathNode {
    uint8_t x, y, z;
    uint8_t dir;                /* +3 the direction it was reached by (1/2/4/8, 0x10 a jump); 0 expanded */
    uint16_t cost;              /* +4 */
    uint16_t pad;
    struct PathNode *next;      /* +8 the cost-sorted list */
    struct PathNode *parent;    /* +0xc */
} PathNode;

/* the pool runs from 0x75cd58 to 0x7705d8 (5000 nodes); a search stops once more than 0x1374 are
   used, so an expansion has 20 nodes of room */
enum { POOL_NODES = 5000, POOL_LIMIT = 0x1374, ROUTE_NODES = 0x55 };

int16_t g_path_owner = -1;                  /* 0x4b3094 */
int16_t g_path_result;                      /* 0x7537b2 */

static struct {
    int8_t mode;                /* 0x7537b0 */
    PathNode *best;             /* 0x7537b4 the cheapest node inserted (starts at the start node) */
    PathNode start;             /* 0x7537b8 */
    int16_t max_idx;            /* 0x7537c8 pool index of the most expensive node */
    PathNode goal;              /* 0x75cd3c the goal, also the list head (its next is the first node) */
    uint8_t road_cost;          /* 0x75cd4c cost of a road step in the grid modes (6) */
    uint8_t near;               /* 0x75cd4d set: no road run look-ahead (always set: see expand_dir) */
    PathNode pool[POOL_NODES];  /* 0x75cd58 (never cleared: stale entries are read as in the original) */
    int8_t near_dist;           /* 0x7705d8 (10) */
    int16_t len;                /* 0x7705da nodes of the last route - 1 */
    int16_t count;              /* 0x7705dc pool nodes used */
    int32_t steps;              /* 0x7705e0 expansions of this call (the budget counter) */
    uint8_t best_set;           /* 0x7705e4 */
    uint8_t jump_x, jump_y;     /* 0x7705e5 / 0x7705e6 */
} ps;

/* the cached block types 0x55fab0 [z][y][x] (coordinates are bytes in the original; a layer past 5,
   which the original reads from the memory after the cache, is 0 here) */
static uint8_t tc(int x, int y, int z)
{
    z &= 0xff;
    if (z >= MAP_Z || !g_game.map) return 0;
    return g_game.map->type_cache[z][y & 0xff][x & 0xff];
}
/* Map_GetTypeMap 0x438900 (0 past layer 5: the original indexes past the column) */
static uint32_t tm(int x, int y, int z)
{
    z &= 0xff;
    if (z >= MAP_Z || !g_game.map) return 0;
    return map_get_type_map(g_game.map, x & 0xff, y & 0xff, z);
}
static int slope_of(uint32_t t) { return t >> 8 & 0x3f; }
static int cheb(int a, int b)
{
    a = abs(a), b = abs(b);
    return b < a ? a : b;
}
static int cheb_goal(int x, int y) { return cheb(x - ps.goal.x, y - ps.goal.y); }
static bool at_goal(int x, int y, int z)
{
    return (uint8_t)x == ps.goal.x && (uint8_t)y == ps.goal.y && (uint8_t)z == ps.goal.z;
}
/* the next pool entry, filled before Path_InsertNode links it in */
static PathNode *pending(void) { return &ps.pool[ps.count]; }
static void set_pending(int x, int y, int z, int cost)
{
    PathNode *n = pending();
    n->x = (uint8_t)x, n->y = (uint8_t)y, n->z = (uint8_t)z;
    n->cost = (uint16_t)cost;
}

/* Path_IsUpRamp 0x46ee20 */
int path_is_up_ramp(uint32_t t, int dir)
{
    int s = slope_of(t);
    switch ((int16_t)dir) {
    case 1: return s == 0x10;
    case 2: return s == 0x18;
    case 4: return s == 0x20;
    case 8: return s == 0x28;
    default: return 0;
    }
}
/* Path_IsDownRamp 0x46ee70 */
int path_is_down_ramp(uint32_t t, int dir)
{
    int s = slope_of(t);
    switch ((int16_t)dir) {
    case 1: return s == 0x18;
    case 2: return s == 0x10;
    case 4: return s == 0x28;
    case 8: return s == 0x20;
    default: return 0;
    }
}

void map_get_block_info_thunk(int x, int y, int z) { (void)tm(x, y, z); }   /* 0x471940 */

/* Path_Reset 0x46e9e0 */
void path_reset(void)
{
    g_path_owner = -1;
    ps.len = 0;
    ps.count = 0;
    ps.steps = 0;
    ps.best_set = 0;
    ps.jump_x = 0;
    ps.jump_y = 0;
}

/* Path_InsertNode 0x46ec30: link the pending pool entry (its x, y, z, cost already written) under
   `parent`, reached by `dir`. It must lie at least 2 blocks from the parent (1 if it is the goal; in
   mode 5 4 when the parent's block has no `dir` bit), except as the first node. The list is walked
   from the head to the first more expensive node; meeting the same block on the way (or a parent
   that is its own parent) drops it, so a cheaper copy further on is never seen. */
static int insert_node(PathNode *parent, uint8_t dir)
{
    PathNode *n = pending();
    int dist = cheb(n->x - parent->x, n->y - parent->y);
    uint16_t c = n->cost;
    int16_t min = 2;
    if (at_goal(n->x, n->y, n->z)) min = 1;
    if (ps.mode == 5 && (tc(parent->x, parent->y, parent->z) & dir) == 0) min = 4;
    if ((int16_t)dist < min && ps.count != 0) return 0;
    uint16_t maxc = ps.pool[ps.max_idx].cost;
    if (c < ps.best->cost) {
        ps.best_set = 1;
        ps.best = n;
    }
    bool dup = parent->parent == parent;
    PathNode *prev = &ps.goal, *q;
    for (;;) {
        q = prev->next;
        if (!q) break;
        if (n->x == q->x && n->y == q->y && n->z == q->z) dup = true;
        if (c < q->cost) break;
        prev = q;
        if (dup) return 0;
    }
    if (dup) return 0;
    n->next = q;
    n->dir = dir;
    n->parent = parent;
    prev->next = n;
    if (maxc < c) ps.max_idx = ps.count;
    ps.count++;
    return 1;
}

/* Path_ExtractRoute 0x46ea10: the chain of parents from the goal (found) or from the first open node
   (the best one when set; pool full) back to the start, written start first into path slot `slot`
   and ended by 0, 0, 0. More than 85 nodes is fatal. In mode 3 the block one step further along the
   goal's direction bits is appended when it is plain flat road (cached type exactly 0x20). The end
   marker of an 85-node route lands in the next slot (as in the original). */
static void extract_route(int res, int slot)
{
    PathNode *p = &ps.goal;
    if ((int16_t)res != 1) {
        if (ps.best_set) p = ps.best;
        bool end = false;
        while (p->dir == 0 && !end) {
            PathNode *q = p->next;
            end = q == NULL;
            if (end) q = p;
            p = q;
        }
    }
    BlockXYZ chain[ROUTE_NODES];
    int n = 0;
    ps.len = 0;
    while (p) {
        p->next = NULL;
        if (n >= ROUTE_NODES) game_fatal(-0xa7, 0x8d, slot);   /* (the original fails after the 86th) */
        chain[n].x = p->x, chain[n].y = p->y, chain[n].z = p->z;
        n++;
        p = p->parent;
    }
    ps.len = (int16_t)(n - 1);
    size_t base = (size_t)(int16_t)slot * PATH_SLOT_SIZE;
    uint8_t *flat = &g_path_slots[0][0];
    int k = 0;
    for (int i = n - 1; i >= 0; i--, k++) {
        if (base + k * 3 + 2 >= sizeof g_path_slots) game_fatal(-0xa7, 0x8d, slot);   /* port */
        flat[base + k * 3] = chain[i].x, flat[base + k * 3 + 1] = chain[i].y, flat[base + k * 3 + 2] = chain[i].z;
    }
    if (ps.mode == 3) {
        int dx = 0, dy = 0;
        switch (tc(ps.goal.x, ps.goal.y, ps.goal.z) & 0xf) {
        case 1: dy = -1; break;
        case 2: dy = 1; break;
        case 4: dx = -1; break;
        case 8: dx = 1; break;
        default: break;
        }
        if (tc(ps.goal.x + dx, ps.goal.y + dy, ps.goal.z) == 0x20) {
            if (base + k * 3 + 2 >= sizeof g_path_slots) game_fatal(-0xa7, 0x8d, slot);   /* port */
            flat[base + k * 3] = (uint8_t)(ps.goal.x + dx);
            flat[base + k * 3 + 1] = (uint8_t)(ps.goal.y + dy);
            flat[base + k * 3 + 2] = ps.goal.z;
            k++;
        }
    }
    if (base + k * 3 + 2 >= sizeof g_path_slots) game_fatal(-0xa7, 0x8d, slot);   /* port */
    flat[base + k * 3] = flat[base + k * 3 + 1] = flat[base + k * 3 + 2] = 0;
}

/* Path_TryStraightLine 0x46eec0: on the goal's layer, from a flat block within reach, a straight
   run toward the goal (x first, then y) over flat road (mode 4: or pavement) that doesn't point back
   at us; reaching the goal within 10 steps links it (cost 0, a jump: 0x10). The layer is the pending
   entry's (expand_node wrote the node's own there). */
static int try_straight_line(PathNode *p, uint8_t px, uint8_t py)
{
    uint8_t z = pending()->z;
    if (z != ps.goal.z || (tm(px, py, z) & 0x3f00) != 0) return 0;
    int8_t sx = 0, sy = 0;
    uint8_t bad = 0;
    if (px < ps.goal.x) sx = 1, bad = 4;
    else if (ps.goal.x < px) sx = -1, bad = 8;
    else if (py < ps.goal.y) sy = 1, bad = 1;
    else if (ps.goal.y < py) sy = -1, bad = 2;
    uint8_t x = px, y = py, n = 0;
    int res = 0;
    do {
        x = (uint8_t)(x + sx), y = (uint8_t)(y + sy);
        if (at_goal(x, y, z)) {
            res = 1;
        } else {
            uint8_t t = (uint8_t)tm(x, y, z);
            bool ok = false;
            if ((t & 0xf) != bad) {
                if ((t & 0x70) == 0x20) ok = (tm(x, y, z) & 0x3f00) == 0;
                else if (ps.mode == 4) ok = (t & 0x70) == 0x30;
            }
            if (!ok) res = 2;
        }
        if (++n > 10) return 0;   /* (also when the goal is the 11th step) */
    } while (res == 0);
    if (res != 1) return 0;
    set_pending(x, y, z, 0);
    insert_node(p, 0x10);
    return 1;
}

/* the sideways lane scan of expand_node (modes 3 / 4 heading away from the goal): along the road
   across the heading, blocks 2..5 away that run back the way we came become nodes (always linked
   with the left-hand direction, also on the right-hand side: as in the original) */
static void lane_scan(PathNode *p, uint8_t x, uint8_t y, uint8_t z, int sx, int sy, uint8_t back, uint8_t dir)
{
    uint8_t n = 0;
    uint8_t t = tc(x, y, z);
    while ((t & 0x70) == 0x20 || ((t & 0x70) == 0x30 && ps.mode == 4 && !(t & 0x80))) {
        x = (uint8_t)(x + sx), y = (uint8_t)(y + sy);
        n++;
        t = tc(x, y, z);
        if ((t & 0xf) == back && n > 1 && n < 6 && !(t & 0x80)) {
            set_pending(x, y, z, cheb_goal(x, y));
            insert_node(p, dir);
        }
    }
}

/* Path_ExpandNode 0x46f0a0 (modes 2..5): from node p, a run along direction d (one of 1 -y, 2 +y,
   4 -x, 8 +x) over blocks that keep the d bit. At each block of the run the next block, its left and
   right neighbours and the turn bits decide what becomes a node: a turn off the run (the next block
   allows the right turn, or the left turn where the left lane doesn't continue), a diagonal scan of
   up to 4 blocks forward-left and forward-right looking for the next turn, sidesteps at a dead end
   (mode 3), a ramp (a jump node one block further, which also ends the run), and the goal itself.
   Costs are Chebyshev distances to the goal, mostly of the block where the run stands (not the node).
   The many variables mirror the original's; the debug dump (0x502f54, "rdiag") is left out. */
static int expand_node(PathNode *p, int16_t d)
{
    const uint8_t gx = ps.goal.x, gy = ps.goal.y, gz = ps.goal.z;
    const int8_t mode = ps.mode;
    const bool m235 = mode == 2 || mode == 3 || mode == 5;
    uint8_t left = 0, right = 0;                /* bVar1 / bVar3 */
    int8_t dx = 0, dy = 0;                      /* local_97 / local_96 */
    int16_t back = 0;                           /* local_28 */
    bool scan = false, away = false, stop = false;
    uint8_t steps = 0;
    uint32_t l38 = 0, l40 = 0;                  /* the last diagonal look-ahead block (stale between runs) */
    int16_t l8e = 0;
    uint8_t px = p->x, py = p->y, pz = p->z;
    PathNode *n = pending();
    n->x = px, n->y = py, n->z = pz;
    int8_t follow = 1;                          /* local_8f */
    int16_t cost = (int16_t)cheb_goal(px, py);
    n->cost = (uint16_t)cost;
    n->parent = p;
    n->dir = (uint8_t)d;
    switch (d) {
    case 1: dy = -1, left = 4, right = 8, back = 2; break;
    case 2: dy = 1, left = 8, right = 4, back = 1; break;
    case 4: dx = -1, left = 2, right = 1, back = 8; break;
    case 8: dx = 1, left = 1, right = 2, back = 4; break;
    default: break;
    }
    int pdir = (int8_t)p->dir;
    if (pdir == back) return 1;
    if (pdir != right && (mode == 3 || mode == 4)) follow = 0;
    if (mode > 2 && mode != 5) {
        follow = 0;
        if (cost < 7 && (px == gx || py == gy) && try_straight_line(p, px, py)) return 1;
        if ((px < gx && d == 4) || (gx < px && d == 8) || (py < gy && d == 1) || (gy < py && d == 2) || ps.count < 2) {
            if (pdir != right) lane_scan(p, px, py, pz, dy, -dx, (uint8_t)back, left);
            if (pdir != left) lane_scan(p, px, py, pz, -dy, dx, (uint8_t)back, left);
        }
    }
    uint8_t nx = (uint8_t)(px + dx), ny = (uint8_t)(py + dy);
    do {
        int8_t dz = 0;
        steps++;
        uint8_t cur = tc(px, py, pz);
        if ((cur & 0x80) && path_is_up_ramp(tm(px, py, pz), d)) dz = -1;
        if (pz < 4 && path_is_down_ramp(tm(nx, ny, pz + 1), d)) dz = 1;
        uint8_t L = tc(px + dy, py - dx, pz);
        uint8_t R = tc(px - dy, py + dx, pz);
        uint8_t N = tc(nx, ny, pz + dz);
        if (follow) {
            if ((L & 0xf) == back || (L & 0x70) != 0x20) follow = 0;
            if ((cur & 0xf) == d && follow) follow++;
            if (follow == 4) follow = 0;
        }
        uint8_t z2 = (uint8_t)(pz + dz);
        set_pending(nx, ny, z2, cheb_goal(px, py));
        l8e = (int16_t)(abs(px - gx) + abs(py - gy));
        if (mode > 2 && !away && mode != 5) {
            if (l8e < 7 && pz == gz && ((px == gx && d > 2) || (py == gy && d < 4))) {
                l8e = (int16_t)cheb_goal(px, py);
                set_pending(px, py, pz, l8e);
                insert_node(p, (uint8_t)d);
            }
            if ((px < gx && d == 4) || (gx < px && d == 8) || (py < gy && d == 1) || (gy < py && d == 2)) away = true;
        }
        /* (after an insert above this reads the next, stale pool entry, like the original) */
        n = pending();
        if (at_goal(n->x, n->y, n->z)) {
            n->cost = (uint16_t)cheb_goal(px, py);
            insert_node(p, (uint8_t)d);
            return 1;
        }
        if (((N & right) == right && ((R & d) != d || (N & 0xf) == right)) ||
            ((N & left) == left && (((L & d) != d && follow == 0) || (N & 0xf) == left))) {
            l8e = (int16_t)cheb(px - gx + dx, py - gy + dy);
            set_pending(nx, ny, pz, l8e);
            insert_node(p, (uint8_t)d);
        }
        if ((N & 0xf) == 0 && mode == 3) {
            if ((L & 0x70) == 0x20) {
                l8e = (int16_t)cheb_goal(px, py);
                set_pending(px + dy, py - dx, pz, l8e);
                insert_node(p, (uint8_t)d);
            }
            if ((R & 0x70) == 0x20) {
                l8e = (int16_t)cheb_goal(px, py);
                set_pending(px - dy, py + dx, pz, l8e);
                insert_node(p, (uint8_t)d);
            }
        }

        /* forward-left diagonal look-ahead */
        uint8_t cnt = 1;
        int8_t sdz = 0;
        if (m235) scan = (N & d) == d && (L & d) == d;
        if (mode == 4) scan = ((N & d) == d || (N & 0x70) == 0x30) && ((L & d) == d || (L & 0x70) == 0x30);
        if (cur & 0x80) {
            if ((cur & d) != d) scan = false;
            if (!(L & 0x80)) scan = false;
        }
        if (N & 0x80) {
            scan = false;
        } else {
            uint8_t ax = px, ay = py, az = z2;
            while (scan) {
                if (++cnt == 6) break;
                ax = (uint8_t)(ax + dy + dx), ay = (uint8_t)(ay + dy - dx);
                az = (uint8_t)(az + sdz);
                uint8_t bx = (uint8_t)(ax + dx), by = (uint8_t)(ay + dy);
                l38 = bx, l40 = by;
                sdz = 0;
                uint8_t c = tc(ax, ay, az);
                if ((c & 0x80) && path_is_up_ramp(tm(ax, ay, az), d)) sdz = -1;
                if (az < 4 && path_is_down_ramp(tm(bx, by, az + 1), d)) sdz = 1;
                uint8_t N2 = tc(bx, by, az + sdz);
                uint8_t L2 = tc(ax + dy, ay - dx, az);
                if (((c & 0x80) && ((c & d) != d || !(L2 & 0x80))) || ((c & 0xf) == 0 && mode != 4)) break;
                if (at_goal(ax, ay, az)) {
                    set_pending(ax, ay, az, cheb_goal(ax, ay));
                    insert_node(p, (uint8_t)d);
                    return 1;
                }
                if ((N2 & left) == left && (left & c) == 0 && follow == 0 && (L2 & d) != d && !(N2 & 0x80)) {
                    l8e = (int16_t)cheb_goal(bx, by);
                    set_pending(bx, by, az + sdz, l8e);
                    insert_node(p, (uint8_t)d);
                    break;
                }
                if ((N2 & 0xf) == 0 && (L2 & d) == d && mode != 4) {
                    l8e = (int16_t)cheb(ax - gx + dy, ay - gy - dx);
                    set_pending(ax + dy, ay - dx, az, l8e);
                    insert_node(p, (uint8_t)d);
                    break;
                }
                if (m235) scan = (N2 & d) == d && (L2 & d) == d;
                if (mode == 4) {
                    if (((N2 & d) != d && (N2 & 0x70) != 0x30) || ((L2 & d) != d && (L2 & 0x70) != 0x30)) {
                        scan = false;
                        break;
                    }
                    scan = true;
                }
            }
        }

        /* forward-right diagonal look-ahead */
        cnt = 1;
        if (m235) scan = (N & d) == d && (R & d) == d;
        else if (mode == 4) scan = ((N & d) == d || (N & 0x70) == 0x30) && ((R & d) == d || (R & 0x70) == 0x30);
        if (cur & 0x80) {
            if ((cur & d) != d) scan = false;
            if (!(R & 0x80)) scan = false;
        }
        if (N & 0x80) {
            scan = false;
        } else {
            uint8_t ax = px, ay = py, az = z2;
            int8_t cdz = 0;
            while (scan) {
                if (++cnt == 6) break;
                int8_t rdz = 0;
                ax = (uint8_t)(ax + dx - dy), ay = (uint8_t)(ay + dy + dx);
                az = (uint8_t)(az + cdz);
                uint8_t bx = (uint8_t)(ax + dx), by = (uint8_t)(ay + dy);
                l38 = bx, l40 = by;
                cdz = 0;
                uint8_t c = tc(ax, ay, az);
                if ((c & 0x80) && path_is_up_ramp(tm(ax, ay, az), d)) rdz = -1, cdz = -1;
                if (az < 4 && path_is_down_ramp(tm(bx, by, az + 1), d)) rdz = 1, cdz = 1;
                uint8_t N2 = tc(bx, by, az + rdz);
                uint8_t R2 = tc(ax - dy, ay + dx, az);
                if (((c & 0xf) == 0 && mode != 4) || ((c & 0x80) && ((c & d) != d || !(R2 & 0x80)))) break;
                if (at_goal(ax, ay, az)) {
                    set_pending(ax, ay, az, cheb_goal(ax, ay));
                    insert_node(p, (uint8_t)d);
                    return 1;
                }
                /* (unlike the left side, any slope ahead is taken) */
                if (((N2 & right) == right && (right & c) == 0 && (R2 & d) != d && !(N2 & 0x80)) || (N2 & 0x80)) {
                    l8e = (int16_t)cheb_goal(bx, by);
                    set_pending(bx, by, az + cdz, l8e);
                    insert_node(p, (uint8_t)d);
                    break;
                }
                if ((N2 & 0xf) == 0 && (R2 & d) == d && mode != 4) {
                    l8e = (int16_t)cheb(ax - gx - dy, ay - gy + dx);
                    set_pending(ax - dy, ay + dx, az, l8e);
                    insert_node(p, (uint8_t)d);
                    break;
                }
                if (m235) {
                    if ((N2 & d) != d || (R2 & d) != d) { scan = false; break; }
                    scan = true;
                } else if (mode == 4) {
                    if (((N2 & d) != d && (N2 & 0x70) != 0x30) || ((R2 & d) != d && (R2 & 0x70) != 0x30)) {
                        scan = false;
                        break;
                    }
                    scan = true;
                }
            }
        }

        /* a ramp: a jump node one block beyond the next, costed by the last look-ahead block */
        if (dz != 0) {
            l8e = (int16_t)cheb((int)l38 - gx, (int)l40 - gy);
            set_pending(nx + dx, ny + dy, z2, l8e);
            insert_node(p, 0x10);
            stop = true;
        }
        bool cont;
        uint8_t t = tc(nx, ny, z2);
        if (mode != 4 || l8e <= (int16_t)steps || away) cont = (t & d) == d;
        else cont = (t & 0x70) == 0x20 || (t & 0x70) == 0x30;
        if (!cont) return 1;
        px = nx, py = ny, pz = z2;
        nx = (uint8_t)(nx + dx), ny = (uint8_t)(ny + dy);
    } while (!stop);
    return 1;
}

/* Path_ExpandDir 0x4710d0 (modes 0 / 1): one step in direction(s) d onto any non-air, non-water,
   non-building block (slopes only straight, and only where the level changes the right way: the
   original's slope tests compare the unshifted slope field with small numbers and never match, kept
   as dead code). The cost adds 1 for pavement / field, 6 for road (the road-run look-ahead behind it
   never runs: its flag 0x75cd4d is always set), 3 for a diagonal and 6 for each axis moving away. */
static void expand_dir(PathNode *p, uint8_t d)
{
    int8_t dx = 0, dy = 0;
    int16_t diag = 0;
    if (d & 1) dy = -1;
    if (d & 2) dy = 1;
    if (d & 4) dx = -1;
    if (d & 8) dx = 1;
    if (dx != 0 && dy != 0) diag = 3;
    uint8_t z = p->z;
    uint32_t t0 = tm(p->x, p->y, z);
    int16_t sd = (int8_t)d;
    if (path_is_up_ramp(t0, sd)) z--;
    uint8_t nx = (uint8_t)(p->x + dx), ny = (uint8_t)(p->y + dy), nz = z;
    if (z < 4 && path_is_down_ramp(tm(nx, ny, z + 1), sd)) nz = (uint8_t)(z + 1);
    if (at_goal(nx, ny, nz)) {
        set_pending(nx, ny, nz, (uint16_t)(p->cost + diag));
        insert_node(p, (uint8_t)sd);
        return;
    }
    uint32_t t = tm(nx, ny, nz);
    if ((uint16_t)t == 0) return;
    uint16_t s = t & 0x3f00;
    if (s != 0) {
        if (diag != 0) return;
        if ((t0 & 0x3f00) == 0 || s == 0) {
            /* dead: s is a multiple of 0x100 */
            if ((s == 0x29 || s == 2 || s == 1) && d != 1 && nz <= z) return;
            if ((s == 0x2a || s == 4 || s == 3) && d != 2 && nz <= z) return;
            if ((s == 0x2b || s == 6 || s == 5) && d != 4 && nz <= z) return;
            if ((s == 0x2c || s == 8 || s == 7) && d != 8 && nz <= z) return;
        }
    }
    int k = t >> 4 & 7;
    if (k == 0 || k == 1 || k == 5) return;
    int16_t extra;
    if (k == 3 || k == 4) {
        extra = 1;
    } else {
        extra = (int8_t)ps.road_cost;
        int16_t run = 1;
        bool hit = at_goal(nx, ny, nz);
        uint8_t bx = nx, by = ny;
        if (ps.near == 0) {
            do {
                if (hit) {
                    if (run != 0) {
                        uint16_t u = (uint16_t)tm(bx, by, nz);
                        int kk = u >> 4 & 7;
                        if (kk == 0 || kk == 1 || kk == 5) return;
                        if (u & 0x3f00) return;
                        if (bx != ps.jump_x && bx != ps.jump_y) {   /* (x against the stored y: as in the original) */
                            ps.jump_x = bx, ps.jump_y = by;
                            nx = bx, ny = by;
                        }
                    }
                    break;
                }
                bx = (uint8_t)(bx + dx), by = (uint8_t)(by + dy);
                extra++;
                uint32_t u = tm(bx, by, nz);
                if (u & 0x3f00) return;
                if (at_goal(bx, by, nz)) hit = true;
                if ((u & 0x70) == 0x20) run++;
                else hit = true;
                if (run > 6) run = 0;
            } while (run != 0);
        }
    }
    (void)tm(nx, ny, nz);
    if (dx == 1) {
        if (ps.goal.x < nx) extra += 6;
    } else if (dx == -1 && nx < ps.goal.x) {
        extra += 6;
    }
    if (dy == -1) {
        if (ny < ps.goal.y) extra += 6;
    } else if (dy == 1 && ps.goal.y < ny) {
        extra += 6;
    }
    if (cheb_goal(nx, ny) < ps.near_dist) ps.near = 1;
    set_pending(nx, ny, nz, (uint16_t)(p->cost + diag + extra));
    insert_node(p, (uint8_t)sd);
}

/* Path_SearchStep 0x470dc0: expand the first open node of the list (from the best node when one was
   set), up to the budget: the first call of a search expands 5 nodes, every later one 4 (the counter
   restarts at 1). 1 the goal node came up (the goal's parent becomes its parent), 0 the list ran out
   (after one more expansion of the last node), 2 the pool is full, 3 to be continued. */
static int search_step(void)
{
    uint8_t found = 0;
    int8_t st = 0;
    for (;;) {
        if (st != 0) {
            if (st == 3) return 3;
            if (st == 1) return 0;
            return found;
        }
        PathNode *p = ps.best_set ? ps.best : &ps.goal;
        if (ps.count > POOL_LIMIT) return 2;
        while (p->dir == 0 && st == 0) {
            PathNode *q = p->next;
            if (!q) st = 1, q = p;
            p = q;
        }
        if (at_goal(p->x, p->y, p->z)) {
            ps.goal.parent = p->parent;
            found = 1;
            if (st == 1) return 0;
            return found;
        }
        if (ps.count > POOL_LIMIT) return 2;
        if (ps.mode < 2) {
            (void)tm(p->x, p->y, p->z);
            uint8_t dd = p->dir;
            if (!(dd & 2)) expand_dir(p, 1);
            if (!(dd & 1)) expand_dir(p, 2);
            if (!(dd & 8)) expand_dir(p, 4);
            if (!(dd & 4)) expand_dir(p, 8);
        } else {
            uint8_t b = tc(p->x, p->y, p->z);
            bool close = cheb_goal(p->x, p->y) < 8;
            /* far from the goal a node doesn't expand along the direction it was reached by */
            for (int d = 1; d <= 8; d <<= 1)
                if ((b & d) == d && (close || p->dir != d)) expand_node(p, (int16_t)d);
        }
        p->dir = 0;
        if (++ps.steps == 5) {
            ps.steps = 1;
            st = 3;
        }
    }
}

/* Path_Find 0x4716f0 */
int path_find(int x, int y, int z, int dx, int dy, int dz, int mode, int ctrl)
{
    uint8_t sx = (uint8_t)x, sy = (uint8_t)y, sz = (uint8_t)z, gx = (uint8_t)dx, gy = (uint8_t)dy, gz = (uint8_t)dz;
    uint8_t c = (uint8_t)ctrl;
    if (g_path_owner < 0) {
        g_path_owner = c;
        if (sx == gx && sy == gy && sz == gz) {
            ps.steps = 0;
            g_path_owner = -1;
            uint8_t *s = &g_path_slots[0][0] + (size_t)c * PATH_SLOT_SIZE;
            s[0] = s[1] = s[2] = 0;
            return 1;
        }
    } else if (g_path_owner != c) {
        return PATH_BUSY;
    }
    AiCtl *rec = ai_get(c);
    if (!rec) game_fatal(-0x91, 0xa6, c);   /* (the original would dereference NULL) */
    ps.steps = rec->path_progress;
    if (ps.steps == 0) {
        ps.start.z = sz;
        ps.mode = (int8_t)mode;
        ps.goal.x = gx, ps.goal.y = gy, ps.goal.z = gz;
        ps.goal.next = &ps.start;
        ps.best = &ps.start;
        ps.count = 0;
        ps.len = 0;
        ps.max_idx = 0;
        ps.best_set = 0;
        ps.near_dist = 10;
        ps.road_cost = 6;
        ps.near = 1;
        ps.jump_x = ps.jump_y = 0;
        ps.start.cost = 0, ps.start.pad = 0;
        ps.goal.cost = 0;
        ps.start.dir = 0x10;
        ps.goal.dir = 0;
        ps.start.parent = NULL;
        ps.goal.parent = NULL;
        ps.start.next = NULL;
        ps.start.x = sx, ps.start.y = sy;
        if ((tc(sx, sy, sz) & 0xf) == 0 || sz > 5 || (tc(gx, gy, gz) & 0xf) == 0 || gz > 5) {
            rec->path_progress = 0;
            g_path_owner = -1;
            return PATH_FAIL;
        }
    }
    int r = search_step();
    if (r == 3) {
        rec->path_progress = ps.steps;
        return 3;
    }
    if (r == 1 || r == 2) {
        /* (+0x38 keeps its last value: the next search of this controller resumes the old state
           unless the caller clears it; as in the original) */
        extract_route(r, c);
        ps.steps = 0;
        g_path_owner = -1;
    } else if (r == 0) {
        rec->path_progress = 0;
        g_path_owner = -1;
        return 0;
    }
    return r;
}

/* Map_FindNearestRoad 0x41a490 */
int map_find_nearest_road(uint8_t q[8])
{
    uint8_t x = q[2], y = q[3], z = q[4];
    uint8_t d = tc(x, y, z) & 0xf;
    /* the step for each direction bit is the original's (1 x - 1, 2 x + 1, 4 y + 1, 8 y - 1; the jump
       table at 0x41a72c), not what the bits mean elsewhere */
    switch (d) {
    case 1:
        if (x != 0 && (tc(x - 1, y, z) & 0xf) == d) { q[2] = (uint8_t)(x - 1); return 1; }
        break;
    case 2:
        if (x != 0xff && (tc(x + 1, y, z) & 0xf) == d) { q[2] = (uint8_t)(x + 1); return 1; }
        break;
    case 4:
        if (y != 0xff && (tc(x, y + 1, z) & 0xf) == d) { q[3] = (uint8_t)(y + 1); return 1; }
        break;
    case 8:
        if (y != 0 && (tc(x, y - 1, z) & 0xf) == d) { q[3] = (uint8_t)(y - 1); return 1; }
        break;
    default: break;
    }
    /* the spiral: legs of growing length, x then y, alternating sides; the start block is tested
       first, coordinates 0 never */
    int16_t step = 1, leg = 0, cx = x, cy = y;
#define NR_OK(px, py) ((px) > 0 && (px) < 0x100 && (py) > 0 && (py) < 0x100 && z < 6 && (tc((px), (py), z) & 0xf) != 0)
    for (int16_t ring = 1;; ) {
        if (ring & 1) {
            leg = (int16_t)(leg + step);
            int16_t end = (int16_t)(leg + cx);
            while (cx != end) {
                if (NR_OK(cx, cy)) goto found;
                cx = (int16_t)(cx + step);
            }
        } else {
            step = (int16_t)-step;
            leg = (int16_t)-leg;
            int16_t end = (int16_t)(leg + cy);
            while (cy != end) {
                if (NR_OK(cx, cy)) goto found;
                cy = (int16_t)(cy + step);
            }
        }
        if (++ring > 0x40) return 0;
    }
#undef NR_OK
found:
    /* prefer the block on the start's column, then on its row */
    if (tc(x, cy, z) & 0xf) { q[3] = (uint8_t)cy; return 1; }
    if (tc(cx, y, z) & 0xf) { q[2] = (uint8_t)cx; return 1; }
    q[3] = (uint8_t)cy;
    q[2] = (uint8_t)cx;
    return 1;
}
