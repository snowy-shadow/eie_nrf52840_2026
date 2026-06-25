#include "slot_machine_logic.h"

#include <array>

#include "lvgl.h"
#include "slot_machine.h"

namespace
{

constexpr uint32_t kDefaultSeed         = 0x12345678u;
constexpr uint32_t kInitialMoney        = 1000;
constexpr uint32_t kInitialMultiplier   = 1;
constexpr uint32_t kInitialBaseBet      = 100;
constexpr uint32_t kPrepareDelayMs      = 800;
constexpr uint32_t kPrepareStepMs       = 150;
constexpr uint32_t kReelShuffleStepMs   = 55;
constexpr uint32_t kReelSpinDurationMs  = 800;
constexpr uint32_t kReelSpinStaggerMs   = 200;
constexpr uint32_t kRiskResultDisplayMs = 1200;

constexpr std::array<symbol_index_t, SLOT_COUNT> kDefaultSymbols = {0, 0, 0};
constexpr std::array<const char*, 4> kLoadingTexts = {"LOADING", "LOADING .", "LOADING ..", "LOADING ..."};

enum class AnimationPhase : uint8_t
{
    idle,
    preparing,
    spinning,
};

struct SpinOutcome
{
    bool is_win          = false;
    bool jackpot         = false;
    uint32_t payout      = 0;
    uint8_t winning_mask = 0;
};

struct AnimationState
{
    AnimationPhase phase                                 = AnimationPhase::idle;
    std::array<symbol_index_t, SLOT_COUNT> final_symbols = kDefaultSymbols;
    SpinOutcome outcome;
    uint8_t active_reel          = 0;
    uint32_t phase_started_ms    = 0;
    uint32_t last_visual_step_ms = 0;
};

struct RiskState
{
    bool active                = false;
    bool showing_result        = false;
    bool jackpot               = false;
    bool result_won            = false;
    bool result_red            = false;
    uint32_t payout            = 0;
    uint8_t winning_mask       = 0;
    uint32_t result_started_ms = 0;
};

struct SlotMachineState
{
    uint32_t total_money                                     = kInitialMoney;
    uint32_t multiplier                                      = kInitialMultiplier;
    uint32_t base_bet                                        = kInitialBaseBet;
    uint32_t rng_state                                       = kDefaultSeed;
    bool pending_win_audio                                   = false;
    std::array<symbol_index_t, SLOT_COUNT> displayed_symbols = kDefaultSymbols;
    AnimationState animation;
    RiskState risk;
};

SlotMachineState slot_machine_state;

constexpr uint32_t total_bet(const SlotMachineState& state) { return state.multiplier * state.base_bet; }

uint32_t elapsed_ms(uint32_t started_ms, uint32_t now_ms) { return now_ms - started_ms; }

bool is_animation_running(const SlotMachineState& state) { return state.animation.phase != AnimationPhase::idle; }

bool is_waiting_on_risk(const SlotMachineState& state) { return state.risk.active && !state.risk.showing_result; }

bool is_risk_result_visible(const SlotMachineState& state) { return state.risk.active && state.risk.showing_result; }

bool can_spin(const SlotMachineState& state)
{
    const uint32_t bet = total_bet(state);
    return !is_animation_running(state) && !state.risk.active && bet > 0 && bet <= state.total_money;
}

void seed_rng(SlotMachineState& state, uint32_t seed) { state.rng_state = seed == 0 ? kDefaultSeed : seed; }

uint32_t rng32(SlotMachineState& state)
{
    if (state.rng_state == 0)
        state.rng_state = kDefaultSeed;

    uint32_t value = state.rng_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    state.rng_state = value;
    return state.rng_state;
}

symbol_index_t random_symbol(SlotMachineState& state)
{
    return static_cast<symbol_index_t>(rng32(state) % SYMBOL_POOL_SIZE);
}

uint32_t current_reel_duration_ms(uint8_t reel_index)
{
    return kReelSpinDurationMs + (static_cast<uint32_t>(reel_index) * kReelSpinStaggerMs);
}

void reset_progress(SlotMachineState& state)
{
    state.total_money       = kInitialMoney;
    state.multiplier        = kInitialMultiplier;
    state.base_bet          = kInitialBaseBet;
    state.displayed_symbols = kDefaultSymbols;
    state.animation         = {};
    state.risk              = {};
}

void apply_value_labels(const SlotMachineState& state)
{
    update_money(state.total_money);
    update_bet_multiplier(state.multiplier);
    update_current_bet(state.base_bet);
}

void apply_displayed_symbols(const SlotMachineState& state) { update_slots(state.displayed_symbols.data()); }

void render_idle_ui(const SlotMachineState& state)
{
    apply_value_labels(state);
    apply_displayed_symbols(state);
    reset_slot_machine_visuals();
}

void render_prepare_ui(const SlotMachineState& state)
{
    set_spinning_reel(state.animation.active_reel);
    show_spin_status(kLoadingTexts[(state.animation.active_reel + 1u) % kLoadingTexts.size()]);
}

void render_spin_ui(const SlotMachineState& state)
{
    set_spinning_reel(state.animation.active_reel);
    show_spin_status("SPINNING");
    apply_displayed_symbols(state);
}

void render_spin_result_ui(const SlotMachineState& state)
{
    apply_value_labels(state);
    apply_displayed_symbols(state);
    show_spin_result(state.animation.outcome.is_win,
                     state.animation.outcome.jackpot,
                     state.animation.outcome.payout,
                     state.animation.outcome.winning_mask);
}

void render_risk_prompt_ui(const SlotMachineState& state)
{
    apply_value_labels(state);
    apply_displayed_symbols(state);
    show_risk_prompt(state.risk.payout, state.risk.winning_mask);
}

void render_risk_result_ui(const SlotMachineState& state)
{
    apply_value_labels(state);
    apply_displayed_symbols(state);
    show_risk_result(state.risk.result_won, state.risk.result_red, state.risk.payout, state.risk.winning_mask);
}

void activate_risk_mode(SlotMachineState& state)
{
    state.risk.active       = true;
    state.risk.jackpot      = state.animation.outcome.jackpot;
    state.risk.payout       = state.animation.outcome.payout;
    state.risk.winning_mask = state.animation.outcome.winning_mask;
}

SpinOutcome generate_spin(SlotMachineState& state, std::array<symbol_index_t, SLOT_COUNT>& result)
{
    SpinOutcome outcome;
    const bool win = (rng32(state) % 100) < 49;

    if (win)
    {
        const symbol_index_t symbol = random_symbol(state);
        if ((rng32(state) % 100) < 10)
        {
            result[0] = symbol;
            result[1] = symbol;
            result[2] = symbol;
        }
        else
        {
            result[0] = symbol;
            result[1] = symbol;
            result[2] = random_symbol(state);
            while (result[2] == symbol)
                result[2] = random_symbol(state);
        }
    }
    else
    {
        result[0] = random_symbol(state);
        do
        {
            result[1] = random_symbol(state);
        } while (result[1] == result[0]);
        do
        {
            result[2] = random_symbol(state);
        } while (result[2] == result[0] || result[2] == result[1]);
    }

    const uint32_t bet = total_bet(state);
    state.total_money -= bet;

    uint32_t payout = 0;
    if (result[0] == result[1] && result[1] == result[2])
    {
        payout = bet * 10;
        outcome.is_win       = true;
        outcome.jackpot      = true;
        outcome.winning_mask = 0b111;
    }
    else if (result[0] == result[1] || result[1] == result[2] || result[0] == result[2])
    {
        payout = bet * 2;
        outcome.is_win = true;
        if (result[0] == result[1])
        {
            outcome.winning_mask = 0b011;
        }
        else if (result[1] == result[2])
        {
            outcome.winning_mask = 0b110;
        }
        else
        {
            outcome.winning_mask = 0b101;
        }
    }

    outcome.payout = payout;

    if (outcome.is_win && state.multiplier < 5)
    {
        const uint32_t net_win  = payout > bet ? (payout - bet) : 0;
        const uint32_t increase = net_win / 10;
        if (increase > 0)
            state.base_bet += increase;
    }

    return outcome;
}

void begin_animation(SlotMachineState& state,
                     const std::array<symbol_index_t, SLOT_COUNT>& final_symbols,
                     const SpinOutcome& outcome,
                     uint32_t now_ms)
{
    state.animation.phase               = AnimationPhase::preparing;
    state.animation.final_symbols       = final_symbols;
    state.animation.outcome             = outcome;
    state.animation.active_reel         = 0;
    state.animation.phase_started_ms    = now_ms;
    state.animation.last_visual_step_ms = now_ms;
}

} // namespace

