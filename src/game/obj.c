#include "obj.h"
#include "car.h"
#include "coll.h"
#include "game.h"
#include "gmath.h"
#include "ped.h"
#include "carcoll.h"
#include "expl.h"
#include "fire.h"
#include "player.h"
#include "powerup.h"
#include "proj.h"
#include "train.h"
#include <stdlib.h>
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

/* ---- the rest of the module (0x44c2f0-0x44ed50) ---- */

/* sin / cos of an angle. Angles are 0..1023, but a landing object of status 4 turns its heading by
   -0x100..0xc0 without wrapping it: the original then reads before the sine table (the tan table) or
   past it; the port wraps the angle (for the box too). */
static int32_t osin(int a) { return g_sin[a & 0x3ff]; }
static int32_t ocos(int a) { return g_sin[(a & 0x3ff) + 0x100]; }

/* Obj_IsOnScreen 0x44c2f0: the object's pixel position is inside the view rectangle of a player (and
   16..0x3ff0 pixels), and its position is inside 0x100000..0x3ff00000. */
bool obj_is_on_screen(const Obj *o)
{
    int px = (int16_t)(o->spr.x >> 16), py = (int16_t)(o->spr.y >> 16);
    bool seen = false;
    for (int n = player_first(); n >= 0; n = player_next(n)) {
        const int32_t *r = player_get_view_rect(n);
        if (r[0] <= px && r[2] <= py && px <= r[1] && py <= r[3] && px > 0xf && py > 0xf && px < 0x3ff1 && py < 0x3ff1) {
            seen = true;
            break;
        }
    }
    if (o->spr.x > 0x100000 && o->spr.y > 0x100000 && o->spr.x < 0x3ff00000 && o->spr.y < 0x3ff00000) return seen;
    return false;
}

/* Obj_SetState 0x44c5f0 */
void obj_set_state(int obj, int state)
{
    Obj *o = &g_objs[(int16_t)obj];
    o->state = (int16_t)state;
    obj_update_sprite(o);
}

/* Obj_ListRotate 0x44cab0: the second entry of the attached list moves to its head (the tank's gun,
   created after the turret, is then updated after it) */
void obj_list_rotate(void)
{
    Obj *h = g_obj_attached_list, *n = h->next;
    h->next = n->next;
    n->next = h;
    g_obj_attached_list = n;
}

/* the 16.16 point `side` pixels across (heading + 0x100) and `fwd` along the heading from (x, y) */
static void offset_point(int32_t x, int32_t y, int a, int side, int fwd, int32_t *ox, int32_t *oy)
{
    int b = (a + 0x100) & 0x3ff;
    *ox = osin(a) * fwd + osin(b) * side + x;
    *oy = ocos(a) * fwd + ocos(b) * side + y;
}

/* Obj_CreateAttached 0x44cad0: the highest free slot (3499..1; none: -1), cleared. The object takes
   its owner's heading and sits at the offset: a car (1, 5) a pixel under its z, a train carriage
   (2: a pixel under, 3: its z minus a 65536th; Train_GetCarriage), a ped (4) a 65536th over, an object
   a pixel under. It is drawn with the frame of its object_info spr_num as the initial sprite, blended
   when `kind` (sic: the original tests the kind against the blended types 10, 0x33, 0x43, 0x30,
   0x31) is one of them, goes into the grid and to the head of the attached list. */
