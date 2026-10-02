/* The frontend's network screens (0x4276e0, 0x428150-0x428950) over a stubbed DirectPlay layer.

   The stubs answer as DirectPlay does on a machine with no service provider: the enumeration
   succeeds (DirectPlayEnumerateA returns DP_OK) and finds nothing, so the connection list says "No
   connections available" and nothing can be created, joined or hosted. The lobby screens are ported
   but unreachable with these stubs. */
#include "front/front_internal.h"
#include "exe.h"
#include <string.h>

enum { FRONT_W = 640, FRONT_H = 480 };

/* ---------------------------------------------------------------- DirectPlay stubs */

void net_set_role(int role) { (void)role; }                     /* Net_SetRole */
bool net_enum_providers(void) { return true; }                  /* Net_EnumProviders 0x486880 */
int net_provider_count(void) { return 0; }                      /* Net_GetProviderCount 0x4869a0 */
void net_init_players(void) {}                                  /* Net_InitPlayers */
void net_end_game(void) {}                                      /* Net_EndGame 0x412d00 */
static const char *net_provider_name(int i) { (void)i; return ""; }   /* Net_GetProviderName */
static bool net_create_provider(int i) { (void)i; return false; }     /* Net_CreateProvider */
static bool net_enum_sessions(void) { return false; }                 /* Net_EnumSessions */
static int net_session_count(void) { return 0; }                      /* Net_GetSessionCount */
static const char *net_session_name(int i) { (void)i; return NULL; }  /* Net_GetSessionName */
static bool net_join_session(int i) { (void)i; return false; }        /* Net_JoinSession */
static bool net_create_player(const char *name) { (void)name; return false; }   /* Net_CreatePlayer */
static bool net_host_session(const char *name) { (void)name; return false; }    /* Net_HostSession 0x486cc0 */
static void net_begin_session(int role) { (void)role; }               /* Net_BeginSession 0x412b20 */
static void net_enum_players(void) {}                                 /* Net_EnumPlayers 0x487000 */
static const char *net_session_title(void) { return ""; }             /* Net_GetSessionTitle 0x412c50 */
static const char *net_lobby_text(void) { return ""; }                /* Net_GetLobbyText 0x412bd0 */
static bool net_can_start(void) { return false; }                     /* Net_CanStart */
static void net_set_aborted(void) {}                                  /* Net_SetAborted */
static void net_start_host(void) {}                                   /* Net_HostSession (lobby: start) */
static bool net_start_handshake(void) { return false; }               /* Net_StartHandshake */
static void net_handshake_failed(void) {}                             /* Net_unk_0044b900 */
static int net_lobby_state(void) { return 0; }                        /* Net_GetLobbyState 0x412b80 */
static void net_close_session(void) {}                                /* Net_CloseSession 0x486f70 */

#define MT (front_fonts.mtext)

static void draw_mid(Surface *s, int y, const char *t)
{
    if (MT && t) font_draw_string(s, MT, 0x140 - font_string_width(MT, t) / 2, y, t);
}

/* Front_ScreenCommsError 0x4276e0 (0x11 comms failure, 0x13 version mismatch) */
void front_screen_comms_error(Front *f, uint32_t in, int screen, Surface *s)
{
    if (in & (FI_ENTER | FI_ESC) || in & FI_SPACE) front_set_screen(f, FS_START);
    front_draw_background(s, 1, 0, f->clock_ms);
    front_draw_title(s, 0x168, net_session_title());
    draw_mid(s, 0x1ac, f->tx[screen != FS_COMMS_VERSION ? T_COMMSFAIL : T_COMMSVERSION]);
    front_draw_key_right(f, s, T_ESC_KEY, T_QUIT);
}

/* Front_ScreenConnectionList 0x428150: the connection (service provider) list, at most 8, listed from the
   last; Enter creates the chosen one and goes on to the session name (host) or the session list
   (join). Esc: player select. */