void slot_machine_initialize(uint32_t seed)
{
    reset_progress(slot_machine_state);
    seed_rng(slot_machine_state, seed);
    render_idle_ui(slot_machine_state);
}

void slot_machine_increase_multiplier()
{
    if (is_animation_running(slot_machine_state) || slot_machine_state.risk.active)
        return;

    const uint32_t next_multiplier = slot_machine_state.multiplier + 1;
    if ((next_multiplier * slot_machine_state.base_bet) > slot_machine_state.total_money)
        return;

    slot_machine_state.multiplier = next_multiplier;
    apply_value_labels(slot_machine_state);
}

void slot_machine_decrease_multiplier()
{
    if (is_animation_running(slot_machine_state) || slot_machine_state.risk.active ||
        slot_machine_state.multiplier <= 1)
        return;

    --slot_machine_state.multiplier;
    apply_value_labels(slot_machine_state);
}

void slot_machine_start_spin(uint32_t now_ms)
{
    if (is_waiting_on_risk(slot_machine_state))
    {
        render_risk_prompt_ui(slot_machine_state);
        return;
    }

    if (!can_spin(slot_machine_state))
        return;

    std::array<symbol_index_t, SLOT_COUNT> final_symbols;
    const SpinOutcome outcome = generate_spin(slot_machine_state, final_symbols);
    begin_animation(slot_machine_state, final_symbols, outcome, now_ms);
    render_prepare_ui(slot_machine_state);
}

