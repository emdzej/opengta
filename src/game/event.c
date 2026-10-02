#include "event.h"
#include "game.h"
#include "mission_run.h"
#include "stubs.h"
#include "trigger.h"
#include <stddef.h>

uint32_t g_frame;

static EventNode sentinel;                   /* 0x4bbb88 */
static EventNode nodes[EVENT_NODES];         /* 0x4bbb9c */
static EventNode *free_list;                 /* 0x4bbb84 (linked through next) */
static EventNode *head;                      /* 0x4bbc64 */

/* Event_Init 0x401000. The sentinel's type and arg are not touched (0 from bss, kept from the last
   level otherwise); nothing reads them. */
void event_init(void)
{
    head = &sentinel;
    g_frame = 0;
    sentinel.time = 0xffffffffu;
    sentinel.next = sentinel.prev = NULL;
    free_list = &nodes[0];
    for (int i = 0; i < EVENT_NODES - 1; i++) nodes[i].next = &nodes[i + 1];
    nodes[EVENT_NODES - 1].next = NULL;
}

/* Links n (due at `due`) in front of the first queued node due at or after it: events due on the same
   frame run in reverse order of scheduling. */
static void insert(EventNode *n, uint32_t due, int type, int arg)
{
    EventNode *at = head;
    while (at->time < due) at = at->next;
    n->time = due;
    n->type = (uint32_t)type;
    n->arg = (uint32_t)arg;
    n->next = at;
    n->prev = at->prev;
    at->prev = n;
    if (n->prev) n->prev->next = n;
    else head = n;
}

/* Event_Schedule 0x401050 */
void event_schedule(int delay, int type, int arg)
{
    if (!free_list) game_fatal(-0x4d, 0x50, 0);   /* no free event node */
    EventNode *n = free_list;
    free_list = n->next;
    insert(n, g_frame + (uint32_t)delay, type, arg);
}

/* Event_ScheduleExit 0x4010d0: a game-end event (type 1). If one is queued already it stays, unless its
   arg is negative and the new arg positive: then it is moved to the new time with the new arg. */
void event_schedule_exit(int delay, int arg)
{
    for (EventNode *n = head; n; n = n->next) {
        if (n->type != EVENT_GAME_END) continue;
        if ((int32_t)n->arg >= 0 || arg < 1) return;
        if (n->prev) n->prev->next = n->next;
        else head = n->next;
        if (n->next) n->next->prev = n->prev;
        insert(n, g_frame + (uint32_t)delay, EVENT_GAME_END, arg);   /* (via the free list in the original) */
        return;
    }
    event_schedule(delay, EVENT_GAME_END, arg);
}

/* Event_Dispatch 0x401200 */
void event_dispatch(int type, int arg)
{
    switch (type) {
    case EVENT_BRIEF_DONE: mission_on_brief_done(arg); break;
    case EVENT_GAME_END: game_request_end(arg); break;
    case EVENT_DOOR_CLOSED: door_on_closed(arg); break;
    case EVENT_DOOR_OPENED: door_on_opened(arg); break;
    case EVENT_TRIGGER_RESET: trigger_reset(arg); break;
    case EVENT_NONE: break;
    default: game_fatal(-0x4a, 0x4f, type);   /* invalid case */
    }
}

/* Event_Tick 0x401280: the due events go back to the free list before their handler runs. */
void event_tick(void)
{
    while (head->time == g_frame) {
        EventNode *n = head;
        head = n->next;
        head->prev = NULL;
        n->next = free_list;
        free_list = n;
        event_dispatch((int)n->type, (int)n->arg);
    }
    g_frame++;
}

int event_pending(void)
{
    int k = 0;
    for (EventNode *n = head; n && n != &sentinel; n = n->next) k++;
    return k;
}

const EventNode *event_head(void) { return head; }
