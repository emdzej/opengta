/* The mission runtime objects (0x473b90-0x4756ff) and their per-frame update (0x479020-0x47bb10):
   triggers, doors, cranes, timed bombs, the race ranking, car triggers, player slots, positional
   sound sources, kill scoring, respawn points, the respray / bomb shop garages. docs/missions.md. */
#include "trigger.h"
#include "../audio/audio.h"
#include "../exe.h"
#include "../map.h"
#include "../render/camera.h"
#include "../text.h"
#include "coll.h"
#include "event.h"
#include "game.h"
#include "mission.h"
#include "mission_obj.h"
#include "mission_run.h"
#include "obj.h"
#include "player.h"
#include "route.h"
#include "stubs.h"
#include "../front/front.h"
#include "blockanim.h"
#include "gang.h"
#include "mapedit.h"
#include "wanted.h"
#include "../hud/hud.h"
#include <stdio.h>
#include <string.h>

MissionRuntime g_mrt;

#define R (&g_mrt)

/* fields the Car / Ped structs don't name */
static inline int16_t car_i16(const Car *c, int ofs) { int16_t v; memcpy(&v, (const uint8_t *)c + ofs, 2); return v; }

/* the emergency models (4 squad car, 5 ambulance, 0xf, 0x10, 0x20, 0x2a fire truck) as the garages,
   doors and the kill scoring test them inline */
static bool emergency_model(int m) { return m == 4 || m == 5 || m == 0x10 || m == 0xf || m == 0x20 || m == 0x2a; }

static int style_number(void) { return g_game.style ? g_game.style->number : 0; }   /* Style_GetNumber 0x47cec0 */

/* inside a player's view rectangle, away from the map's edge (the test 0x473b90 / 0x473c50 and the
   CARWAIT trigger repeat) */
static bool on_any_screen_px(int px, int py)
{
    for (int n = player_first(); n >= 0; n = player_next(n)) {
        const int32_t *r = player_get_view_rect(n);
        if (r[0] <= px && r[2] <= py && px <= r[1] && py <= r[3] && px > 0xf && py > 0xf && px < 0x3ff1 && py < 0x3ff1)
            return true;
    }
    return false;
}

/* ---- 0x473b90-0x474080 ---- */

/* Ped_IsOnAnyScreen 0x473b90: in a view and not within 16 pixels of the map's edge. */
bool ped_is_on_any_screen(const Ped *p)
{
    bool on = on_any_screen_px(p->spr.x >> 16, p->spr.y >> 16);
    return p->spr.x > 0x100000 && p->spr.y > 0x100000 && p->spr.x < 0x3ff00000 && p->spr.y < 0x3ff00000 && on;
}

/* Car_IsOnAnyScreen 0x473c50 */
bool car_is_on_any_screen(int car)
{
    const Car *c = car_get(car);
    return on_any_screen_px(c->spr.x >> 16, c->spr.y >> 16);
}

/* Mission_MapDoorType 0x473cd0: 1..4 stay, anything else is 0 (of the low byte). */
int mission_map_door_type(int t)
{
    t &= 0xff;
    return t >= 1 && t <= 4 ? t : 0;
}

/* Mission_ClampCoord 0x473d10 */
int mission_clamp_coord(int v)
{
    if (v < 0) return 0;
    if (v > 0x3fffffff) v = 0x3fc00000;
    return v;
}

void mission_set_player_slot(int n, int v) { R->player_slot[(int8_t)n] = v; }   /* 0x473d30 */
void mission_clear_player_slot(int n) { R->player_slot[(int8_t)n] = -1; }       /* 0x473d50 */
int mission_get_player_slot(int n) { return R->player_slot[(int8_t)n]; }        /* 0x473d70 */
bool mission_has_player_slot(int n) { return R->player_slot[(int8_t)n] >= 0; }  /* 0x473d80 */
void mission_reset_player_slots(void)                                            /* 0x473da0 */
{
    for (int i = 0; i < 4; i++) R->player_slot[i] = -1;
}

/* Mission_ResetSoundSources 0x473dc0: the original clears the five flags with a dword and a byte. */
void mission_reset_sound_sources(void) { memset(R->sound_used, 0, sizeof R->sound_used); }

/* Mission_AddSoundSource 0x473dd0: the first free of the five emitters (Snd_SetEmitterA). */
int mission_add_sound_source(int32_t x, int32_t y, int32_t z)
{
    for (int i = 0; i < SOUND_SOURCE_MAX; i++)
        if (!R->sound_used[i]) {
            Snd_SetEmitterA(i, x, y, z);
            R->sound_used[i] = 1;
            return i;
        }
    return -1;
}

void mission_remove_sound_source(int slot)   /* 0x473e30 */
{
    Snd_ClearEmitterA(slot);
    R->sound_used[(int8_t)slot] = 0;
}

void mission_remove_all_sound_sources(void)  /* 0x473e50 */
{
    for (int i = 0; i < SOUND_SOURCE_MAX; i++) {
        Snd_ClearEmitterA(i);
        R->sound_used[i] = 0;
    }
}

/* Mission_ResetTimedBombs 0x473e70 */
void mission_reset_timed_bombs(void)
{
    for (int i = 0; i < TIMED_BOMB_MAX; i++) {
        TimedBomb *b = &R->bombs[i];
        b->who = -1, b->state = 0, b->id = -1, b->kind = -1, b->frames = -1;
    }
}

/* Mission_AddTimedBomb 0x473ea0: only into a slot whose `who` is free (-1); state 0 until a script
   sets it counting. */
bool mission_add_timed_bomb(int n, int kind, int id, int who, int frames)
{
    TimedBomb *b = &R->bombs[(int16_t)n];
    if (b->who != -1) return false;
    b->kind = (int16_t)kind;
    b->id = (int16_t)id;
    b->who = (int16_t)who;
    b->state = 0;
    b->frames = (int8_t)frames;
    return true;
}

/* Mission_UpdateTimedBombs 0x473ef0: a counting bomb whose frame count was 0 (before the decrement)
   goes off at its car / ped as a crime report (Police_ReportCrime with the owner) and frees the slot. */
void mission_update_timed_bombs(void)
{
    for (int i = 0; i < TIMED_BOMB_MAX; i++) {
        TimedBomb *b = &R->bombs[i];
        if (b->state != 1) continue;
        if (b->frames-- != 0) continue;
        int32_t x = 0, y = 0, z = 0;
        if (b->kind == 0) {
            const Car *c = car_get(b->id);
            x = c->spr.x, y = c->spr.y, z = c->spr.z;
        } else if (b->kind == 1) {
            const Ped *p = ped_get(b->id);
            x = p->spr.x, y = p->spr.y, z = p->spr.z;
        }
        b->state = 2;
        police_report_crime(b->kind, b->id, b->who, x, y, z);
        b->who = -1, b->id = -1, b->kind = -1, b->frames = -1;
    }
}

/* Mission_ResetProgress 0x473fb0: no checkpoints (20 bytes of -1), the finish frames and order -1,
   every player at checkpoint 0. */
void mission_reset_progress(void)
{
    memset(R->targets, 0xff, sizeof R->targets);
    for (int i = 0; i < 4; i++) R->progress[i] = -1;
    R->ntargets = 0;
    R->ranks_sorted = 0;
    for (int n = player_first(); n >= 0; n = player_next(n)) R->progress[n] = 0;
}

/* Mission_SetTargetOrder 0x474020 */
void mission_set_target_order(int trigger)
{
    int k = (int8_t)R->ntargets;
    if (k < 0 || k >= TRIGGER_TARGETS) game_fatal(-0xed, 0x1a3, k);   /* the original writes past the 20 */
    R->targets[k] = (int8_t)trigger;
    R->ntargets++;
}

/* Mission_AllPlayersBelow 0x474040: no player's next checkpoint trigger is above n */
bool mission_all_players_below(int n)
{
    for (int p = player_first(); p >= 0; p = player_next(p))
        if (R->targets[R->progress[(int8_t)p]] <= n) return false;
    return true;
}

bool mission_is_flag_set(int n) { return R->city_flag[(int16_t)n] == 1; }      /* 0x474080 */
bool door_is_face_slot_used(int n) { return R->face_used[n & 0xff] != 0; }      /* 0x4740a0 */
int mission_get_byte7711b0(int n) { return R->city_enable[n & 0xff]; }          /* 0x4740c0 */

