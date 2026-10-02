/* A tiny PNG writer for the tests: 8-bit RGB, stored (uncompressed) deflate blocks, no dependencies.

     png_write(path, px, w, h, PNG_XRGB)   pixels 0x00RRGGBB (the 32 bpp render surface / style CLUTs)
     png_write(path, px, w, h, PNG_ABGR)   pixels 0xAABBGGRR (surface.h's display format)

   With PNG_XRGB a pixel of 0xffffffff (never a converted CLUT word: byte 3 is clear) is written as
   magenta, so tests can pre-fill the frame with it to show what was never drawn. Returns false if the
   file can't be written. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PNG_XRGB, PNG_ABGR };

static uint32_t png_crc_(uint32_t crc, const uint8_t *p, size_t n)
{
    static uint32_t table[256];
    if (!table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    crc = ~crc;
    while (n--) crc = table[(crc ^ *p++) & 0xff] ^ (crc >> 8);
    return ~crc;
}

static void png_put32_(FILE *f, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
    fwrite(b, 1, 4, f);
}

static void png_chunk_(FILE *f, const char *type, const uint8_t *data, size_t n)
{
    png_put32_(f, (uint32_t)n);
    fwrite(type, 1, 4, f);
    if (n) fwrite(data, 1, n, f);
    uint32_t crc = png_crc_(0, (const uint8_t *)type, 4);
    png_put32_(f, n ? png_crc_(crc, data, n) : crc);
}

static inline bool png_write(const char *path, const uint32_t *px, int w, int h, int fmt)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ihdr[13] = { (uint8_t)(w >> 24), (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w,
                         (uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h, 8, 2, 0, 0, 0 };
    png_chunk_(f, "IHDR", ihdr, 13);
    size_t row = 1 + 3 * (size_t)w, raw_n = (size_t)h * row;
    uint8_t *raw = malloc(raw_n ? raw_n : 1);
    for (int y = 0; y < h; y++) {
        uint8_t *r = raw + (size_t)y * row;
        r[0] = 0;
        for (int x = 0; x < w; x++) {
            uint32_t c = px[(size_t)y * w + x], R, G, B;
            if (fmt == PNG_XRGB) {
                if (c == 0xffffffffu) c = 0xff00ff;
                R = c >> 16 & 0xff, G = c >> 8 & 0xff, B = c & 0xff;
            } else {
                R = c & 0xff, G = c >> 8 & 0xff, B = c >> 16 & 0xff;
            }
            r[1 + 3 * x] = (uint8_t)R, r[2 + 3 * x] = (uint8_t)G, r[3 + 3 * x] = (uint8_t)B;
        }
    }
    size_t nblk = (raw_n + 65534) / 65535, zn = 2 + raw_n + (nblk ? nblk : 1) * 5 + 4;
    uint8_t *z = malloc(zn), *o = z;
    *o++ = 0x78, *o++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_n; i++) a = (a + raw[i]) % 65521, b = (b + a) % 65521;
    size_t p = 0;
    do {
        size_t n = raw_n - p < 65535 ? raw_n - p : 65535;
        *o++ = p + n == raw_n;
        *o++ = (uint8_t)n, *o++ = (uint8_t)(n >> 8), *o++ = (uint8_t)~n, *o++ = (uint8_t)(~n >> 8);
        memcpy(o, raw + p, n), o += n, p += n;
    } while (p < raw_n);
    uint32_t ad = b << 16 | a;
    *o++ = (uint8_t)(ad >> 24), *o++ = (uint8_t)(ad >> 16), *o++ = (uint8_t)(ad >> 8), *o++ = (uint8_t)ad;
    png_chunk_(f, "IDAT", z, (size_t)(o - z));
    png_chunk_(f, "IEND", NULL, 0);
    bool ok = !ferror(f);
    fclose(f);
    free(raw);
    free(z);
    return ok;
}
