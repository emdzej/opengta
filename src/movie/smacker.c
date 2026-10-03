/* Smacker decoder (smacker.h). Clean-room: written from the format description on MultimediaWiki's
   "Smacker" page (container layout, header fields, packed Huffman trees with the low / high byte trees
   and the three "recently used" escape values, the palette, audio and video chunks, block types) and
   from looking at GTA's MOVIE.SMK; no other decoder's code was used. docs/movie.md has the format as
   implemented here.

   Only what a Smacker 2 file like MOVIE.SMK needs is required; the Smacker 4 full-block sub-types are
   decoded too, since they are a few lines. Bink audio tracks are reported and skipped. */
#include "movie/smacker.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { HEADER_SIZE = 0x68 };

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* ---- bit reader: bits are taken from the lowest bit of each byte first ---- */

typedef struct {
    const uint8_t *p;
    size_t nbits, pos;
    bool over;               /* read past the end (zeros were returned) */
} Bits;

static void bits_init(Bits *b, const uint8_t *p, size_t len) { *b = (Bits){ p, len * 8, 0, false }; }

static unsigned bit1(Bits *b)
{
    if (b->pos >= b->nbits) { b->over = true; return 0; }
    unsigned v = b->p[b->pos >> 3] >> (b->pos & 7) & 1;
    b->pos++;
    return v;
}

static uint32_t bitsn(Bits *b, int n)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++) v |= (uint32_t)bit1(b) << i;
    return v;
}

/* ---- Huffman trees ----
   Nodes in pre-order: an inner node is followed by its '0' subtree and stores the index of its '1'
   child; a leaf has LEAF set and its value in the low 16 bits, or ESC + the slot of a recently-used
   value (big trees). */

#define LEAF 0x80000000u
#define ESC 0x40000000u
enum { MAX_DEPTH = 64 };

static bool tree_push(SmkTree *t, uint32_t v)
{
    if (t->n == t->cap) {
        int cap = t->cap ? t->cap * 2 : 64;
        uint32_t *n = realloc(t->node, (size_t)cap * sizeof *n);
        if (!n) return false;
        t->node = n;
        t->cap = cap;
    }
    t->node[t->n++] = v;
    return true;
}

static void tree_free(SmkTree *t)
{
    free(t->node);
    *t = (SmkTree){0};
}

/* A leaf value read from a tree (0 when the tree is absent: nothing is read). */
static uint32_t tree_get(const SmkTree *t, Bits *b)
{
    if (!t->present || !t->n) return 0;
    int i = 0;
    while (!(t->node[i] & LEAF)) {
        i = bit1(b) ? (int)t->node[i] : i + 1;
        if (i >= t->n || b->over) return 0;
    }
    return t->node[i];
}

/* The 8-bit trees: the structure bits with 8-bit leaves. */
static bool small_build(Bits *b, SmkTree *t, int depth)
{
    if (depth > MAX_DEPTH || b->over) return false;
    if (bit1(b)) {
        int i = t->n;
        if (!tree_push(t, 0) || !small_build(b, t, depth + 1)) return false;
        t->node[i] = (uint32_t)t->n;
        return small_build(b, t, depth + 1);
    }
    return tree_push(t, LEAF | bitsn(b, 8));
}

/* A packed 8-bit tree: the presence bit, the tree, and a closing 0 bit. */
static bool small_read(Bits *b, SmkTree *t)
{
    *t = (SmkTree){0};
    if (!bit1(b)) return true;
    t->present = true;
    if (!small_build(b, t, 0)) return false;
    bit1(b);
    return !b->over;
}

typedef struct {
    SmkTree lo, hi;
    uint32_t esc[3];
} BigCtx;

static bool big_build(Bits *b, SmkTree *t, BigCtx *c, int depth)
{
    if (depth > MAX_DEPTH || b->over) return false;
    if (bit1(b)) {
        int i = t->n;
        if (!tree_push(t, 0) || !big_build(b, t, c, depth + 1)) return false;
        t->node[i] = (uint32_t)t->n;
        return big_build(b, t, c, depth + 1);
    }
    uint32_t v = (tree_get(&c->lo, b) & 0xff) | (tree_get(&c->hi, b) & 0xff) << 8;
    for (uint32_t k = 0; k < 3; k++)
        if (v == c->esc[k]) return tree_push(t, LEAF | ESC | k);
    return tree_push(t, LEAF | v);
}

