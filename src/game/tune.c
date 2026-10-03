/* The car tuning file (tune.h). The original parses in place in two adjacent static buffers, the key
   0x501d88 (0x50 bytes) and the file 0x501dd8 (0x1000), and some of its copies run from one into the
   other: the port keeps them as one block so that those copies do what they do there. */
#include "tune.h"
#include "../exe.h"
#include "../vfs.h"
#include "car.h"
#include "carinfo.h"
#include "fileio.h"
#include "game.h"
#include <stdio.h>
#include <string.h>

enum { KEY_SIZE = 0x50, FILE_SIZE = 0x1000 };
static char mem[KEY_SIZE + FILE_SIZE + 1];  /* 0x501d88: the key, then the file text 0x501dd8 */
#define KEY (mem)
#define TEXT (mem + KEY_SIZE)

static int is_ws(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }

/* a key between from and its end: from is the first character after the opening whitespace; the
   scan stops at ']' or the end of the text, last non-blank remembered. When the key ends right before
   the ']', the trimmed key is copied to KEY; otherwise (blanks before the ']', or none) everything up
   to the terminating 0 is (quirk: in the original that copies the rest of the file over the start of
   the file buffer, the parse going on in the shifted text). Returns the scan's stop (']' or 0). */
static char *copy_key(char *from, char *last)
{
    char *p = from;
    while (*p != 0 && *p != ']') {
        if (!is_ws(*p)) last = p;
        p++;
    }
    char *dst = KEY;
    if (last[1] == ']') {
        last[1] = 0;
        for (char *s = from; (*dst++ = *s++) != 0;) {}
        last[1] = ']';
    } else {
        for (char *s = from; (*dst++ = *s++) != 0;) {}
    }
    return p;
}

/* the parameters: the name's string in the exe, the record field, how it is stored */
enum { T_BYTE, T_INT, T_SHORT, T_FLOAT };
static const struct { uint32_t name; uint8_t off, type; } params[] = {
    { 0x4abfb4, 0x76, T_BYTE },  /* "centre of mass x" */
    { 0x4abfa0, 0x77, T_BYTE },  /* "centre of mass y" */
    { 0x4abf8c, 0x78, T_INT },   /* "moment of inertia" */
    { 0x4abf84, 0x7c, T_FLOAT }, /* "mass" */
    { 0x4abf7c, 0x80, T_FLOAT }, /* "gear 1" */
    { 0x4abf6c, 0x84, T_FLOAT }, /* "tyre adhesion x" */
    { 0x4abf5c, 0x88, T_FLOAT }, /* "tyre adhesion y" */
    { 0x4abf48, 0x8c, T_FLOAT }, /* "handbrake friction" */
    { 0x4abf34, 0x90, T_FLOAT }, /* "footbrake friction" */
    { 0x4abf20, 0x94, T_FLOAT }, /* "front brake bias" */
    { 0x4abf14, 0x98, T_SHORT }, /* "turn ratio" */
    { 0x4abf00, 0x9a, T_SHORT }, /* "drive wheel offset" */
    { 0x4abee8, 0x9c, T_SHORT }, /* "steering wheel offset" */
    { 0x4abed0, 0x9e, T_FLOAT }, /* "back end slide value" */
    { 0x4abeb8, 0xa2, T_FLOAT }, /* "handbrake slide value" */
};

/* Tune_SetCarParam 0x412e90: "car" (strncmp, 3 characters), blanks, an optional '-', decimal digits
   (a short) give the model; a model without a record (0x4be178 = -1) is ignored. After blanks the
   rest is the parameter name (copy_key again, over the key itself), compared exactly with the 15
   names; the byte, int and short fields take the integer, the floats the fixed-point value. Anything
   else is fatal -0x89 (line 0x9d) with the key. */
void tune_set_car_param(char *key, int value, float fvalue)
{
    if (strncmp(key, exe_str(0x4abfc8), 3) == 0) {   /* "car" */
        char *p = key + 3;
        while (*p != 0 && is_ws(*p)) p++;
        int16_t sign = 1, model = 0;
        if (*p == '-') p++, sign = -1;
        while (*p != 0 && *p > '/' && *p < ':') model = (int16_t)(*p++ - '0' + model * 10);
        model = (int16_t)(sign * model);
        /* (a model outside the table reads outside it in the original; the port takes it as none) */
        int rec = model >= 0 && model < CAR_MODELS ? g_car_model_index[model] : -1;
        if (rec == -1) return;
        while (*p != 0 && is_ws(*p)) p++;
        copy_key(p, p - 1);
        uint8_t *r = (uint8_t *)(uintptr_t)car_info_record(rec);
        for (size_t i = 0; r && i < sizeof params / sizeof *params; i++) {
            if (strcmp(KEY, exe_str(params[i].name)) != 0) continue;
            uint8_t *f = r + params[i].off;
            switch (params[i].type) {
            case T_BYTE: f[0] = (uint8_t)value; break;
            case T_SHORT: f[0] = (uint8_t)value, f[1] = (uint8_t)(value >> 8); break;
            case T_INT: memcpy(f, &value, 4); break;
            case T_FLOAT: memcpy(f, &fvalue, 4); break;
            }
            return;
        }
        key = KEY;
    }
    game_set_error_file(key);
    game_fatal(-0x89, 0x9d, 0);
}

/* Tune_LoadFile 0x412d20: the file (File_LoadOptional 0x42de30 into the 0x1000-byte buffer: missing
   is an empty text, a read error fatal at line 0x9a, too long fatal 0x9b) is scanned for '['; after
   blanks the key runs to ']' (copy_key), after blanks the value is an optional '-' and decimal digits
   (an int), handed to Tune_SetCarParam with value / 65536 (the float 0x4a7300) as the float. The scan
   goes on after the digits, up to the end of the text. A key that hit the end of the text has the
   value read past it, in what the buffer held before (a longer file loaded earlier, else zeros). */
void tune_load_file(const char *name)
{
    char path[96];
    snprintf(path, sizeof path, exe_str(0x4abea8), name);   /* "..\gtadata\%s" */
    game_set_error_file(path);
    VfsFile *f = vfs_open(textfile_rel(path));
    if (!f) {
        TEXT[0] = 0;
    } else {
        uint64_t size = vfs_file_size(f);
        if (size + 1 > FILE_SIZE) game_fatal(0x9b, -0x17, (int)(size - FILE_SIZE + 1));
        int64_t got = vfs_read_at(f, 0, TEXT, (size_t)size);
        vfs_close(f);
        if (got != (int64_t)size) game_fatal(-2, 0x9a, 0);
        TEXT[size] = 0;
    }
    char *p = TEXT;
    for (;;) {
        while (*p != '[') {
            if (*p == 0) return;
            p++;
        }
        while (p[1] != 0 && is_ws(p[1])) p++;
        char *k = copy_key(p + 1, p);
        p = k + 1;
        while (*p != 0 && is_ws(*p)) p++;
        int sign = 1, v = 0;
        if (*p == '-') p++, sign = -1;
        while (*p != 0 && *p > '/' && *p < ':') v = *p++ - '0' + v * 10;
        tune_set_car_param(KEY, sign * v, (float)(sign * v) * (1.0f / 65536.0f));
    }
}
