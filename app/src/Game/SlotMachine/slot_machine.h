#pragma once
#include "lvgl.h"
#include <stddef.h>
#include <stdint.h>

#define SLOT_COUNT 3u
#define SYMBOL_POOL_SIZE 4u

// Shared slot machine types.
typedef uint8_t symbol_index_t;

// UI objects exposed for LVGL wiring.
extern lv_obj_t* slot_labels[SLOT_COUNT];
extern lv_obj_t* money_label;
extern lv_obj_t* bet_multiplier_label;
extern lv_obj_t* current_bet_label;

// UI functions.
void create_slot_machine_gui();
void update_slots(const symbol_index_t symbols[SLOT_COUNT]);

void update_money(int money);
void update_bet_multiplier(int multiplier);
void update_current_bet(int bet);
void reset_slot_machine_visuals();
void set_spinning_reel(size_t active_reel);
void show_spin_status(const char* text);
void show_spin_result(bool is_win, bool jackpot, uint32_t payout, uint8_t winning_mask);
void show_risk_prompt(uint32_t payout, uint8_t winning_mask);
void show_risk_result(bool won, bool actual_red, uint32_t payout, uint8_t winning_mask);