/* ---- doors ---- */

/* Door_Create 0x4740f0: the door at block (x, y, z) facing `orient`; a block animation on the face
   that opens (with the side faces of the neighbouring blocks set to side_tile when nonzero), its
   tile remap slot marked in use. No bound on the 64 records in the original. */
int door_create(int x, int y, int z, int orient, int frames, int tile, int side_tile, int cond, int persistent)
{
    int n = R->ndoors;
    if (n >= DOOR_MAX) game_fatal(-0x4a, 0x56, n);   /* (port: the original runs past the table) */
    Door *d = &R->doors[n];
    d->state = DOOR_CLOSED;
    d->orient = orient;
    d->x = (uint8_t)x, d->y = (uint8_t)y, d->z = (uint8_t)z;
    d->cond = (int16_t)cond;
    d->persistent = (uint8_t)persistent;
    int slot = 0;
    if ((uint8_t)side_tile) {
        switch (orient) {
        case 0: case 1:
            for (int i = -1; i <= 1; i++) map_set_block_face(x, y + i, z + 1, 4, side_tile & 0xff);
            break;
        case 2: case 3:
            for (int i = -1; i <= 1; i++) map_set_block_face(x + i, y, z + 1, 4, side_tile & 0xff);
            break;
        default: game_fatal(-0x4a, 0x56, orient);
        }
    }
    d->tile = tile;
    d->frames = frames;
    d->locked = 1;
    int a;
    switch (orient) {
    case 0:
    case 2:   /* one face of the door's block: left (1) or the top (3) */
        a = blockanim_create(x, y, z, orient);
        d->anim = (uint8_t)a;
        blockanim_set_tile(a, 2, tile);
        slot = blockanim_get_tile_slot(d->anim) & 0xff;
        map_set_block_face(x, y, z, orient == 0 ? 1 : 3, slot);
        map_or_block_flags(x, y, z, 0x80);
        break;
    case 1:   /* the face between this block and the next one in x */
        a = blockanim_create(x + 1, y, z, 1);
        d->anim = (uint8_t)a;
        blockanim_set_tile(a, 2, tile);
        slot = blockanim_get_tile_slot(d->anim) & 0xff;
        map_set_block_face(x, y, z, 1, 0);
        map_or_block_flags(x, y, z, 0x80);
        map_set_block_face(x + 1, y, z, 0, slot);
        map_or_block_flags(x + 1, y, z, 0x80);
        break;
    case 3:   /* the same in y */
        a = blockanim_create(x, y + 1, z, 3);
        d->anim = (uint8_t)a;
        blockanim_set_tile(a, 2, tile);
        slot = blockanim_get_tile_slot(d->anim) & 0xff;
        map_set_block_face(x, y, z, 3, 0);
        map_or_block_flags(x, y, z, 0x80);
        map_set_block_face(x, y + 1, z, 2, slot);
        map_or_block_flags(x, y + 1, z, 0x80);
        break;
    default: game_fatal(-0x4a, 0x56, orient);
    }
    R->face_used[slot] = 1;
    d->face_slot = (uint8_t)slot;
    stub_calls[STUB_DOOR]++;
    return R->ndoors++;
}

/* Door_ResetAllTemp 0x4745c0: the temporary doors closed and locked (no animation, no count). */
void door_reset_all_temp(void)
{
    for (int i = 0; i < DOOR_MAX; i++)
        if (!R->doors[i].persistent) R->doors[i].state = DOOR_CLOSED, R->doors[i].locked = 1;
}

/* Door_Open 0x4745f0: the block becomes passable (kind bits 0x40), the face slot free, the animation
   runs forward and fires event 2 (Door_OnClosed: the opening finished) */
void door_open(int door)
{
    Door *d = &R->doors[door];
    d->state = DOOR_OPENING;
    map_set_block_kind(d->x, d->y, d->z, 0x40);
    R->face_used[d->face_slot] = 0;
    blockanim_start_forward(d->anim, 0xf, d->frames, 2, d->tile);
    blockanim_set_event(d->anim, 2, door);
}

/* Door_Close 0x474670: backwards, event 3. The block's kind is not set back. */
void door_close(int door)
{
    Door *d = &R->doors[door];
    d->state = DOOR_CLOSING;
    blockanim_start_reverse(d->anim, 0xf, d->frames, 2, d->tile);
    blockanim_set_event(d->anim, 3, door);
    R->face_used[d->face_slot] = 1;
}

/* Door_SetOpenAny 0x4746d0: unlocks it for any car (the condition stays) within `range` blocks. */
void door_set_open_any(int door, int range)
{
    Door *d = &R->doors[door];
    d->range = range;
    d->locked = 1;   /* quirk: locked, yet counted as unlocked (Door_Lock then doesn't uncount it) */
    d->car = -1, d->remap = -1;
    R->doors_unlocked++;
}

/* Door_SetOpenByCar 0x474710: condition 2 (car id, remap; -2 or below keeps the old value) */
void door_set_open_by_car(int door, int range, int car, int remap)
{
    Door *d = &R->doors[door];
    d->range = range;
    d->locked = 0;
    if ((int16_t)car > -2) d->car = (int16_t)car;
    d->cond = 2;
    if ((int16_t)remap > -2) d->remap = (int16_t)remap;
    R->doors_unlocked++;
}

/* Door_SetOpenMode5 0x474770: condition 5 (a bomb car, of script line `car` or any) */
void door_set_open_mode5(int door, int range, int car)
{
    Door *d = &R->doors[door];
    d->range = range;
    d->locked = 0;
    d->cond = 5;
    if ((int16_t)car > -2) d->car = (int16_t)car;
    R->doors_unlocked++;
}

/* Door_SetOpenMode3 0x4747b0: condition 3 (the model the player views) */
void door_set_open_mode3(int door, int range, int model)
{
    Door *d = &R->doors[door];
    d->range = range;
    d->locked = 0;
    if ((int16_t)model > -2) d->car = (int16_t)model;
    d->remap = -1;
    d->cond = 3;
    R->doors_unlocked++;
}

/* Door_Lock 0x474800 / Door_Unlock 0x474840: not while animating; keep the unlocked count. */
bool door_lock(int door)
{
    Door *d = &R->doors[door];
    if (d->state == DOOR_CLOSING || d->state == DOOR_OPENING) return false;
    if (d->locked == 0) R->doors_unlocked--;
    d->locked = 1;
    return true;
}

bool door_unlock(int door)
{
    Door *d = &R->doors[door];
    if (d->state == DOOR_CLOSING || d->state == DOOR_OPENING) return false;
    if (d->locked != 0) R->doors_unlocked++;
    d->locked = 0;
    return true;
}

/* Door_OnClosed 0x474880 (event 2, scheduled by the opening): the door is open. Door_OnOpened
   0x4748c0 (event 3, by the closing): closed. Both play sample 0x26 at the door. */
void door_on_closed(int door)
{
    Door *d = &R->doors[door];
    d->state = DOOR_OPEN;
    Snd_PlayAtXY(d->x, d->y, 0x26);   /* block coordinates as the original passes them */
}

void door_on_opened(int door)
{
    Door *d = &R->doors[door];
    d->state = DOOR_CLOSED;
    Snd_PlayAtXY(d->x, d->y, 0x26);
}

/* Door_PlayerInFront 0x474900: a player within 3 blocks on the opening side, in the door's row. */
bool door_player_in_front(int door)
{
    const Door *d = &R->doors[door];
    int dx = d->x, dy = d->y;
    for (int n = player_first(); n >= 0; n = player_next(n)) {
        const int32_t *p = player_get_controlled_pos(n);
        int px = p[0] >> 22, py = p[1] >> 22;
        switch (d->orient) {
        case 0:
            if (py == dy && px >= dx && px <= dx + 3) return true;
            break;
        case 1:
            if (py == dy && px >= dx - 3 && px <= dx) return true;
            /* falls into case 2 as the original */
            /* fall through */
        case 2:
            if (px == dx && py >= dy && py <= dy + 3) return true;
            break;
        case 3:
            if (px == dx && py >= dy - 3 && py <= dy) return true;
            break;
        }
    }
    return false;
}

/* Door_Update 0x479f00 (only unlocked doors, only open or closed ones): a player out of range closes
   an open door unless one stands in front of it; a closed one opens for a player in range in a car
   that meets the condition. */
