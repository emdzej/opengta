#include "input.h"
#include "../exe.h"
#include "game.h"
#include "player.h"
#include "replay.h"
#include "../hud/hud.h"
#include <string.h>

/* GTA Settings' default bindings ("Controls\Control 0..9" as it creates them, the first of its three
   presets): Left, Right, Up, Down, Space, Enter, Ctrl, X, Z, Tab as DirectInput key codes. The
   original reads them from the registry (Config_ReadRegistry 0x46e820, fatal when missing); the port
   has no registry, so these are what it reads. */
int32_t g_config_controls[INPUT_ACTIONS] = { 0xcb, 0xcd, 0xc8, 0xd0, 0x39, 0x1c, 0x1d, 0x2d, 0x2c, 0x0f };
int32_t g_input_keys[INPUT_ACTIONS];

static struct {
    bool init;
    bool keyboard, joystick;    /* 0x5134e0, 0x513654: an action is bound to a key / to the joystick */
    uint8_t held[INPUT_ACTIONS];   /* 0x5135f4: action held (keyboard) */
    int8_t steer;               /* 0x513650 */
    int8_t accel, brake;        /* 0x5134d9, 0x5135f0 */
    uint8_t space, fire, enter, special, next, prev;   /* 0x5134da, 0x5134d8, 0x513652, 0x5135f1, 0x51361c, 0x513651 */
} in;

/* ---- the event queue ---- */
enum { QUEUE = 256 };
static uint16_t queue[QUEUE];
static int q_head, q_tail;
static uint8_t host_held[KEY_COUNT];

void input_flush_keys(void) { q_head = q_tail = 0; }      /* Input_FlushKeys 0x414a70: Event_Flush */
void input_post_key(int code)
{
    int n = (q_tail + 1) % QUEUE;
    if (n == q_head) return;   /* full: dropped (MGL's queue is finite too) */
    queue[q_tail] = (uint16_t)code;
    q_tail = n;
}
/* Input_GetKey 0x414a80: the next key down / up event, 0 when the queue is empty (other events are
   skipped). */
int input_get_key(void)
{
    if (q_head == q_tail) return 0;
    int k = queue[q_head];
    q_head = (q_head + 1) % QUEUE;
    return k;
}

/* platform code -> the original's: Input_GetKey adds 0x100 only to the cursor block (0x47..0x53 with
   no character); other extended keys (keypad Enter, right Ctrl / Alt) read as their plain codes. */
static int original_code(int k)
{
    if (k < 0x100) return k;
    int s = k & 0xff;
    switch (s) {
    case 0x47: case 0x48: case 0x49: case 0x4b: case 0x4d: case 0x4f: case 0x50: case 0x51: case 0x52:
    case 0x53:
        return k;
    }
    return s;
}

void input_feed_held(const uint8_t held[KEY_COUNT])
{
    for (int k = 0; k < KEY_COUNT; k++) {
        if (!held[k] == !host_held[k]) continue;
        host_held[k] = held[k] ? 1 : 0;
        input_post_key(original_code(k) + (held[k] ? 0 : 0x80));
    }
}

/* ---- bindings ---- */

/* Config_MapControlKeys 0x46e960: registry value v -> key: below 0x80 as is, 0x80..0xff the extended
   keys (+0x80: DirectInput's 0xc8 Up is 0x148), 0x100..0x11f joystick buttons (+0x2f1: 0x3f1..),
   0x140..0x145 joystick axes (+0x2a9: 0x3e9..0x3ee), anything else 0. Registry entry i goes to the
   action in the table 0x4a8c58 (read from the exe). */
static void config_map_control_keys(int32_t keys[INPUT_ACTIONS])
{
    for (int i = 0; i < INPUT_ACTIONS; i++) {
        int v = g_config_controls[i];
        if (v < 0x100) {
            if (v > 0x7f) v += 0x80;
        } else if (v < 0x120) {
            v += 0x2f1;
        } else if (v < 0x140 || v > 0x145) {
            v = 0;
        } else {
            v += 0x2a9;
        }
        int a = (int)exe_u32(0x4a8c58 + 4 * (uint32_t)i);
        if (a >= 0 && a < INPUT_ACTIONS) keys[a] = v;
    }
}

/* Input_Init 0x432b40: Joy_Init, the bindings; an action below 0x3e9 means the keyboard is used,
   above it the joystick (its axes and buttons marked used). The port has no joystick: bindings to it
   never fire. */
void input_init(void)
{
    in.init = true;
    config_map_control_keys(g_input_keys);
    in.keyboard = in.joystick = false;
    for (int i = 0; i < INPUT_ACTIONS; i++) {
        if (g_input_keys[i] < 0x3e9) in.keyboard = true;
        else in.joystick = true;
    }
}

void input_reset_state(void)
{
    bool init = in.init, kb = in.keyboard, joy = in.joystick;
    memset(&in, 0, sizeof in);
    in.init = init, in.keyboard = kb, in.joystick = joy;
}

/* the value fields of the word */
static void put_steer(uint32_t *w) { *w = (*w & 0xffffe1ffu) | 1 | ((uint32_t)(in.steer & 0xf) << 9); }
static void put_accel(uint32_t *w) { *w = (*w & 0xfffe7fffu) | 2 | ((uint32_t)(in.accel & 3) << 15); }
static void put_brake(uint32_t *w) { *w = (*w & 0xffff9fffu) | 0x80 | ((uint32_t)(in.brake & 3) << 13); }