int obj_create_attached(int owner, int kind, int side, int fwd, int type)
{
    kind = (int16_t)kind, side = (int16_t)side, fwd = (int16_t)fwd, type = (int16_t)type;
    const Car *c = NULL;
    const int32_t *tr = NULL;
    const Ped *p = NULL;
    const Obj *ob = NULL;
    if (kind == 1 || kind == 5) c = car_get(owner);
    else if (kind == 2) tr = train_get_carriage(owner);
    else if (kind == 4) p = ped_get(owner);
    else ob = &g_objs[(int16_t)owner];
    int slot = 0xdab;
    while (g_objs[slot].state != 0)
        if (--slot < 1) return -1;
    Obj *o = &g_objs[slot];
    memset(o, 0, sizeof *o);
    o->owner = (int16_t)owner;
    o->off_fwd = (int16_t)fwd;
    o->off_side = (int16_t)side;
    o->attach_kind = (int16_t)kind;
    o->id = (int16_t)slot;
    o->type = (int16_t)type;
    int a;
    int32_t x, y, z;
    if (c) {
        a = c->spr.angle;
        z = c->spr.z - 0x10000;
        offset_point(c->spr.x, c->spr.y, a, side, fwd, &x, &y);
        if (kind == 5) o->spr.angle = (int16_t)a;   /* (Sprite_Init sets it again) */
    } else if (kind == 2 || kind == 3) {
        /* kind 3 reads the carriage pointer that only kind 2 fetched (NULL in the original) */
        if (!tr) tr = train_get_carriage(owner);
        z = kind == 2 ? tr[2] - 0x10000 : tr[2] - 1;
        a = (int16_t)tr[6];
        offset_point(tr[0], tr[1], a, side, fwd, &x, &y);
    } else if (p) {
        a = p->spr.angle;
        z = p->spr.z + 1;
        offset_point(p->spr.x, p->spr.y, a, side, fwd, &x, &y);
    } else {
        a = ob->spr.angle;
        z = ob->spr.z - 0x10000;
        offset_point(ob->spr.x, ob->spr.y, a, side, fwd, &x, &y);
    }
    o->speed = o->life = o->heading = 0;
    o->u12 = 0;
    o->frame_timer = 0;
    o->state = 1;
    const ObjInfo *info = info_of(type);
    o->weight = (int16_t)info->weight;
    o->next = o->prev = NULL;
    sprite_init(&o->spr, x, y, z, (int16_t)a, info->spr_num);
    if (kind == 10 || kind == 0x33 || kind == 0x43 || kind == 0x30 || kind == 0x31) sprite_set_blend(&o->spr);
    obj_update_sprite(o);
    coll_insert(COLL_OBJECT, slot, o, o->spr.unk20, o->spr.x, o->spr.y);
    o->next = g_obj_attached_list;
    g_obj_attached_list = o;
    return slot;
}

/* unlinks object id from the moving list (chained through next) if it is there */
static void unlink_moving(int id)
{
    unlink(&g_obj_moving_list, (int16_t)id);
}

/* Obj_DeleteByOwner 0x44d360: every slot 3499..1 whose owner is `owner`, in use or not (a free slot
   keeps the owner of its last object: Obj_Delete runs on it again, as in the original), leaves the
   moving list and is deleted. */
void obj_delete_by_owner(int owner)
{
    for (int i = 0xdab; i >= 1; i--) {
        if (g_objs[i].owner != (int16_t)owner) continue;
        unlink_moving(i);
        obj_delete(i);
    }
}

/* the depth key of an object at (x, y, z): its z, a layer higher over a slope */
static int32_t slope_key(int32_t x, int32_t y, int32_t z)
{
    return cell_type(x, y, z) < 0x81 ? z : z - 0x400000;
}

/* Obj_CreateAnimated 0x44d3d0: blocks 1..255 only (else -1); the highest free slot, cleared, on a
   random first frame (1..7) of `type`, burning on `owner` (attach kind `kind`), into the grid, the
   animated list and Fire_Register. The depth key goes a layer up over a slope; a key of 0 or less
   deletes it again (-1). The sprite is set up with the owner as its angle, then angle 0. */
int obj_create_animated(int32_t x, int32_t y, int32_t z, int type, int owner, int kind)
{
    int bx = x >> 22, by = y >> 22;
    if (bx < 1 || bx > 0xff || by < 1 || by > 0xff) return -1;
    int slot = 0xdab;
    while (g_objs[slot].state != 0)
        if (--slot < 1) return -1;
    Obj *o = &g_objs[slot];
    memset(o, 0, sizeof *o);
    o->type = (int16_t)type;
    o->id = (int16_t)slot;
    const ObjInfo *info = info_of((int16_t)type);
    o->weight = (int16_t)info->weight;
    sprite_init(&o->spr, x, y, z, (int16_t)owner, obj_sprite_base);
    o->next = o->prev = NULL;
    coll_insert(COLL_OBJECT, slot, o, o->spr.unk20, o->spr.x, o->spr.y);
    int16_t r = (int16_t)math_random();
    o->attach_kind = (int16_t)kind;
    o->spr.angle = 0;
    o->u10 = 0;
    o->next = g_obj_anim_list;
    o->in_anim_list = 1;
    g_obj_anim_list = o;
    o->state = (int16_t)(r % 7 + 1);
    o->owner = (int16_t)owner;
    obj_update_sprite(o);
    fire_register(slot);
    o->spr.zkey = slope_key(x, y, z);
    if (o->spr.zkey > 0) return slot;
    obj_delete(slot);
    return -1;
}

