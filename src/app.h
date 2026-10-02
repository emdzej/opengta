/* The application: one call per frame from the platform backend. */
#pragma once
#include <stdbool.h>

/* The frontend paces its loop at one frame per 35 ms (WinMain 0x437230 sleeps the rest of 35 ms); the
   game waits for 3 ticks of the 70 Hz sound timer per frame (Game_Run 0x4148a0): the app runs at 70 Hz
   in game, one tick per call. */
#define APP_FRAME_HZ (1000.0 / 35.0)
#define APP_GAME_HZ 70.0
#define APP_AUDIO_RATE 22050

bool app_init(void);        /* the file layer is mounted; false = fatal (already logged) */
bool app_frame(void);       /* false = quit */
void app_exit(void);
/* Stereo float at APP_AUDIO_RATE: the frame's share of the mix (called after app_frame). */
void app_audio(float *out, unsigned frames);