void front_screen_connection_list(Front *f, uint32_t in, Surface *s)
{
    front_draw_background(s, 1, 0, f->clock_ms);
    front_draw_title(s, 0xb0, f->tx[net_provider_count() < 1 ? T_FIX2 : T_FIX1]);
    int n = net_provider_count();
    if (n > 8) n = 8;
    for (int i = 1; i <= n; i++) front_draw_menu_item_c(s, i, f->provider + 1, net_provider_name(net_provider_count() - i));
    if (in & FI_ESC) {
        front_set_screen(f, FS_PLAYERS);
        front_sample(f, 4);
    }
    if (in & FI_ENTER || in & FI_SPACE) {
        front_sample(f, 2);
        if (net_provider_count() > 0 && net_create_provider(net_provider_count() - f->provider - 1)) {
            if (f->mode == 1) front_set_screen(f, FS_NET_NAME);
            else if (f->mode == 2) {
                front_clear_or_draw_bg(s, 0);
                if (net_enum_sessions()) {
                    if (net_session_count() <= f->session) f->session = 0;
                    front_set_screen(f, FS_SESSIONS);
                }
            }
        }
    }
    if (in & FI_UP) {
        if (--f->provider < 0) f->provider = net_provider_count() - 1;
        front_sample(f, 7);
    }
    if (in & FI_DOWN) {
        if (++f->provider >= net_provider_count()) f->provider = 0;
        front_sample(f, 8);
    }
}

/* Front_ScreenSessionList 0x4282c0: the sessions found; Enter joins the chosen one with the player's
   name and goes to the lobby. Esc: the connection list. */
void front_screen_session_list(Front *f, uint32_t in, Surface *s)
{
    if (in & FI_ESC) {
        front_set_screen(f, FS_CONNECTIONS);
        front_sample(f, 4);
    }
    if (in & FI_ENTER || in & FI_SPACE) {
        front_sample(f, 2);
        if (net_session_count() > 0 && net_join_session(f->session) && net_create_player(front_player(f)->name)) {
            f->lobby = 0;
            f->lobby_first = 1;
            front_set_screen(f, FS_LOBBY_JOIN);
        }
    }
    if (in & FI_UP) {
        front_sample(f, 7);
        if (--f->session < 0) f->session = net_session_count() - 1;
    }
    if (in & FI_DOWN) {
        front_sample(f, 8);
        if (++f->session >= net_session_count()) f->session = 0;
    }
    const char *name = net_session_name(f->session);
    front_draw_background(s, 1, 0, f->clock_ms);
    draw_mid(s, 0xf0, f->tx[net_session_count() > 0 ? T_FIX3 : T_FIX4]);
    if (name && MT) {
        char buf[256];
        front_fmt(buf, sizeof buf, "'%s'", "s", name);   /* 0x4b07d0 */
        draw_mid(s, MT->height + 0xf0, buf);
    }
}

/* Front_ScreenEnterName 0x428480: the session name (PLAYER_A.DAT +0x401, 15 characters at most) with
   a blinking "<" cursor; Enter hosts it. Esc: the connection list. */
void front_screen_enter_name(Front *f, uint32_t in, Surface *s)
{
    char *nm = save_data.net_name;
    nm[sizeof save_data.net_name - 1] = 0;   /* the original's strlen relies on the terminator */
    long len1 = (long)strlen(nm) + 1, len = len1 - 1;
    if (++f->name_blink > 7) f->name_blink = 0;
    if (in & FI_DELETE) {
        front_sample(f, 0xd);
        if (len != 0) nm[len1 - 2] = 0;
        len = len1 - 2;
    }
    if ((in & FI_CHAR || in & FI_SPACE) && len >= 0 && len < 0xf) {
        front_sample(f, 0xc);
        char c;
        if (!(in & FI_SPACE)) {
            c = (char)(in >> 16);
            if (in & FI_SHIFT && c >= 'a' && c <= 'z') c -= 0x20;
        } else
            c = ' ';
        nm[len] = c;
        nm[len + 1] = 0;
        len++;
    }
    if (in & FI_ESC) {
        front_set_screen(f, FS_CONNECTIONS);
        if (f->hooks.sfx) f->hooks.sfx(4);
    }
    if (in & FI_ENTER) {
        front_sample(f, 2);
        if (len != 0) {
            front_clear_or_draw_bg(s, 0);
            if (net_host_session(nm) && net_create_player(front_player(f)->name)) {
                f->lobby = 1;
                f->lobby_first = 1;
                front_set_screen(f, FS_LOBBY_HOST);
            }
        }
    }
    front_draw_background(s, 1, 0, f->clock_ms);
    draw_mid(s, 0xf0, f->tx[T_FIX5]);
    if (!MT) return;
    draw_mid(s, MT->height + 0xf0, nm);
    if (f->name_blink < 4) font_draw_string(s, MT, font_string_width(MT, nm) / 2 + 0x140, MT->height + 0xf0, exe_str(0x4b07d8));
}