/* Obj_Kick 0x44d5d0: only objects of status 0, 8 or 9. A bomb (0x16) in state 1 is deleted and
   explodes (a pixel up, its ped's player blamed). The traffic signs 0x21 / 0x22 don't move. Others
   join the moving list when at rest (unless weight 3 or invisible), then (not weight 3) get the
   heading and the speed (2 * speed >> weight) - 1 (a quarter of it without a depth, at least 1;
   negative turns it around) and start tumbling (state 2) when faster than 4. The original also
   compares the angle with itself +- 0x20 to turn it by 0x40, which is never true. */
void obj_kick(int32_t x, int32_t y, int obj, int speed, int angle)
{
    (void)x, (void)y;
    Obj *o = &g_objs[(int16_t)obj];
    const ObjInfo *info = info_of(o->type);
    if (info->status != 0 && info->status != 8 && info->status != 9) return;
    if (o->type == 0x16) {
        if (o->state == 1) {
            obj_delete(obj);
            expl_create(o->spr.x, o->spr.y, o->spr.z - 0x10000, player_find_by_ped(o->u1e));
        }
        return;
    }
    if (o->type == 0x21 || o->type == 0x22) return;
    if (o->speed == 0 && o->weight != 3 && info->status != OBJ_STATUS_INVISIBLE) {
        o->next = g_obj_moving_list;   /* (prev isn't set) */
        g_obj_moving_list = o;
    }
    if (o->weight == 3) return;
    speed = (int16_t)speed;
    o->heading = (int16_t)(angle & 0x3ff);
    int16_t s = (int16_t)(((speed * 2) >> (o->weight & 0x1f)) - 1);
    o->speed = s;
    if (info->depth < 6) o->speed = (int16_t)(s >> 2);
    if (o->speed == 0) o->speed = 1;
    if (o->speed < 0) {
        o->speed = (int16_t)-o->speed;
        o->heading = (int16_t)(((angle & 0x3ff) - 0x200) & 0x3ff);
    }
    if (o->speed > 4 && o->state == 1) {
        o->state = 2;
        obj_update_sprite(o);
    }
}

/* Obj_RemoveMoving 0x44eb30: out of the moving list, then Obj_Delete */
void obj_remove_moving(int obj)
{
    unlink_moving(obj);
    obj_delete(obj);
}

/* Obj_OnCarWrecked 0x44ecc0: the tank's explosion (2 pixels up) and its turret (+0x11a) and gun
   (+0x11c) objects become the wrecked types 0x26 / 0x27 */
void obj_on_car_wrecked(Car *c)
{
    expl_create(c->spr.x, c->spr.y, c->spr.z - 0x20000, c->player);
    g_objs[c->siren_state].type = 0x27;
    g_objs[c->horn].type = 0x26;
}

/* Obj_DeleteWrapper 0x44ed50 */
void obj_delete_wrapper(int obj) { obj_delete(obj); }

/* ---- Obj_UpdateAll 0x44d790 ---- */

static int32_t clamp_world(int32_t v)
{
    if (v < 0) v = 0;
    if (v > 0x3fc00000) v = 0x3fff0000;
    return v;
}

/* the cached type of the cell of a 16.16 position, as the original indexes it (car_type_cache: 0
   outside the array) */
static uint8_t cache_at(int32_t x, int32_t y, int32_t z) { return car_type_cache(x, y, z); }

