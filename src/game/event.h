/* The frame clock and the delayed-event queue (0x401000-0x401280). Events fire on the frame they are
   due: a queue of 10 nodes {time, type, arg, next, prev} kept sorted by due frame and closed by a
   sentinel node due at 0xffffffff. Event_Tick runs once per Game_Update, dispatches every event due
   at the current frame and then advances the frame counter (0x4bbb80). */
#pragma once
#include <stdint.h>

enum { EVENT_NODES = 10 };

/* event types (Event_Dispatch 0x401200) */
enum {
    EVENT_BRIEF_DONE = 0,       /* Mission_OnBriefDone 0x445580 */
    EVENT_GAME_END = 1,         /* Game_RequestEnd 0x4309e0 */
    EVENT_DOOR_CLOSED = 2,      /* Door_OnClosed 0x474880 */
    EVENT_DOOR_OPENED = 3,      /* Door_OnOpened 0x4748c0 */
    EVENT_TRIGGER_RESET = 4,    /* Trigger_Reset 0x47baf0 */
    EVENT_NONE = 5,
};

typedef struct EventNode {
    uint32_t time;              /* due frame */
    uint32_t type, arg;
    struct EventNode *next, *prev;
} EventNode;

extern uint32_t g_frame;                     /* 0x4bbb80: frames since the level started */

void event_init(void);                       /* Event_Init 0x401000 */
void event_schedule(int delay, int type, int arg);   /* Event_Schedule 0x401050 */
void event_schedule_exit(int delay, int arg);        /* Event_ScheduleExit 0x4010d0 */
void event_dispatch(int type, int arg);      /* Event_Dispatch 0x401200 */
void event_tick(void);                       /* Event_Tick 0x401280 */
int event_pending(void);                     /* queued events (for checks) */
const EventNode *event_head(void);           /* 0x4bbc64 */
