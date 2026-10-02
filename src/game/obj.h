/* Objects (0x44c2d0-0x44ee4f): the object table (0x6b40d0, 3500 x 0x88; Obj_Get 0x44c2d0), the style's
   object_info records (Obj_LoadInfos 0x44ed60), the CMP object_pos section (Obj_SetMapObjects
   0x44ee20) and the objects a level starts with (Obj_InitFromMap 0x44c620, Obj_CreateStatic 0x44c870,
   Obj_Create 0x44cef0). Object behaviour (Obj_UpdateAll 0x44d790) is not ported yet. */
#pragma once
#include "../render/sprite.h"
#include "layout.h"
#include <stdint.h>

enum { OBJ_MAX = 3500, OBJ_INFO_MAX = 0x100, OBJ_POS_SIZE = 14, OBJ_SMASHABLE_MAX = 200 };

/* object_info status byte (+0x12) */
enum { OBJ_STATUS_NORMAL = 0, OBJ_STATUS_1 = 1, OBJ_STATUS_INVISIBLE = 3, OBJ_STATUS_ANIM = 5,
       OBJ_STATUS_CAR = 6, OBJ_STATUS_7 = 7, OBJ_STATUS_8 = 8, OBJ_STATUS_ANIM9 = 9 };

typedef struct Obj {
    int16_t id;                 /* +0x00 */
    int16_t speed;              /* +0x02 */
    int16_t life;               /* +0x04 */
    int16_t heading;            /* +0x06 */
    int16_t weight;             /* +0x08 object_info +0xe */
    int16_t type;               /* +0x0a object_info index */
    int16_t state;              /* +0x0c frame state: 0 free, 1 normal, 7 (status 1 objects) */
    int16_t frame_timer;        /* +0x0e */
    int16_t u10, u12;
    int16_t owner;              /* +0x14 (-1) */
    int16_t param;              /* +0x16 creation parameter of types 0x3f / 0x40 / 0x4d */
    int16_t attach_kind;        /* +0x18 */
    int16_t off_x, off_y;       /* +0x1a, +0x1c */
    int16_t u1e;
    uint8_t in_anim_list;       /* +0x20 */
    uint8_t u21;
    uint8_t pad22[2];
    struct Obj *next;           /* +0x24 next in the list of its status (animated 0x728430, status 7 0x6b40c0) */
    struct Obj *prev;           /* +0x28 */
    Sprite spr;                 /* +0x2c x +0x2c, y +0x30, z +0x34, z key +0x38, angle +0x44 */
} Obj;
GAME_OFS(Obj, speed, 0x02); GAME_OFS(Obj, heading, 0x06); GAME_OFS(Obj, weight, 0x08);
GAME_OFS(Obj, type, 0x0a); GAME_OFS(Obj, state, 0x0c); GAME_OFS(Obj, frame_timer, 0x0e);
GAME_OFS(Obj, owner, 0x14); GAME_OFS(Obj, param, 0x16); GAME_OFS(Obj, attach_kind, 0x18);
GAME_OFS(Obj, off_x, 0x1a); GAME_OFS(Obj, in_anim_list, 0x20); GAME_OFS(Obj, u21, 0x21);
GAME_OFS32(Obj, next, 0x24); GAME_OFS32(Obj, prev, 0x28); GAME_OFS32(Obj, spr, 0x2c);
GAME_SIZE32(Obj, 0x88);

/* object_info record (style; 20 + 2 * num_into bytes). Obj_LoadInfos converts w/h/depth below
   0x10000 to 16.16 and rebases spr_num by the object sprite group, in place. */
typedef struct __attribute__((packed)) {   /* records are only 2-aligned */
    int32_t w, h, depth;        /* +0x00, +0x04, +0x08 */
    uint16_t spr_num;           /* +0x0c */
    uint16_t weight;            /* +0x0e */
    uint16_t aux;               /* +0x10 */
    uint8_t status;             /* +0x12 */
    uint8_t num_into;           /* +0x13 */
    uint16_t into[];            /* +0x14 objects spawned when smashed */
} ObjInfo;
_Static_assert(sizeof(ObjInfo) == 0x14, "object_info header");

/* CMP object_pos record (14 bytes) */
typedef struct {
    uint16_t x, y, z;           /* pixels */
    uint8_t type, remap;        /* remap >= 0x80: a parked car of model `type` */
    uint16_t rotation, pitch, roll;
} ObjPos;
_Static_assert(sizeof(ObjPos) == OBJ_POS_SIZE, "object_pos layout");

extern Obj g_objs[OBJ_MAX];                 /* 0x6b40d0 */
extern ObjInfo *g_obj_infos[OBJ_INFO_MAX];  /* 0x5c2c78 (pointer 0x728444) */
extern int g_obj_info_count;                /* 0x728438 */
extern const ObjPos *g_obj_map;             /* 0x728440 */
extern int g_obj_map_count;                 /* 0x72843c */
extern Obj *g_obj_anim_list;                /* 0x728430 status 5 / 9 */
extern Obj *g_obj_status7_list;             /* 0x6b40c0 */
extern Obj *g_obj_moving_list;              /* 0x6b40bc */
extern Obj *g_obj_attached_list;            /* 0x6b40c4 */
extern int g_obj_smashable;                 /* 0x6b40c8 */
extern int g_obj_parked_alarm;              /* 0x728434: every other parked car (vtype 4) gets the alarm */

static inline Obj *obj_get(int id) { return &g_objs[id]; }       /* Obj_Get 0x44c2d0 */
/* Obj_LoadInfos 0x44ed60 on the style's object_info section (modified in place). False if more
   than 0x100 records (Error_Fatal -24 in the original). */
bool obj_load_infos(uint8_t *data, int size, int sprite_base);
void obj_set_map_objects(const uint8_t *data, int size);   /* Obj_SetMapObjects 0x44ee20 */
void obj_init_from_map(void);               /* Obj_InitFromMap 0x44c620 */
void obj_create_static(int slot, int type, int32_t x, int32_t y, int32_t z, int angle);   /* 0x44c870 */
/* Obj_Create 0x44cef0: a free object (highest free slot) of `type` at (x, y, z); -1 if refused. */
int obj_create(int32_t x, int32_t y, int32_t z, int type, int angle);
void obj_update_sprite(Obj *o);             /* Obj_UpdateSprite 0x44c3a0 */
void obj_delete(int id);                    /* Obj_Delete 0x44eb90 */
void obj_unlink_animated(int id);           /* Obj_UnlinkAnimated 0x44ea30 */
int objs_in_use(void);                      /* slots with state != 0 (for checks) */
