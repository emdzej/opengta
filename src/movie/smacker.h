/* A Smacker (RAD Game Tools, "SMK2" / "SMK4") video decoder: what SMACKW32.DLL does for GTA's intro
   (GTADATA/MOVIE.SMK, Movie_PlayIntro 0x44b160). Clean-room, written from the public description of the
   format (MultimediaWiki's "Smacker" page); docs/movie.md describes it as implemented.

   The whole file is read into memory at open (MOVIE.SMK is 1.6 MB). Frames are decoded in order with
   smk_next_frame: the palette (8-bit RGB, 256 entries), the 8-bit indexed picture (w * h, persistent:
   skip blocks keep the previous frame's pixels) and each audio track's samples of that frame (signed
   16-bit or unsigned 8-bit PCM, interleaved left / right when stereo, as the track says). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { SMK_TRACKS = 7 };

/* A Huffman tree as unpacked from the file (flat array; node i's '0' child is i + 1). */
typedef struct {
    uint32_t *node;
    int n, cap;
    bool present;
    uint32_t last[3];        /* the three recently used values (the "big" 16-bit trees only) */
} SmkTree;

typedef struct {
    bool present;            /* AudioRate bit 30 */
    bool compressed;         /* bit 31: Huffman DPCM */
    bool bits16;             /* bit 29 */
    bool stereo;             /* bit 28 */
    uint8_t codec;           /* bits 26-27: 0 = DPCM (others are Bink audio: not supported) */
    uint32_t rate;           /* bits 0-23, Hz */
    uint32_t largest;        /* AudioSize: the largest unpacked chunk, bytes */
    /* the current frame's samples */
    uint8_t *buf;
    uint32_t len;            /* bytes */
    uint32_t cap;
} SmkTrack;

typedef struct {
    uint8_t *file;
    size_t size;
    int version;             /* 2 or 4 */
    uint32_t w, h;
    uint32_t frames;         /* logical frames */
    uint32_t nphys;          /* + the ring frame when flags bit 0 */
    int32_t frame_rate;      /* the header's field */
    uint32_t frame_us;       /* microseconds per frame */
    uint32_t flags;          /* 1 ring frame, 2 Y-interlaced, 4 Y-doubled */
    uint32_t trees_size;
    uint32_t trees_used;     /* bytes the four trees took (a check: equals trees_size) */
    uint32_t *frame_size;    /* bytes (low two bits cleared) */
    uint8_t *frame_type;     /* bit 0 palette, bits 1-7 audio tracks 0-6 */
    bool *keyframe;          /* bit 0 of the size */
    size_t *frame_off;
    SmkTrack track[SMK_TRACKS];
    SmkTree mmap, mclr, full, type;
    uint8_t pal[768];        /* RGB, 8 bits per component */
    bool new_palette;        /* the frame just decoded had a palette chunk */
    uint8_t *video;          /* w * h indices (h rounded up to a multiple of 4, as is w) */
    uint32_t stride, vh;     /* the padded buffer size */
    uint32_t cur;            /* the next frame to decode */
} Smacker;

/* Opens a file through the file layer (or a copy of `data`); NULL with a message in err on failure. */
Smacker *smk_open(const char *rel, char *err, size_t errlen);
Smacker *smk_open_mem(const uint8_t *data, size_t size, char *err, size_t errlen);
void smk_close(Smacker *s);
/* Decodes frame s->cur and advances it. False at the end or on a malformed frame. */
bool smk_next_frame(Smacker *s);
/* Decodes only the audio chunks of a frame into the tracks' buffers (the picture, the palette and
   s->cur are not touched): for a player that wants the whole soundtrack up front. */
bool smk_frame_audio(Smacker *s, uint32_t frame);
/* Back to frame 0 (the picture and palette are cleared). */
void smk_rewind(Smacker *s);
