/* FXT language files (text module 0x47d680-0x47dbd0) and the UTF-8 helpers (0x486670-0x486760). */
#include "text.h"
#include "exe.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool loaded;              /* 0x775550 */
static int language;             /* 0x775554 */
static bool wide;                /* 0x775558 */
static char file[32];            /* 0x77555c */
static uint8_t *fxt;             /* 0x775578 */
static size_t fxt_size;
static char error[160];

/* The file names of the language table: the original's "..\gtadata\english.fxt" (0x4b34f8),
   french 0x4b34e0, german 0x4b34c8, italian 0x4b34b0, japanese 0x4b3498, special 0x4b3480. */
static const char *language_file(int lang)
{
    switch (lang) {
    case TEXT_ENGLISH: return "GTADATA/ENGLISH.FXT";
    case TEXT_FRENCH: return "GTADATA/FRENCH.FXT";
    case TEXT_GERMAN: return "GTADATA/GERMAN.FXT";
    case TEXT_ITALIAN: return "GTADATA/ITALIAN.FXT";
    case TEXT_JAPANESE: return "GTADATA/JAPANESE.FXT";   /* not shipped with the 2002 release */
    case TEXT_SPECIAL: return "GTADATA/SPECIAL.FXT";
    default: return NULL;
    }
}

/* Text_SetLanguage 0x47d680. Only Japanese sets the wide flag (for the others the copy loop leaves its
   last byte, the terminator, in 0x775558, i.e. 0). On an unknown language the original calls
   Error_Fatal(-74), which does not return. */
bool text_set_language(int lang)
{
    language = lang;
    const char *f = language_file(lang);
    if (!f) {
        snprintf(error, sizeof error, "invalid language %d", lang);
        return false;
    }
    snprintf(file, sizeof file, "%s", f);
    wide = lang == TEXT_JAPANESE;
    return true;
}

/* Text_InitLanguage 0x47d7f0: the same switch, after clearing the loaded flag (the buffer is not
   freed: Text_Free is only called at shutdown). */
void text_init_language(int lang)
{
    loaded = false;
    text_set_language(lang);
}

int text_language(void) { return language; }
bool text_wide(void) { return wide; }
const char *text_file(void) { return file; }
const char *text_error(void) { return error; }

/* Text_Free 0x47d970 */
void text_free(void)
{
    if (loaded) {
        free(fxt);
        fxt = NULL;
        fxt_size = 0;
        loaded = false;
    }
}

/* The loading half of Text_Get: File_LoadAlloc 0x42dce0 (exact size, no terminator), then every byte
   b[i] -= k with k starting at 0x64 and stepping by m, m starting at 0x63 and doubling, all mod 256:
   k runs 0x64, 0xc7, 0x8d, 0x19, 0x31, 0x61, 0xc1, 0x81, then m is 0 and every further byte is
   decremented by 1. The file must end in "[]" (else fatal -86). */
static bool fxt_load(void)
{
    size_t n;
    uint8_t *b = vfs_read_all(file, &n);
    if (!b) {
        snprintf(error, sizeof error, "cannot open %s", file);
        return false;
    }
    uint8_t k = 0x64, m = 0x63;
    for (size_t i = 0; i < n; i++) {
        b[i] = (uint8_t)(b[i] - k);
        k = (uint8_t)(k + m);
        m = (uint8_t)(m * 2);
    }
    fxt = b;
    fxt_size = n;
    loaded = true;   /* set before the check, as in the original */
    if (n < 2 || b[n - 2] != '[' || b[n - 1] != ']') {
        snprintf(error, sizeof error, "invalid format in text file : %s", file);
        return false;
    }
    return true;
}

/* Text_Get 0x47d9a0. A linear scan from the start of the file on every call: find the next '[', and
   if the identifier after it equals key character for character up to its ']', the text starts after
   the ']' (NUL-terminated in the file). Anything that isn't a match (a longer or shorter identifier, a
   mismatch) resumes the scan for '[' from the character where the comparison stopped, so a '[' inside a
   text body also opens an identifier. An empty identifier "[]" ends the table: the key is missing
   (fatal -85 in the original). The file ends in "[]" (checked at load), so the scan stays inside it.
   The empty key never matches. */