/* One object of the moving list (not rockets: type 0x1f). States: 1 at rest, 2-6 sliding, 7 bouncing,
   8-13 tumbling, 14-19 in the air (falling off an edge), 20-25 tumbling again, 26-31 sinking in water
   (status 8 objects float at 26). It moves speed pixels along its heading (speed at most 48), stopped
   by a building corner (state 7); a tumbling object knocks peds down (anim 0x2c); a ped walking into a
   light one stops it. Each frame it spins 0x40 (states 1-25 less the turns of 14-25) and the state
   steps through the switch below; over an edge (ground 8 pixels lower) it falls 5 pixels a frame and
   peds panic (always, or 1 in 4 for status 4); landing on water sinks it (26), elsewhere it lands
   (status 0: state 7 or 2 with debris, status 2: 7, status 4: 2) and smashes into its object_info
   `into` objects, kicked on with speed 3. Then it slows down by 2 a frame (1 at speed 1); Map_SlopeDelta
   is given the block kind, not a type map, so the slope never speeds it up. At speed 0 it settles on
   the ground, leaves the list (through its prev link, which Obj_Kick never sets: the head becomes the
   next object, dropping the ones before it) and kicks a ped standing on it (weight 0). */
static void update_moving(Obj *o)
{
    const ObjInfo *info0 = info_of(o->type);
    if (o->type == 0x29) {   /* a car's towed thing: follows the car */
        o->heading = 0x200;
        o->speed = 0x10;
        coll_remove(o, o->spr.unk20);
        if (o->attach_kind == 1) {
            const Car *c = car_get(o->owner);
            o->spr.angle = c->spr.angle;
            o->spr.z = c->spr.z - 1;
            o->spr.zkey = c->spr.zkey - 1;
            o->spr.x = c->spr.x;
            o->spr.y = c->spr.y;
            o->state = 1;
        }
        sprite_set_frame(&o->spr, info0->spr_num + o->state - 1);
        coll_insert(COLL_OBJECT, o->id, o, o->spr.unk20, o->spr.x, o->spr.y);
    }
    if (o->type == PROJ_ROCKET) return;
    int a = o->heading;
    const ObjInfo *info = info_of(o->type);
    if (o->speed > 0x30) o->speed = 0x30;
    int32_t nx = clamp_world(osin(a) * o->speed + o->spr.x), ny = clamp_world(ocos(a) * o->speed + o->spr.y);
    CollBox *b = coll_build_box(nx, ny, o->spr.z, info->w >> 17, info->h >> 17, a & 0x3ff, (int16_t)(info->depth >> 16), &static_box);
    coll_compute_bounds(b);
    bool free_path = true;
    int kind = (cache_at(o->spr.x, o->spr.y, o->spr.z) & 0x70) >> 4;
    uint8_t last = 0;
    for (int i = 0; i < 4; i++) {
        last = cache_at(b->x[i], b->y[i], b->gz[i]);
        if ((last & 0x70) == 0x50) {
            free_path = false;
            o->state = 7;
            break;
        }
    }
    CollHit *h = coll_query_box_first(b, COLL_PED, 3, o->id);
    if (h) {
        Ped *p = h->owner;
        if (o->state > 7 && o->state < 0x1a) p->anim = 0x2c;
        if (p->anim > 0 && p->anim < 0x11 && o->weight == 0) o->speed = 0;
    }
    coll_unlock();
    coll_query_box_first(b, COLL_CAR, 3, o->id);
    coll_unlock();
    int s = free_path ? o->speed : (o->speed = 0);
    nx = clamp_world(osin(a) * s + o->spr.x);
    ny = clamp_world(ocos(a) * s + o->spr.y);
    int32_t ground = map_get_ground_z(g_game.map, o->spr.x, o->spr.y, o->spr.z - 0x200000);
    if (ground <= 0x3f0000) ground = 0x3f0000;
    int16_t slope = (int16_t)map_slope_delta((uint32_t)kind, o->spr.x, o->spr.y, nx, ny);
    coll_remove(o, o->spr.unk20);
    o->spr.y = ny;
    o->spr.x = nx;
    bool sloped = false;
    for (int i = 0; i < 4; i++) sloped |= (cache_at(b->x[i], b->y[i], b->gz[i] - 1) & 0x80) != 0;
    if (sloped) {
        coll_compute_min_z(b);
        o->spr.zkey = g_cc.min_gz - 0x400000;
    } else {
        o->spr.zkey = o->spr.z;
    }
    if (o->spr.zkey < 0) o->spr.zkey = 1;
    int st = o->state;
    if (st < 0xe || st > 0x14) o->spr.z = ground;
    if (st != 0) coll_insert(COLL_OBJECT, o->id, o, o->spr.unk20, o->spr.x, o->spr.y);
    st = o->state;
    if (st < 0x1a) o->spr.angle = (int16_t)((o->spr.angle + 0x40) & 0x3ff);
    if (st > 0xd && st < 0x1a) o->spr.angle = (int16_t)((o->spr.angle - 0x40) & 0x3ff);
    if (st > 0x13 && st < 0x1a) o->spr.angle = (int16_t)((o->spr.angle - 0x40) & 0x3ff);
    if (st < 0x1b) o->state = (int16_t)(st + 1);
    switch (o->state) {
    case 1: o->state = 0; break;
    case 2: o->state = 1; break;
    case 7: o->state = 2; break;
    case 8: o->state = 7; break;
    case 0xe: o->state = 8; break;
    case 0x14: o->state = 0xe; break;
    case 0x1a: o->state = 0x14; break;
    }
    if (o->state > 0x19) {
        o->speed = 2;
        if (o->life > 5) {
            o->life = 0;
            o->state++;
        }
    }
    obj_update_sprite(o);
    st = o->state;
    if (o->spr.z + 0x80000 < ground && (st < 0xe || (st > 0x13 && st < 0x1a))) {
        o->spr.z += 0x50000;
        if (info->status == 4 && o->speed < 3) o->speed = (int16_t)((math_random() & 1) + 3);
        if (o->state < 8) o->state = 8;
        if ((int16_t)math_random() < 8000 || info->status != 4) ped_panic_near(o->spr.x, o->spr.y, o->spr.z, -1);
    }
    if (o->state > 0xd && o->state < 0x14) {
        o->speed = 3;
        o->life++;
        o->spr.z -= 0x50000;
        if ((0x10 - o->weight) - (int)(math_random() & 7) <= o->life) o->state = 0x14;
        obj_update_sprite(o);
    }
    if (o->state > 0x1e) {
        if (info->status == OBJ_STATUS_8) o->state = 0x1a;
        if (o->state > 0x1e) {
            o->state = 0;
            coll_remove(o, o->spr.unk20);
            o->speed = 0;
        }
    }
    st = o->state;
    if (((st > 7 && st < 0xe) || (st > 0x13 && st < 0x1a)) && ground <= o->spr.z + 0xa0000) {
        if (kind == 1) {   /* water */
            if (st < 0x1a) {
                o->state = 0x1a;
                obj_update_sprite(o);
                o->life = 0;
                o->spr.angle = 0x100;
            }
        } else {
            h = coll_query_box_first(b, COLL_PED, 3, o->id);
            if (h) ((Ped *)h->owner)->anim = 0x2c;
            coll_unlock();
            if (o->state < 8 || o->state > 0xd) {
                if (info->status == 0) o->state = info->num_into ? 2 : 7;
                else if (info->status == 2) o->state = 7;
                else if (info->status == 4) o->state = 2;
            } else {
                o->state = 7;
                o->speed = 3;
                o->life = 0;
                if (info->status == 4) o->heading = (int16_t)(o->heading + ((int)(math_random() & 7) - 4) * 0x40);
            }
            obj_update_sprite(o);
            int32_t ox = o->spr.x, oy = o->spr.y;
            int back = (o->heading - 0x200) & 0x3ff;
            int32_t sx = osin((int16_t)back), sy = ocos((int16_t)back);
            for (int k = 0; k < info->num_into; k++) {
                int d = obj_create(sx * 5 + ox, sy * 5 + oy, o->spr.z, (int16_t)info->into[k], 0);
                if ((int16_t)d == -1) continue;
                Obj *q = &g_objs[(int16_t)d];
                if (q->state == 2) {
                    q->state = 0xe;
                    obj_update_sprite(q);
                    q->life = 0;
                }
                int r = math_random();
                obj_kick(o->spr.x, o->spr.y, d, 3, ((int16_t)(r >> 5) + o->heading) & 0x3ff);
                math_random();
            }
        }
        o->spr.z = ground;
    }
    if (!(last & 0x80) || slope < 1) {
        if (o->speed > 1) o->speed--;
        if (o->speed > 0) o->speed--;
    } else {
        if (++o->speed > 8) o->speed = 7;
    }
    if (kind == 1 && o->state > 0 && o->state < 7) {
        o->state = 0x1a;
        obj_update_sprite(o);
        o->life = 0;
        o->spr.angle = 0x100;
    }
    if (o->speed < 1) {
        o->speed = 0;
        int32_t g = map_get_ground_z(g_game.map, o->spr.x, o->spr.y, o->spr.z - 0x200000);
        if (g <= 0x400000) g = 0x3effff;
        o->spr.z = g;
        if (abs(g - o->spr.zkey) > 0x400000) {
            o->spr.zkey = g - 0x400000;
            if (o->spr.zkey < 1) o->spr.zkey = 1;
        }
        if (o->next) o->next->prev = o->prev;
        if (!o->prev) g_obj_moving_list = o->next;
        else o->prev->next = o->next;
        h = coll_query_box_first(b, COLL_PED, 3, o->id);
        if (h && o->weight < 1) {
            const Ped *p = h->owner;
            obj_kick(p->spr.x, p->spr.y, o->id, 3, p->spr.angle);
        }
        coll_unlock();
    }
    o->life++;
    if (o->spr.zkey < 1) {
        o->state = 0;
        coll_remove(o, o->spr.unk20);
    }
}