/* The lobby page of both lobby screens: session title, "status: <lobby text>", Esc: Quit and, when the
   game can start, Enter: Play. */
static void lobby_draw(Front *f, Surface *s)
{
    net_enum_players();
    front_draw_background(s, 1, 0, f->clock_ms);
    front_draw_title(s, 0x168, net_session_title());
    char buf[256];
    front_fmt(buf, sizeof buf, f->tx[T_SSCOLON], "ss", f->tx[T_STATUS], net_lobby_text());
    front_draw_title_alt(s, 0x1ac, buf);
    front_draw_key_left(f, s, T_ESC_KEY, T_QUIT);
    if (net_can_start()) front_draw_key_right(f, s, T_RTN_KEY, T_PLAY);
}

/* Front_ScreenLobbyHost 0x428680 */
void front_screen_lobby_host(Front *f, uint32_t in, Surface *s)
{
    if (f->lobby_first) {
        net_begin_session(f->lobby);
        lobby_draw(f, s);
        f->lobby_first = 0;
    }
    if (in & FI_ESC) {
        net_set_aborted();
        front_sample(f, 4);
    }
    if (in & FI_ENTER || in & FI_SPACE) {
        front_sample(f, 2);
        net_start_host();
    }
    lobby_draw(f, s);
    if (f->lobby == 3) {
        f->start_code = FRONT_HOST;
        net_init_players();
        if (!net_start_handshake()) {
            net_handshake_failed();
            front_set_screen(f, FS_COMMS_FAIL);
        } else
            front_set_screen(f, FS_LOADING);
        front_apply_volumes(f);
        return;
    }
    f->lobby = net_lobby_state();
    if (f->lobby == 5) {
        net_close_session();
        front_set_screen(f, FS_NET_NAME);
    } else if (f->lobby == 4)
        front_set_screen(f, FS_COMMS_FAIL);
    else if (f->lobby == 8)
        front_set_screen(f, FS_COMMS_VERSION);
}

/* Front_ScreenLobbyJoin 0x428950 */
void front_screen_lobby_join(Front *f, uint32_t in, Surface *s)
{
    if (!f->lobby_first) {
        if (f->lobby == 3) {
            f->start_code = FRONT_JOIN;
            net_init_players();
            if (!net_start_handshake()) {
                net_handshake_failed();
                front_set_screen(f, FS_COMMS_FAIL);
            } else
                front_set_screen(f, FS_LOADING);
            front_apply_volumes(f);
        } else {
            f->lobby = net_lobby_state();
            if (f->lobby == 5) {
                net_close_session();
                front_set_screen(f, FS_SESSIONS);
            } else if (f->lobby == 4)
                front_set_screen(f, FS_COMMS_FAIL);
            else if (f->lobby == 8)
                front_set_screen(f, FS_COMMS_VERSION);
        }
    } else {
        net_begin_session(f->lobby);
        lobby_draw(f, s);
        f->lobby_first = 0;
    }
    if (in & FI_ESC) {
        net_set_aborted();
        front_sample(f, 4);
    }
    lobby_draw(f, s);
}
