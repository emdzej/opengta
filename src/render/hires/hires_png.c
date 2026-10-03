/* PNG decoding (see hires_png.h). */
#include "hires_png.h"
#include "../../inflate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static uint32_t *fail(char *err, size_t cap, const char *msg)
{
    if (err && cap) snprintf(err, cap, "%s", msg);
    return NULL;
}

static uint8_t paeth(int a, int b, int c)
{
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return (uint8_t)(pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
}

uint32_t *hires_png_decode(const uint8_t *d, size_t size, int *pw, int *ph, char *err, size_t cap)
{
    if (size < 8 || memcmp(d, "\x89PNG\r\n\x1a\n", 8)) return fail(err, cap, "not a PNG file");
    uint32_t w = 0, h = 0;
    int depth = 0, type = -1, interlace = 0;
    uint8_t pal[256][4];
    int npal = 0;
    memset(pal, 0xff, sizeof pal);
    uint8_t *idat = NULL;
    size_t nidat = 0;
    for (size_t p = 8; p + 12 <= size;) {
        uint32_t n = be32(d + p);
        const uint8_t *t = d + p + 4, *c = d + p + 8;
        if (n > size - p - 12) { free(idat); return fail(err, cap, "truncated PNG chunk"); }
        if (!memcmp(t, "IHDR", 4) && n >= 13) {
            w = be32(c), h = be32(c + 4), depth = c[8], type = c[9], interlace = c[12];
        } else if (!memcmp(t, "PLTE", 4)) {
            npal = (int)(n / 3 > 256 ? 256 : n / 3);
            for (int i = 0; i < npal; i++) pal[i][0] = c[3 * i], pal[i][1] = c[3 * i + 1], pal[i][2] = c[3 * i + 2];
        } else if (!memcmp(t, "tRNS", 4) && type == 3) {
            for (uint32_t i = 0; i < n && i < 256; i++) pal[i][3] = c[i];
        } else if (!memcmp(t, "IDAT", 4)) {
            uint8_t *q = realloc(idat, nidat + n + 1);
            if (!q) { free(idat); return fail(err, cap, "out of memory"); }
            idat = q;
            memcpy(idat + nidat, c, n);
            nidat += n;
        } else if (!memcmp(t, "IEND", 4)) {
            break;
        }
        p += 12 + (size_t)n;
    }
    const int ch = type == 0 ? 1 : type == 2 ? 3 : type == 3 ? 1 : type == 4 ? 2 : type == 6 ? 4 : 0;
    const char *why = NULL;
    if (!w || !h || w > 4096 || h > 4096) why = "PNG: no or too large an image (up to 4096 x 4096)";
    else if (!ch) why = "PNG: unknown colour type";
    else if (depth != 8) why = "PNG: only 8 bits per channel are supported (re-save as 8-bit RGBA, RGB or palette)";
    else if (interlace) why = "PNG: interlaced images are not supported (re-save without interlacing)";
    else if (type == 3 && !npal) why = "PNG: palette image without a palette";
    else if (!nidat) why = "PNG: no image data";
    if (why) { free(idat); return fail(err, cap, why); }
    const size_t stride = (size_t)w * ch, raw_n = (stride + 1) * h;
    uint8_t *raw = malloc(raw_n), *prev = calloc(stride, 1);
    uint32_t *out = malloc((size_t)w * h * 4);
    if (!raw || !prev || !out || zlib_inflate(idat, nidat, raw, raw_n) != (long)raw_n) {
        free(idat), free(raw), free(prev), free(out);
        return fail(err, cap, "PNG: corrupt or short image data");
    }
    free(idat);
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *r = raw + y * (stride + 1), f = r[0], *cur = r + 1;
        if (f > 4) { free(raw), free(prev), free(out); return fail(err, cap, "PNG: bad row filter"); }
        for (size_t i = 0; i < stride; i++) {
            int a = i >= (size_t)ch ? cur[i - ch] : 0, b = prev[i], c = i >= (size_t)ch ? prev[i - ch] : 0;
            switch (f) {
            case 1: cur[i] = (uint8_t)(cur[i] + a); break;
            case 2: cur[i] = (uint8_t)(cur[i] + b); break;
            case 3: cur[i] = (uint8_t)(cur[i] + ((a + b) >> 1)); break;
            case 4: cur[i] = (uint8_t)(cur[i] + paeth(a, b, c)); break;
            default: break;
            }
        }
        for (uint32_t x = 0; x < w; x++) {
            const uint8_t *s = cur + (size_t)x * ch;
            uint8_t R, G, B, A;
            switch (type) {
            case 0: R = G = B = s[0], A = 0xff; break;
            case 4: R = G = B = s[0], A = s[1]; break;
            case 2: R = s[0], G = s[1], B = s[2], A = 0xff; break;
            case 3: R = pal[s[0]][0], G = pal[s[0]][1], B = pal[s[0]][2], A = s[0] < npal ? pal[s[0]][3] : 0xff; break;
            default: R = s[0], G = s[1], B = s[2], A = s[3]; break;
            }
            out[(size_t)y * w + x] = (uint32_t)A << 24 | (uint32_t)B << 16 | (uint32_t)G << 8 | R;
        }
        memcpy(prev, cur, stride);
    }
    free(raw);
    free(prev);
    *pw = (int)w, *ph = (int)h;
    return out;
}