/* One object of the status-7 list: every object_info h (integer part) frames a step; after depth
   (integer part) steps it is deleted and explodes a pixel up, blamed on its ped's player (u1e). */
static bool update_timer(Obj *o)
{
    const ObjInfo *info = info_of(o->type);
    if (++o->frame_timer < (int16_t)(info->h >> 16)) return false;
    o->u10++;
    o->frame_timer = 0;
    if ((int16_t)(info->depth >> 16) != o->u10) return false;
    expl_create(o->spr.x, o->spr.y, o->spr.z - 0x10000, player_find_by_ped(o->u1e));
    return true;
}

/* a fire on its owner: an object (0), a car (1; one parked at the map's corner, below 0x100000 in x
   and y, puts the fire out), else a ped. Its z is under the owner's (cars 2 pixels down, the others 1;
   the 0x22 test can't be true for a fire). */
static bool follow_owner(Obj *o)
{
    if (o->attach_kind == 0) {
        const Obj *w = &g_objs[o->owner];
        o->spr.angle = w->spr.angle;
        o->spr.z = w->spr.z - 1;
        o->spr.zkey = o->type == 0x22 ? w->spr.zkey + 1 : w->spr.zkey - 1;
        o->spr.x = w->spr.x;
        o->spr.y = w->spr.y;
    } else if (o->attach_kind == 1) {
        const Car *c = car_get(o->owner);
        if (c->spr.x < 0x100000 && c->spr.y < 0x100000) return true;
        o->spr.angle = c->spr.angle;
        o->spr.z = c->spr.z - 2;
        o->spr.zkey = c->spr.zkey - 2;
        o->spr.x = c->spr.x;
        o->spr.y = c->spr.y;
    } else {
        const Ped *p = ped_get(o->owner);
        o->spr.angle = p->spr.angle;
        o->spr.z = p->spr.z - 1;
        o->spr.zkey = p->spr.zkey - 1;
        o->spr.x = p->spr.x;
        o->spr.y = p->spr.y;
    }
    return false;
}

