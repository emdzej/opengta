#include "map.h"
#include "exe.h"
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
    /* the change areas (set up at the end of Map_Load): blocks at data_end, columns 0x2000 bytes on */
    m->chg_block = (MapBlock *)m->data_end;
    m->chg_block_used = 0;
    m->chg_column = (int16_t *)(m->data_end + 0x2000);
    m->chg_column_used = 0;

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

/* ---- map edits (0x437b50-0x438020) ---- */

/* The column of (x, y) made writable: a column of the loaded data is copied (7 - h shorts: its height
   and the block indices) to the next free place of the column change area and the base table points at
   the copy. NULL on overflow. */
static int16_t *cow_column(Map *m, int x, int y)
{
    uint32_t *base = (uint32_t *)m->buf;
    int16_t *col = (int16_t *)(m->columns + base[y * MAP_W + x]);
    if ((uint8_t *)col >= m->data_end) return col;
    int n = 7 - col[0];
    m->chg_column_used += n * 2;
    if (m->chg_column_used > 0x2000) return NULL;   /* Error_Fatal -0x15, line 0x14 */
    int16_t *copy = m->chg_column;
    m->chg_column += n;
    memcpy(copy, col, (size_t)n * 2);
    return copy;
}

/* The block at layer z of column col made writable (a loaded block is copied to the block change
   area). NULL on overflow. */
static MapBlock *cow_block(Map *m, const int16_t *col, int z)
{
    MapBlock *b = &m->blocks[col[z - col[0] + 1]];
    if ((uint8_t *)b >= m->data_end) return b;
    if (m->chg_block_used + 8 > 0x2000) return NULL;   /* Error_Fatal -0x15, line 0x18 */
    m->chg_block_used += 8;
    MapBlock *copy = m->chg_block++;
    *copy = *b;
    return copy;
}

/* Both copies, then the column's entry and the base table updated (as each Map_Set* does). The source
   block is returned in *src (Map_SetBlockKind reads it after writing the copy). */
static MapBlock *cow(Map *m, int x, int y, int z, int16_t **colp, const MapBlock **src)
{
    const int16_t *old = map_column(m, x, y);
    if (src) *src = &m->blocks[old[z - old[0] + 1]];
    int16_t *col = cow_column(m, x, y);
    if (!col) return NULL;
    MapBlock *b = cow_block(m, col, z);
    if (!b) return NULL;
    *colp = col;
    return b;
}

static void cow_commit(Map *m, int x, int y, int z, int16_t *col, const MapBlock *b)
{
    col[z - col[0] + 1] = (int16_t)(b - m->blocks);
    ((uint32_t *)m->buf)[y * MAP_W + x] = (uint32_t)((uint8_t *)col - m->columns);
}

/* the type cache entry of (x, y, z) from type_map t */
static void cache_set(Map *m, int x, int y, int z, uint16_t t)
{
    m->type_cache[z][y][x] = (uint8_t)((t & 0x7f) | (t & 0x3f00 ? 0x80 : 0));
}

/* the type_map of block (x, y, z) through the (updated) base table, 0 above the column */
static uint16_t type_now(const Map *m, int x, int y, int z)
{
    const int16_t *c = map_column(m, x, y);
    return z < c[0] ? 0 : m->blocks[c[z - c[0] + 1]].type_map;
}

/* Map_SetBlockType 0x437b50 */
bool map_edit_block_type(Map *m, int x, int y, int z, uint32_t info)
{
    int16_t *col;
    MapBlock *b = cow(m, x, y, z, &col, NULL);
    if (!b) return false;
    b->type_map = (uint16_t)info;
    b->ext = (uint8_t)(info >> 16);
    cow_commit(m, x, y, z, col, b);
    cache_set(m, x, y, z, (uint16_t)info);
    return true;
}

