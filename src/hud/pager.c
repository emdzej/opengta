/* The pager (0x482140-0x482fb0, 0x485c30, 0x486430): 16 message slots that scroll through the pager
   window top left, countdowns with live digits, and the network chat line being typed. */
#include "hud.h"
#include "hud_internal.h"
#include "../audio/audio.h"
#include "../exe.h"
#include "../font.h"
#include "../text.h"
#include "../game/player.h"
#include <stdio.h>
#include <string.h>

Pager g_pager;

static int cols(void) { return hud_fonts.pager_cols; }   /* 0x784860 */

/* The next slot to show, the loop Pager_AddMessage, Pager_Update, Pager_RemoveCountdown, HUD_ChatBegin
   and HUD_ChatKey each inline: a local chat line first, else the oldest queued message or countdown. */
static int pager_pick(void)
{
    uint32_t best = 0xffffffffu;
    int r = -1;
    for (int i = 0; i < PAGER_SLOTS; i++) {
        int st = g_pager.slot[i].state;
        if (st == PAGER_CHAT_LOCAL) return i;
        if ((st == PAGER_SHOWING || st == PAGER_COUNTDOWN) && g_pager.slot[i].seq < best) {
            best = g_pager.slot[i].seq;
            r = i;
        }
    }
    return r;
}

static int pager_width(const char *s) { return hud_fonts.pager ? font_string_width(hud_fonts.pager, s) : 0; }

/* HUD_SetPagerSpeed 0x482140 */
void hud_set_pager_speed(int speed)
{
    static const uint8_t sub[4] = { 0, 1, 2, 4 }, px[4] = { 1, 1, 2, 3 };
    if (speed < 0 || speed > 3) hud_fatal(-0x4a, 200, speed);
    g_pager.speed_sub = sub[speed];
    g_pager.speed_px = px[speed];
}

bool hud_is_pager_busy(void) { return g_pager.visible; }   /* HUD_IsPagerBusy 0x482440 */

/* Pager_Reset 0x482450 (HUD_Init has the same lines inline) */
void pager_reset(void)
{
    g_pager.blink = 0;
    g_pager.light = false;
    g_pager.visible = false;
    g_pager.update = false;
    g_pager.cur = -1;
    for (int i = 0; i < PAGER_SLOTS; i++) g_pager.slot[i].state = PAGER_FREE;
    g_pager.seq = 0;
    g_pager.last_shown = -1;
    g_pager.last_added = -1;
}

void pager_hud_init(void)
{
    static bool speed_read;
    if (!speed_read) {   /* the initialised data 0x4b3610 / 0x4b3614 */
        speed_read = true;
        g_pager.speed_sub = exe_data(0x4b3610, 4) ? (int)exe_u32(0x4b3610) : 2;
        g_pager.speed_px = exe_data(0x4b3614, 4) ? (int)exe_u32(0x4b3614) : 2;
    }
    pager_reset();
}