void door_update(int door)
{
    Door *d = &R->doors[door];
    if (d->state != DOOR_OPEN && d->state != DOOR_CLOSED) return;
    for (int n = player_first(); n >= 0; n = player_next(n)) {
        const int32_t *p = player_get_controlled_pos(n);
        int kind = player_get_controlled_kind(n);
        int px = p[0] >> 22, py = p[1] >> 22;
        if (px < d->x - d->range || px > d->x + d->range || py < d->y - d->range || py > d->y + d->range) {
            if (d->state == DOOR_OPEN && !door_player_in_front(door)) door_close(door);
            continue;
        }
        if (d->state != DOOR_CLOSED || kind != PLAYER_IN_CAR) continue;
        const Car *c = car_get(player_get_controlled_id(n));
        bool open = false;
        switch (d->cond) {
        case 0: open = true; break;
        case 1: open = emergency_model(c->model) || c->model == 0x2c || c->vtype == 0xe; break;   /* (0x2a not tested here: 0x2c and tanks instead) */
        case 2: open = (d->car == -1 || c->id == d->car) && ((uint16_t)d->remap == 0xffff || (uint16_t)d->remap == c->remap); break;
        case 3: open = car_get(player_get_view_id(n))->model == d->car; break;   /* the viewed car, not the controlled one */
        case 5: open = (d->car == -1 || c->script_line == d->car) && c->bomb != 0; break;
        }
        if (open) door_open(door);
    }
}

/* ---- triggers ---- */

/* Trigger_Create 0x4744a0. The kinds that watch a car or ped keep c in d (and c = 0). No bound on the
   210 records in the original. */
int trigger_create(int x, int y, int z, int kind, int a, int b, int c, int persistent)
{
    int n = R->ntriggers;
    if (n >= TRIGGER_MAX) game_fatal(-0xed, 0x1a5, n);   /* (port) */
    Trigger *t = &R->triggers[n];
    t->x = (uint8_t)x, t->y = (uint8_t)y, t->z = (uint8_t)z;
    t->kind = kind;
    t->a = a;
    t->b = b;
    t->c = c;
    t->state = TRIG_ARMED;
    t->d = -1;
    t->proc = -1;
    t->persistent = (uint8_t)persistent;
    switch (kind) {
    case 10: case 0xb: case 0xe: case 0xf: case 0x10: case 0x13: case 0x15: case 0x18: case 0x1b:
    case 0x1c: case 0x1d: case 0x1e: case 0x19: case 0x21:
        t->c = 0;
        t->d = c;
        break;
    }
    stub_calls[STUB_TRIGGER]++;
    return R->ntriggers++;
}

void trigger_set_flag1c(int trigger, int proc) { R->triggers[trigger].proc = (int8_t)proc; }   /* 0x474580 */

/* Trigger_DisableAllTemp 0x4745a0: every record (not only the used ones), unless persistent */
void trigger_disable_all_temp(void)
{
    for (int i = 0; i < TRIGGER_MAX; i++)
        if (!R->triggers[i].persistent) R->triggers[i].state = TRIG_DEAD;
}

void trigger_disarm(int trigger) { R->triggers[trigger].state = TRIG_ARMED; }   /* 0x474d10 */

void trigger_set_state3(int trigger)   /* 0x474d30 */
{
    if (trigger < 0 || trigger >= TRIGGER_MAX) { game_fatal(-0xed, 0x1a5, trigger); return; }
    R->triggers[trigger].state = TRIG_RUNNING;
}

void trigger_kill(int trigger)   /* 0x474d70 */
{
    if (trigger < 0 || trigger >= TRIGGER_MAX) { game_fatal(-0xed, 0x1a6, trigger); return; }
    R->triggers[trigger].state = TRIG_DEAD;
}

void trigger_set_param14(int trigger, int v) { R->triggers[trigger].d = v; }   /* 0x474db0 */
void trigger_set_param08(int trigger, int v) { R->triggers[trigger].a = v; }   /* 0x474dd0 */
void trigger_reset(int trigger) { R->triggers[trigger].state = TRIG_FIRE; }     /* 0x47baf0 */

/* ---- ranking / score target ---- */

/* Mission_ScoreTargetReached 0x4749c0: a score game (type 0) where a player has the target */
bool mission_score_target_reached(void)
{
    int8_t kind;
    int32_t value = 0;
    front_get_multi_target(&kind, &value);
    if (kind != 0) return false;
    for (int n = player_first(); n >= 0; n = player_next(n))
        if (value > 0 && player_get_score(n) >= value) return true;
    return false;
}

/* Mission_GetPlayerVal 0x474a20: the finish frame of a player who has finished, else -1 */
int mission_get_player_val(int n)
{
    for (int i = 0; i < 4; i++)
        if (R->finish_order[i] == (int8_t)n) return R->finish_frame[n];
    return -1;
}

static int finish_place(int p)
{
    for (int i = 0; i < 4; i++)
        if (R->finish_order[i] == p) return i;
    return 99;
}

/* Mission_SortPlayerRanks 0x474a50: bubble sort of the four player numbers: more checkpoints first;
   equal checkpoints by finishing place, then (both unplaced or the same place) the one nearer
   (squared blocks) to the next checkpoint. The distance uses the trigger's x for both axes' second
   term (quirk: (py - ty) * (py - tx) + (px - tx)^2). */
void mission_sort_player_ranks(void)
{
    for (int i = 0; i < 4; i++) R->ranks[i] = (int16_t)i;
    R->ranks_sorted = 1;
    bool sorted;
    do {
        sorted = true;
        for (int i = 0; i < 3; i++) {
            int a = R->ranks[i], b = R->ranks[i + 1];
            bool swap = false;
            if (R->progress[a] < R->progress[b]) swap = true;
            else if (R->progress[a] == R->progress[b]) {
                int fa = finish_place(a), fb = finish_place(b);
                if (fa == fb) {
                    const int32_t *pa = player_get_controlled_pos(a), *pb = player_get_controlled_pos(b);
                    if (R->progress[a] > 0xd1) game_fatal(-0xed, 0x1a3, R->progress[a]);
                    const Trigger *t = &R->triggers[R->progress[a]];   /* (indexes triggers by the count, as the original) */
                    int tx = t->x, ty = t->y;
                    int ax = (pa[0] >> 22) - tx, bx = (pb[0] >> 22) - tx;
                    int da = ((pa[1] >> 22) - ty) * ((pa[1] >> 22) - tx) + ax * ax;
                    int db = ((pb[1] >> 22) - ty) * ((pb[1] >> 22) - tx) + bx * bx;
                    swap = da < db;
                } else if (fb < fa) swap = true;
            }
            if (swap) {
                R->ranks[i + 1] = (int16_t)a;
                R->ranks[i] = (int16_t)b;
                sorted = false;
            }
        }
    } while (!sorted);
}

/* Mission_GetPlayerRank 0x474bd0 */
int mission_get_player_rank(int n)
{
    if (!R->ranks_sorted) mission_sort_player_ranks();
    for (int i = 0; i < 4; i++)
        if (R->ranks[i] == (int8_t)n) return i;
    return -1;
}

/* ---- car triggers ---- */

void car_trig_reset_all(void)   /* 0x474c10 */
{
    for (int i = 0; i < CARTRIG_MAX; i++) R->cartrigs[i].trigger = R->cartrigs[i].car = -1;
}

bool car_trig_add(int trigger, int car)   /* 0x474c30 */
{
    for (int i = 0; i < CARTRIG_MAX; i++)
        if (R->cartrigs[i].trigger == -1) {
            R->cartrigs[i].trigger = trigger;
            R->cartrigs[i].car = car;
            return true;
        }
    return false;
}

/* CarTrig_CheckEnter 0x474c60: a player driving `player_car` when the binding's car is `car` fires
   (state 0) the bound trigger unless it is dead, and the binding is freed. */
void car_trig_check_enter(int car, int player_car)
{
    for (int i = 0; i < CARTRIG_MAX; i++) {
        CarTrig *ct = &R->cartrigs[i];
        if (ct->car != car) continue;
        for (int n = player_first(); n >= 0; n = player_next(n))
            if (player_get_controlled_kind(n) == PLAYER_IN_CAR && player_get_controlled_id(n) == player_car &&
                R->triggers[ct->trigger].state != TRIG_DEAD) {
                ct->car = -1;
                R->triggers[ct->trigger].state = TRIG_FIRE;
                ct->trigger = -1;
                return;
            }
    }
}

