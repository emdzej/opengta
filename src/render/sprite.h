/* Sprites (0x47bb10-0x47cd00, the delta overlays 0x414310-0x414590): the style's sprite table
   (Sprite_LoadInfo 0x47ca50, Sprite_SetGroupBases 0x47cbd0), the sprite object every drawable entity
   embeds (cars at +0x250, peds at +0x90, objects at +0x2c), damage/door deltas composited through a
   10-entry cache (Sprite_GetComposite 0x4143a0), rotated drawing (Sprite_Draw 0x47bc00 /
   Sprite_DrawCached 0x47c130 onto Poly_DrawSprite), the per-layer draw trees (Sprite_Queue 0x47c940,
   Sprite_DrawLevel 0x47c030) and the queueing of the visible entities (Render_QueueVisibleEntities
   0x437000). See docs/sprites.md.

   Frame order in the original: Game_Frame 0x430b20 empties the trees (DrawList_Clear), Game_Render
   0x430d40 queues the visible sprites then Render_DrawCity 0x4389f0 calls Sprite_DrawLevel(z) for each
   layer z = 5..0 after projecting the layer's top plane and before drawing its blocks. */
#pragma once
#include "../style.h"
#include "camera.h"
#include <stdbool.h>
#include <stdint.h>

/* The 21 sprite groups, in the order of the style's sprite numbers section (u16 counts). The group
   bases are cumulative counts, kept as shorts at the addresses given. */
enum {
    SPRITE_GROUP_ARROW,           /* 0x774efa (always 0): HUD arrows and icons */
    SPRITE_GROUP_DIGITS,          /* 0x774f0a */
    SPRITE_GROUP_BOAT,            /* 0x774efc (car info vtype 13) */
    SPRITE_GROUP_BOX,             /* 0x774eee */
    SPRITE_GROUP_BUS,             /* 0x774ef0 (vtypes 0, 2) */
    SPRITE_GROUP_CAR,             /* 0x774ee8 (vtypes 1, 4) */
    SPRITE_GROUP_OBJECT,          /* 0x774f0c */
    SPRITE_GROUP_PED,             /* 0x774ef8 */
    SPRITE_GROUP_SPEEDO,          /* 0x774f10 */
    SPRITE_GROUP_TANK,            /* 0x774efe (vtype 14) */
    SPRITE_GROUP_TRAFFIC_LIGHTS,  /* 0x774eec */
    SPRITE_GROUP_TRAIN,           /* 0x774eea (vtype 8) */
    SPRITE_GROUP_TRDOORS,         /* 0x774f0e */
    SPRITE_GROUP_BIKE,            /* 0x774ef6 (vtype 3) */
    SPRITE_GROUP_TRAM,            /* 0x774f04 (vtype 9) */
    SPRITE_GROUP_WBUS,            /* 0x774f06 */
    SPRITE_GROUP_WCAR,            /* 0x774f02 */
    SPRITE_GROUP_EX,              /* 0x774ef4 */
    SPRITE_GROUP_TUMCAR,          /* 0x774ef2 */
    SPRITE_GROUP_TUMTRUCK,        /* 0x774f00 */
    SPRITE_GROUP_FERRY,           /* 0x774f08 */
    SPRITE_GROUPS
};

enum {
    SPRITE_MAX = 0x424,           /* entries of the info table 0x773e38 */
    SPRITE_DELTAS_MAX = 32,       /* bits of the delta mask */
    SPRITE_LEVELS = 6,            /* draw trees 0x774ec8, one per map layer */
    SPRITE_CACHE = 10,            /* delta composites 0x502e88: 8 of up to 64 x 64, 2 of up to 128 x 128 */
    CAR_REMAPS = 12,              /* palettes per car info record after the car palette base */
};

typedef struct { uint16_t size; const uint8_t *data; } SpriteDelta;

/* A sprite info record (style sprite info section; 12 bytes + 6 per delta, 0x773e38[n] points at it).
   The original relocates the offsets into pointers in place; the port keeps the record raw and resolves
   them here. Offsets address 64 KB pages of 256-byte rows: data = page + v * 256 + u. */
