#include "map.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The core assumes a little-endian host (wasm, x86, arm64): the map buffer is used in place, as the
   original does. */

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static Map *fail(Map *m, uint8_t *file, char *err, size_t cap, const char *rel, const char *msg)
{
    if (err) snprintf(err, cap, "%s: %s", rel, msg);
    free(file);
    map_free(m);
    return NULL;
}

/* Map_Load 0x438200 (minus the parts that belong to other modules: Obj_SetMapObjects, Route_LoadCmp,
   Area_SetNavData, Style_Request, Area_LocalizeNames and the per-player camera resets; their inputs are
   kept in the Map). The original reads straight into its buffers through TextFile_Read; the port reads
   the file and copies. Fatal errors of the original become a NULL return. */
Map *map_load(const char *rel, char *err, size_t errcap)
{
    size_t size;
    uint8_t *d = vfs_read_all(rel, &size);
    if (!d) return fail(NULL, NULL, err, errcap, rel, "can't read");
    Map *m = calloc(1, sizeof *m);
    if (!m) return fail(NULL, d, err, errcap, rel, "out of memory");
    /* 28-byte header: version, style, sample, pad, route, object, column, block, nav sizes */
    if (size < 28 || rd32(d) != MAP_VERSION) return fail(m, d, err, errcap, rel, "wrong version");
    m->style = d[4];
    m->sample = d[5];
    m->route_size = rd32(d + 8);
    m->object_size = rd32(d + 12);
    m->column_size = rd32(d + 16);
    m->block_size = rd32(d + 20);
    m->nav_size = rd32(d + 24);
    if (m->object_size > MAP_OBJECTS_MAX) return fail(m, d, err, errcap, rel, "too many objects");
    if (m->route_size + MAP_LOCATIONS_SIZE > MAP_ROUTES_MAX) return fail(m, d, err, errcap, rel, "route data too big");
    if (m->nav_size > MAP_NAV_MAX) return fail(m, d, err, errcap, rel, "too much nav data");
    /* one allocation: base (0x40000) + columns + blocks, then 0x4000 bytes for the two change areas */
    size_t main = 0x40000 + (size_t)m->column_size + m->block_size;
    size_t need = 28 + main + m->object_size + m->route_size + MAP_LOCATIONS_SIZE + m->nav_size;
    if (size < need) return fail(m, d, err, errcap, rel, "truncated");
    if (!(m->buf = calloc(1, main + 0x4000))) return fail(m, d, err, errcap, rel, "out of memory");
    size_t o = 28;
    memcpy(m->buf, d + o, main);
    o += main;
    memcpy(m->objects, d + o, m->object_size);
    o += m->object_size;
    memcpy(m->routes, d + o, m->route_size + MAP_LOCATIONS_SIZE);
    o += m->route_size + MAP_LOCATIONS_SIZE;
    memcpy(m->nav, d + o, m->nav_size);
    free(d);
    m->base = (const uint32_t *)m->buf;
    m->columns = m->buf + 0x40000;
    m->blocks = (MapBlock *)(m->columns + m->column_size);
    m->nblocks = m->block_size / 8;
    m->data_end = (uint8_t *)m->blocks + m->block_size;

    /* The original trusts the file; the port checks every column so that a bad file can't make it read
       outside the buffer. Valid data passes unchanged. */
    for (int i = 0; i < MAP_W * MAP_H; i++) {
        uint32_t b = m->base[i];
        if (b % 2 || (size_t)b + 2 > m->column_size) return fail(m, NULL, err, errcap, rel, "bad column offset");
        const int16_t *c = (const int16_t *)(m->columns + b);
        if (c[0] < MAP_Z) {
            if (c[0] < 0 || (size_t)b + 2 * (1 + MAP_Z - c[0]) > m->column_size)
                return fail(m, NULL, err, errcap, rel, "bad column");
            for (int k = 1; k <= MAP_Z - c[0]; k++)
                if (c[k] < 0 || (uint32_t)c[k] >= m->nblocks) return fail(m, NULL, err, errcap, rel, "bad block index");
        }
    }

    /* Extents: the bounding box of the columns with something in them (h < 6). The original narrows the
       search as it goes: per row, x from 0 up to the current minimum, x from 255 down to the current
       maximum; then per column in [min x, max x], the same for y. */
    m->min_x = 0x100, m->max_x = -1, m->min_y = 0x100, m->max_y = -1;
    for (int y = 0; y < MAP_H; y++) {
        for (int x = 0; x < m->min_x; x++)
            if (map_column(m, x, y)[0] < 6) { m->min_x = x; break; }
        for (int x = 0xff; x > m->max_x; x--)
            if (map_column(m, x, y)[0] < 6) { m->max_x = x; break; }
    }
    if (m->max_x < m->min_x) return fail(m, NULL, err, errcap, rel, "empty map");
    for (int x = m->min_x; x <= m->max_x; x++) {
        for (int y = 0; y < m->min_y; y++)
            if (map_column(m, x, y)[0] < 6) { m->min_y = y; break; }
        for (int y = 0xff; y > m->max_y; y--)
            if (map_column(m, x, y)[0] < 6) { m->max_y = y; break; }
    }

    /* The type cache 0x55fab0, from Map_GetTypeMap of every cell. */
    for (int x = 0; x < MAP_W; x++)
        for (int y = 0; y < MAP_H; y++)
            for (int z = 0; z < MAP_Z; z++) {
                uint32_t t = map_get_type_map(m, x, y, z);
                m->type_cache[z][y][x] = (uint8_t)((t & 0x7f) | (t & 0x3f00 ? 0x80 : 0));
            }
    return m;
}

void map_free(Map *m)
{
    if (!m) return;
    free(m->buf);
    free(m);
}

MapBlock *map_get_block(const Map *m, int x, int y, int z)
{
    if (x < 0) x = 0;
    else if (x > 0xff) x = 0xff;
    if (y < 0) y = 0;
    else if (y > 0xff) y = 0xff;
    const int16_t *c = map_column(m, x, y);
    if (c[0] <= z) return &m->blocks[c[z - c[0] + 1]];
    return NULL;
}

uint32_t map_get_type_map(const Map *m, int x, int y, int z)
{
    const int16_t *c = map_column(m, x, y);
    if (c[0] <= z) {
        const MapBlock *b = &m->blocks[c[z - c[0] + 1]];
        return (uint32_t)b->ext << 16 | b->type_map;
    }
    return 0;
}

uint32_t map_get_type_at(const Map *m, int32_t x, int32_t y, int32_t z)
{
    /* the original indexes base[((y >> 14) & ~0xff) + (x >> 22)] unchecked */
    int32_t i = (int32_t)((uint32_t)(y >> 14) & 0xffffff00u) + (x >> 22);
    if (i < 0 || i >= MAP_W * MAP_H) return 0;   /* TODO(0x4388a0): the original reads outside the table */
    const int16_t *c = (const int16_t *)(m->columns + m->base[i]);
    if (c[0] <= z >> 22) {
        const MapBlock *b = &m->blocks[c[(z >> 22) - c[0] + 1]];
        return (uint32_t)b->ext << 16 | b->type_map;
    }
    return 0;
}

int map_get_face(const Map *m, int x, int y, int z, int face)
{
    const int16_t *c = map_column(m, x, y);
    if (c[0] > z) return -1;
    const MapBlock *b = &m->blocks[c[z - c[0] + 1]];
    switch (face) {
    case 0: return b->left;
    case 1: return b->right;
    case 2: return b->top;
    case 3: return b->bottom;
    case 4: return b->lid;
    default: return -1;
    }
}