/* ---- cranes ---- */

static bool crane_holds(int car)
{
    for (int i = 0; i < CRANE_MAX; i++) {
        if (R->cranes[i].car == car) return true;
        for (int k = 0; k < CRANE_STACK; k++)
            if (R->cranes[i].stack[k] == car) return true;
    }
    return false;
}

/* Crane_CheckSpace 0x474df0: every stacked car still in its grid place and no other car (not held by
   a crane, not `car`) in the 4 x 2 drop area; with nothing stacked, no other car on the crane's
   block. Quirk: whether stack slot k is used is read from crane 0's record. */
bool crane_check_space(int crane, int car)
{
    Crane *cr = &R->cranes[crane];
    bool ok = true;
    int n = cr->count;
    for (int k = 0, place = 5; k < n; k++, place--) {
        coll_unlock();
        if (R->cranes[0].stack[k] == -1) continue;
        const Car *sc = car_get(cr->stack[k]);
        for (CollHit *h = coll_query_cars(sc->spr.x, sc->spr.y, sc->id); h; h = h->next) {
            const Car *o = h->owner;
            if (crane_holds(o->id) || o->id == car) continue;
            int bx = o->spr.x >> 22, by = o->spr.y >> 22;
            if (bx >= cr->x >> 6 && by >= cr->y >> 6 && bx <= (cr->x >> 6) + 3 && by <= (cr->y >> 6) + 1) {
                ok = false;
                break;
            }
        }
        if (ok) {
            const Car *c = car_get(cr->stack[k]);
            if (c->spr.x != (place % 3 + (cr->x >> 6)) * 0x400000 + 0x200000 ||
                c->spr.y != ((place / 3) * cr->dir + (cr->y >> 6)) * 0x400000 + 0x200000)
                ok = false;
        }
        n = cr->count;
    }
    if (n == 0) {
        for (CollHit *h = coll_query_cars(cr->x << 16, cr->y << 16, (int16_t)cr->car); h; h = h->next) {
            const Car *o = h->owner;
            if (o->spr.x >> 22 == cr->x >> 6 && ((int16_t)(o->spr.y >> 16) & ~0x3f) == (cr->y & ~0x3f) && o->id != cr->car)
                ok = false;
        }
    }
    coll_unlock();
    return ok;
}

/* Crane_RequestCar 0x475010 */
int crane_request_car(int crane, int car)
{
    Crane *cr = &R->cranes[crane];
    const Car *c = car_get(car);
    if (cr->state != 0) return 2;
    if (cr->count == CRANE_STACK) return 1;
    if (c->burning > 0) return 3;
    if (c->script_line >= 0) return 4;
    cr->car = car;
    cr->state = 1;
    return 0;
}

/* the car turned toward heading 0x100 by 8 a frame (snapped within 7) */
static void crane_turn_car(Car *c)
{
    int16_t a = c->spr.angle, d = (int16_t)(a - 0x100);
    if (d < -7 || d > 7) {
        if (d < 0) d = (int16_t)(a + 0x300);
        a = (int16_t)(d < 0x201 ? a - 8 : a + 8);
    } else {
        a = 0x100;
    }
    a &= 0x3ff;
    c->spr.angle = a;
    c->next_heading = (c->next_heading & ~0xffff) | (uint16_t)a;   /* a 16-bit store */
    c->front_heading = a;
}

static bool car_near(const Car *c, int px, int py)
{
    int32_t x = px * 0x10000, y = py * 0x10000;
    return x - 0x200000 <= c->spr.x && y - 0x200000 <= c->spr.y && c->spr.x <= x + 0x200000 && c->spr.y <= y + 0x200000;
}

static void crane_drop_car(Crane *cr)   /* the car left: no crane holds it, back to the rest position */
{
    int car = cr->car;
    for (int i = 0; i < CRANE_MAX; i++)
        if (R->cranes[i].car == car) R->cranes[i].car = -1;
    cr->state = 7;
}

/* Crane_Update 0x475090: states 1 (arm out over the pickup point), 2 (car lifted toward the arm),
   3 (arm and car back), 4 (car up to z + 0x3e), 5 (arm to the drop row), 6 (car put in its grid
   place: 3 x 2 cars), 7 (arm home), 8 (full: the stack is deleted once off screen), 10 (blocked:
   "crane_screwed"). */
void crane_update(int crane)
{
    Crane *cr = &R->cranes[crane];
    if (cr->state != 7 && cr->state != 8 && cr->car >= 0 && !crane_check_space(crane, -1)) cr->state = 10;
    int pick_y = cr->y - cr->dir * 0x80;
    Car *c;
    switch (cr->state) {
    case 1:
        if (obj_step_axis(cr->obj, pick_y, -cr->dir, 1)) cr->state++;
        if (!car_near(car_get(cr->car), cr->x, pick_y)) crane_drop_car(cr);
        break;
    case 2:
        c = car_get(cr->car);
        if (!car_near(c, cr->x, pick_y)) {
            crane_drop_car(cr);
            break;
        }
        if (c->driver == -1) {
            c->owner_status = 99;
            c->script_held = 1;
            crane_turn_car(c);
            if (car_move_towards(cr->car, cr->x, pick_y, cr->z + 2, 1)) cr->state++;
        }
        break;
    case 3:
        if (obj_step_axis(cr->obj, cr->y, cr->dir, 1)) cr->state++;
        car_move_axis(cr->car, cr->y, cr->dir, 1);
        crane_turn_car(car_get(cr->car));
        break;
    case 4:
        if (car_move_axis(cr->car, cr->z + 0x3e, 1, 2)) cr->state++;
        break;
    case 5:
        if (obj_step_axis(cr->obj, cr->y - cr->dir * 0x40, -cr->dir, 1)) cr->state++;
        break;
    case 6:
        if (car_place_in_grid(cr->car, 5 - cr->count, cr->dir)) {
            cr->stack[cr->count] = cr->car;
            cr->car = -1;
            cr->state = 0;
            cr->count++;
        }
        if (cr->count == CRANE_STACK) cr->state = 8;
        break;
    case 7:
        if (obj_step_axis(cr->obj, cr->y, cr->dir, 1)) {
            cr->state = 0;
            if (cr->car >= 0) car_get(cr->car)->script_held = 0;
        }
        break;
    case 8:
        /* Quirk: the crane's pixel coordinates go to Pos_IsNearScreen, which takes 16.16: the point is
           near the map's corner, never on screen, so a full stack goes at once. */
        if (!pos_is_near_screen(cr->x, cr->y)) {
            for (int i = 0; i < cr->count; i++) {
                car_delete(cr->stack[i]);
                cr->stack[i] = -1;
            }
            cr->count = 0;
            cr->state = 0;
        }
        break;
    case 10:
        hud_show_subtitle(1, text_get("crane_screwed"));
        cr->state = 7;
        break;
    }
    if (cr->car != -1) car_commit_move(car_get(cr->car & 0xffff));
}

/* Crane_Create 0x475620: its object (type 0x1e) at pixel (x, y, z), idle. No bound in the original. */
int crane_create(int x, int y, int z, int dir)
{
    int n = R->ncranes;
    if (n >= CRANE_MAX) game_fatal(-0x4a, 0x56, n);   /* (port) */
    Crane *cr = &R->cranes[n];
    cr->obj = (int16_t)obj_create(x << 16, y << 16, z * 0x10000 - 1, 0x1e, 0);
    cr->x = x, cr->y = y, cr->z = z;
    cr->state = 0;
    cr->dir = dir;
    stub_calls[STUB_CRANE]++;
    return R->ncranes++;
}

bool crane_is_car_held(int car) { return crane_holds(car); }   /* Crane_IsCarHeld 0x4756c0 */

/* ---- 0x479020-0x47bb10 ---- */

/* Score_PedKilled 0x479020: who gets what for the death of `victim`. Cause 3: the player of the ped
   it targeted; else the killer (+0x5a), with the cause refined: 7 killed by a cop (control 1), 4 run
   over by the car it was the driver slot of, 5 a player's kill during the anims 0x8c / 0x91, 6 by a
   driver of an emergency car (not for cause 2). An unclaimed death on screen in single player goes to
   player 0 (unless it is player 0). Civilians score by objective, the others by control type. */
