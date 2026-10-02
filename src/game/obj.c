#include "obj.h"
#include "car.h"
#include "coll.h"
#include "game.h"
#include "gmath.h"
#include "ped.h"
#include "stubs.h"
#include <string.h>

Obj g_objs[OBJ_MAX] = { 0 };
ObjInfo *g_obj_infos[OBJ_INFO_MAX];
int g_obj_info_count;
const ObjPos *g_obj_map;
int g_obj_map_count;
Obj *g_obj_anim_list, *g_obj_status7_list, *g_obj_moving_list, *g_obj_attached_list;
int g_obj_smashable;
int g_obj_parked_alarm;
static int obj_sprite_base;                  /* 0x774f0c, the object sprite group */
static CollBox static_box;                   /* 0x6b4078 */

/* Obj_LoadInfos 0x44ed60 */
bool obj_load_infos(uint8_t *data, int size, int sprite_base)
{
    int n = 0;
    for (int off = 0; off < size; n++) off += 0x14 + 2 * data[off + 0x13];
    g_obj_info_count = n;
    if (n * 4 > 0x400) return false;   /* Error_Fatal -0x18 */
    obj_sprite_base = sprite_base;
    uint8_t *p = data;
    for (int i = 0; i < n; i++) {
        ObjInfo *o = (ObjInfo *)p;
        g_obj_infos[i] = o;
        if (o->w < 0x10000) o->w <<= 16;
        if (o->depth < 0x10000) o->depth <<= 16;
        if (o->h < 0x10000) o->h <<= 16;
        o->spr_num = (uint16_t)(o->spr_num + sprite_base);
        p += 0x14 + 2 * o->num_into;
    }
    return true;
}

/* Obj_SetMapObjects 0x44ee20 */
void obj_set_map_objects(const uint8_t *data, int size)
{
    g_obj_map = (const ObjPos *)data;
    g_obj_map_count = size / OBJ_POS_SIZE;
}

static const ObjInfo *info_of(int type)
{
    if (type < 0 || type >= g_obj_info_count) game_fatal(-0x4a, 0x28, type);   /* (unchecked in the original) */
    return g_obj_infos[type];
}

/* the cached type of the cell of a 16.16 position (the original indexes without bounds) */
static uint8_t cell_type(int32_t x, int32_t y, int32_t z)
{
    int bx = x >> 22, by = y >> 22, bz = z >> 22;
    if (bx < 0 || by < 0 || bz < 0 || bx >= MAP_W || by >= MAP_H || bz >= MAP_Z) return 0;
    return g_game.map->type_cache[bz][by][bx];
}

/* Obj_UpdateSprite 0x44c3a0: the frame of state s is spr_num + s - 1 for states 1-8, then three runs
   of six (states 9-14, 15-20, 21-26) map to spr_num + 1..6, and states 27-31 to spr_num + 8..12.
   Invisible objects (status 3) and other states keep their frame. */
void obj_update_sprite(Obj *o)
{
    const ObjInfo *info = info_of(o->type);
    if (info->status == OBJ_STATUS_INVISIBLE) return;
    int s = o->state - 1, d;
    if (s >= 0 && s <= 7) d = s;
    else if (s >= 8 && s <= 0x19) d = (s - 8) % 6 + 1;
    else if (s >= 0x1a && s <= 0x1e) d = s - 0x1a + 8;
    else return;
    sprite_set_frame(&o->spr, info->spr_num + d);
}

/* sets the low byte of the state (the original writes a byte there) */
static void set_state_byte(Obj *o, int v) { o->state = (int16_t)((o->state & ~0xff) | (v & 0xff)); }

/* Obj_CreateStatic 0x44c870: object `slot` of `type` at (x, y, z) heading `angle`. Types 0x1a-0x1c sit
   half a block lower. Invisible types (status 3) only get a position (no sprite init: the depth key
   stays as it was); status-1 types start in state 7, the rest in 1. Animated types (5, 9) and status 7
   are linked into their lists. All but types 0x10, 0x41, 0x42, 0x47-0x49 go into the collision grid.
   If a corner of the object's box is on a slope, the depth key is a layer higher (type 0x57: 0x450000). */
