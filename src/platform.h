/* Platform contract: what the portable core needs from a host (backend: platform_gasm.c). The game data
   comes through the file layer (vfs.h). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Virtual pad buttons, the gasm ABI layout (SNES positions: A east, B south, X north, Y west). */
enum {
    PAD_A = 1 << 0, PAD_B = 1 << 1, PAD_X = 1 << 2, PAD_Y = 1 << 3, PAD_L = 1 << 4, PAD_R = 1 << 5,
    PAD_SELECT = 1 << 6, PAD_START = 1 << 7, PAD_UP = 1 << 8, PAD_DOWN = 1 << 9, PAD_LEFT = 1 << 10,
    PAD_RIGHT = 1 << 11,
};

/* Keys are PC set-1 scan codes as the original reads them from WM_KEYDOWN (lParam bits 16-23), with
   0x100 added for extended keys (bit 24): Enter 0x1c, keypad Enter 0x11c, Up 0x148, Left 0x14b. */
enum { KEY_COUNT = 0x200 };

void plat_log(const char *msg);
uint32_t plat_pad(int player);          /* stable within a frame */
/* Held keys: keys[code] = 1. False if the runner has no keyboard. */
bool plat_keys(uint8_t keys[KEY_COUNT]);
/* Key presses since the previous frame, in order (no auto-repeat); count, at most cap. */
int plat_key_presses(uint16_t *codes, int cap);
/* Launch parameter (gasm --param / URL query); false if unset. */
bool plat_param(const char *name, char *dst, size_t cap);
/* One frame of RGBA8 pixels (stride = w * 4). */
void plat_present(const uint32_t *rgba, int w, int h);
/* Persistent user files (saves, settings). */
uint8_t *plat_load_user_file(const char *name, size_t *size);
bool plat_save_user_file(const char *name, const void *data, size_t size);