/* Map_SetBlockKind 0x437cb0 */
bool map_edit_block_kind(Map *m, int x, int y, int z, int kind)
{
    int16_t *col;
    const MapBlock *src;
    MapBlock *b = cow(m, x, y, z, &col, &src);
    if (!b) return false;
    b->type_map &= 0xff8f;
    b->type_map = (uint16_t)((kind & 0x70) | src->type_map);   /* src is b itself after an earlier edit */
    b->ext = src->ext;
    cow_commit(m, x, y, z, col, b);
    cache_set(m, x, y, z, type_now(m, x, y, z));
    return true;
}

/* Map_OrBlockFlags 0x437e70 */
bool map_edit_or_flags(Map *m, int x, int y, int z, uint32_t flags)
{
    int16_t *col;
    const MapBlock *src;
    MapBlock *b = cow(m, x, y, z, &col, &src);
    if (!b) return false;
    b->type_map = (uint16_t)(src->type_map | flags);
    b->ext = (uint8_t)(src->ext | flags >> 16);
    cow_commit(m, x, y, z, col, b);
    cache_set(m, x, y, z, type_now(m, x, y, z));
    return true;
}

/* Map_SetBlockFace 0x438020 */
bool map_edit_block_face(Map *m, int x, int y, int z, int face, int tile)
{
    int16_t *col;
    MapBlock *b = cow(m, x, y, z, &col, NULL);
    if (!b) return false;
    switch (face) {
    case 0: b->left = (uint8_t)tile; break;
    case 1: b->right = (uint8_t)tile; break;
    case 2: b->top = (uint8_t)tile; break;
    case 3: b->bottom = (uint8_t)tile; break;
    case 4: b->lid = (uint8_t)tile; break;
    }
    cow_commit(m, x, y, z, col, b);
    return true;
}

/* Map_IsFaceSolid 0x438650 */
bool map_is_face_solid(const Map *m, int x, int y, int z, int face)
{
    const int16_t *c = map_column(m, x, y);
    if (z < c[0]) return false;
    const MapBlock *b = &m->blocks[c[z - c[0] + 1]];
    if (b->type_map & 0x80) {   /* flat */
        switch (face) {
        case 0: return b->left != 0;
        case 2: return b->top != 0;
        case 4: return b->lid != 0;
        default: return false;   /* 1 and 3; others fatal (-0x4a, 0x175) */
        }
    }
    if (b->type_map & 0x3f00) {
        /* the slope table 0x4b0c88: 3 bytes per slope type, the first the high side */
        const uint8_t *t = exe_data(0x4b0c88 + (uint32_t)(b->type_map >> 8 & 0x3f) * 3, 1);
        int high = t ? t[0] : 0;
        switch (face) {
        case 0: return b->left != 0 || high == 4;
        case 1: return b->right != 0 || high == 3;
        case 2: return b->top != 0 || high == 1;
        case 3: return b->bottom != 0 || high == 2;
        case 4: return b->lid != 0;
        default: return false;
        }
    }
    switch (face) {
    case 0: return b->left != 0;
    case 1: return b->right != 0;
    case 2: return b->top != 0;
    case 3: return b->bottom != 0;
    case 4: return b->lid != 0;
    default: return false;
    }
}

/* Map_IsCovered 0x438800: x and y are taken as shorts of the block (unchecked in the original; the port
   answers false outside the map). */
bool map_covered(const Map *m, int32_t x, int32_t y, int32_t z)
{
    int bx = (int16_t)(x >> 22), by = (int16_t)(y >> 22);
    if (bx < 0 || by < 0 || bx >= MAP_W || by >= MAP_H) return false;   /* (port guard) */
    const int16_t *c = map_column(m, bx, by);
    int l = (int16_t)(z >> 22);
    if (l >= c[0] && l < MAP_Z) {
        const MapBlock *b = &m->blocks[c[l - c[0] + 1]];
        if (b->lid && (b->type_map & 0x3f80) == 0) return true;
    }
    for (int k = (int16_t)((z >> 22) - 1); k >= 0; k--) {
        if (k < c[0] || k >= MAP_Z) continue;
        const MapBlock *b = &m->blocks[c[k - c[0] + 1]];
        if (b->lid && !(b->type_map & 0x80)) return true;
    }
    return false;
}