void obj_create_static(int slot, int type, int32_t x, int32_t y, int32_t z, int angle)
{
    slot = (int16_t)slot;
    Obj *o = &g_objs[slot];
    o->type = (int16_t)type;
    o->id = (int16_t)slot;
    if (type == 0x1a || type == 0x1b || type == 0x1c) z -= 0x80000;
    o->speed = o->life = o->heading = 0;
    o->u12 = 0;
    o->owner = -1;
    o->frame_timer = 0;
    const ObjInfo *info = info_of((int16_t)type);
    o->weight = (int16_t)info->weight;
    if (info->status == OBJ_STATUS_INVISIBLE) {
        o->spr.z = z;
        o->spr.x = x;
        o->spr.angle = (int16_t)angle;
        o->spr.y = y;
    } else {
        sprite_init(&o->spr, x, y, z, (int16_t)angle, obj_sprite_base);
    }
    set_state_byte(o, info->status == OBJ_STATUS_1 ? 7 : 1);
    if (info->status == OBJ_STATUS_ANIM || info->status == OBJ_STATUS_ANIM9) {
        o->next = g_obj_anim_list;
        g_obj_anim_list = o;
        o->in_anim_list = 1;
    } else if (info->status == OBJ_STATUS_7) {
        o->next = g_obj_status7_list;
        g_obj_status7_list = o;
    } else {
        o->prev = NULL;
        o->next = NULL;
    }
    obj_update_sprite(o);
    int t = o->type;
    if (t != 0x10 && t != 0x41 && t != 0x42 && t != 0x47 && t != 0x48 && t != 0x49)
        coll_insert(COLL_OBJECT, slot, o, o->spr.unk20, o->spr.x, o->spr.y);
    /* the id argument is the high half of the depth (+0xa of the record) */
    const CollBox *b = coll_build_box(o->spr.x, o->spr.y, o->spr.z, info->w >> 17, info->h >> 17, o->spr.angle,
                                      (int16_t)(info->depth >> 16), &static_box);
    int32_t zz = o->spr.z;
    bool flat = true;
    for (int i = 0; i < 4; i++)
        if (cell_type(b->x[i], b->y[i], zz) & 0x80) flat = false;
    if (!flat) {
        o->spr.zkey = zz - 0x400000;
        if (o->type == 0x57) o->spr.zkey = zz - 0x450000;
    }
}

/* Obj_InitFromMap 0x44c620: the CMP object_pos records in order. Objects take consecutive slots from
   0 (z + 1 in 16.16); type 2 outside style 1 becomes two objects, types 3 and 2. Records with remap
   >= 0x80 are parked cars of model `type` (z one pixel up) unless one of that model already stands
   there; every other one of the plain cars (vtype 4) gets its alarm armed (siren state 99), starting
   with the first. The rest of the table is cleared, then the power-ups. */
void obj_init_from_map(void)
{
    int slot = 0;
    g_obj_parked_alarm = 0;
    g_obj_anim_list = NULL;
    g_obj_attached_list = NULL;
    g_obj_status7_list = NULL;
    g_obj_smashable = 0;
    for (int i = 0; i < g_obj_map_count; i++) {
        const ObjPos *p = &g_obj_map[i];
        int32_t x = p->x << 16, y = p->y << 16;
        if (p->remap < 0x80) {
            if (p->type == 2 && style_requested() != 1) {
                obj_create_static(slot, 3, x, y, p->z * 0x10000 + 1, p->rotation);
                obj_create_static(slot + 1, 2, x, y, p->z << 16, p->rotation);
                slot += 2;
                continue;
            }
            obj_create_static(slot, p->type, x, y, p->z * 0x10000 + 1, p->rotation);
            slot++;
        } else {
            int32_t z = p->z * 0x10000 - 0x10000;
            if (coll_find_car_at(x, y, p->type)) continue;
            int c = car_spawn_ex_on_ground(x, y, z, p->type, 0, p->rotation, 0);
            if ((int16_t)c < 0) game_fatal(-0x8b, 0x9f, p->type);
            Car *car = car_get(c);
            if (car->vtype == 4) {
                if (g_obj_parked_alarm == 0) car->siren_state = 99;
                if (++g_obj_parked_alarm > 1) g_obj_parked_alarm = 0;
            }
        }
    }
    g_obj_moving_list = NULL;
    for (int i = (int16_t)slot; i < OBJ_MAX; i++) {
        g_objs[i].state = 0;
        obj_update_sprite(&g_objs[i]);
    }
    powerup_init_all();
}

