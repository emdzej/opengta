/* The application: one call per frame from the platform backend. */
#pragma once
#include <stdbool.h>

/* The original paces its main loop at one frame per 35 ms (WinMain 0x437230 sleeps the rest of 35 ms). */
#define APP_FRAME_HZ (1000.0 / 35.0)
#define APP_AUDIO_RATE 22050

bool app_init(void);        /* the file layer is mounted; false = fatal (already logged) */
bool app_frame(void);       /* false = quit */
void app_exit(void);
/* Stereo float at APP_AUDIO_RATE: the frame's share of the mix (called after app_frame). */
void app_audio(float *out, unsigned frames);
