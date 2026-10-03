/* The intro movie player: Movie_PlayIntro 0x44b160 as a per-frame step (docs/movie.md).

   The original blocks in a loop of its own until the movie ends or a key is pressed. Here the
   frontend (front_init / front_frame) starts it and then calls movie_intro_step once per frontend
   frame (35 ms, APP_FRAME_HZ) until it returns false; the movie's frames are shown on the steps whose
   time has reached them (25 fps on 28.57 steps a second: one step in eight shows the previous frame again). */
#pragma once
#include "surface.h"
#include <stdbool.h>
#include <stdint.h>

/* Whether movie_intro_start plays the movie (default off, so that callers of front_init that expect
   the CD screen right away, like tests/frontend_test.c, are unaffected; the app turns it on). */
void movie_intro_set_enabled(bool on);
/* The first half of Movie_PlayIntro: opens GTADATA/MOVIE.SMK and the sound device. False if there is
   no movie (the original then just closes the sound device again and returns) or it is disabled. */
bool movie_intro_start(void);
/* One frontend frame of the movie loop. events are the key events of the frame in Input_GetKey 0x414a80
   form (scan code, +0x100 for the cursor / keypad keys, +0x80 for a release), in order. Draws the
   640 x 480 picture into s. False once the movie is over (the last frame, or a key other than Alt):
   the screen has been cleared and the movie closed. */
bool movie_intro_step(const uint16_t *events, int nevents, Surface *s);
/* Closes a movie still playing (application exit). */
void movie_intro_stop(void);
bool movie_intro_playing(void);
/* For tests: the frame shown last (-1 before the first), the frames shown so far. */
int movie_intro_frame(void);
