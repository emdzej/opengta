/* Game text: the FXT language files (text module 0x47d680-0x47dbd0) and the UTF-8 helpers of the HUD
   module (0x486670-0x486760). Layout: docs/text-fonts.md.

   Strings are UTF-8-like: 1, 2 or 3 bytes per code (lead byte < 0x80, 0xc0-0xdf, 0xe0-0xff), codes up to
   0xffff, decoded without validation exactly as the original does. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum {
    TEXT_ENGLISH = 0, TEXT_FRENCH = 1, TEXT_GERMAN = 2, TEXT_ITALIAN = 3, TEXT_JAPANESE = 4,
    TEXT_SPECIAL = 99,          /* SPECIAL.FXT: English with the uncensored lines */
};

/* Text_InitLanguage 0x47d7f0: the language is the registry value "Language" (Config_GetLanguageChar
   0x46e9c0 returns it as a digit; read by Config_ReadRegistry); here a parameter. Forgets a loaded file
   (without freeing it, as the original) and selects the file for the language. */
void text_init_language(int language);
/* Text_SetLanguage 0x47d680: selects the file (PLAYER_A.DAT can override the language). Does not unload
   a file already loaded: the next text_get still searches the old one, as in the original. False for an
   unknown language (the original: fatal error -74 "invalid case"). */
bool text_set_language(int language);
int text_language(void);                      /* 0x775554 */
bool text_wide(void);                         /* 0x775558: Japanese (two-byte font) */
const char *text_file(void);                  /* 0x77555c: "GTADATA/ENGLISH.FXT", ... */

/* Text_Get 0x47d9a0: the text of [key], loading and decrypting the file on first use. NULL if the file
   can't be loaded or the key is missing: the original stops with a fatal error then (-86 "invalid
   format in text file" when the file does not end in "[]", -85 "text identifier [%s] not found in
   language file"); text_error says which. */
const char *text_get(const char *key);
const char *text_error(void);
/* Text_Free 0x47d970 */
void text_free(void);

bool text_is_foreign(void);                   /* Text_IsForeign 0x47da90: not English or special */
/* Text_MapChar 0x47dab0: the font glyph code of a character code: lower case to upper case and, for
   French, German and Italian, codes 0x80-0xff (0x100 too for French/German) through the table at
   0x4b3280 (read from the exe; unmapped if the exe isn't loaded). */
uint16_t text_map_char(uint16_t c);
bool text_has_wide_chars(const char *s);      /* Text_HasWideChars 0x47dbd0: a code >= 0x100 */

/* UTF-8 helpers (hud module) */
uint16_t text_utf8_next(const char **p);      /* Text_Utf8Next 0x486670 */
uint16_t text_utf8_peek(const char *p);       /* Text_Utf8Peek 0x4866c0 */
void text_utf8_skip(const char **p);          /* Text_Utf8Skip 0x486700 */
void text_utf8_back(const char **p);          /* Text_Utf8Back 0x486730 */
/* Text_ToUtf8Mapped 0x486760: dst = src with every code through text_map_char, re-encoded. dst needs
   up to 3 bytes per code plus the terminator. */
void text_to_utf8_mapped(char *dst, const char *src);
