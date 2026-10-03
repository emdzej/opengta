/* The ambulance crews (0x401340-0x401a7f): an ambulance (model 5) and its medic, driven by a sentinel
   of kind 1, sent from the nearest hospital to the peds of the ambulance call queue (sentinel.h); the
   medic walks to each body and revives it, then the crew drives back to the hospital. At most 10
   crews (0x50f290). See docs/police.md. */
#pragma once
#include "sentinel.h"
#include <stdint.h>

enum { AMBU_CREWS_MAX = 10 };

extern int16_t g_ambu_crews[AMBU_CREWS_MAX];   /* 0x50f290 sentinels of the active crews */
extern int16_t g_ambu_ncrews;                  /* 0x505840 */

void ambu_send_to_hospital(Sentinel *s);       /* Ambu_SendToHospital 0x401340 */
void ambu_assign_victim(SentRequest *r);       /* Ambu_AssignVictim 0x401460 */
int ambu_dispatch(int s, const SentRequest *r);   /* Ambu_Dispatch 0x4015d0: the car (-1 none) */
void ambu_remove(Sentinel *s);                 /* Ambu_Remove 0x401700 */
int ambu_update(Sentinel *s);                  /* Ambu_Update 0x401790: 1 to drive on */