void slot_machine_resolve_risk(bool choose_red)
{
    if (is_animation_running(slot_machine_state) || !is_waiting_on_risk(slot_machine_state))
        return;

    const bool actual_red = (rng32(slot_machine_state) & 1u) != 0;
    const bool won        = choose_red == actual_red;

    if (won)
    {
        slot_machine_state.total_money += (slot_machine_state.risk.payout * 2u);
        slot_machine_state.pending_win_audio = true;
    }

    slot_machine_state.risk.showing_result    = true;
    slot_machine_state.risk.result_won        = won;
    slot_machine_state.risk.result_red        = actual_red;
    slot_machine_state.risk.result_started_ms = lv_tick_get();
    apply_value_labels(slot_machine_state);
    apply_displayed_symbols(slot_machine_state);
    render_risk_result_ui(slot_machine_state);
}

void slot_machine_collect_risk()
{
    if (is_animation_running(slot_machine_state) || !is_waiting_on_risk(slot_machine_state))
        return;

    slot_machine_state.total_money += slot_machine_state.risk.payout;
    slot_machine_state.pending_win_audio = true;
    slot_machine_state.risk = {};
    render_idle_ui(slot_machine_state);
}

void slot_machine_reset()
{
    reset_progress(slot_machine_state);
    render_idle_ui(slot_machine_state);
}

void slot_machine_step_animation(uint32_t now_ms)
{
    if (is_risk_result_visible(slot_machine_state))
    {
        if (elapsed_ms(slot_machine_state.risk.result_started_ms, now_ms) >= kRiskResultDisplayMs)
        {
            slot_machine_state.risk = {};
            render_idle_ui(slot_machine_state);
        }
        return;
    }

    if (!is_animation_running(slot_machine_state))
        return;

    AnimationState& animation = slot_machine_state.animation;

    if (animation.phase == AnimationPhase::preparing)
    {
        if (elapsed_ms(animation.last_visual_step_ms, now_ms) >= kPrepareStepMs)
        {
            animation.active_reel         = static_cast<uint8_t>((animation.active_reel + 1u) % SLOT_COUNT);
            animation.last_visual_step_ms = now_ms;
            render_prepare_ui(slot_machine_state);
        }

        if (elapsed_ms(animation.phase_started_ms, now_ms) < kPrepareDelayMs)
            return;

        animation.phase               = AnimationPhase::spinning;
        animation.active_reel         = 0;
        animation.phase_started_ms    = now_ms;
        animation.last_visual_step_ms = 0;
        render_spin_ui(slot_machine_state);
        return;
    }

    if (animation.last_visual_step_ms == 0 || elapsed_ms(animation.last_visual_step_ms, now_ms) >= kReelShuffleStepMs)
    {
        slot_machine_state.displayed_symbols[animation.active_reel] = random_symbol(slot_machine_state);
        animation.last_visual_step_ms                               = now_ms;
        render_spin_ui(slot_machine_state);
    }

    if (elapsed_ms(animation.phase_started_ms, now_ms) < current_reel_duration_ms(animation.active_reel))
        return;

    slot_machine_state.displayed_symbols[animation.active_reel] = animation.final_symbols[animation.active_reel];
    apply_displayed_symbols(slot_machine_state);

    ++animation.active_reel;
    if (animation.active_reel >= SLOT_COUNT)
    {
        animation.phase = AnimationPhase::idle;

        if (animation.outcome.is_win && animation.outcome.payout > 0)
        {
            activate_risk_mode(slot_machine_state);
            render_risk_prompt_ui(slot_machine_state);
            return;
        }

        render_spin_result_ui(slot_machine_state);
        return;
    }

    animation.phase_started_ms    = now_ms;
    animation.last_visual_step_ms = 0;
    render_spin_ui(slot_machine_state);
}

bool slot_machine_is_animating() { return is_animation_running(slot_machine_state); }

bool slot_machine_is_risk_active() { return is_waiting_on_risk(slot_machine_state); }

bool slot_machine_consume_win_audio_request()
{
    if (!slot_machine_state.pending_win_audio)
        return false;

    slot_machine_state.pending_win_audio = false;
    return true;
}