/* Obj_Create 0x44cef0: the highest free slot (3499 down to 1) gets the object. Refused: blocks 0 or
   past 255, z above 0x1400000, below layer 1 (z < 0x3c0000), types 0xb / 0xc / 0x11 when 17 of them
   are in the cell already (and more than 200 in all), type 10 off roads or with 3 in the cell, type -1.
   Type 0x40 and 0x4d objects make their object_info animated (status 5) for good. Fires (0x12, 0x13,
   0x2e) start on a random frame. The depth key is a layer up over a slope (at least 1); type 0x57
   5 pixels more; an object whose key ends up at 0 or below is deleted again. */
int obj_create(int32_t x, int32_t y, int32_t z, int type, int angle)
{
    int bx = x >> 22, by = y >> 22;
    type = (int16_t)type;
    if (bx < 1 || bx > 0xff || by < 1 || by > 0xff || z > 0x1400000) return -1;
    if (z <= 0x3bffff) return -1;
    bool small = type == 0xb || type == 0xc || type == 0x11;
    if (small && coll_get_ped_count(x, y) >= 0x11) return -1;
    if (type == 10) {
        if (coll_get_obj_count(x, y) >= 3) return -1;
        if ((cell_type(x, y, z) & 0x70) != 0x20) return -1;
    } else if (type == -1) {
        return -1;
    }
    int slot = 0xdab;
    while ((g_objs[slot].state & 0xff) != 0 && --slot > 0) {}
    if (small) {
        if (slot < 1) return -1;
        if (g_obj_smashable + 1 > OBJ_SMASHABLE_MAX) return -1;
        g_obj_smashable++;
    }
    if (slot <= 0) return -1;
    Obj *o = &g_objs[slot];
    memset(o, 0, sizeof *o);
    o->id = (int16_t)slot;
    o->type = (int16_t)type;
    o->owner = -1;
    o->u21 = 0;
    ObjInfo *info = (ObjInfo *)info_of(type);
    if (type == 0x40 || type == 0x4d) {
        o->param = (int16_t)angle;
        o->frame_timer = 0;
        o->u12 = 1;
        info->status = OBJ_STATUS_ANIM;   /* the shared record */
    }
    if (type == 0x3f) o->param = (int16_t)angle;
    set_state_byte(o, 1);
    o->weight = (int16_t)info->weight;
    if (info->status != OBJ_STATUS_INVISIBLE) {
        sprite_init(&o->spr, x, y, z, (int16_t)angle, obj_sprite_base);
        if (type == 10 || type == 0x33 || type == 0x43 || type == 0x30 || type == 0x31) sprite_set_blend(&o->spr);
    }
    o->next = o->prev = NULL;
    if (small) coll_inc_ped_count(x, y);
    if (type == 10) coll_inc_obj_count(x, y);
    coll_insert(COLL_OBJECT, slot, o, o->spr.unk20, o->spr.x, o->spr.y);
    if (info->status == OBJ_STATUS_ANIM || info->status == OBJ_STATUS_ANIM9) {
        set_state_byte(o, 1);
        if (type == 0x12 || type == 0x13 || type == 0x2e) {
            int16_t r = (int16_t)math_random();
            o->attach_kind = 1;
            o->spr.angle = 0;
            o->u10 = 0;
            o->frame_timer = 0;
            set_state_byte(o, r % 7 + 1);
            o->owner = (int16_t)angle;
            fire_register(slot);
        }
        o->u10 = 0;
        o->next = g_obj_anim_list;
        g_obj_anim_list = o;
        o->in_anim_list = 1;
    } else if (info->status == OBJ_STATUS_7) {
        o->u10 = 0;
        o->frame_timer = 0;
        set_state_byte(o, 1);
        o->next = g_obj_status7_list;
        g_obj_status7_list = o;
    }
    obj_update_sprite(o);
    if (cell_type(x, y, z) < 0x81) {
        o->spr.zkey = o->spr.z;
    } else {
        o->spr.zkey = o->spr.z - 0x400000;
        if (o->spr.z - 0x400000 < 1) o->spr.zkey = 1;
    }
    if (o->type == 0x57) o->spr.zkey -= 0x50000;
    if (o->spr.zkey > 0) return slot;
    obj_delete(slot);
    return -1;
}

