#pragma once

#include <stdbool.h>
#include <stdint.h>

void slot_machine_initialize(uint32_t seed);
void slot_machine_increase_multiplier();
void slot_machine_decrease_multiplier();
void slot_machine_start_spin(uint32_t now_ms);
void slot_machine_resolve_risk(bool choose_red);
void slot_machine_collect_risk();
void slot_machine_reset();
void slot_machine_step_animation(uint32_t now_ms);
bool slot_machine_is_animating();
bool slot_machine_is_risk_active();
bool slot_machine_consume_win_audio_request();