const char *text_get(const char *key)
{
    if (!loaded && !fxt_load()) return NULL;
    if (!fxt || fxt_size < 2 || fxt[fxt_size - 2] != '[' || fxt[fxt_size - 1] != ']') {
        snprintf(error, sizeof error, "invalid format in text file : %s", file);
        return NULL;   /* the original was stopped by the load check */
    }
    const char *p = (const char *)fxt;
    char c = *p;
    for (;;) {
        while (c != '[') c = *++p;
        c = *++p;
        if (c == ']') {
            snprintf(error, sizeof error, "text identifier [%s] not found in language file", key);
            return NULL;
        }
        const char *k = key;
        do {
            if (c != *k) break;
            c = *++p;
            k++;
        } while (c != ']');
        if (*k == '\0' && *p == ']') return p + 1;
    }
}

/* Text_IsForeign 0x47da90 */
bool text_is_foreign(void) { return language != TEXT_ENGLISH && language != TEXT_SPECIAL; }

/* The accent table 0x4b3280: u16 glyph codes indexed by character code (only 0x80..0x100 are read). */
static uint16_t accent(uint16_t c) { return exe_data(0x4b3280 + 2u * c, 2) ? exe_u16(0x4b3280 + 2u * c) : c; }

/* Text_MapChar 0x47dab0. Unknown languages: fatal -74 in the original; unmapped here. */
uint16_t text_map_char(uint16_t c)
{
    switch (language) {
    case TEXT_ENGLISH:
    case TEXT_SPECIAL:
        if (c > 0x60 && c < 0x7b) return (uint16_t)(c - 0x20);
        break;
    case TEXT_FRENCH:
    case TEXT_GERMAN:
        if (c > 0x60 && c < 0x7b) return (uint16_t)(c - 0x20);
        if (c > 0x7f && c < 0x101) return accent(c);   /* 0x100 reads past the table: kept */
        break;
    case TEXT_ITALIAN:
        if (c > 0x60 && c < 0x7b) return (uint16_t)(c - 0x20);
        if (c > 0x7f && c < 0x100) return accent(c);
        break;
    default:
        break;
    }
    return c;
}

/* ---- UTF-8 (hud module). The original sign-extends the lead byte and computes in 16 bits, which
   comes to the usual decoding: a byte < 0x80 is the code; any other lead byte with bit 5 set starts 3
   bytes (4 + 6 + 6 payload bits), without bit 5 2 bytes (5 + 6). No validation: continuation bytes
   and 0xf0-0xff are taken as leads like the others. */

/* Text_Utf8Peek 0x4866c0 */
uint16_t text_utf8_peek(const char *p)
{
    const uint8_t *s = (const uint8_t *)p;
    if (s[0] < 0x80) return s[0];
    if (s[0] & 0x20) return (uint16_t)((s[0] & 0x0f) << 12 | (s[1] & 0x3f) << 6 | (s[2] & 0x3f));
    return (uint16_t)((s[0] & 0x1f) << 6 | (s[1] & 0x3f));
}

/* Text_Utf8Skip 0x486700 */
void text_utf8_skip(const char **p)
{
    const uint8_t *s = (const uint8_t *)*p;
    *p += s[0] < 0x80 ? 1 : (s[0] & 0x20) ? 3 : 2;
}

/* Text_Utf8Next 0x486670 */
uint16_t text_utf8_next(const char **p)
{
    uint16_t c = text_utf8_peek(*p);
    text_utf8_skip(p);
    return c;
}

/* Text_Utf8Back 0x486730: one byte back if the previous byte is ASCII; two if it is a continuation byte
   and the one before has bit 6 set (a lead byte, or ASCII 0x40-0x7f); else three. */
void text_utf8_back(const char **p)
{
    const uint8_t *s = (const uint8_t *)*p;
    if (s[-1] < 0x80) *p -= 1;
    else if (s[-2] & 0x40) *p -= 2;
    else *p -= 3;
}

/* Text_HasWideChars 0x47dbd0 */
bool text_has_wide_chars(const char *s)
{
    while (*s)
        if (text_utf8_next(&s) > 0xff) return true;
    return false;
}

/* Text_ToUtf8Mapped 0x486760 */
void text_to_utf8_mapped(char *dst, const char *src)
{
    uint8_t *d = (uint8_t *)dst;
    while (*src) {
        uint16_t c = text_map_char(text_utf8_next(&src));
        if (c < 0x80) *d++ = (uint8_t)c;
        else if (c < 0x800) {
            *d++ = (uint8_t)(0xc0 + (c >> 6 & 0x1f));
            *d++ = (uint8_t)(0x80 + (c & 0x3f));
        } else {
            *d++ = (uint8_t)(0xe0 + (c >> 12 & 0x0f));
            *d++ = (uint8_t)(0x80 + (c >> 6 & 0x3f));
            *d++ = (uint8_t)(0x80 + (c & 0x3f));
        }
    }
    *d = 0;
}
