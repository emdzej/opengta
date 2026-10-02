/* Shared by the frontend state machine files (front.c, front_screens.c, front_net.c). */
#pragma once
#include "front/front.h"
#include "front/front_text.h"
#include "front/images.h"
#include "savedata.h"
#include <stddef.h>

/* sprintf with the game's format strings (FXT texts such as "%s: %s"), arguments described by types
   ('s' string, 'd' int): a text whose conversions don't match its arguments prints what it can
   instead of reading garbage. Handles %s, %d / %i / %u with flags '0' '-' and a width, %%. */
void front_fmt(char *dst, size_t cap, const char *fmt, const char *types, ...);

/* Records of save_data (0x510298). */
static inline SavePlayer *front_player(const Front *f)
{
    (void)f;
    return &save_data.player[(int8_t)save_data.current & 7];
}
/* best[level] of a player record, as the original addresses it (0x510428 + level * 4): levels past 5
   (the network table's 6..11) land in the fields after best[], inside the record. */
static inline int32_t *front_best(SavePlayer *p, int level)
{
    return (int32_t *)((uint8_t *)p + 0x10 + 4 * level);
}
static inline int32_t *front_chapter(const Front *f) { return &front_player(f)->chapter[f->net]; }
static inline int32_t *front_mission(const Front *f) { return &front_player(f)->mission[f->net]; }
static inline const FrontCity *front_city(const Front *f) { return &f->cities[*front_chapter(f)]; }

void front_sample(const Front *f, int n);                   /* Snd_PlaySampleN */
void front_save(Front *f);                                  /* fopen("wb") + fwrite of 0x414 bytes */
void front_revert(Front *f);                                /* fopen("rb") + fread of 0x414 bytes */

/* Bottom line helpers: sprintf(sscolon, key, label) in F_MTEXT at row 480 - height. */
void front_draw_key_left(Front *f, Surface *s, int key, int label);
void front_draw_key_right(Front *f, Surface *s, int key, int label);
void front_draw_key_center(Front *f, Surface *s, int key, int label);

void front_draw_enter_prompt(Front *f, Surface *s, const char *label);      /* 0x426fd0 */
void front_draw_high_scores(Front *f, Surface *s, int x, int y, int level); /* 0x427fc0 */
void front_draw_player_scores(Front *f, Surface *s, int x, int y, int player, const char *title); /* 0x429df0 */
void front_draw_player_portrait(Front *f, Surface *s, int x, int y, int player, const char *name,
                                int blink);                                 /* 0x429f60 */

/* Screens (front_screens.c) */
void front_screen_main(Front *f, uint32_t in, Surface *s);          /* 0x4277c0 */
void front_screen_loading(Front *f, uint32_t in, Surface *s);       /* 0x428bf0 */
void front_screen_start(Front *f, uint32_t in, Surface *s);         /* 0x428ee0 */
void front_screen_options(Front *f, uint32_t in, Surface *s);       /* 0x429180 */
void front_screen_multi_options(Front *f, uint32_t in, Surface *s); /* 0x429650 */
void front_screen_player_select(Front *f, uint32_t in, Surface *s); /* 0x429a20 */
void front_screen_rename(Front *f, uint32_t in, Surface *s);        /* 0x42a160 */
void front_screen_results(Front *f, uint32_t in, Surface *s);       /* 0x42a750 */
void front_screen_reset_player(Front *f, uint32_t in, Surface *s);  /* 0x42b260 */

/* Network screens and the DirectPlay stubs (front_net.c) */
void front_screen_comms_error(Front *f, uint32_t in, int screen, Surface *s); /* 0x4276e0 */
void front_screen_connection_list(Front *f, uint32_t in, Surface *s);  /* 0x428150 */
void front_screen_session_list(Front *f, uint32_t in, Surface *s);  /* 0x4282c0 */
void front_screen_enter_name(Front *f, uint32_t in, Surface *s);    /* 0x428480 */
void front_screen_lobby_host(Front *f, uint32_t in, Surface *s);    /* 0x428680 */
void front_screen_lobby_join(Front *f, uint32_t in, Surface *s);    /* 0x428950 */
void net_set_role(int role);                                        /* Net_SetRole */
bool net_enum_providers(void);                                      /* Net_EnumProviders 0x486880 */
int net_provider_count(void);                                       /* Net_GetProviderCount 0x4869a0 */
void net_init_players(void);                                        /* Net_InitPlayers */
void net_end_game(void);                                            /* Net_EndGame 0x412d00 */