/* A 16-bit tree: presence bit; the low-byte and high-byte trees; the three escape values (16 bits
   each); the tree, whose leaves are a low and a high byte decoded with the two byte trees; a 0 bit. */
static bool big_read(Bits *b, SmkTree *t)
{
    *t = (SmkTree){0};
    if (!bit1(b)) return true;
    t->present = true;
    BigCtx c;
    bool ok = small_read(b, &c.lo) && small_read(b, &c.hi);
    if (ok) {
        for (int k = 0; k < 3; k++) c.esc[k] = bitsn(b, 16);
        ok = big_build(b, t, &c, 0);
        bit1(b);
    }
    tree_free(&c.lo);
    tree_free(&c.hi);
    return ok && !b->over;
}

/* A value from a 16-bit tree. An escape leaf stands for one of the three values used last; a value
   that differs from the latest pushes the other two down. */
static uint32_t big_get(SmkTree *t, Bits *b)
{
    if (!t->present) return 0;
    uint32_t n = tree_get(t, b), v;
    if (n & ESC) v = t->last[n & 3];
    else v = n & 0xffff;
    if (t->last[0] != v) {
        t->last[2] = t->last[1];
        t->last[1] = t->last[0];
        t->last[0] = v;
    }
    return v;
}

/* ---- open / close ---- */

static Smacker *fail(Smacker *s, char *err, size_t errlen, const char *msg)
{
    if (err && errlen) snprintf(err, errlen, "smacker: %s", msg);
    smk_close(s);
    return NULL;
}

static Smacker *smk_parse(uint8_t *file, size_t size, char *err, size_t errlen)
{
    Smacker *s = calloc(1, sizeof *s);
    if (!s) { free(file); return fail(NULL, err, errlen, "out of memory"); }
    s->file = file;
    s->size = size;
    if (size < HEADER_SIZE || memcmp(file, "SMK", 3) || (file[3] != '2' && file[3] != '4'))
        return fail(s, err, errlen, "not a Smacker file");
    s->version = file[3] - '0';
    s->w = rd32(file + 4);
    s->h = rd32(file + 8);
    s->frames = rd32(file + 0xc);
    s->frame_rate = (int32_t)rd32(file + 0x10);
    s->flags = rd32(file + 0x14);
    /* frame time: > 0 milliseconds, < 0 hundredths of a millisecond, 0 = 10 fps */
    s->frame_us = s->frame_rate > 0 ? (uint32_t)s->frame_rate * 1000u
                : s->frame_rate < 0 ? (uint32_t)-(int64_t)s->frame_rate * 10u : 100000u;
    for (int t = 0; t < SMK_TRACKS; t++) {
        SmkTrack *tr = &s->track[t];
        uint32_t r = rd32(file + 0x48 + 4 * t);
        tr->largest = rd32(file + 0x18 + 4 * t);
        tr->compressed = r >> 31 & 1;
        tr->present = r >> 30 & 1;
        tr->bits16 = r >> 29 & 1;
        tr->stereo = r >> 28 & 1;
        tr->codec = r >> 26 & 3;
        tr->rate = r & 0xffffff;
    }
    s->trees_size = rd32(file + 0x34);
    if (!s->w || !s->h || s->w > 4096 || s->h > 4096 || !s->frames || s->frames > 1000000)
        return fail(s, err, errlen, "bad header");
    s->nphys = s->frames + (s->flags & 1);
    size_t tables = HEADER_SIZE + (size_t)s->nphys * 5;
    if (tables + s->trees_size > size) return fail(s, err, errlen, "truncated tables");
    s->frame_size = calloc(s->nphys, sizeof *s->frame_size);
    s->frame_type = calloc(s->nphys, 1);
    s->keyframe = calloc(s->nphys, sizeof *s->keyframe);
    s->frame_off = calloc(s->nphys, sizeof *s->frame_off);
    s->stride = (s->w + 3) & ~3u;
    s->vh = (s->h + 3) & ~3u;
    s->video = calloc((size_t)s->stride * s->vh, 1);
    if (!s->frame_size || !s->frame_type || !s->keyframe || !s->frame_off || !s->video)
        return fail(s, err, errlen, "out of memory");
    size_t off = tables + s->trees_size;
    for (uint32_t i = 0; i < s->nphys; i++) {
        uint32_t v = rd32(file + HEADER_SIZE + 4 * i);
        s->keyframe[i] = v & 1;
        s->frame_size[i] = v & ~3u;
        s->frame_type[i] = file[HEADER_SIZE + 4 * s->nphys + i];
        s->frame_off[i] = off;
        off += s->frame_size[i];
        if (off > size) return fail(s, err, errlen, "frame past the end of the file");
    }
    /* the four trees, in this order */
    Bits b;
    bits_init(&b, file + tables, s->trees_size);
    if (!big_read(&b, &s->mmap) || !big_read(&b, &s->mclr) || !big_read(&b, &s->full) || !big_read(&b, &s->type))
        return fail(s, err, errlen, "bad Huffman trees");
    s->trees_used = (uint32_t)((b.pos + 7) / 8);
    for (int t = 0; t < SMK_TRACKS; t++) {
        SmkTrack *tr = &s->track[t];
        if (!tr->present) continue;
        tr->cap = tr->largest ? tr->largest : 0x10000;
        if (!(tr->buf = malloc(tr->cap))) return fail(s, err, errlen, "out of memory");
    }
    return s;
}