/* a fire without an owner sets a ped walking into it on fire (object 0x2e; health - 10) and adds 5
   damage to a car on it */
static void fire_burn_around(Obj *o)
{
    CollBox *b = coll_build_box(o->spr.x, o->spr.y, o->spr.z, 4, 4, o->spr.angle, 0x10, &static_box);
    CollHit *h = coll_query_box_first(b, COLL_PED, 3, o->id);
    coll_unlock();
    if (h) {
        Ped *p = h->owner;
        if (p->carried == -1) {
            p->carried = (int16_t)obj_create_animated(p->spr.x, p->spr.y, p->spr.z - 0x10000, 0x2e, p->id, 2);
            if (p->player_ctl != 1) {
                p->state = 1;
                p->target_x = p->spr.x;
                p->target_y = p->spr.y;
                p->target_ped = -1;
            }
        }
        p->health = (int8_t)(p->health - 10);
    }
    h = coll_query_box_first(b, COLL_CAR, 3, o->id);
    if (h) {
        Car *c = h->owner;
        c->damage = (int16_t)(c->damage + 5);
        if (c->damage > 100) c->damage = 100;
    }
    coll_unlock();
}

/* One object of the animated list; returns the id to delete after it (-1 none). */
static int update_animated(Obj *o)
{
    const ObjInfo *info = info_of(o->type);
    int del = -1;
    o->frame_timer++;
    if (o->type == 0x3f && o->frame_timer == 1 && o->state == 5) obj_create(o->spr.x, o->spr.y, o->spr.z, 0x40, o->param);
    int t = o->type;
    if (t == 0x12 || t == 0x13 || t == 0x2e) {
        ped_panic_near(o->spr.x, o->spr.y, o->spr.z, -1);
        coll_remove(o, o->spr.unk20);
        if (o->state == 1) obj_create(o->spr.x, o->spr.y, o->spr.z + 1, 10, o->spr.angle);   /* smoke */
        if (o->owner == -1) fire_burn_around(o);
        else if (follow_owner(o)) del = o->id;
        if (o->spr.zkey < 1) o->spr.zkey = 1;
        if (o->spr.z < 1) o->spr.z = 1;
        sprite_set_frame(&o->spr, info->spr_num + o->state - 1);
        coll_insert(COLL_OBJECT, o->id, o, o->spr.unk20, o->spr.x, o->spr.y);
    }
    t = o->type;
    if (t == 10 || t == 0x33) {   /* smoke rises (z - 1) and drifts (+x, -y) */
        coll_remove(o, o->spr.unk20);
        int32_t z = o->spr.z, k = o->spr.zkey - 0x10000;
        o->spr.z = z - 0x10000;
        o->spr.zkey = k;
        if (k < 1) o->spr.zkey = 1;
        if (z - 0x10000 < 1) o->spr.z = 1;
        if (o->spr.x < 0x3f800000) o->spr.x += 0x10000;
        if (o->spr.y > 0x40000) o->spr.y -= 0x10000;
        sprite_set_frame(&o->spr, info->spr_num + o->state - 1);
        coll_insert(COLL_OBJECT, o->id, o, o->spr.unk20, o->spr.x, o->spr.y);
    }
    if (o->type == 0x43) {
        o->spr.zkey++;
        sprite_set_frame(&o->spr, info->spr_num + o->state - 1);
    }
    t = o->type;
    if (t == 0x40 || t == 0x4d) {   /* lives as long as its ped (param) isn't dead: then 1000 frames */
        const Ped *p = ped_get(o->param);
        if (p->state != 0x17 && p->state != 5 && p->state != 0xc) {
            o->life++;
            if (p->state == 0x18) o->life = 1000;
            if (o->life > 1000) obj_delete(o->id);
        }
    } else if (info->status == OBJ_STATUS_ANIM9) {   /* cycles 1..aux, a frame every 3 */
        if (++o->frame_timer > 2) {
            if (++o->state > (int)info->aux) o->state = 1;
            sprite_set_frame(&o->spr, info->spr_num + o->state - 1);
            o->frame_timer = 0;
        }
    } else if ((int16_t)(info->h >> 16) <= o->frame_timer) {
        /* a frame every h (integer part) frames over w (integer part) frames; with a depth, depth
           (integer part) cycles and the object goes (a fire 0x12 leaves a dying fire 0x13 and adds to
           its car's burning count) */
        o->state++;
        o->frame_timer = 0;
        if ((int16_t)(info->w >> 16) < o->state) {
            o->state = 1;
            if (info->depth != 0 && o->u12 == 0) o->u10++;
        }
        if (o->state == 1 && (int16_t)(info->depth >> 16) <= o->u10 && o->u12 == 0) {
            del = o->id;
            if (t == 0x12) {
                obj_create(o->spr.x, o->spr.y, o->spr.z, 0x13, o->owner);
                if (o->attach_kind == 1) car_get(o->owner)->burning++;
            }
        } else {
            sprite_set_frame(&o->spr, info->spr_num + o->state - 1);
        }
    }
    return del;
}