/* Copies src to dst (cap bytes), cut off to fit (the original's overlong mapped texts, see below). */
static void copy_cut(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

/* Pager_AddMessage 0x4824a0. Quirks kept: the length used for appending and for the slot is that of
   the caller's text, not of its mapped UTF-8 copy (longer when it has accented letters, which are
   then cut off); a text already showing in a queued slot is dropped; a message arriving while the
   last added one is still queued is appended to it after a space. */
void pager_add_message(const char *msg)
{
    static char mapped[0x1000];   /* 0x784c40 */
    int len = (int)strlen(msg);
    text_to_utf8_mapped(mapped, msg);
    for (int i = 0; i < PAGER_SLOTS; i++)
        if (g_pager.slot[i].state == PAGER_SHOWING && strstr(g_pager.slot[i].text, mapped)) return;
    int last = g_pager.last_added;
    if (last != -1 && g_pager.slot[last].state == PAGER_SHOWING) {
        PagerSlot *p = &g_pager.slot[last];
        if (p->len + 1 + len > PAGER_MAX_LEN) return;
        p->text[p->len] = ' ';
        copy_cut(p->text + p->len + 1, sizeof p->text - (size_t)p->len - 1, mapped);
        p->len += len + 1;
        p->text[p->len] = 0;
        p->width = pager_width(p->text);
        Snd_PlaySample28();
        return;
    }
    int n = 0;
    while (g_pager.slot[n].state != PAGER_FREE)
        if (++n >= PAGER_SLOTS) return;
    g_pager.visible = true;
    g_pager.update = true;
    g_pager.blink_on = true;
    int c = cols();
    if (len + c > PAGER_MAX_LEN) hud_fatal(-0x82, 0xbf, len);
    PagerSlot *p = &g_pager.slot[n];
    memset(p->text, ' ', (size_t)c);
    copy_cut(p->text + c, sizeof p->text - (size_t)c, mapped);
    p->len = len + c;
    p->text[p->len] = 0;
    p->width = pager_width(p->text);
    p->seq = g_pager.seq++;
    p->scroll = 0;
    p->state = PAGER_SHOWING;
    if (g_pager.cur == -1) g_pager.cur = g_pager.last_shown = pager_pick();
    g_pager.last_added = n;
    Snd_PlaySample28();
}

/* Pager_RemoveCountdown 0x482710 */
void pager_remove_countdown(int id)
{
    for (int i = 0; i < PAGER_SLOTS; i++) {
        PagerSlot *p = &g_pager.slot[i];
        if (p->state != PAGER_COUNTDOWN || p->cd_id != id) continue;
        p->state = PAGER_FREE;
        if (i == g_pager.cur) {
            g_pager.last_shown = g_pager.cur;
            g_pager.cur = pager_pick();
            if (g_pager.cur == -1) g_pager.update = g_pager.visible = g_pager.blink_on = false;
        }
        Snd_DisableAlarmLoop();
    }
}

/* Pager_AddCountdown 0x4827c0: the message, then the seconds after its end (outside its length, so
   Pager_Update can rewrite them). Scrolling stops 58 pixels before the end (not scaled at res 2).
   The original works on whatever slot was added last even if the message was dropped or appended
   (and on slot -1 if none ever was: the port ignores that case). */
void pager_add_countdown(const char *msg, int value, int id)
{
    pager_add_message(msg);
    int n = g_pager.last_added;
    if (n < 0) return;
    PagerSlot *p = &g_pager.slot[n];
    p->cd_value = (int16_t)value;
    p->state = PAGER_COUNTDOWN;
    p->cd_tick = 0x19;
    p->cd_id = id;
    int v = p->cd_value, digits = 1;   /* Util_NumDigits 0x43cbd0 */
    if (v < 0) v = -v;
    for (; v > 9; v /= 10) digits++;
    if (p->len + digits >= PAGER_TEXT) return;
    memset(p->text + p->len, '0', (size_t)digits);
    p->text[p->len + digits] = 0;
    p->width = pager_width(p->text);
    p->cd_max = (int16_t)(p->width - 0x3a);
    if (p->cd_max < 0) p->cd_max = 0;
    snprintf(p->text + p->len, sizeof p->text - (size_t)p->len, "%d", p->cd_value);
}

/* Pager_Resume 0x482fb0 (F7): the last message again, unless the player is typing */
void pager_resume(void)
{
    if (g_pager.last_shown == -1) return;
    if (g_players[g_player_viewed].u184 != -1) return;   /* Player_GetLocalVal184 0x463a50 */
    g_pager.visible = g_pager.update = true;
    g_pager.cur = g_pager.last_shown;
    PagerSlot *p = &g_pager.slot[g_pager.cur];
    if (p->state != PAGER_COUNTDOWN) p->state = PAGER_SHOWING;
    p->scroll = 0;
}

/* Pager_Update 0x486430: countdowns tick (with the alarm loop), the shown slot scrolls by the speed;
   at the end it is freed and the next one comes (a local chat line stays one pixel short of the
   end); the light blinks every 5 frames; the chat cursor blinks every frame. */
void pager_update(void)
{
    for (int i = 0; i < PAGER_SLOTS; i++) {
        PagerSlot *p = &g_pager.slot[i];
        if (p->state != PAGER_COUNTDOWN) continue;
        Snd_EnableAlarmLoop();
        if (--p->cd_tick == 0) {
            p->cd_value--;
            p->cd_tick = 0x19;
            snprintf(p->text + p->len, sizeof p->text - (size_t)p->len, "%d", p->cd_value);
            if (p->cd_value == 0) {
                p->state = PAGER_SHOWING;
                Snd_DisableAlarmLoop();
            }
        }
    }
    if (g_pager.cur < 0) return;   /* the original indexes slot -1 */
    if (g_pager.slot[g_pager.cur].state == PAGER_COUNTDOWN)
        for (int i = 0; i < PAGER_SLOTS; i++)
            if (g_pager.slot[i].state == PAGER_SHOWING) g_pager.cur = i;
    int cur = g_pager.cur, shown = cur;
    PagerSlot *p = &g_pager.slot[cur];
    p->scroll += g_pager.speed_px;
    if (p->state == PAGER_COUNTDOWN && p->cd_max < p->scroll) p->scroll = p->cd_max;
    if (p->width < p->scroll) p->scroll = p->width;
    if (p->scroll == p->width) {
        if (p->state == PAGER_CHAT_LOCAL) {
            p->scroll = p->width - 1;
        } else {
            p->state = PAGER_FREE;
            shown = pager_pick();
            g_pager.cur = shown;
            g_pager.last_shown = cur;
            if (shown == -1) {
                g_pager.update = g_pager.visible = g_pager.blink_on = false;
                return;   /* the original then reads the state of slot -1 */
            }
        }
    }
    if (g_pager.blink_on && --g_pager.blink < 1) {
        g_pager.blink = 5;
        g_pager.light = !g_pager.light;
    }
    PagerSlot *q = &g_pager.slot[shown];
    if (q->state == PAGER_CHAT_LOCAL && q->len > 0) {
        char *c = &q->text[q->len - 1];
        *c = *c == '_' ? ' ' : '_';
    }
}

/* HUD_DrawPager 0x485c30: the pager sprite (3) at the top left, the text in the pager font (palette
   aux 3) at (11, 6) * res in a window 58 * res wide, and the light (sprite 4) at (11, 19) * res. */
void hud_draw_pager(Surface *s)
{
    const SpriteInfo *in = hud_sprite(3);
    if (!in || g_pager.cur < 0) return;
    int res = g_hud_frame.res;
    g_pager.height = in->h;
    sprite_draw_screen(0, 0, in);
    int row = text_wide() ? (res != 1 ? 8 : 5) * res : 6 * res;
    const PagerSlot *p = &g_pager.slot[g_pager.cur];
    font_select(hud_fonts.pager);
    hud_draw_text_clipped(s, p->text, surface_offset(s, res * 0xb, row), p->scroll, res * 0x3a);
    if (g_pager.light) {
        const SpriteInfo *l = hud_sprite(4);
        if (l) sprite_draw_screen(res * 0xb, res * 0x13, l);
    }
}

/* HUD_ChatBegin 0x4828a0 (called by Net_BuildChatPrefix 0x44c1b0 with the "to ..." prefix). With no
   chat line open: the shown message is freed (the next one isn't picked: the original only clears the
   blink when there is none), a free slot becomes the line: the prefix after the leading spaces and a
   '_' cursor; for the local viewer it is shown at once (state 3), else it waits (4). With a line open,
   it is closed. The network side is not ported (DirectPlay). */
void hud_chat_begin(int to, const char *prefix, int max_len, int min_len)
{
    Player *pl = &g_players[g_player_viewed];
    bool local = player_is_viewed_local();
    if (pl->u184 != -1) {
        g_pager.slot[pl->u184].state = PAGER_FREE;
        pl->u184 = -1;
        if (local) {
            g_pager.cur = pager_pick();
            if (g_pager.cur == -1) g_pager.update = g_pager.visible = g_pager.blink_on = false;
        }
        return;
    }
    if (local) {
        if (g_pager.cur != -1) g_pager.slot[g_pager.cur].state = PAGER_FREE;
        if (pager_pick() == -1) g_pager.blink_on = false;
    }
    int n = 0;
    while (g_pager.slot[n].state != PAGER_FREE)
        if (++n >= PAGER_SLOTS) return;
    pl->u184 = n;
    PagerSlot *p = &g_pager.slot[n];
    if (!player_is_viewed_local()) {
        p->state = PAGER_CHAT_REMOTE;
    } else {
        g_pager.visible = g_pager.update = true;
        p->state = PAGER_CHAT_LOCAL;
        g_pager.cur = g_pager.last_shown = n;
    }
    int len = (int)strlen(prefix), c = cols();
    if (c + len > PAGER_MAX_LEN) hud_fatal(-0x82, 0xbf, len);
    memset(p->text, ' ', (size_t)c);
    text_to_utf8_mapped(p->text + c, prefix);
    p->text[c + len] = '_';
    p->text[c + len + 1] = 0;
    p->len = c + 1 + len;
    p->scroll = 0;
    p->width = pager_width(p->text) - 0x3a;
    p->chat_max = c + 1 + max_len;
    p->chat_min = c + 1 + min_len;
    p->seq = g_pager.seq;   /* not incremented */
    p->chat_to = to;
}

/* HUD_ChatKey 0x482b10: a character goes before the cursor while the line is shorter than its
   maximum; Backspace removes one down to the prefix; Enter drops the cursor and lets the line scroll
   out like a message (a remote player's line addressed to someone else is just freed). */
void hud_chat_key(int n, int ch)
{
    if (n < 0 || n >= PAGER_SLOTS) return;
    PagerSlot *p = &g_pager.slot[n];
    int len = p->len;
    if (ch == 0xd) {
        p->len = len - 1;
        if (len >= 1) p->text[len - 1] = 0;
        p->width = pager_width(p->text);
        if (p->state == PAGER_CHAT_REMOTE && p->chat_to != -1 && p->chat_to != g_player_local) {
            p->state = PAGER_FREE;
            return;
        }
        p->state = PAGER_SHOWING;
        g_players[g_player_viewed].u184 = -1;
        if (g_pager.cur == -1) {
            g_pager.cur = pager_pick();
            if (g_pager.cur != -1) g_pager.update = g_pager.visible = g_pager.blink_on = true;
        }
        return;
    }
    if (ch == 0x7f) {
        if (p->chat_min < len && len >= 2) {
            p->len = len - 1;
            p->text[len - 2] = '_';
            p->text[len - 1] = 0;
        }
    } else if (len < p->chat_max && len >= 1 && len + 1 < PAGER_TEXT) {
        p->text[len - 1] = (char)ch;
        p->text[len] = '_';
        p->len = len + 1;
        p->text[len + 1] = 0;
    }
    p->width = pager_width(p->text) - 0x3a;
}
