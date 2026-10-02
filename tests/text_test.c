/* FXT language files (src/text.c) against the data: all five files decrypt and end in "[]", known keys
   resolve, a missing key fails like the original's, and the UTF-8 helpers / character mapping. */
#include "exe.h"
#include "text.h"
#include "vfs_host.h"
#include <stdio.h>
#include <string.h>

static int fail;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fail = 1; } } while (0)

static void show(const char *key)
{
    const char *t = text_get(key);
    printf("  [%s] %s\n", key, t ? t : "(missing)");
}

int main(void)
{
    char err[256];
    if (!vfs_mount_default()) { printf("SKIP: no data root\n"); return 0; }
    bool have_exe = exe_init(err, sizeof err);
    if (!have_exe) printf("note: %s (character mapping not checked)\n", err);

    static const struct { int lang; const char *busted, *wasted; } L[] = {
        {TEXT_ENGLISH, "BUSTED!", "WASTED!"},
        {TEXT_FRENCH, "ARRESTATION!", "REFROIDI!"},
        {TEXT_GERMAN, "VERHAFTET!", "GET\xc3\x96TET!"},
        {TEXT_ITALIAN, "PRESO!", "FATTO FUORI!"},
        {TEXT_SPECIAL, "BUSTED!", "WASTED!"},
    };
    for (size_t i = 0; i < sizeof L / sizeof *L; i++) {
        text_free();
        text_init_language(L[i].lang);
        printf("%s:\n", text_file());
        const char *b = text_get("4003"), *w = text_get("4004");
        CHECK(b && !strcmp(b, L[i].busted), "%s [4003] = '%s'", text_file(), b ? b : text_error());
        CHECK(w && !strcmp(w, L[i].wasted), "%s [4004] = '%s'", text_file(), w ? w : text_error());
        show("4003");
        show("4004");
        show("2500");
        show("car0");
        show("1001");
        /* prefixes and extensions of a key don't match */
        CHECK(text_get("400") == NULL, "[400] found");
        CHECK(text_get("40031") == NULL, "[40031] found");
        CHECK(strstr(text_error(), "not found"), "error '%s'", text_error());
        CHECK(text_get("") == NULL, "empty key found");
    }

    /* Text_SetLanguage does not reload a loaded file (only Text_InitLanguage forgets it) */
    text_free();
    text_init_language(TEXT_ENGLISH);
    text_get("4003");
    text_set_language(TEXT_GERMAN);
    CHECK(!strcmp(text_get("4003"), "BUSTED!"), "set_language reloaded");
    CHECK(!text_set_language(5), "language 5 accepted");

    /* UTF-8 */
    const char *s = "A\xc3\x96\xe2\x82\xac" "B";
    const char *p = s;
    CHECK(text_utf8_next(&p) == 'A', "utf8 A");
    CHECK(text_utf8_next(&p) == 0xd6, "utf8 O-umlaut");
    CHECK(text_utf8_peek(p) == 0x20ac, "utf8 euro");
    text_utf8_skip(&p);
    CHECK(*p == 'B', "utf8 skip");
    text_utf8_back(&p);
    CHECK(p == s + 3, "utf8 back 3");
    text_utf8_back(&p);
    CHECK(p == s + 1, "utf8 back 2");
    CHECK(text_has_wide_chars(s) && !text_has_wide_chars("A\xc3\x96"), "wide chars");

    /* character mapping: lower to upper case; accents through the exe table in French/German/Italian */
    text_set_language(TEXT_ENGLISH);
    CHECK(text_map_char('a') == 'A' && text_map_char(0xe9) == 0xe9, "map english");
    if (have_exe) {
        text_set_language(TEXT_FRENCH);
        uint16_t e = text_map_char(0xe9);   /* e acute -> E acute (0xc9) */
        printf("french map: 0xe9 -> 0x%x, 0xe0 -> 0x%x\n", e, text_map_char(0xe0));
        CHECK(e == 0xc9, "map french e-acute = 0x%x", e);
        char out[16];
        text_to_utf8_mapped(out, "\xc3\xa9t\xc3\xa9");
        CHECK(!strcmp(out, "\xc3\x89T\xc3\x89"), "mapped '%s'", out);
    }
    text_free();
    printf(fail ? "FAIL\n" : "PASS\n");
    return fail;
}