/* One object of the attached list: back on its owner (see Obj_CreateAttached); the turning lights
   0x35 (cars) and 0x30 / 0x31 (objects) cycle their states. */
static void update_attached(Obj *o)
{
    coll_remove(o, o->spr.unk20);
    int k = o->attach_kind;
    if (k == 1 || k == 5) {
        const Car *c = car_get(o->owner);
        int a = c->spr.angle;
        if (k == 5) o->spr.angle = (int16_t)a;
        o->spr.z = c->spr.z - 2;
        o->spr.zkey = c->spr.zkey - 2;
        offset_point(c->spr.x, c->spr.y, a, o->off_side, o->off_fwd, &o->spr.x, &o->spr.y);
        if (o->type == 0x35 && ++o->state == 0xc) o->state = 1;
    } else if (k == 2) {
        const int32_t *tr = train_get_carriage(o->owner & 0xff);   /* (the low byte of the owner) */
        int a = (int16_t)tr[6];
        o->spr.angle = (int16_t)a;
        o->spr.z = tr[2] - 0x10000;
        o->spr.zkey = tr[3] - 1;
        if (o->off_side < 0) o->spr.angle = (int16_t)((a - 0x200) & 0x3ff);
        offset_point(tr[0], tr[1], a, o->off_side, o->off_fwd, &o->spr.x, &o->spr.y);
    } else if (k == 4) {
        const Ped *p = ped_get(o->owner);
        int a = p->spr.angle;
        o->spr.angle = (int16_t)a;
        o->spr.z = p->spr.z + 1;
        o->spr.zkey = p->spr.zkey + 1;
        offset_point(p->spr.x, p->spr.y, a, o->off_side, o->off_fwd, &o->spr.x, &o->spr.y);
    } else if (k == 0) {
        const Obj *w = &g_objs[o->owner];
        int a = w->spr.angle;
        o->spr.angle = (int16_t)a;
        o->spr.z = w->spr.z - 1;
        o->spr.zkey = o->type == 0x22 ? w->spr.zkey + 1 : w->spr.zkey - 1;
        offset_point(w->spr.x, w->spr.y, a, o->off_side, o->off_fwd, &o->spr.x, &o->spr.y);
        if ((o->type == 0x30 || o->type == 0x31) && ++o->state == 9) o->state = 1;
    }
    obj_update_sprite(o);
    coll_insert(COLL_OBJECT, o->id, o, o->spr.unk20, o->spr.x, o->spr.y);
}

/* Obj_UpdateAll 0x44d790: the moving list, the status-7 list, the animated list, the attached list,
   then the projectiles (Proj_UpdateAll). A list's next object is read after the current one is done
   (deletions wait for that). */
void obj_update_all(void)
{
    for (Obj *o = g_obj_moving_list; o; o = o->next) update_moving(o);
    for (Obj *o = g_obj_status7_list; o;) {
        bool boom = update_timer(o);
        Obj *n = o->next;
        if (boom) obj_delete(o->id);
        o = n;
    }
    for (Obj *o = g_obj_anim_list; o;) {
        const ObjInfo *info = info_of(o->type);
        int del = update_animated(o);
        Obj *n = o->next;
        if (del != -1) {
            if (info->aux == 1) obj_unlink_animated(del);
            else obj_delete(del);
        }
        o = n;
    }
    for (Obj *o = g_obj_attached_list; o; o = o->next) update_attached(o);
    proj_update_all();
}
