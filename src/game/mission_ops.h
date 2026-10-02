/* The opcode handlers that have their own function in the original (MissionOp_* 0x43e980-0x4453c0),
   called by Mission_StepProcess (mission_run.c). They work on the interpreter's view of the running
   process in g_mission (cur, cur_pc, cur_player, cur_ped, cur_wait...). docs/missions.md. */
#pragma once
#include <stdint.h>

/* Mission_GetParkExitPos 0x440790: where PARK let the driver out (pixels) and the heading */
void mission_get_park_exit_pos(int32_t *x, int32_t *y, int16_t *angle);

void mission_op_locate_stopped(void);
void mission_op_destroy(void);
void mission_op_answer(void);
void mission_op_steal(void);
void mission_op_send_to(void);
void mission_op_make_obj(void);
void mission_op_m_phone(void);
void mission_op_goto(void);
void mission_op_crane(void);
void mission_op_park(void);
void mission_op_ped_on(void);
void mission_op_car_on(void);
void mission_op_ped_send_to(void);
void mission_op_do_repo(void);
void mission_op_start_model(void);
void mission_op_do_model(void);
void mission_op_return_control(void);
void mission_op_goto_dropoff(void);
void mission_op_model_hunt(void);
void mission_op_model_future(void);
void mission_op_change_ped_type(void);
void mission_op_ped_out_of_car(void);
void mission_op_get_driver_info(void);
void mission_op_kill_ped(void);
void mission_op_dummy_drive_on(void);
void mission_op_is_powerup_done(void);
void mission_op_frenzy_brief(void);
void mission_op_add_a_life(void);
void mission_op_kf_brief_timed(void);
void mission_op_kf_cancel_briefing(void);
void mission_op_kf_brief_general(void);
void mission_op_kf_cancel_general(void);
void mission_op_reset_kf(void);
void mission_op_wait_for_players(void);
void mission_op_red_arrow(void);
void mission_op_red_arrow_off(void);
void mission_op_is_a_train_wrecked(void);
void mission_op_inc_heads(void);
void mission_op_is_ped_stunned(void);