Smacker *smk_open_mem(const uint8_t *data, size_t size, char *err, size_t errlen)
{
    uint8_t *copy = malloc(size ? size : 1);
    if (!copy) return fail(NULL, err, errlen, "out of memory");
    memcpy(copy, data, size);
    return smk_parse(copy, size, err, errlen);
}

Smacker *smk_open(const char *rel, char *err, size_t errlen)
{
    size_t size;
    uint8_t *file = vfs_read_all(rel, &size);
    if (!file) {
        if (err && errlen) snprintf(err, errlen, "smacker: can't read %s", rel);
        return NULL;
    }
    return smk_parse(file, size, err, errlen);
}

void smk_close(Smacker *s)
{
    if (!s) return;
    tree_free(&s->mmap);
    tree_free(&s->mclr);
    tree_free(&s->full);
    tree_free(&s->type);
    for (int t = 0; t < SMK_TRACKS; t++) free(s->track[t].buf);
    free(s->frame_size);
    free(s->frame_type);
    free(s->keyframe);
    free(s->frame_off);
    free(s->video);
    free(s->file);
    free(s);
}

void smk_rewind(Smacker *s)
{
    s->cur = 0;
    memset(s->pal, 0, sizeof s->pal);
    memset(s->video, 0, (size_t)s->stride * s->vh);
    s->new_palette = false;
}

/* ---- palette chunk ---- */

/* 6-bit component -> 8 bits, rounded (0 -> 0, 63 -> 255). */
static uint8_t pal6(unsigned v) { return (uint8_t)(((v & 0x3f) * 255 + 31) / 63); }

/* The chunk's first byte is its length / 4 (the byte included). Blocks fill the new palette in order:
   0x80 | n: n + 1 entries kept from the old palette at the same place; 0x40 | n, src: n + 1 entries
   copied from the old palette starting at src; otherwise three 6-bit components (red, green, blue). */
static bool do_palette(Smacker *s, const uint8_t *p, size_t len, size_t *used)
{
    if (!len) return false;
    size_t n = (size_t)p[0] * 4;
    if (!n || n > len) return false;
    uint8_t old[768];
    memcpy(old, s->pal, sizeof old);
    size_t i = 1;
    unsigned e = 0;
    while (e < 256 && i < n) {
        uint8_t c = p[i++];
        if (c & 0x80) {
            unsigned cnt = (c & 0x7f) + 1u;
            e += cnt;                      /* the entries stay (s->pal still holds them) */
        } else if (c & 0x40) {
            if (i >= n) break;
            unsigned cnt = (c & 0x3f) + 1u, src = p[i++];
            for (unsigned k = 0; k < cnt && e < 256 && src + k < 256; k++, e++)
                memcpy(s->pal + 3 * e, old + 3 * (src + k), 3);
        } else {
            if (i + 2 > n) break;
            s->pal[3 * e] = pal6(c);
            s->pal[3 * e + 1] = pal6(p[i++]);
            s->pal[3 * e + 2] = pal6(p[i++]);
            e++;
        }
    }
    *used = n;
    return true;
}

