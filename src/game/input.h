/* Controls (0x432b40-0x433bc0): the per-frame control word Input_ReadControls 0x432e00 builds from
   the key events of the frame, the ten key bindings and the key event queue the original gets from
   SciTech MGL (Input_GetKey 0x414a80). See docs/peds.md, "Control word".

   The control word is a set of change events, not a held state: a frame without changes is 0, and
   Player_ApplyInput 0x463ec0 keeps the held values in the player record (+0x191..+0x196).

     bit  0      steering changed: bits 9..12 the new value (4-bit two's complement, -7 left .. 7 right)
     bit  1      accelerate changed: bits 15..16 the new value (0 or 3)
     bit  7      brake changed: bits 13..14 the new value (0 or 3)
     bit  2      action 4 (Space) changed: bit 20 held (handbrake / jump: player +0x193)
     bit  3      fire changed: bit 21 held
     bit 22      enter / exit pressed (no release event)
     bit  5      action 7 (Tab) changed: bit 19 held (horn / special: player +0x196)
     bit  4      next weapon changed: bit 18 held
     bit  8      previous weapon changed: bit 17 held
     bit  6      a key event for Game_HandleKey: the scan code in bits 23..31 (+0x80 released)

   Keys are the original's codes: PC set-1 scan codes, +0x100 for the cursor block keys (0x47..0x53
   with no character, Input_GetKey), +0x80 for a release. */
#pragma once
#include "../platform.h"
#include <stdbool.h>
#include <stdint.h>

enum { INPUT_ACTIONS = 10 };

/* actions (the indices Input_ActionPressed 0x4331f0 switches on) */
enum {
    INPUT_LEFT, INPUT_RIGHT, INPUT_ACCEL, INPUT_BRAKE, INPUT_SPACE, INPUT_FIRE, INPUT_ENTER,
    INPUT_SPECIAL, INPUT_NEXT_WEAPON, INPUT_PREV_WEAPON,
};

/* Input_Init 0x432b40: the bindings (Config_MapControlKeys 0x46e960 of the registry's "Controls\Control
   0..9", Config_ReadRegistry 0x46e820), joystick use. Called once by WinMain (input_read_controls also
   runs it on first use). */
void input_init(void);
/* the registry values (0x753784): DirectInput key codes as GTA Settings writes them */
extern int32_t g_config_controls[INPUT_ACTIONS];
extern int32_t g_input_keys[INPUT_ACTIONS];  /* 0x513620: the key of each action (original codes) */

uint32_t input_read_controls(void);          /* Input_ReadControls 0x432e00 */
void input_action_pressed(int action, uint32_t *w);    /* Input_ActionPressed 0x4331f0 */
void input_action_released(int action, uint32_t *w);   /* Input_ActionReleased 0x4333c0 */
/* the state of the actions between frames (0x513650 steering, 0x5134d9 accelerate, ...), cleared by
   Replay_Begin */
void input_reset_state(void);

/* ---- the key event queue (MGL's event queue in the original) ---- */
void input_flush_keys(void);                 /* Input_FlushKeys 0x414a70 */
int input_get_key(void);                     /* Input_GetKey 0x414a80: next event code, 0 if none */
void input_post_key(int code);               /* queue an event (original code, +0x80 released) */
/* The host's held keys (platform.h codes: set-1 scan codes, +0x100 extended), once per host frame:
   the changes since the last call become press / release events. The app calls it before each
   game_run_step. */
void input_feed_held(const uint8_t held[KEY_COUNT]);
