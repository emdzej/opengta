/* City maps (.CMP, version 331): Map_Load 0x438200 and the map queries of 0x437ae0-0x438950.
   Layout: docs/formats.md. Conventions are the exe's:
   - z = 0 is the TOP layer, z = 5 the lowest (ground level is usually z = 4 / 5);
   - a column is u16 h (number of empty layers at the top), then 6 - h block indices for z = h .. 5;
   - world coordinates are 16.16 "pixels", 64 per block: a block is 0x400000 units, the block of a
     position is coord >> 22.
   The loaded data is kept in one buffer laid out like the original's (base table, columns, blocks, then
   0x4000 bytes for the copy-on-write map change areas), so the Map_Set* ports can follow it. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { MAP_W = 256, MAP_H = 256, MAP_Z = 6, MAP_VERSION = 331 };
/* Limits checked by Map_Load (the sizes of the original's static buffers). */
enum { MAP_OBJECTS_MAX = 0xadd4, MAP_ROUTES_MAX = 0x4000, MAP_NAV_MAX = 0x578, MAP_LOCATIONS_SIZE = 0x6c };

/* A block descriptor (8 bytes, as stored and as the game addresses it). */
typedef struct {
    uint16_t type_map;      /* bits 0-3 directions (up, down, left, right), 4-6 type, 7 flat, 8-13 slope, 14-15 lid rotation */
    uint8_t ext;            /* bits 0-2 traffic lights / train, 3-4 lid remap, 5 flip top-bottom, 6 flip left-right, 7 railway */
    uint8_t left, right, top, bottom, lid;   /* face tiles (0 = no face); Map_GetFace index 0..4 */
} MapBlock;
_Static_assert(sizeof(MapBlock) == 8, "block layout");

typedef struct {
    uint8_t style, sample;                    /* header bytes 4, 5 */
    uint32_t route_size, object_size, column_size, block_size, nav_size;
    uint8_t *buf;                             /* 0x5c2c48: base | columns | blocks | 0x4000 change area */
    const uint32_t *base;                     /* 0x5c1c1c: byte offset of column (x, y) at [y * 256 + x] */
    uint8_t *columns;                         /* 0x5c2c70 */
    MapBlock *blocks;                         /* 0x5c0bfc */
    uint32_t nblocks;
    uint8_t *data_end;                        /* 0x5bfbd8: end of the loaded data; change areas follow */
    int min_x, max_x, min_y, max_y;           /* 0x5c1c30, 0x5c1c28, 0x5c0bf8, 0x5bfbec: non-empty columns */
    /* 0x55fab0: [z][y][x] = type_map low 7 bits (directions, type), bit 7 = sloped */
    uint8_t type_cache[MAP_Z][MAP_H][MAP_W];
    /* Raw sections for the later ports (Obj_SetMapObjects 0x44ee20, Route_LoadCmp 0x471970, Area_SetNavData
       0x44b590): */
    uint8_t objects[MAP_OBJECTS_MAX];         /* 0x5c3078, 14 bytes each */
    uint8_t routes[MAP_ROUTES_MAX];           /* 0x5ee750: routes, then the 0x6c bytes of service locations */
    uint8_t nav[MAP_NAV_MAX];                 /* 0x5cde50, 35 bytes each */
} Map;

/* Map_Load 0x438200: loads rel ("GTADATA/NYC.CMP"). NULL on error (message in err). The style the map
   asks for (Style_Request) is m->style. */
Map *map_load(const char *rel, char *err, size_t errcap);
void map_free(Map *m);                        /* Map_Free 0x4381a0 */

/* The column of (x, y), no clamping (callers clamp, as the original does). */
static inline const int16_t *map_column(const Map *m, int x, int y)
{
    return (const int16_t *)(m->columns + m->base[y * MAP_W + x]);
}
/* Map_GetBlock 0x437ae0: block (x, y, z), x and y clamped to 0..255; NULL above the column top. */
MapBlock *map_get_block(const Map *m, int x, int y, int z);
/* Map_GetTypeMap 0x438900: type_map | ext << 16 of block (x, y, z) (no clamping), 0 if none. */
uint32_t map_get_type_map(const Map *m, int x, int y, int z);
/* Map_GetTypeAt 0x4388a0: the same for a 16.16 world position. */
uint32_t map_get_type_at(const Map *m, int32_t x, int32_t y, int32_t z);
/* Map_GetFace 0x438950: face tile 0 left, 1 right, 2 top, 3 bottom, 4 lid (-1: no block / bad face,
   where the original raises a fatal error). */
int map_get_face(const Map *m, int x, int y, int z, int face);