/* ---- audio chunk ---- */

static bool track_reserve(SmkTrack *tr, uint32_t len)
{
    if (len <= tr->cap) return true;
    uint8_t *n = realloc(tr->buf, len);
    if (!n) return false;
    tr->buf = n;
    tr->cap = len;
    return true;
}

/* Huffman DPCM: a presence bit, stereo bit, 16-bit bit; one byte tree per byte of a sample frame (left
   low, left high, right low, right high); the first sample frame raw (right channel first, high byte
   first); every later byte a delta from its tree, added to the previous sample of its channel. */
static bool audio_dpcm(SmkTrack *tr, const uint8_t *p, size_t len, uint32_t unpacked)
{
    Bits b;
    bits_init(&b, p, len);
    tr->len = 0;
    if (!bit1(&b)) return true;            /* no data: silence */
    bool stereo = bit1(&b), bits16 = bit1(&b);
    int ch = stereo ? 2 : 1, bps = bits16 ? 2 : 1, nt = ch * bps;
    SmkTree t[4];
    memset(t, 0, sizeof t);
    bool ok = true;
    for (int i = 0; i < nt && ok; i++) ok = small_read(&b, &t[i]);
    if (ok && track_reserve(tr, unpacked)) {
        int32_t pred[2] = {0, 0};
        for (int c = ch - 1; c >= 0; c--)
            pred[c] = bits16 ? (int32_t)(bitsn(&b, 8) << 8 | bitsn(&b, 8)) : (int32_t)bitsn(&b, 8);
        uint32_t frame = (uint32_t)(ch * bps), nframes = unpacked / frame;
        uint8_t *o = tr->buf;
        for (uint32_t f = 0; f < nframes; f++) {
            for (int c = 0; c < ch; c++) {
                if (f) {
                    if (bits16) {
                        uint32_t lo = tree_get(&t[c * 2], &b) & 0xff, hi = tree_get(&t[c * 2 + 1], &b) & 0xff;
                        pred[c] = (int16_t)(uint16_t)(pred[c] + (hi << 8 | lo));
                    } else
                        pred[c] = (uint8_t)(pred[c] + (tree_get(&t[c], &b) & 0xff));
                }
                if (bits16) {
                    *o++ = (uint8_t)pred[c];
                    *o++ = (uint8_t)(pred[c] >> 8);
                } else
                    *o++ = (uint8_t)pred[c];
            }
        }
        tr->len = nframes * frame;
    } else
        ok = false;
    for (int i = 0; i < 4; i++) tree_free(&t[i]);
    return ok;
}

static bool do_audio(SmkTrack *tr, const uint8_t *p, size_t len)
{
    if (!tr->present) { tr->len = 0; return true; }
    if (tr->compressed) {
        if (len < 4) return false;
        if (tr->codec) { tr->len = 0; return true; }   /* Bink audio: not decoded */
        return audio_dpcm(tr, p + 4, len - 4, rd32(p));
    }
    if (!track_reserve(tr, (uint32_t)len)) return false;
    memcpy(tr->buf, p, len);
    tr->len = (uint32_t)len;
    return true;
}

/* ---- video ---- */

/* The run length of a type word (bits 2-7): index 0-58 give 1-59, 59-63 give 128, 256, 512, 1024, 2048. */
static unsigned run_length(unsigned i) { return i < 59 ? i + 1 : 128u << (i - 59); }

