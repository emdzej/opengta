/* Mission_ReadIni 0x44ab90 and the section tokeniser 0x44ace0-0x44b130. */
#include "../exe.h"
#include "fileio.h"
#include "game.h"
#include "mission.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Mission g_mission = { 0 };
static bool text_allocated;                  /* 0x6b3ea4 */

/* ---- exe tables ---- */

static const char *name_table(uint32_t va, int i, int n)
{
    if (i < 0 || i >= n) return NULL;
    uint32_t p = exe_u32(va + 4 * (uint32_t)i);
    return p ? exe_str(p) : NULL;
}
/* the tables end with a NULL pointer: 72 object types, 150 opcodes */
const char *mission_type_name(int t) { return name_table(0x4b0d98, t, 72); }
const char *mission_opcode_name(int op) { return name_table(0x4b0ec0, op, 150); }

/* The loader's lookup: the first entry equal to the word (the opcode table has XXXX twice). */
static int lookup(const char *(*get)(int), const char *word)
{
    for (int i = 0;; i++) {
        const char *s = get(i);
        if (!s) return -1;
        if (!strcmp(s, word)) return i;
    }
}
int mission_lookup_type(const char *word) { return lookup(mission_type_name, word); }
int mission_lookup_opcode(const char *word) { return lookup(mission_opcode_name, word); }

/* ---- Mission_SetIniSection 0x44ab50 / Mission_ReadIni 0x44ab90 / Mission_FreeIni 0x44acc0 ---- */

bool mission_set_ini_section(int n)
{
    if (n >= 0 && n <= MISSION_SECTION_MAX) {
        g_mission.section = n;
        return true;
    }
    return false;   /* Error_Fatal -0x58 */
}

static void copy_str(char *dst, size_t cap, const char *src)
{
    snprintf(dst, cap, "%s", src);
}

/* Section 0 is the built-in "default mission" on level001.cmp with no objects or commands; any other
   section is read from MISSION.INI: "[N]", then the name up to ',', a number, the CMP file name up to
   ',', the style number, and the rest of the section up to the next '[' (or the end of the file). The
   CMP name is requested with priority 1, the style with priority 2 (a style of 0 requests nothing: the
   map's header decides). */
void mission_read_ini(void)
{
    Mission *m = &g_mission;
    if (!text_allocated) {
        m->text = malloc(MISSION_TEXT_MAX + 1);   /* + 1: textfile_read_string may end at [max] */
        if (!m->text) game_fatal(-7, 0, MISSION_TEXT_MAX);
        text_allocated = true;
    }
    if (m->section == 0) {
        copy_str(m->name, sizeof m->name, exe_str(0x4b1db8));   /* "default mission" */
        m->number2 = 0;
        copy_str(m->cmp, sizeof m->cmp, exe_str(0x4b1da8));     /* "level001.cmp" */
        m->style = 0;
        m->text[0] = 0;
    } else {
        char tag[12];
        textfile_open(exe_str(0x4b1d90));                         /* "..\gtadata\mission.ini" */
        snprintf(tag, sizeof tag, exe_str(0x4b1d88), m->section); /* "[%d]" */
        textfile_skip_to(tag);
        textfile_read_string(m->name, 0x51, ',');
        m->number2 = textfile_read_int();
        textfile_read_string(m->cmp, 0xd, ',');
        m->style = textfile_read_int();
        textfile_read_string(m->text, MISSION_TEXT_MAX, '[');
        textfile_close();
    }
    map_set_name(m->cmp, 1);
    style_request(m->style, 2);
    m->p = m->text;
}

void mission_free_ini(void)
{
    if (text_allocated) {
        free(g_mission.text);
        g_mission.text = NULL;
        g_mission.p = NULL;
        text_allocated = false;
    }
}

/* ---- tokeniser ---- */

static bool ws(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }

/* The whitespace skip all readers share: a '{' right after a skipped blank starts a comment that runs
   to the next '}'. A '{' at the starting position is not a comment (the test comes after the step).
   The original runs past the end on an unterminated comment; the port stops at the terminator. */
static void skip_blanks(bool spaces_only)
{
    const char *p = g_mission.p;
    while (*p && (spaces_only ? *p == ' ' : ws(*p))) {
        p++;
        if (*p == '{') {
            while (p[1] && p[1] != '}') p++;
            p += p[1] ? 2 : 1;
        }
    }
    g_mission.p = p;
}

static int read_signed(void)
{
    const char *p = g_mission.p;
    int sign = 1, v = 0;
    if (*p == '-') p++, sign = -1;
    while (*p > '/' && *p < ':') v = *p++ - '0' + v * 10;
    g_mission.p = p;
    return sign * v;
}

/* Ini_PeekToken 0x44ace0: skips spaces (and comments); true when the character after the current one
   ends the line (\n, \r, \t) or the current one is '(' (a value that is the last on its line). */
bool ini_peek_token(void)
{
    skip_blanks(true);
    const char *p = g_mission.p;
    return p[1] == '\n' || p[1] == '\r' || p[1] == '\t' || p[0] == '(';
}

/* Ini_ReadInt 0x44ad50: blanks and comments, an optional '-', digits (none: 0). */
int ini_read_int(void)
{
    skip_blanks(false);
    return read_signed();
}

/* Ini_ReadWord 0x44ade0: the characters up to the next blank. The original has no bound and copies
   the terminating NUL of the text too if no blank comes; the port stops at the end and at cap. */
void ini_read_word(char *dst, int cap)
{
    skip_blanks(false);
    const char *p = g_mission.p;
    int n = 0;
    while (*p && !ws(*p)) {
        if (n < cap - 1) dst[n++] = *p;
        p++;
    }
    dst[n] = 0;
    g_mission.p = p;
}

/* Ini_ReadIntChecked 0x44ae50: like Ini_ReadInt after skipping spaces only, and fatal (-0xb1, with the
   line number) if the value is the last on its line and doesn't start with '-' or a digit, i.e. when
   a line ends before all its values. */
int ini_read_int_checked(int line)
{
    if (ini_peek_token()) {
        char c = *g_mission.p;
        if (c != '-' && (c < '0' || c > '9')) game_fatal(-0xb1, 0xe0, line);
    }
    return read_signed();
}

/* one coordinate: up to `open`, then blanks, a signed number */
static int32_t coord(char open)
{
    const char *p = strchr(g_mission.p, open);
    if (!p) game_fatal(-0x5f, 0x68, open);
    g_mission.p = p + 1;
    return ini_read_int();
}

/* Ini_ReadCoords 0x44aed0: "(x, y, z)" from the next '(' on. */
void ini_read_coords(int32_t *x, int32_t *y, int32_t *z)
{
    *x = coord('(');
    *y = coord(',');
    *z = coord(',');
    const char *p = strchr(g_mission.p, ')');
    if (!p) game_fatal(-0x5f, 0x68, ')');
    g_mission.p = p + 1;
}

/* Ini_ReadOptDigit 0x44b130: the digit between the line number and '(' (the persistence flag), or -1.
   The character at the position is stepped over unconditionally, then spaces; the position is left
   on the digit (Ini_ReadCoords then skips it looking for '('). */
int ini_read_opt_digit(void)
{
    const char *p = g_mission.p;
    char c = *p;
    do {
        if (c == '(') {
            g_mission.p = p;
            return -1;
        }
        c = *++p;
    } while (c == ' ');
    g_mission.p = p;
    if (c == '(') return -1;
    return c - '0';
}
