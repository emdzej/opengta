/* PLAYER_A.DAT (Front_LoadSettings 0x42b4a0). */
#include "savedata.h"
#include "exe.h"
#include "platform.h"
#include "text.h"
#include "vfs.h"
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(SaveHiscore) == 0x14, "hiscore");
_Static_assert(sizeof(SavePlayer) == 0x50, "player record");
_Static_assert(offsetof(SaveData, score_target) == 0x0c, "+0xc");
_Static_assert(offsetof(SaveData, language) == 0x14, "+0x14");
_Static_assert(offsetof(SaveData, hiscore) == 0x18, "+0x18");
_Static_assert(offsetof(SaveData, player) == 0x180, "+0x180");
_Static_assert(offsetof(SaveData, current) == 0x400, "+0x400");
_Static_assert(sizeof(SaveData) == SAVE_SIZE, "PLAYER_A.DAT size");

SaveData save_data;

#define USER_FILE "PLAYER_A.DAT"
#define DATA_FILE "GTADATA/PLAYER_A.DAT"   /* "..\gtadata\player_a.dat", pointer 0x4af400 */

/* strcpy from a string in the exe: the string and its terminator; the bytes after it keep what was
   there (the shipped file shows the remains of longer names). A string longer than the field would
   run into the next one in the original; the shipped ones fit, here it is cut. */
static void copy_exe_str(char *dst, size_t cap, uint32_t va)
{
    const char *s = va ? exe_str(va) : "";
    size_t n = strlen(s);
    if (n >= cap) n = cap - 1;
    memcpy(dst, s, n);
    dst[n] = 0;
}

/* Front_LoadSettings 0x42b4a0, the defaults, written over whatever the buffer holds (zero at start, or
   the bytes of a short read). Player names: the second word of each {picture path, name} pair of the
   table 0x4a7390 (8 pairs, "Ulrika" .. "Kivlov"). High scores: every level gets the scores
   0x4a73d0[0..2] (1, 2, 3) with the names of the pointers 0x4af404[0..2] (BILLY, PAUL, STEVE). Not
   set: +0x06, the padding, the bytes after each name's terminator. */
void save_defaults(bool demo_mode)
{
    SaveData *d = &save_data;
    d->current = 1;
    d->music_sequential = demo_mode;
    d->sfx_volume = 4;
    d->music_volume = 6;
    d->pager_speed = 2;
    d->video_mode = 0;
    d->unk5 = 0;
    d->effects = 1;
    d->multi_target = 0;
    d->score_target = 100000;
    d->kill_target = 10;
    d->language = -1;
    for (int i = 0; i < SAVE_PLAYERS; i++) {
        SavePlayer *p = &d->player[i];
        copy_exe_str(p->name, sizeof p->name, exe_u32(0x4a7394 + 8u * i));
        p->best[0] = 0;
        for (int l = 1; l < SAVE_LEVELS; l++) p->best[l] = -1;
        p->chapter[0] = p->chapter[1] = 0;
        p->mission[0] = p->mission[1] = 0;
        for (int l = 0; l < SAVE_LEVELS; l++) p->seen[l] = 0;
    }
    for (int l = 0; l < SAVE_LEVELS; l++)
        for (int k = 0; k < SAVE_HISCORES; k++) {
            d->hiscore[l][k].score = (int32_t)exe_u32(0x4a73d0 + 4u * k);
            copy_exe_str(d->hiscore[l][k].name, sizeof d->hiscore[l][k].name, exe_u32(0x4af404 + 4u * k));
        }
    copy_exe_str(d->net_name, sizeof d->net_name, 0x4b080c);   /* sprintf(.., "GTA Game") */
}

bool save_store(void) { return plat_save_user_file(USER_FILE, &save_data, sizeof save_data); }

/* fread of 0x414 bytes: a longer file is fine; a shorter one leaves its bytes in the buffer and the
   defaults are written over them. */
static bool load_file(uint8_t *b, size_t n)
{
    if (!b) return false;
    memcpy(&save_data, b, n < SAVE_SIZE ? n : SAVE_SIZE);
    free(b);
    return n >= SAVE_SIZE;
}

bool save_load(bool demo_mode)
{
    size_t n = 0;
    uint8_t *user = plat_load_user_file(USER_FILE, &n);
    bool ok = user && n >= SAVE_SIZE && load_file(user, n);
    if (!ok) {
        if (user && n < SAVE_SIZE) free(user);   /* a broken user file: start from the shipped one */
        ok = load_file(vfs_read_all(DATA_FILE, &n), n);
    }
    if (!ok) {
        save_defaults(demo_mode);
        save_store();   /* the original: fatal -202/-204 if it can't write */
        ok = exe_loaded();
    }
    if (save_data.language != -1) text_set_language(save_data.language);
    return ok;
}
