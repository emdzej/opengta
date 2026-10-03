/* The AI controller table 0x507ea0 (ai.h): the record accessors of the emergency services module
   (Sentinel_FindFree 0x41ad30, Sentinel_Get 0x41ad60, Sentinel_ClearTable 0x41adb0, Sentinel_Reset
   0x41aae0). The drivers that use the records (Sentinel_DriveCar and the police / ambulance / fire
   logic) are sentinel.c's; the mission dummies are dummy.c's. */
#include "ai.h"
#include "game.h"
#include <string.h>

AiCtl g_ai[AI_MAX];

/* Sentinel_Get 0x41ad60 */
uint8_t *sentinel_get(int i)
{
    int16_t n = (int16_t)i;
    if (n < 0) game_fatal(-0x91, 0xa6, n);
    if (n < AI_MAX) return (uint8_t *)&g_ai[n];
    return NULL;
}

/* Sentinel_FindFree 0x41ad30: the first record of 0..49 whose kind is 0 */
int sentinel_find_free(void)
{
    for (int i = 0; i < AI_FREE_SEARCH; i++)
        if (g_ai[i].kind == AI_KIND_FREE) return i;
    return -1;
}

/* the defaults both resets write; they differ in +0x08 (-1 / 0) and +0x1c (0 / 0xff) */
static void ai_defaults(AiCtl *r)
{
    r->kind = 0;
    r->u04 = 0;
    r->group = 0;
    r->arrived = r->u0d = r->u0e = r->u0f = r->u10 = r->u11 = r->u12 = r->u13 = 0;
    r->u14 = r->u16 = r->u18 = 0;
    r->u1a = 0;
    r->car = -1;
    r->u20 = r->u21 = r->u22 = 0;
    for (int k = 0; k < 5; k++) r->u24[k] = -1;
    r->path_progress = 0;
    r->u3c = 0;
    r->car_ptr = 0;
    r->u44 = -1;
    r->u46 = 0;
    r->path_index = -1;
    r->u4a = r->u4b = 0;
    r->u4c = 0;
    r->state = 0;
    r->u4e = 0;
    r->lights_state = r->lights_mode = r->lights_saved = r->lights_x = r->lights_y = 0;
    r->junction = r->foot_ped = r->u62 = -1;
    r->dest_x = r->dest_y = r->dest_z = 0;
    r->u68 = r->u6a = r->pursuit = -1;
    r->u6e = r->u6f = r->u70 = 0;
    r->u72 = r->u74 = 0;
    r->u76 = 0;
    memset(r->u77, 0, sizeof r->u77);
    r->u95 = r->u96 = 0;
}

/* Sentinel_ClearTable 0x41adb0: records 0..127 (the 129th is left alone); also +0x50..+0x56 = -1 */
void sentinel_clear_table(void)
{
    for (int i = 0; i < AI_TABLE; i++) {
        AiCtl *r = &g_ai[i];
        r->id = (int16_t)i;
        ai_defaults(r);
        r->path_slot = -1;
        r->sub_state = 0;
        r->u50 = r->u52 = r->u54 = r->u56 = -1;
    }
}

/* Sentinel_Reset 0x41aae0: one record (the id and +0x50..+0x56 are kept) */
void sentinel_reset(AiCtl *r)
{
    ai_defaults(r);
    r->path_slot = 0;
    r->sub_state = 0xff;
}