void score_ped_killed(Ped *victim, int cause)
{
    int pl;
    if (cause == 3) {
        pl = victim->target_ped < 0 ? -1 : (int8_t)player_find_by_ped(victim->target_ped);
    } else if (victim->u5a < 0) {
        pl = -1;
    } else {
        pl = (int8_t)player_find_by_ped(victim->u5a);
        const Ped *k = ped_get(victim->u5a);
        if (k->control == 1) cause = 7;
        else if (k->state == 7 && k->car == victim->id - PED_DRIVER_FIRST) cause = 4;
        else if (pl >= 0 && (victim->anim == 0x8c || victim->anim == 0x91)) cause = 5;
        else if (cause != 2 && k->car >= 0 && k->state == 7 && emergency_model(car_get(k->car)->model)) cause = 6;
    }
    if (victim->state == 7) car_sync_driver_sprite(victim->id);
    if (pl == -1) {
        if (g_player_count != 1) return;
        if (player_get_ped(player_first()) == victim->id) return;
        if (!ped_is_on_screen(victim)) return;
        pl = (int8_t)player_first();
        if (pl == -1) return;
    }
    if (pl < -1) return;
    if (victim->control == 1) player_add_wanted_points(victim->id, 100);
    int bonus = -1;
    int16_t ctl = victim->control;
    if (ctl == 0 || ctl == -1) {
        switch (victim->objective) {
        case 0x14: bonus = 5; break;
        case 0x15: case 0x16: case 0x2a: bonus = 7; break;
        case 0x17: bonus = 8; break;
        case 0x18: case 0x1c: case 0x1a: case 0x1d: case 0x1b: case 0x1e: bonus = 9; break;
        case 0x1f: case 0x21: case 0x28: case 0x20: case 0x32: case 0x27: case 0x33: case 0x37: bonus = 2; break;
        case 0x22: bonus = 0xc; break;
        case 0x25: bonus = 10; break;
        case 0x29: case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x2f: case 0x31:
            player_sub_score(pl, 10000);   /* killing these costs */
            return;
        case 0x30: bonus = 0xd; break;
        case 0x36: bonus = 0xe; break;
        default: bonus = 1; break;   /* (0x23 too) */
        }
    } else {
        switch (ctl) {
        case 1: bonus = 2; break;
        case 2: bonus = 3; break;
        case 6: bonus = 4; break;
        case 8: case 9: case 10: case 0xb:   /* a player: not by himself */
            if (victim->u5a != victim->id) bonus = 10;
            break;
        }
    }
    if (bonus >= 0) player_award_bonus(pl, bonus, victim->spr.x, victim->spr.y, victim->spr.z, 1, cause);
}

static int cheb(int ax, int ay, int bx, int by)
{
    int dx = ax - bx, dy = ay - by;
    dx = dx < 0 ? -dx : dx, dy = dy < 0 ? -dy : dy;
    return dx > dy ? dx : dy;
}

/* Player_ChooseRespawnPoint 0x479610 (network respawns): the hospitals sorted by distance from the
   ped (Chebyshev blocks); from `start` on, the first whose nearest player is more than 20 blocks
   away. Otherwise a second pass meant to sort by that distance: quirks kept: it writes its swaps
   into the first order, then returns the first position (not the hospital) whose entry differs from
   the nearest hospital; with one hospital the low byte of the last order entry. */
int player_choose_respawn_point(const Ped *p, int start)
{
    int n = (int16_t)g_hospitals;
    if (n > LOCATION_N) n = LOCATION_N;
    uint8_t far[LOCATION_N] = { 0 };
    int16_t mind[LOCATION_N], order[LOCATION_N], dist[LOCATION_N], idx[LOCATION_N];
    for (int i = 0; i < LOCATION_N; i++) mind[i] = order[i] = dist[i] = -1, idx[i] = (int16_t)i;
    const BlockXYZ *loc = g_locations + LOC_HOSPITAL * LOCATION_N;
    int px = (p->spr.x >> 22) & 0xff, py = (p->spr.y >> 22) & 0xff;
    for (int i = 0; i < n; i++) {
        order[i] = (int16_t)i;
        dist[i] = (int16_t)cheb(px, py, loc[i].x, loc[i].y);
    }
    bool sorted;
    do {
        sorted = true;
        for (int i = 0; i < n - 1; i++)
            if (dist[order[i + 1]] < dist[order[i]]) {
                int16_t t = order[i];
                order[i] = order[i + 1], order[i + 1] = t;
                sorted = false;
            }
    } while (!sorted && n - 1 >= 1);
    for (int i = 0; i < n; i++) {
        int16_t best = 0x7fff;
        for (int q = player_first(); q >= 0; q = player_next(q)) {
            const int32_t *pos = player_get_controlled_pos(q);
            int16_t d = (int16_t)cheb(pos[0] >> 22, pos[1] >> 22, loc[i].x, loc[i].y);
            if (d < best) best = d;
        }
        mind[i] = best;
        if (mind[i] > 0x14) far[i] = 1;
    }
    for (int k = (int16_t)start; k < n; k++)
        if (far[order[k]] == 1) return order[k] & 0xff;
    do {
        sorted = true;
        for (int i = 0; i < n - 1; i++)
            if (mind[idx[i]] < mind[idx[i + 1]]) {
                order[i] = idx[i + 1];
                idx[i + 1] = idx[i];
                sorted = false;
            }
    } while (!sorted && n - 1 >= 1);
    for (int i = 0; i < n; i++)
        if (idx[i] != order[0]) return i;
    return n > 0 ? order[n - 1] & 0xff : 0;
}

/* Player_Respawn 0x479930: at the nearest hospital (single player) or a free point (network); the
   camera and the control on the ped, wanted level and points cleared, walking, health 100, a carried
   object deleted. */
void player_respawn(int n)
{
    Ped *p = ped_get(player_get_ped(n));
    if (g_player_count < 2) player_respawn_at_station(p, 1);
    else player_respawn_multi(p);
    player_set_view_target(n, PLAYER_ON_FOOT, p->id);
    player_set_controlled(n, PLAYER_ON_FOOT, p->id);
    player_clear_wanted_level(p->id);
    player_clear_wanted_points(p->id);
    CameraPlayer cp;   /* Camera_Snap 0x43c5d0 */
    CameraWorld w = player_camera_world(n);
    player_camera(n, &cp);
    camera_snap(&cp, &w);
    player_camera_store(n, &cp);
    p->state = 2;   /* the original writes 2 and 0 as two halves */
    p->health = 100;
    if (p->carried >= 0) {
        obj_delete(p->carried);
        p->carried = -1;
    }
}

/* Player_SetStatByCity 0x4799d0: sets the player's wanted points to a per-city level for a crime of
   `kind` 1..3 depending on the wanted level (0..2; 4 stops), then reports a crime (kind 7). */
void player_set_stat_by_city(int n, int kind)
{
    int ped = (int16_t)player_get_ped(n);
    int city = style_number();
    int wp = (int16_t)player_get_wanted_points(ped);
    int lvl = player_get_wanted_level_by_ped(ped);
    int v = 0;
    /* tiers: A = 0x98 / 0x66 / 0x66, B = - / 0xca / 0xb1 (city 1: falls to C), C = 0xfc, D = 0x160 /
       0xfc / 0xfc, E = 0x1f6 / 0x179 / 0x160 */
    enum { TA, TB, TD, TE, TNONE } tier = TNONE;
    if (lvl == 4) return;
    if (lvl == 0) tier = kind == 1 ? TA : kind == 2 ? TB : kind == 3 ? TD : TE;
    else if (lvl == 1) tier = kind == 1 ? TB : kind == 2 ? TD : TE;
    else if (lvl == 2) tier = kind == 1 ? TD : TE;
    switch (tier) {
    case TA: v = city == 1 ? 0x98 : city == 2 || city == 3 ? 0x66 : 0; break;
    case TB: v = city == 1 ? 0xfc : city == 2 ? 0xca : city == 3 ? 0xb1 : 0; break;
    case TD: v = city == 1 ? 0x160 : city == 2 || city == 3 ? 0xfc : 0; break;
    case TE: v = city == 1 ? 0x1f6 : city == 2 ? 0x179 : city == 3 ? 0x160 : 0; break;
    case TNONE: break;
    }
    player_add_wanted_points(ped, v - wp);
    police_report_crime(1, ped, 7, 0, 0, 0);
}