/* Input_ActionPressed 0x4331f0: left / right add -7 / +7 to the steering (from 0 or from the other
   side: both held cancel out), accelerate / brake go to 3, the switches to held. Repeats are ignored. */
void input_action_pressed(int a, uint32_t *w)
{
    switch (a) {
    case INPUT_LEFT: if (in.steer > -7) in.steer -= 7, put_steer(w); break;
    case INPUT_RIGHT: if (in.steer < 7) in.steer += 7, put_steer(w); break;
    case INPUT_ACCEL: if (in.accel < 3) in.accel = 3, put_accel(w); break;
    case INPUT_BRAKE: if (in.brake < 3) in.brake = 3, put_brake(w); break;
    case INPUT_SPACE: if (in.space != 1) in.space = 1, *w |= 0x100004; break;
    case INPUT_FIRE: if (in.fire != 1) in.fire = 1, *w |= 0x200008; break;
    case INPUT_ENTER: if (in.enter != 1) in.enter = 1, *w |= 0x400000; break;
    case INPUT_SPECIAL: if (in.special != 1) in.special = 1, *w |= 0x80020; break;
    case INPUT_NEXT_WEAPON: if (in.next != 1) in.next = 1, *w |= 0x40010; break;
    case INPUT_PREV_WEAPON: if (in.prev != 1) in.prev = 1, *w |= 0x20100; break;
    }
}

/* Input_ActionReleased 0x4333c0: the reverse; enter has no release event (only its held flag clears). */
void input_action_released(int a, uint32_t *w)
{
    switch (a) {
    case INPUT_LEFT: if (in.steer < 7) in.steer += 7, put_steer(w); break;
    case INPUT_RIGHT: if (in.steer > -7) in.steer -= 7, put_steer(w); break;
    case INPUT_ACCEL: if (in.accel > 0) in.accel = 0, put_accel(w); break;
    case INPUT_BRAKE: if (in.brake > 0) in.brake = 0, put_brake(w); break;
    case INPUT_SPACE: if (in.space) in.space = 0, *w = (*w & 0xffefffffu) | 4; break;
    case INPUT_FIRE: if (in.fire) in.fire = 0, *w = (*w & 0xffdfffffu) | 8; break;
    case INPUT_ENTER: in.enter = 0; break;
    case INPUT_SPECIAL: if (in.special) in.special = 0, *w = (*w & 0xfff7ffffu) | 0x20; break;
    case INPUT_NEXT_WEAPON: if (in.next) in.next = 0, *w = (*w & 0xfffbffffu) | 0x10; break;
    case INPUT_PREV_WEAPON: if (in.prev) in.prev = 0, *w = (*w & 0xfffdffffu) | 0x100; break;
    }
}

static uint32_t key_event(uint32_t w, int k) { return w | (uint32_t)k << 23 | 0x40; }

/* Input_ReadControls 0x432e00.
   Recording: the queued key events in order; a bound key's press / release becomes its action
   (a release only if the press was seen), anything else (or a key the HUD wants: HUD_WantsKey
   0x482c80) goes into the high bits as a key event, which ends the frame's events (one key event
   per frame; the rest stay queued). Then the joystick (not ported). A non-zero word is recorded,
   except that a key event that playback would let through anyway is left out of the record.
   Playback: until the next record's frame, only the passthrough keys are read (with 0x5031f4 any key
   abandons); at its frame the record is the word, and when it has no key event the frame's key may
   still be a passthrough one. After the last record a single player game goes back to recording
   (mode 0), a network game stops (mode 2). */
uint32_t input_read_controls(void)
{
    if (!in.init) input_init();
    uint32_t w = 0;
    Replay *r = &g_replay;
    if (r->mode == REPLAY_RECORDING) {
        for (int k = input_get_key(); k != 0; k = input_get_key()) {
            bool bound = false;
            if (!hud_wants_key(k) && in.keyboard) {
                for (int i = 0; i < INPUT_ACTIONS; i++) {
                    if (k == g_input_keys[i]) {
                        in.held[i] = 1;
                        input_action_pressed(i, &w);
                        bound = true;
                    } else if (k == g_input_keys[i] + 0x80 && in.held[i]) {
                        in.held[i] = 0;
                        input_action_released(i, &w);
                        bound = true;
                    }
                }
            }
            if (!bound) w = key_event(w, k);
            if (w & 0x40) break;
        }
        /* (joystick: Joy_Poll and the bound buttons / axes, Input_ActionAnalog 0x4335a0; not ported) */
        uint32_t rec = w;
        if (rec != 0) {
            if ((rec & 0x40) && replay_is_passthrough_key((int)(rec >> 23))) rec &= 0x7fffbf;
            if (rec != 0) replay_record(rec);
        }
    } else if (r->mode == REPLAY_PLAYING) {
        if (r->frame < r->rec[r->pos * 2]) {
            int k = input_get_key();
            if (k != 0 && g_replay_any_key_quits) {
                game_request_abandon();
                return w;
            }
            if (replay_is_passthrough_key(k)) return key_event(w, k);
        } else {
            w = r->rec[r->pos * 2 + 1];
            r->pos++;
            if (r->pos >= r->count) r->mode = g_player_count != 1 ? REPLAY_DONE : REPLAY_RECORDING;
            if (!(w & 0x40)) {
                int k = input_get_key();
                if (k != 0 && g_replay_any_key_quits) {
                    game_request_abandon();
                    return w;
                }
                if (replay_is_passthrough_key(k)) return key_event(w, k);
            }
        }
    }
    return w;
}
