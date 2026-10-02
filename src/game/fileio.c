#include "fileio.h"
#include "../vfs.h"
#include "game.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *file_data;                   /* the whole file (the original: FILE *0x511954) */
static size_t file_size, file_pos;
static bool file_open;                       /* 0x511958 */

const char *textfile_rel(const char *path)
{
    if (path[0] == '.' && path[1] == '.' && (path[2] == '\\' || path[2] == '/')) return path + 3;
    return path;
}

static int getc_(void) { return file_pos < file_size ? file_data[file_pos++] : -1; }
static void ungetc_(void) { if (file_pos > 0) file_pos--; }
static bool is_ws(int c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }

/* TextFile_Close 0x42e2e0 */
void textfile_close(void)
{
    if (file_open) {
        free(file_data);
        file_data = NULL;
        file_size = file_pos = 0;
        file_open = false;
    }
}

/* TextFile_Open 0x42e270: fatal -1 (open) if missing; the name is what Error_SetFileName records. */
void textfile_open(const char *path)
{
    textfile_close();
    game_set_error_file(path);
    file_data = vfs_read_all(textfile_rel(path), &file_size);
    if (!file_data) game_fatal(-1, 0x15, 0);
    file_pos = 0;
    file_open = true;
}

/* TextFile_Seek 0x42e320 */
void textfile_seek(long delta)
{
    if (!file_open) game_fatal(-0x16, 0x19, 0);
    long p = (long)file_pos + delta;
    if (p < 0) {
        textfile_close();
        game_fatal(-0x10, 0x19, 0);
    }
    file_pos = (size_t)p;   /* fseek may go past the end; reads then fail */
}

/* TextFile_Read 0x42e390 */
void textfile_read(void *dst, size_t n)
{
    if (!file_open) game_fatal(-0x16, 0x17, 0);
    if (file_pos > file_size || file_size - file_pos < n) {
        textfile_close();
        game_fatal(-2, 0x17, 0);
    }
    memcpy(dst, file_data + file_pos, n);
    file_pos += n;
}

/* TextFile_SkipTo 0x42e400: reads until the characters of s have come in a row. After a mismatch the
   match restarts with the next character (the mismatching one is not tried against s[0]). End of file
   is fatal (-0x57, "string not found"). */
void textfile_skip_to(const char *s)
{
    for (;;) {
        int i = 0;
        for (;;) {
            int c = getc_();
            if (c == -1) game_fatal(-0x57, 0x62, 0);
            if (c != (unsigned char)s[i]) break;
            if (s[++i] == 0) return;
        }
    }
}

/* TextFile_ReadInt 0x42e440: an unsigned decimal ended by ',' or end of file; '[' ends it too and is
   put back. Whitespace may come before and after the digits, anything else is fatal (-0x59). */
int textfile_read_int(void)
{
    int v = 0, state = 0;   /* 0 before the digits, 1 digits, 2 after */
    for (;;) {
        int c = getc_();
        if (c == ',' || c == -1) return v;
        if (c == '[') {
            ungetc_();
            return v;
        }
        if (state == 0) {
            if (is_ws(c)) continue;
            state = 1;
        } else if (state == 2) {
            if (!is_ws(c)) game_fatal(-0x59, 100, 0);
            continue;
        }
        if (c >= '0' && c <= '9') v = c - '0' + v * 10;
        else if (is_ws(c)) state = 2;
        else game_fatal(-0x59, 100, 0);
    }
}

/* TextFile_ReadString 0x42e540: a token up to delim (or end of file), leading whitespace skipped,
   trailing whitespace cut (inner whitespace kept). Non-blank characters past max are fatal (-0x5a),
   blanks past it are dropped. The terminator goes after the last non-blank character, which can be
   dst[max]: callers give max + 1 bytes. */
void textfile_read_string(char *dst, int max, char delim)
{
    int last = -1, n = 0;
    bool started = false;
    for (;;) {
        int c = getc_();
        if (c == (unsigned char)delim || c == -1) {
            dst[last + 1] = 0;
            return;
        }
        if (!started) {
            if (is_ws(c)) continue;
            started = true;
        }
        if (!is_ws(c)) {
            last = n;
            if (n >= max) game_fatal(-0x5a, 0x65, 0);
        } else if (n >= max) {
            continue;
        }
        dst[n++] = (char)c;
    }
}