/* Mission_InitCityTables 0x479ab0: the per-city tables (the exceptions are instruction operands of
   the original; three bytes come from the exe at 0x4abe6d + city - 1 and the flag lists from
   0x4b3210 / 0x4b3224 / 0x4b3254, u16 lists ended by 0xffff), then every runtime table reset. */
void mission_init_city_tables(void)
{
    int city = style_number();
    memset(R->city_enable, 1, sizeof R->city_enable);
    memset(R->face_used, 0, sizeof R->face_used);
    static const uint16_t off1[] = { 0x6c, 0x6d, 0x6e, 0xa8, 0xbe, 0xc2 };
    static const uint16_t on1[] = { 0x2e, 0x37, 0x38, 0x72, 0xa8, 0xbb, 0xbe, 0xc2 };
    static const uint16_t off2[] = { 0x14, 0x3a, 0x6c, 0x70, 0x7e, 0xa0, 0xbc, 0xc1 };
    static const uint16_t on2[] = { 0x06, 0x30, 0x6f, 0x70, 0x7e, 0x8e, 0xa0, 0xc1 };
    static const uint16_t off3[] = { 0x65, 0x66, 0x8c, 0xab, 0xc5 };
    static const uint16_t on3[] = { 0x45, 0x46, 0x64, 0x65, 0x66, 0x6a, 0x7c, 0x7d, 0x9b, 0x9c, 0x9f, 0xa0, 0xa1,
                                    0xa2, 0xa3, 0xa4, 0xa9, 0xaa, 0xb8, 0xb9, 0xba, 0xc5 };
#define SETS(arr, tab, v) for (size_t i_ = 0; i_ < sizeof arr / sizeof arr[0]; i_++) R->tab[arr[i_]] = v
    if (city == 1) {
        SETS(off1, city_enable, 0);
        SETS(on1, face_used, 1);
        if (exe_loaded()) R->city_enable[exe_data(0x4abe6d, 1)[0]] = 0;
    } else if (city == 2) {
        SETS(off2, city_enable, 0);
        SETS(on2, face_used, 1);
        if (exe_loaded()) R->city_enable[exe_data(0x4abe6e, 1)[0]] = 0;
    } else if (city == 3) {
        SETS(off3, city_enable, 0);
        SETS(on3, face_used, 1);
        if (exe_loaded()) R->city_enable[exe_data(0x4abe6f, 1)[0]] = 0;
    } else {
        game_fatal(-0x4a, 0x14d, city);
    }
    if (city == 1 || city == 2) R->face_used[0x33] = 1, R->face_used[0x6c] = 1;
#undef SETS
    memset(R->city_flag, 0, sizeof R->city_flag);
    uint32_t list = city == 1 ? 0x4b3210 : city == 2 ? 0x4b3224 : city == 3 ? 0x4b3254 : 0;
    if (list && exe_loaded())
        for (int i = 0;; i++) {
            uint16_t v = exe_u16(list + 2 * i);
            if (v == 0xffff) break;
            R->city_flag[(int16_t)v & 0x1ff] = 1;   /* (a short index: the lists hold small numbers) */
        }
    R->ndoors = 0;
    R->doors_unlocked = 0;
    R->ntriggers = 0;
    R->phones_off = 0;
    for (int i = 0; i < 4; i++) R->finish_frame[i] = -1, R->finish_order[i] = -1;
    for (int i = 0; i < CRANE_MAX; i++) {
        Crane *cr = &R->cranes[i];
        cr->state = 9;
        cr->car = -1;
        cr->count = 0;
        cr->slot = -1;
        for (int k = 0; k < CRANE_STACK; k++) cr->stack[k] = -1;
    }
    R->ncranes = 0;
    g_car_list_count = 0;
    for (int i = 0; i < CARLIST_MAX; i++) {
        CarListEntry *e = &g_car_list[i];
        e->a = e->model = e->remap = e->target = -1;
        e->count = 0;
    }
    mission_reset_progress();
    mission_reset_timed_bombs();
    mission_reset_sound_sources();
    car_trig_reset_all();
    mission_alarm_sound_reset();
    mission_reset_player_slots();
}

/* Mission_UpdateTriggers 0x479e00: unlocked doors (when any), live triggers, cranes, the gangs, the
   timed bombs; in a network race the game ends 70 frames after every player has finished. */
void mission_update_triggers(void)
{
    if (R->doors_unlocked != 0)
        for (int i = 0; i < R->ndoors; i++)
            if (R->doors[i].locked == 0) door_update(i);
    for (int i = 0; i < R->ntriggers; i++)
        if (R->triggers[i].state != TRIG_DEAD) trigger_update(i);
    for (int i = 0; i < R->ncranes; i++) crane_update(i);
    gang_update();
    mission_update_timed_bombs();
    if (g_player_count > 1) {
        int8_t kind;
        int32_t value = 0;
        front_get_multi_target(&kind, &value);
        if (kind == 2) {
            for (int n = player_first(); n >= 0; n = player_next(n)) {
                int i = 0;
                while (R->finish_order[i] != (int8_t)n)
                    if (++i > 3) return;
            }
            event_schedule_exit(0x46, 10);
        }
    }
}

/* "%s" in a text replaced by arg (the original sprintf's the FXT text) */
static void fmt1(char *dst, size_t cap, const char *fmt, const char *arg)
{
    size_t o = 0;
    for (const char *s = fmt ? fmt : ""; *s && o + 1 < cap; s++) {
        if (s[0] == '%' && s[1] == 's') {
            for (const char *a = arg; *a && o + 1 < cap; a++) dst[o++] = *a;
            s++;
        } else if (s[0] == '%' && s[1] == '%') {
            dst[o++] = '%';
            s++;
        } else {
            dst[o++] = *s;
        }
    }
    dst[o] = 0;
}

static void pager_text(const char *key) { pager_add_message(text_get(key)); }

/* the cost message after a respray / bomb: "all_your" when it took the whole score, else bomb_cost
   with the amount */
static void pager_cost(int n, int cost)
{
    if (player_get_score(n) == cost) {
        pager_text("all_your");
    } else {
        char num[16], buf[100];
        snprintf(num, sizeof num, "%d", cost);   /* _itoa 0x4a6136, radix 10 */
        fmt1(buf, sizeof buf, text_get("bomb_cost"), num);
        pager_add_message(buf);
    }
}

static int start_process(int player, int trigger, int label)
{
    int proc = (int16_t)mission_start_process(player, trigger, label);
    mission_set_process_trigger(trigger, proc);
    return proc;
}

/* The respray garage (kind 3) for player n in car c. */
static void fire_respray(Trigger *t, int n, Car *c)
{
    int cost;
    if (emergency_model(c->model)) goto off;
    if (c->script_line == -1) {
        t->state = TRIG_ARMED;
        goto clean;
    }
    if (c->model == 0x2f) {   /* the model car */
        if (n == g_player_local && !hud_is_pager_busy()) pager_text("no_modelcar_respray");
        t->state = TRIG_ARMED;
        return;
    }
    if (!car_has_door(c, (int16_t)t->a) || c->remap == (uint32_t)t->a) goto clean;
    /* a mission car with another colour: repainted to a */
    cost = garage_respray_cost(c->id, n);
    if (player_get_score(n) < cost) goto broke;
    if (player_get_controlled_kind(n) == PLAYER_IN_CAR) car_set_remap(car_get(player_get_controlled_id(n)), t->a);
    {
        int32_t px = t->x * 0x400000 + 0x200000;
        Snd_PlayAt(px, t->y * 0x400000 + 0x200000, px, 0x2a);   /* quirk: x again as z */
    }
    if (n == g_player_local) {
        pager_text("resray_done");
        pager_cost(n, cost);
    }
    goto pay;
clean:
    /* a wanted player's car resprayed (the cost of the wanted level and the damage) */
    if (player_get_wanted_points(c->driver) <= 0 || (cost = garage_respray_cost(c->id, n)) <= 0) goto off;
    if (cost > player_get_score(n)) goto broke;
    if (n == g_player_local) {
        pager_text("clean_done");
        pager_cost(n, cost);
    }
pay:
    car_repair(c);
    player_sub_score(n, cost);
    player_clear_wanted_level(c->driver);
    player_clear_wanted_points(c->driver);
    {
        int crim = (int16_t)police_find_criminal_by_ped(c->driver);
        if (crim != -1) {
            player_clear_wanted_level(c->driver);
            player_clear_wanted_points(c->driver);
            police_clear_criminal(crim);
        }
    }
off:
    t->state = TRIG_ARMED;
    return;
broke:
    if (n == g_player_local && !hud_is_pager_busy()) {
        pager_text("no_respray");
        t->state = TRIG_ARMED;
        return;
    }
    goto off;
}

