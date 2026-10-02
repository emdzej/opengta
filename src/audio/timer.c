/* The 70 Hz AIL timer 0x47dc00-0x47dcb0. Game_Run 0x4148a0 starts it before its loop and calls
   Timer_WaitTicks(3) once per frame, after reading the controls: the in-game frame rate is 70 / 3 =
   23.33 Hz (42.86 ms a frame, 945 frames of 22050 Hz audio) whenever the frame's work takes less than
   that. The count restarts after each wait, so a slower frame is not made up for: frames take a whole
   number of 1/70 s ticks, at least 3. The timer only exists with sound on (0x502f6c) and 0x5031d0 clear;
   without it the game loop runs unthrottled. (The frontend's pacing is WinMain's Sleep to 35 ms.)

   The callback runs on the Miles timer; here on the mixer's clock (mss_render), so the ticks count
   audio actually rendered. */
#include "audio.h"
#include "mss.h"
#include "snd_internal.h"

static int handle = -1;                  /* 0x77557c */
static uint32_t ticks;                   /* 0x775580 */
static bool running;                     /* 0x775584 */
static bool enabled;                     /* 0x502f34 (Game_Run sets it) */
static bool timer_off;                   /* 0x5031d0 (a launch option) */

static void tick(void *user)             /* 0x47dc70 */
{
    (void)user;
    ticks++;
}

/* Timer_Start 0x47dc00 */
void Timer_Start(void)
{
    if (!sndg.opt.sound || timer_off) return;
    handle = mss_register_timer(tick, NULL);
    if (handle == -1) return;            /* Error_Fatal(-151, 0xb6) */
    mss_set_timer_frequency(handle, TIMER_HZ);
    ticks = 0;
    mss_start_timer(handle);
    running = true;
}

/* Timer_WaitTicks 0x47dc80, without the busy wait: see audio.h. */
bool Timer_WaitTicks(unsigned n)
{
    if (!enabled || !running) return true;
    if (ticks < n) return false;
    ticks = 0;
    return true;
}

/* Timer_Stop 0x47dcb0 */
void Timer_Stop(void)
{
    if (!running) return;
    mss_stop_timer(handle);
    mss_release_timer(handle);
    handle = -1;
    running = false;
}

void Timer_SetEnabled(bool on) { enabled = on; }
uint32_t Timer_Ticks(void) { return ticks; }