typedef struct SpriteInfo {
    uint8_t w, h;                 /* +0, +1 */
    uint8_t ndeltas;              /* +2 */
    uint16_t size;                /* +4: w * h; > 0x1000 picks the big composite slots */
    uint16_t clut;                /* +6: palette, relative to the sprite palette base */
    const uint8_t *data;          /* +8: the graphic */
    const SpriteDelta *delta;     /* +0xc + 6 i: {u16 size, u32 offset} */
} SpriteInfo;

/* The sprite object (0x5c bytes; Sprite_Init 0x47c9f0). Field offsets are the original's; with 32-bit
   pointers (the gasm build) the struct has the original's size and layout. */
typedef struct Sprite {
    int32_t x, y, z;              /* +0x00..+0x08: 16.16 world; only the integer parts are projected */
    int32_t zkey;                 /* +0x0c: depth key: draw tree (zkey >> 22) and order within it */
    uint8_t remap;                /* +0x10: 0 = the sprite's own palette, else palette + remap - 1 */
    int16_t palette;              /* +0x12: remap palette base (car: car base + record * 12) */
    uint8_t blend;                /* +0x14: drawn blended when the option 0x5031e4 is on */
    uint16_t frame;               /* +0x16: sprite number */
    int16_t angle;                /* +0x18: 0..1023, 0 = graphic upright (its top toward -y) */
    int16_t cached_angle;         /* +0x1a: angle the corners at +0x28 were computed for */
    uint32_t deltas;              /* +0x1c: delta mask */
    int32_t unk20[2];             /* +0x20 (not used by the sprite code) */
    int32_t corner[8];            /* +0x28: rotated corner offsets {x, y} x 4 (16.16, y up) */
    const SpriteInfo *info;       /* +0x48 */
    const SpriteInfo *cached_info;/* +0x4c: info the corners were computed for */
    struct Sprite *next;          /* +0x50: chain of sprites attached to this one (drawn relative) */
    uint16_t saved_frame;         /* +0x54 */
    uint32_t saved_deltas;        /* +0x58 */
} Sprite;
_Static_assert(sizeof(void *) != 4 || sizeof(Sprite) == 0x5c, "Sprite mirrors the original's 0x5c bytes");

/* 0x5031e4: blended sprites allowed (set from a frontend option, 0x51029f, on by default). */
extern bool sprite_blend_option;

/* ---- the style's sprites ---- */

/* Sprite_LoadInfo 0x47ca50 on the sections style_load read raw: the info table, the group bases, the
   draw trees and the composite cache. False if the sections don't parse (the original doesn't check). */
bool sprite_load_info(const Style *s);
int sprite_count(void);                              /* 0x774ee4 */
const SpriteInfo *sprite_get_info(int n);            /* Sprite_GetInfo 0x47c990 (NULL past the table) */
void sprite_set_group_bases(const uint8_t *numbers); /* Sprite_SetGroupBases 0x47cbd0 */
int sprite_group_base(int group);
int sprite_group_count(int group);                   /* from the numbers section */
/* Palettes: Tile_SelectSprite 0x4385b0 (palette index entry n) and Tile_SelectSpriteRemap 0x4385e0 (the
   sprite palette clut, or palette + remap - 1 when remapped). They set poly_clut and return it. */
const uint32_t *sprite_select_palette(int n);
const uint32_t *sprite_select_remap(int clut, int remap, int palette);
int sprite_car_palette(int record);                  /* car base 0x77530c + record * 12 */
int sprite_ped_palette(void);                        /* 0x7750d0: car base + car records * 12 */

/* ---- the sprite object ---- */