/* The bomb shop (kind 8): fits a bomb (car +0x9c = 1, timer 0x7d) for bombshop_cost. */
static void fire_bomb_shop(Trigger *t, int n)
{
    if (player_get_controlled_kind(n) != PLAYER_IN_CAR) { t->state = TRIG_ARMED; return; }
    int id = (int16_t)player_get_controlled_id(n);
    Car *c = car_get(id);
    const char *msg = NULL;
    if (c->model == 0x2f) msg = "no_modelcar_respray";
    else if (c->model == 0x25) msg = "no_tank_respray";
    else if (!emergency_model(c->model) || c->model == 4 || c->model == 0x20) {   /* squad cars (4, 0x20) are allowed */
        if (c->bomb == 0) {
            int cost = g_mission.bombshop_cost;
            if (player_get_score(n) < cost) {
                if (n == g_player_local && !hud_is_pager_busy()) {
                    pager_text("no_respray");
                    t->state = TRIG_ARMED;
                    return;
                }
                t->state = TRIG_ARMED;
                return;
            }
            if (n == g_player_local) {
                pager_text("bomb_added");
                pager_cost(n, cost);
            }
            Car *b = car_get(id);
            b->bomb = 1;
            b->damage = 0;
            b->bomb_timer = 0x7d;
            player_sub_score(n, cost);
        }
        t->state = TRIG_ARMED;
        return;
    } else msg = "no_emerg_respray";
    if (n == g_player_local && !hud_is_pager_busy()) pager_text(msg);
    t->state = TRIG_ARMED;
}

/* the door a trigger works (kinds 2 and 10): an open door closes, a closed one opens */
static void toggle_door(Trigger *t)
{
    Door *d = &R->doors[t->a];
    if (d->state == DOOR_OPEN) {
        t->state = TRIG_RUNNING;
        door_close(t->a);
    } else if (d->state == DOOR_CLOSED) {
        t->state = TRIG_RUNNING;
        door_open(t->a);
    }
}

/* Trigger_Update 0x47a3a0: per player (not on a train): where it is (its car or ped, or for the
   kinds that watch a car / ped that one), whether its condition holds, the arming (state 1 -> 0, or
   2 with a delay), and in state 0 the kind's action. The car / ped pointers persist from one player
   to the next as the original's locals. Kinds: docs/missions.md. */