static void do_video(Smacker *s, const uint8_t *p, size_t len)
{
    Bits b;
    bits_init(&b, p, len);
    s->mmap.last[0] = s->mmap.last[1] = s->mmap.last[2] = 0;
    s->mclr.last[0] = s->mclr.last[1] = s->mclr.last[2] = 0;
    s->full.last[0] = s->full.last[1] = s->full.last[2] = 0;
    s->type.last[0] = s->type.last[1] = s->type.last[2] = 0;
    uint32_t bw = s->stride / 4, nblocks = bw * (s->vh / 4), blk = 0;
    size_t st = s->stride;
    while (blk < nblocks) {
        uint32_t tw = big_get(&s->type, &b);
        unsigned kind = tw & 3, run = run_length(tw >> 2 & 0x3f);
        uint8_t extra = (uint8_t)(tw >> 8);
        int sub = 0;                       /* Smacker 4 full blocks: 0 plain, 1 double, 2 half */
        if (kind == 1 && s->version == 4) sub = bit1(&b) ? (bit1(&b) ? 2 : 1) : 0;
        for (; run && blk < nblocks; run--, blk++) {
            uint8_t *d = s->video + (size_t)(blk / bw) * 4 * st + (blk % bw) * 4;
            switch (kind) {
            case 0: {   /* two colours, a 16-bit map: set bits take the high byte */
                uint32_t clr = big_get(&s->mclr, &b), map = big_get(&s->mmap, &b);
                uint8_t c1 = (uint8_t)(clr >> 8), c0 = (uint8_t)clr;
                for (int y = 0; y < 4; y++, d += st)
                    for (int x = 0; x < 4; x++, map >>= 1) d[x] = map & 1 ? c1 : c0;
                break;
            }
            case 1:
                if (sub == 1) {   /* 2x2 cells: each value gives two cells side by side */
                    for (int y = 0; y < 4; y += 2) {
                        uint32_t v = big_get(&s->full, &b);
                        uint8_t a = (uint8_t)v, c = (uint8_t)(v >> 8);
                        for (int k = 0; k < 2; k++) {
                            uint8_t *r = d + (size_t)(y + k) * st;
                            r[0] = r[1] = a;
                            r[2] = r[3] = c;
                        }
                    }
                } else {          /* each row: pixels 2-3, then 0-1 (half: rows 0, 2 doubled) */
                    for (int y = 0; y < 4; y++) {
                        uint8_t *r = d + (size_t)y * st;
                        if (sub == 2 && (y & 1)) { memcpy(r, r - st, 4); continue; }
                        uint32_t v = big_get(&s->full, &b);
                        r[2] = (uint8_t)v;
                        r[3] = (uint8_t)(v >> 8);
                        v = big_get(&s->full, &b);
                        r[0] = (uint8_t)v;
                        r[1] = (uint8_t)(v >> 8);
                    }
                }
                break;
            case 2:       /* unchanged */
                break;
            case 3:       /* solid */
                for (int y = 0; y < 4; y++, d += st) memset(d, extra, 4);
                break;
            }
        }
    }
}

/* ---- frames ---- */

/* Frame i's chunks: the palette, the audio tracks, the video. With video false only the audio is
   decoded (the palette and the picture are left alone). */
static bool do_frame(Smacker *s, uint32_t i, bool video)
{
    const uint8_t *p = s->file + s->frame_off[i];
    size_t len = s->frame_size[i];
    uint8_t type = s->frame_type[i];
    if (video) s->new_palette = false;
    if (type & 1) {
        if (!len || (size_t)p[0] * 4 > len || !p[0]) return false;
        size_t used = (size_t)p[0] * 4;
        if (video && !do_palette(s, p, len, &used)) return false;
        p += used;
        len -= used;
        if (video) s->new_palette = true;
    }
    for (int t = 0; t < SMK_TRACKS; t++) {
        s->track[t].len = 0;
        if (!(type >> (t + 1) & 1)) continue;
        if (len < 4) return false;
        uint32_t n = rd32(p);
        if (n < 4 || n > len) return false;
        if (!do_audio(&s->track[t], p + 4, n - 4)) return false;
        p += n;
        len -= n;
    }
    if (video) do_video(s, p, len);
    return true;
}

bool smk_next_frame(Smacker *s)
{
    if (s->cur >= s->nphys) return false;
    return do_frame(s, s->cur++, true);
}

bool smk_frame_audio(Smacker *s, uint32_t frame)
{
    return frame < s->nphys && do_frame(s, frame, false);
}