void sprite_init(Sprite *sp, int32_t x, int32_t y, int32_t z, int angle, int frame);   /* Sprite_Init 0x47c9f0 */
void sprite_set_frame(Sprite *sp, int frame);        /* Sprite_SetFrame 0x47c960 (clears the deltas) */
void sprite_save_frame(Sprite *sp);                  /* Sprite_SaveFrame 0x47c9a0 */
static inline void sprite_set_remap(Sprite *sp, int remap) { sp->remap = (uint8_t)remap; }        /* Sprite_SetRemap 0x47c9c0 */
static inline void sprite_set_palette(Sprite *sp, int pal) { sp->palette = (int16_t)pal; }        /* Sprite_SetPalette 0x47c9d0 */
static inline void sprite_set_blend(Sprite *sp) { sp->blend = 1; }                                /* Sprite_SetBlend 0x47c9e0 */
void sprite_add_delta(Sprite *sp, int n);            /* Sprite_AddDelta 0x414310 (n < the sprite's deltas) */
void sprite_toggle_delta(Sprite *sp, int n);         /* Sprite_ToggleDelta 0x414330 */
void sprite_remove_delta(Sprite *sp, int n);         /* Sprite_RemoveDelta 0x414360 */
void sprite_clear_deltas(Sprite *sp);                /* Sprite_ClearDeltas 0x414380 */
void sprite_save_deltas(Sprite *sp);                 /* Sprite_SaveDeltas 0x414390 */
/* Sprite_GetComposite 0x4143a0: the graphic with the sprite's deltas applied (the raw graphic if none). */
const uint8_t *sprite_get_composite(const Sprite *sp);

/* ---- drawing (into poly_sprite's target, with the camera of render_copy_camera) ---- */

void sprite_draw(Sprite *sp);          /* Sprite_Draw 0x47bc00: centre projected, size in screen pixels */
void sprite_draw_cached(Sprite *sp);   /* Sprite_DrawCached 0x47c130: every corner projected */
void sprite_draw_with_attached(Sprite *sp);   /* the draw tree callback 0x47c050 */
/* Sprite_DrawScreen 0x47bbc0: sprite n unrotated, top left at (x, y) (Blit_Tile32 0x4894a1). */
void sprite_draw_screen(int x, int y, const SpriteInfo *info);

void sprite_clear_levels(void);        /* DrawList_Clear_thunk 0x47c020 */
void sprite_queue(Sprite *sp);         /* Sprite_Queue 0x47c940 */
void sprite_draw_level(int z);         /* Sprite_DrawLevel 0x47c030 */

/* ---- Render_QueueVisibleEntities 0x437000 ----
   The entity side walks the collision grid (0x5278f8: 128 x 128 cells of 2 x 2 blocks, each a list of
   entries {u8 kind +0, owner +8, next +0xc}); this interface stands in for it until the entity
   tables are ported. */
enum {
    SPRITE_KIND_PED = 1,          /* ped +0x90 */
    SPRITE_KIND_OBJECT = 3,       /* object +0x2c, unless its object type's +0x12 is 3 */
    SPRITE_KIND_CAR = 6,          /* car +0x250 */
    SPRITE_KIND_0x1E = 0x1e,      /* owner +0x34 */
    /* kinds 0xc and 0xd (always) and 7, 8, 10, 0xe, 0x13 (with the trains switch): the owner is the sprite */
};
typedef void (*SpriteVisitFn)(void *vctx, int kind, void *owner);
typedef struct SpriteWorld {
    void *ctx;
    /* visit(vctx, kind, owner) for every entry of cell (cx, cy), in list order */
    void (*walk_cell)(void *ctx, int cx, int cy, SpriteVisitFn visit, void *vctx);
    /* the sprite embedded in the owner of a kind 1, 3 (NULL if invisible), 6 or 0x1e (+0x34) entry */
    Sprite *(*embedded)(void *ctx, int kind, void *owner);
    bool trains_lights;           /* 0x502f48: kinds 7, 8, 10, 0xe, 0x13 are drawn */
} SpriteWorld;
/* Queue the sprites of the cells under the view rect (the local player's, +4 of its record). */
void render_queue_visible_entities(const SpriteWorld *w, const ViewRect *view);