void trigger_update(int ti)
{
    Trigger *t = &R->triggers[ti];
    Car *car = NULL, *tcar = NULL;    /* local_140: the player's car or the watched car; local_118 */
    Ped *ped = NULL, *other = NULL;   /* local_138; local_11c */
    int cx = 0, cy = 0;               /* local_124 / local_120: the watched car's block (kinds 0xb, 0x16) */
    for (int n = player_first(); n >= 0; n = player_next(n)) {
        bool in = false;
        const int32_t *pos = player_get_controlled_pos(n);
        int py = pos[1] >> 22, px = pos[0] >> 22;
        int kind = player_get_controlled_kind(n);
        int id = (int16_t)player_get_controlled_id(n);
        if (kind == PLAYER_IN_CAR) car = car_get(id);
        else if (kind == PLAYER_ON_FOOT) ped = ped_get(id);
        else if (kind == PLAYER_ON_TRAIN) continue;
        else return;

        /* which position the trigger tests, and whether it looks at this player at all */
        bool eval = true;
        switch (t->kind) {
        case 3: case 8: case 9: case 0x17:
            eval = kind == PLAYER_IN_CAR;
            break;
        case 10: case 0xf: case 0x10: case 0x18:
            eval = kind == PLAYER_IN_CAR && t->d != -1;
            break;
        case 0xb: case 0x16:
            if (t->a != -1) {
                tcar = car_get(t->a);
                cx = tcar->spr.x >> 22, cy = tcar->spr.y >> 22;
            } else eval = false;
            break;
        case 0xc:   /* MPHONES: the first update only arms it; a player in the model car is ignored */
            if (t->state == TRIG_FIRE) {
                t->state = TRIG_ARMED;
                eval = false;
            } else eval = !(kind == PLAYER_IN_CAR && car->model == 0x2f);
            break;
        case 0xe: case 0x1c: case 0x1d: case 0x20:   /* the watched car, unless a player drives it */
            eval = false;
            if (t->d != -1) {
                car = car_get((int16_t)t->d);
                if (car->driver < 0 || (ped = ped_get(car->driver))->objective != 0x25) {
                    px = car->spr.x >> 22, py = car->spr.y >> 22;
                    eval = true;
                }
            }
            break;
        case 0x15:   /* the watched ped, unless it is a player */
            eval = false;
            if ((uint32_t)t->d != 0xffffffffu && (ped = ped_get(t->d & 0xffff))->objective != 0x25) {
                px = ped->spr.x >> 22, py = ped->spr.y >> 22;
                eval = true;
            }
            break;
        case 0x19: case 0x21:   /* the watched car, or the player's own car */
            if (t->d == -1) {
                if (kind != PLAYER_IN_CAR) break;   /* (on foot: tested with the player's position) */
                car = car_get(id);
            } else {
                car = car_get((int16_t)t->d);
            }
            px = car->spr.x >> 22, py = car->spr.y >> 22;
            break;
        case 0x1a:
            eval = kind == PLAYER_ON_FOOT;
            if (eval) ped = ped_get(id);
            break;
        case 0x1b:
            eval = kind == PLAYER_ON_FOOT && t->d != -1;
            if (eval) ped = ped_get(id), other = ped_get((int16_t)t->d);
            break;
        case 0x1e:   /* the ped of d near the car of a */
            eval = false;
            if (t->d != -1 && t->a != -1) {
                ped = ped_get((int16_t)t->d);
                if (ped->objective != 0x25) {
                    px = ped->spr.x >> 22, py = ped->spr.y >> 22;
                    car = car_get((int16_t)t->a);
                    eval = true;
                }
            }
            break;
        }
        if (!eval) continue;

        /* in the square of radius b around the trigger's block (or the watched car's) */
        int k = t->kind, r = t->b;
        if (k == 0xb || k == 0x16) in = cx - r <= px && px <= cx + r && cy - r <= py && py <= cy + r;
        else in = t->x - r <= px && px <= t->x + r && t->y - r <= py && py <= t->y + r;

        bool hit;
        if (k == 9) {
            hit = car->script_line == r && car->script_line >= 0;   /* CARTRIGGER: the car of line b, anywhere */
        } else {
            switch (k) {
            case 0x1a: case 0x18: case 10: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x20:
            case 0x13: case 0x14: case 0xb: case 0x16: case 0x10:
                hit = false;   /* their own condition below */
                break;
            default:
                hit = in;
            }
        }
        if (!hit) {
            int8_t mine = R->targets[R->progress[(int8_t)n]];
            switch (k) {
            case 0xb: case 0x16: hit = in; break;
            case 0x18: hit = t->d == car->id && car->bomb > 0; break;
            case 0x1a: hit = ped->firing == 1 && in && ped->weapon != 0; break;
            case 0x1b: hit = ped->firing == 1 && ped_is_on_any_screen(other) && ped->weapon != 0; break;
            case 10: hit = in && t->d == car->id; break;
            case 0x1c: hit = in && car->damage > 99; break;
            case 0x1d: case 0x20: hit = true; break;
            case 0x13: case 0x14: hit = in && mine == ti; break;
            case 0x1e: hit = ped->car == car->id && ped->anim > 0x1d && (ped->state == 7 || ped->state == 6); break;
            case 0x10: hit = in && kind == PLAYER_IN_CAR && t->d == car->id && t->state != TRIG_RUNNING; break;
            }
            if (!hit && (k == 0x19 || k == 0x21)) t->d = -1;   /* the watch restarts with the next car */
        }
        if (hit && t->state == TRIG_ARMED) {
            if (t->c < 1 || k == 0x1d || k == 0x20) {
                t->state = TRIG_FIRE;
            } else {
                event_schedule(t->c, 4, ti);
                t->state = TRIG_DELAYED;
            }
        }
        if (t->state != TRIG_FIRE) continue;

        switch (t->kind) {
        case 0: t->state = TRIG_RUNNING; door_open(t->a); break;     /* (the original sets the state after) */
        case 1: door_close(t->a); t->state = TRIG_RUNNING; break;
        case 2: case 10: toggle_door(t); break;
        case 3:
            if (player_get_controlled_kind(n) == PLAYER_IN_CAR) fire_respray(t, n, car_get(id));
            break;
        case 4: case 0x15: case 0x1c: case 0x1a: case 0x1b:
            start_process(n, ti, t->a);
            break;
        case 8: fire_bomb_shop(t, n); break;
        case 9:
        start9:
            start_process(n, ti, t->a);
            t->state = TRIG_RUNNING;
            break;
        case 0xb:
            t->state = TRIG_RUNNING;
            if (kind == PLAYER_IN_CAR) hunt_add_car_target((int16_t)t->a, car->driver, (int16_t)t->d);
            else if (kind == PLAYER_ON_FOOT) hunt_add_car_target((int16_t)t->a, ped->id, (int16_t)t->d);
            break;
        case 0xc:
            if (!R->phones_off && !mission_get_player_phone_flag(n)) goto start9;
            break;
        case 0xd: {   /* PHONE_TOGG: leaving the phone's 3-block square (within radius b) re-arms it */
            int x0 = t->x, y0 = t->y;
            if (x0 - r <= px && px <= x0 + r && y0 - r <= py && py <= y0 + r &&
                (px < x0 - 3 || px > x0 + 3 || py < y0 - 3 || py > y0 + 3) && R->triggers[t->a].state == TRIG_RUNNING)
                R->triggers[t->a].state = TRIG_ARMED;
            break;
        }
        case 0xe:
            if (car->control != 1) start_process(n, ti, t->a);
            break;
        case 0xf:
            if (car->model == t->d && kind == PLAYER_IN_CAR && car->id == (int16_t)player_get_controlled_id(n)) {
                start_process(n, ti, t->a);
                t->state = TRIG_RUNNING;
            }
            break;
        case 0x10:
            if (car->id == t->d && kind == PLAYER_IN_CAR && car->id == (int16_t)player_get_controlled_id(n)) goto start9;
            break;
        case 0x11: {   /* CLOCK_START: arms its CLOCK_STOP */
            int stop = t->d;
            t->a = 0;
            t->state = TRIG_RUNNING;
            R->triggers[stop].state = TRIG_ARMED;
            break;
        }
        case 0x12:
            R->triggers[t->a].state = TRIG_DEAD;
            t->state = TRIG_DEAD;
            break;
        case 0x13: {   /* MIDPOINT_MULTI: a race checkpoint; a = the first player through */
            int pr = R->progress[(int8_t)n];
            const char *msg = NULL;
            int voice = 0;
            if (t->a == -1) {
                if (R->targets[pr] != ti) break;
                R->progress[(int8_t)n] = pr + 1;
                t->a = n;
                t->state = TRIG_ARMED;
                R->triggers[t->d].state = TRIG_ARMED;
                voice = 0xf, msg = "1st_over_checkpoint";
            } else if (R->targets[pr] == ti && t->a != n) {
                R->progress[(int8_t)n] = pr + 1;
                voice = 0x10, msg = "second_checkpoint";
            } else break;
            if (player_is_local(n)) {
                Snd_PlayVoice(voice);
                hud_show_subtitle(1, text_get(msg));
                hud_arrow_off();
                const Trigger *nx = &R->triggers[t->d];
                hud_arrow_to_pos(nx->x << 6, nx->y << 6, nx->z << 6);
            }
            t->state = TRIG_ARMED;
            break;
        }
        case 0x14: {   /* FINAL_MULTI: the finish line */
            int pr = R->progress[(int8_t)n];
            if (R->targets[pr] != ti || t->a == n) break;
            if (R->finish_frame[n] == -1) R->finish_frame[n] = (int32_t)g_frame;
            R->progress[(int8_t)n] = pr + 1;
            if (t->a == -1) {
                t->a = n;
                R->finish_order[0] = (int8_t)n;
                const char *msg;
                if (!player_is_local(n)) {
                    Snd_PlayVoice(6);
                    msg = "lost_race";
                } else {
                    Snd_PlayVoice(4);
                    hud_arrow_off();
                    msg = "win_race";
                }
                hud_show_subtitle(1, text_get(msg));
                t->state = TRIG_ARMED;
            } else {
                if (player_is_local(n)) {
                    hud_show_subtitle(1, text_get("lost_race"));
                    hud_arrow_off();
                }
                for (int i = 0; i < 4; i++)
                    if (R->finish_order[i] == -1) {
                        R->finish_order[i] = (int8_t)n;
                        break;
                    }
                t->state = TRIG_ARMED;
            }
            break;
        }
        case 0x16:
            t->state = TRIG_RUNNING;
            hunt_add_block_target(tcar->id, t->x, t->y, t->z);
            break;
        case 0x17:   /* a bomb taken off */
            if (player_get_controlled_kind(n) == PLAYER_IN_CAR && car->bomb > 0) {
                if (n == g_player_local) pager_text("bomb_off");
                car_get(car->id)->bomb = 0;
            }
            t->state = TRIG_DEAD;
            break;
        case 0x18:
            start_process(n, ti, t->a);
            t->state = TRIG_DEAD;
            break;
        case 0x19: case 0x21:   /* DAMAGE_TRIG: the first car seen, then fires when it is wrecked */
            if (t->d == -1) t->d = id;   /* (on foot: the ped id) */
            else if (car->damage > 99) {
                start_process(n, ti, t->a);
                t->state = TRIG_DEAD;
            }
            break;
        case 0x1d:   /* CARWAIT: the car standing still on screen 65 frames (400 if parked dummy) */
            if (car->speed == 0 && on_any_screen_px(car->spr.x >> 16, car->spr.y >> 16)) {
                int limit = car_i16(car, 0xdc) == 2 && car->physics == 0 ? 400 : 0x41;
                if (++t->c >= limit) start_process(n, ti, t->a);
            } else {
                t->c = 0;
            }
            break;
        case 0x1e:
            start_process(n, ti, t->b);
            t->state = TRIG_DEAD;
            break;
        case 0x1f:
            t->state = TRIG_DEAD;
            Snd_PlayVoice(0xe);
            break;
        case 0x20: {   /* CARSTUCK: on a block whose type bits are 0 (air), not moving, on screen, 65 frames */
            const Car *c = car_get(car->id);
            int bz = (c->spr.z - 0x40000) >> 22, bx = c->spr.x >> 22, by = c->spr.y >> 22;
            uint8_t ty = (bz >= 0 && bz < MAP_Z && bx >= 0 && bx < MAP_W && by >= 0 && by < MAP_H)
                             ? g_game.map->type_cache[bz][by][bx] : 0;
            if ((ty & 0xf) != 0 || car->speed != 0 || !car_is_on_screen(car)) t->c = 0;
            else if (++t->c > 0x40) start_process(n, ti, t->a);
            break;
        }
        }
        if (t->proc >= 0 && t->kind != 0x21) mission_kill_process(t->proc);
    }
}

/* Garage_ResprayCost 0x47ba30: ((W + value * 1000 * damage / 100) * (100 + multiplier)) / 100 with
   W = 0 when not wanted, else 1000 * 2^(level - 1), and value the car info word at +0x6e; 500 when
   both terms are 0. */
int garage_respray_cost(int car, int player)
{
    const Car *c = car_get(car);
    int damage = c->damage;
    const uint8_t *info = car_info_of_model(car_get(c->id)->model);
    int value = info ? (info[0x6e] | info[0x6f] << 8) : 0;
    int lvl = player_get_wanted_level(player);
    int mult = player_get_multiplier(player);
    int w = 0;
    if (lvl >= 1) {
        w = 1000;
        for (int i = 1; i < lvl; i++) w *= 2;
    }
    int dmg = value * 1000 * damage / 100;
    if (dmg == 0 && lvl == 0) return 500;
    return (w + dmg) * (mult + 100) / 100;
}