int objs_in_use(void)
{
    int k = 0;
    for (int i = 0; i < OBJ_MAX; i++) k += (g_objs[i].state & 0xff) != 0;
    return k;
}

/* Obj_UnlinkAnimated 0x44ea30: out of the animated list (fatal -0x74 if listed but not found). A fire
   (types 0x12, 0x13, 0x2e) lets go of its owner: a car's burning count drops, a ped's carried object
   clears; it goes back to its first frame. */
void obj_unlink_animated(int id)
{
    id = (int16_t)id;
    Obj *o = &g_objs[id], *p;
    if (!g_obj_anim_list || !o->in_anim_list) return;
    if (g_obj_anim_list->id == id) {
        p = g_obj_anim_list;
        g_obj_anim_list = p->next;
    } else {
        Obj *prev = g_obj_anim_list;
        for (p = prev->next;; prev = p, p = p->next) {
            if (!p) game_fatal(-0x74, 0, 0x2a);
            if (p->id == id) {
                prev->next = p->next;
                break;
            }
        }
    }
    if (p->type == 0x12 || p->type == 0x13 || p->type == 0x2e) {
        if (p->owner != -1) {
            if (p->attach_kind != 0) {
                if (p->attach_kind == 1) car_get(p->owner)->burning--;
                else ped_get(p->owner)->carried = -1;
            }
            p->attach_kind = -1;
            p->owner = -1;
        }
        p->state = 0;
        p->frame_timer = 0;
        p->u10 = 0;
        sprite_set_frame(&p->spr, info_of(o->type)->spr_num - 1);
    }
    p->in_anim_list = 0;
}

/* unlinks id from a list chained through next (the original dereferences NULL when id isn't in a
   list whose head has no successor; the port stops) */
static void unlink(Obj **list, int id)
{
    Obj *h = *list;
    if (!h) return;
    if (h->id == id) {
        *list = h->next;
        return;
    }
    for (Obj *prev = h, *n = h->next; n; prev = n, n = n->next)
        if (n->id == id) {
            prev->next = n->next;
            return;
        }
}

/* Obj_Delete 0x44eb90: out of the grid and the counters, state 0, out of its status list (6: the
   attached list, 5 / 9: the animated one, 7). */
void obj_delete(int id)
{
    id = (int16_t)id;
    Obj *o = &g_objs[id];
    coll_remove(o, o->spr.unk20);
    if (o->type == 0xb || o->type == 0xc || o->type == 0x11) {
        coll_dec_ped_count(o->spr.x, o->spr.y);
        g_obj_smashable--;
    }
    if (o->type == 10) coll_dec_obj_count(o->spr.x, o->spr.y);
    o->state &= ~0xff;
    int st = info_of(o->type)->status;
    if (st == OBJ_STATUS_CAR) unlink(&g_obj_attached_list, id);
    else if (st == OBJ_STATUS_ANIM || st == OBJ_STATUS_ANIM9) obj_unlink_animated(id);
    else if (st == OBJ_STATUS_7) unlink(&g_obj_status7_list, id);
}
