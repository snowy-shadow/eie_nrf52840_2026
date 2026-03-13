#include "slot_machine.h"
#include <cstdio>

lv_obj_t* slot_labels[SLOT_COUNT];
lv_obj_t* money_label;
lv_obj_t* bet_multiplier_label;
lv_obj_t* current_bet_label;
static lv_obj_t* reel_cards[SLOT_COUNT];
static lv_obj_t* reel_name_labels[SLOT_COUNT];
static lv_obj_t* reel_accent_bars[SLOT_COUNT];
static lv_obj_t* status_panel;
static lv_obj_t* status_label;
static lv_obj_t* risk_overlay;
static lv_obj_t* risk_title_label;
static lv_obj_t* risk_amount_label;
static lv_obj_t* risk_red_card;
static lv_obj_t* risk_blue_card;
static lv_obj_t* risk_cash_card;
static uint32_t reel_accent_colors[SLOT_COUNT];

namespace {

struct SymbolVisual {
    const char* code;
    const char* name;
    uint32_t accent_hex;
};

constexpr uint32_t kScreenTopHex = 0x071321;
constexpr uint32_t kScreenBottomHex = 0x163a54;
constexpr uint32_t kPanelHex = 0x0d1f32;
constexpr uint32_t kPanelHighlightHex = 0x1d3f5a;
constexpr uint32_t kTextPrimaryHex = 0xf3f6fb;
constexpr uint32_t kTextMutedHex = 0x8ea3b8;
constexpr uint32_t kSpinAccentHex = 0x7dd3fc;
constexpr uint32_t kWinAccentHex = 0xfacc15;
constexpr uint32_t kLossAccentHex = 0xfb7185;
constexpr uint32_t kRiskRedHex = 0xfb7185;
constexpr uint32_t kRiskBlueHex = 0x60a5fa;
constexpr uint32_t kRiskCashHex = 0x34d399;

const SymbolVisual kSymbolVisuals[SYMBOL_POOL_SIZE] = {
    { "CHR", "CHERRY", 0xf87171 },
    { "LEM", "LEMON", 0xfacc15 },
    { "ORG", "ORANGE", 0xfb923c },
    { "777", "SEVEN", 0x60a5fa },
};

static lv_color_t hex_color(uint32_t value)
{
    return lv_color_hex(value);
}

static void style_panel(lv_obj_t* obj, uint32_t bg_hex, lv_opa_t bg_opa, uint32_t border_hex, lv_opa_t border_opa)
{
    lv_obj_set_style_radius(obj, 22, 0);
    lv_obj_set_style_bg_opa(obj, bg_opa, 0);
    lv_obj_set_style_bg_color(obj, hex_color(bg_hex), 0);
    lv_obj_set_style_bg_grad_color(obj, hex_color(kPanelHighlightHex), 0);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_opa(obj, border_opa, 0);
    lv_obj_set_style_border_color(obj, hex_color(border_hex), 0);
    lv_obj_set_style_shadow_width(obj, 24, 0);
    lv_obj_set_style_shadow_spread(obj, 0, 0);
    lv_obj_set_style_shadow_color(obj, hex_color(0x000000), 0);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_40, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void create_backdrop_orb(lv_obj_t* parent, lv_align_t align, int32_t x_ofs, int32_t y_ofs, lv_coord_t size, uint32_t color_hex)
{
    lv_obj_t* orb = lv_obj_create(parent);
    lv_obj_set_size(orb, size, size);
    lv_obj_align(orb, align, x_ofs, y_ofs);
    lv_obj_set_style_radius(orb, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(orb, hex_color(color_hex), 0);
    lv_obj_set_style_bg_opa(orb, LV_OPA_20, 0);
    lv_obj_set_style_border_width(orb, 0, 0);
    lv_obj_set_style_shadow_width(orb, 0, 0);
    lv_obj_clear_flag(orb, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_background(orb);
}

static lv_obj_t* create_info_pill(lv_obj_t* parent, lv_coord_t width, lv_obj_t** out_label)
{
    lv_obj_t* pill = lv_obj_create(parent);
    lv_obj_set_size(pill, width, 24);
    lv_obj_set_style_radius(pill, 14, 0);
    lv_obj_set_style_bg_color(pill, hex_color(0x10263a), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_80, 0);
    lv_obj_set_style_border_width(pill, 1, 0);
    lv_obj_set_style_border_color(pill, hex_color(0x274766), 0);
    lv_obj_set_style_border_opa(pill, LV_OPA_60, 0);
    lv_obj_set_style_shadow_width(pill, 0, 0);
    lv_obj_set_style_pad_all(pill, 0, 0);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);

    *out_label = lv_label_create(pill);
    lv_obj_set_style_text_font(*out_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(*out_label, hex_color(kTextPrimaryHex), 0);
    lv_obj_center(*out_label);
    return pill;
}

static void apply_reel_idle_style(size_t reel_index)
{
    const lv_color_t accent = hex_color(reel_accent_colors[reel_index]);
    lv_obj_set_style_bg_color(reel_cards[reel_index], hex_color(0x102235), 0);
    lv_obj_set_style_bg_grad_color(reel_cards[reel_index], hex_color(0x173149), 0);
    lv_obj_set_style_border_color(reel_cards[reel_index], accent, 0);
    lv_obj_set_style_border_opa(reel_cards[reel_index], LV_OPA_50, 0);
    lv_obj_set_style_border_width(reel_cards[reel_index], 2, 0);
    lv_obj_set_style_shadow_color(reel_cards[reel_index], accent, 0);
    lv_obj_set_style_shadow_opa(reel_cards[reel_index], LV_OPA_20, 0);
    lv_obj_set_style_shadow_width(reel_cards[reel_index], 18, 0);
    lv_obj_set_y(reel_cards[reel_index], 0);
}

static void apply_symbol_visual(size_t reel_index, symbol_index_t symbol)
{
    const SymbolVisual fallback = { "???", "UNKNOWN", 0x94a3b8 };
    const SymbolVisual& visual = symbol < SYMBOL_POOL_SIZE ? kSymbolVisuals[symbol] : fallback;

    reel_accent_colors[reel_index] = visual.accent_hex;
    lv_label_set_text(slot_labels[reel_index], visual.code);
    lv_label_set_text(reel_name_labels[reel_index], visual.name);
    lv_obj_set_style_bg_color(reel_accent_bars[reel_index], hex_color(visual.accent_hex), 0);
    lv_obj_set_style_bg_grad_color(reel_accent_bars[reel_index], hex_color(0xffffff), 0);
    lv_obj_set_style_text_color(slot_labels[reel_index], hex_color(visual.accent_hex), 0);
    apply_reel_idle_style(reel_index);
}

static void set_status_palette(uint32_t panel_hex, uint32_t text_hex)
{
    lv_obj_set_style_bg_color(status_panel, hex_color(panel_hex), 0);
    lv_obj_set_style_bg_grad_color(status_panel, hex_color(kPanelHighlightHex), 0);
    lv_obj_set_style_text_color(status_label, hex_color(text_hex), 0);
}

static void style_risk_choice_card(lv_obj_t* card, uint32_t base_hex, uint32_t border_hex)
{
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_bg_color(card, hex_color(base_hex), 0);
    lv_obj_set_style_bg_grad_color(card, hex_color(kPanelHighlightHex), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, hex_color(border_hex), 0);
    lv_obj_set_style_border_opa(card, LV_OPA_90, 0);
    lv_obj_set_style_shadow_width(card, 18, 0);
    lv_obj_set_style_shadow_color(card, hex_color(border_hex), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_20, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t* create_risk_choice_card(
    lv_obj_t* parent,
    lv_coord_t width,
    lv_coord_t height,
    lv_align_t align,
    int32_t x_ofs,
    int32_t y_ofs,
    uint32_t base_hex,
    uint32_t border_hex,
    const char* title)
{
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, width, height);
    lv_obj_align(card, align, x_ofs, y_ofs);
    style_risk_choice_card(card, base_hex, border_hex);

    lv_obj_t* title_label = lv_label_create(card);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title_label, hex_color(kTextPrimaryHex), 0);
    lv_obj_center(title_label);
    return card;
}

static void hide_risk_overlay()
{
    lv_obj_add_flag(risk_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void show_risk_overlay()
{
    lv_obj_clear_flag(risk_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void set_risk_cards_prompt_style()
{
    style_risk_choice_card(risk_red_card, 0x3b1524, kRiskRedHex);
    style_risk_choice_card(risk_blue_card, 0x10243f, kRiskBlueHex);
    style_risk_choice_card(risk_cash_card, 0x103226, kRiskCashHex);
}

static void set_risk_cards_result_style(bool won, bool actual_red)
{
    style_risk_choice_card(risk_red_card, actual_red ? 0x4a1425 : 0x1a1f28, actual_red ? kRiskRedHex : 0x425466);
    style_risk_choice_card(risk_blue_card, !actual_red ? 0x11294a : 0x1a1f28, !actual_red ? kRiskBlueHex : 0x425466);
    style_risk_choice_card(risk_cash_card, won ? 0x123d28 : 0x3a1220, won ? kRiskCashHex : kLossAccentHex);
}

static void highlight_winning_reel(size_t reel_index)
{
    lv_obj_set_style_bg_color(reel_cards[reel_index], hex_color(0x2a2208), 0);
    lv_obj_set_style_bg_grad_color(reel_cards[reel_index], hex_color(0x4b3a0f), 0);
    lv_obj_set_style_border_color(reel_cards[reel_index], hex_color(kWinAccentHex), 0);
    lv_obj_set_style_border_opa(reel_cards[reel_index], LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(reel_cards[reel_index], 3, 0);
    lv_obj_set_style_shadow_color(reel_cards[reel_index], hex_color(kWinAccentHex), 0);
    lv_obj_set_style_shadow_opa(reel_cards[reel_index], LV_OPA_60, 0);
    lv_obj_set_style_shadow_width(reel_cards[reel_index], 24, 0);
    lv_obj_set_y(reel_cards[reel_index], -2);
}

static void highlight_winning_mask(uint8_t winning_mask)
{
    for (size_t i = 0; i < SLOT_COUNT; ++i)
        apply_reel_idle_style(i);

    for (size_t i = 0; i < SLOT_COUNT; ++i) {
        if ((winning_mask & (1u << i)) != 0)
            highlight_winning_reel(i);
    }
}

}

void create_slot_machine_gui()
{
    lv_obj_t* scr = lv_scr_act();

    lv_obj_set_style_bg_color(scr, hex_color(kScreenTopHex), 0);
    lv_obj_set_style_bg_grad_color(scr, hex_color(kScreenBottomHex), 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    create_backdrop_orb(scr, LV_ALIGN_TOP_LEFT, -20, -30, 75, 0x2dd4bf);
    create_backdrop_orb(scr, LV_ALIGN_BOTTOM_RIGHT, 22, 55, 90, 0x0ea5e9);
    create_backdrop_orb(scr, LV_ALIGN_CENTER, 0, -8, 105, 0x1d4ed8);

    lv_obj_t* top_panel = lv_obj_create(scr);
    lv_obj_set_size(top_panel, 300, 36);
    lv_obj_align(top_panel, LV_ALIGN_TOP_MID, 0, 12);
    style_panel(top_panel, kPanelHex, LV_OPA_80, 0x28506f, LV_OPA_70);
    lv_obj_move_foreground(top_panel);

    lv_obj_t* stats_row = lv_obj_create(top_panel);
    lv_obj_set_size(stats_row, 272, 22);
    lv_obj_align(stats_row, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(stats_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(stats_row, 0, 0);
    lv_obj_set_style_shadow_width(stats_row, 0, 0);
    lv_obj_set_style_pad_all(stats_row, 0, 0);
    lv_obj_set_style_pad_gap(stats_row, 10, 0);
    lv_obj_set_layout(stats_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(stats_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(stats_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(stats_row, LV_OBJ_FLAG_SCROLLABLE);

    create_info_pill(stats_row, 90, &money_label);
    create_info_pill(stats_row, 86, &current_bet_label);
    create_info_pill(stats_row, 80, &bet_multiplier_label);

    lv_obj_t* stage_panel = lv_obj_create(scr);
    lv_obj_set_size(stage_panel, 220, 152);
    lv_obj_align(stage_panel, LV_ALIGN_CENTER, 0, -2);
    style_panel(stage_panel, kPanelHex, LV_OPA_80, 0x315f82, LV_OPA_80);

    lv_obj_t* stage_glow = lv_obj_create(stage_panel);
    lv_obj_set_size(stage_glow, 184, 16);
    lv_obj_align(stage_glow, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_set_style_radius(stage_glow, 10, 0);
    lv_obj_set_style_bg_color(stage_glow, hex_color(0xffffff), 0);
    lv_obj_set_style_bg_opa(stage_glow, LV_OPA_10, 0);
    lv_obj_set_style_border_width(stage_glow, 0, 0);
    lv_obj_set_style_shadow_width(stage_glow, 0, 0);
    lv_obj_clear_flag(stage_glow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* reel_row = lv_obj_create(stage_panel);
    lv_obj_set_size(reel_row, 188, 104);
    lv_obj_align(reel_row, LV_ALIGN_CENTER, 0, 6);
    lv_obj_set_style_bg_opa(reel_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(reel_row, 0, 0);
    lv_obj_set_style_shadow_width(reel_row, 0, 0);
    lv_obj_set_style_pad_all(reel_row, 0, 0);
    lv_obj_set_style_pad_gap(reel_row, 6, 0);
    lv_obj_set_layout(reel_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(reel_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(reel_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(reel_row, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t i = 0; i < SLOT_COUNT; ++i)
    {
        reel_cards[i] = lv_obj_create(reel_row);
        lv_obj_set_size(reel_cards[i], 58, 100);
        style_panel(reel_cards[i], 0x102235, LV_OPA_COVER, 0x35597a, LV_OPA_70);

        reel_accent_bars[i] = lv_obj_create(reel_cards[i]);
        lv_obj_set_size(reel_accent_bars[i], 40, 5);
        lv_obj_align(reel_accent_bars[i], LV_ALIGN_TOP_MID, 0, 10);
        lv_obj_set_style_radius(reel_accent_bars[i], 4, 0);
        lv_obj_set_style_border_width(reel_accent_bars[i], 0, 0);
        lv_obj_set_style_shadow_width(reel_accent_bars[i], 0, 0);
        lv_obj_clear_flag(reel_accent_bars[i], LV_OBJ_FLAG_SCROLLABLE);

        slot_labels[i] = lv_label_create(reel_cards[i]);
        lv_obj_set_style_text_font(slot_labels[i], &lv_font_montserrat_24, 0);
        lv_obj_align(slot_labels[i], LV_ALIGN_CENTER, 0, -8);

        reel_name_labels[i] = lv_label_create(reel_cards[i]);
        lv_obj_set_style_text_font(reel_name_labels[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(reel_name_labels[i], hex_color(kTextMutedHex), 0);
        lv_obj_align(reel_name_labels[i], LV_ALIGN_BOTTOM_MID, 0, -8);

        reel_accent_colors[i] = kSymbolVisuals[i % SYMBOL_POOL_SIZE].accent_hex;
        apply_symbol_visual(i, static_cast<symbol_index_t>(i % SYMBOL_POOL_SIZE));
    }

    risk_overlay = lv_obj_create(stage_panel);
    lv_obj_set_size(risk_overlay, 188, 104);
    lv_obj_align(risk_overlay, LV_ALIGN_CENTER, 0, 6);
    style_panel(risk_overlay, 0x0f1d2f, LV_OPA_90, 0x9a7b16, LV_OPA_90);
    lv_obj_add_flag(risk_overlay, LV_OBJ_FLAG_HIDDEN);

    risk_title_label = lv_label_create(risk_overlay);
    lv_label_set_text(risk_title_label, "DOUBLE OR NOTHING");
    lv_obj_set_style_text_font(risk_title_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(risk_title_label, hex_color(kWinAccentHex), 0);
    lv_obj_align(risk_title_label, LV_ALIGN_TOP_MID, 0, 6);

    risk_amount_label = lv_label_create(risk_overlay);
    lv_label_set_text(risk_amount_label, "RISKING $0");
    lv_obj_set_style_text_font(risk_amount_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(risk_amount_label, hex_color(kTextPrimaryHex), 0);
    lv_obj_align(risk_amount_label, LV_ALIGN_TOP_MID, 0, 22);

    risk_red_card = create_risk_choice_card(
        risk_overlay,
        50,
        42,
        LV_ALIGN_BOTTOM_LEFT,
        6,
        -10,
        0x3b1524,
        kRiskRedHex,
        "RED");

    risk_blue_card = create_risk_choice_card(
        risk_overlay,
        50,
        42,
        LV_ALIGN_BOTTOM_RIGHT,
        -6,
        -10,
        0x10243f,
        kRiskBlueHex,
        "BLUE");

    risk_cash_card = create_risk_choice_card(
        risk_overlay,
        60,
        36,
        LV_ALIGN_BOTTOM_MID,
        0,
        -12,
        0x103226,
        kRiskCashHex,
        "CASH OUT");

    lv_obj_t* bottom_panel = lv_obj_create(scr);
    lv_obj_set_size(bottom_panel, 220, 44);
    lv_obj_align(bottom_panel, LV_ALIGN_BOTTOM_MID, 0, -12);
    style_panel(bottom_panel, kPanelHex, LV_OPA_80, 0x28506f, LV_OPA_70);

    status_panel = lv_obj_create(bottom_panel);
    lv_obj_set_size(status_panel, 192, 24);
    lv_obj_center(status_panel);
    lv_obj_set_style_radius(status_panel, 14, 0);
    lv_obj_set_style_border_width(status_panel, 1, 0);
    lv_obj_set_style_border_color(status_panel, hex_color(0x315f82), 0);
    lv_obj_set_style_border_opa(status_panel, LV_OPA_70, 0);
    lv_obj_set_style_shadow_width(status_panel, 0, 0);
    lv_obj_clear_flag(status_panel, LV_OBJ_FLAG_SCROLLABLE);

    status_label = lv_label_create(status_panel);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_center(status_label);

    show_spin_status("PRESS E TO SPIN");
}

void update_slots(const symbol_index_t symbols[SLOT_COUNT])
{
    for (size_t i = 0; i < SLOT_COUNT; ++i)
        apply_symbol_visual(i, symbols[i]);
}

void update_money(int money)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "BANK $%d", money);
    lv_label_set_text(money_label, buf);
}

void update_bet_multiplier(int multiplier)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "MULTI %dX", multiplier);
    lv_label_set_text(bet_multiplier_label, buf);
}

void update_current_bet(int bet)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "BET $%d", bet);
    lv_label_set_text(current_bet_label, buf);
}

void reset_slot_machine_visuals()
{
    for (size_t i = 0; i < SLOT_COUNT; ++i)
        apply_reel_idle_style(i);

    hide_risk_overlay();
    show_spin_status("PRESS E TO SPIN");
}

void set_spinning_reel(size_t active_reel)
{
    for (size_t i = 0; i < SLOT_COUNT; ++i) {
        if (i != active_reel) {
            apply_reel_idle_style(i);
            continue;
        }

        lv_obj_set_style_bg_color(reel_cards[i], hex_color(0x0e2a3c), 0);
        lv_obj_set_style_bg_grad_color(reel_cards[i], hex_color(0x144760), 0);
        lv_obj_set_style_border_color(reel_cards[i], hex_color(kSpinAccentHex), 0);
        lv_obj_set_style_border_opa(reel_cards[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(reel_cards[i], 3, 0);
        lv_obj_set_style_shadow_color(reel_cards[i], hex_color(kSpinAccentHex), 0);
        lv_obj_set_style_shadow_opa(reel_cards[i], LV_OPA_50, 0);
        lv_obj_set_style_shadow_width(reel_cards[i], 24, 0);
        lv_obj_set_y(reel_cards[i], -3);
    }
}

void show_spin_status(const char* text)
{
    hide_risk_overlay();
    lv_label_set_text(status_label, text);
    set_status_palette(0x10263a, kTextPrimaryHex);
}

void show_spin_result(bool is_win, bool jackpot, uint32_t payout, uint8_t winning_mask)
{
    char buf[48];

    hide_risk_overlay();
    highlight_winning_mask(is_win ? winning_mask : 0);

    if (is_win) {
        std::snprintf(buf, sizeof(buf), "%s +$%lu", jackpot ? "JACKPOT" : "PAIR HIT", static_cast<unsigned long>(payout));
        lv_label_set_text(status_label, buf);
        set_status_palette(jackpot ? 0x4b3a0f : 0x123d28, jackpot ? kWinAccentHex : 0x86efac);
    } else {
        lv_label_set_text(status_label, "NO MATCH  TRY AGAIN");
        set_status_palette(0x3a1220, kLossAccentHex);
    }
}

void show_risk_prompt(uint32_t payout, uint8_t winning_mask)
{
    char buf[32];

    highlight_winning_mask(winning_mask);
    show_risk_overlay();
    set_risk_cards_prompt_style();
    lv_label_set_text(risk_title_label, "DOUBLE OR NOTHING");
    std::snprintf(buf, sizeof(buf), "RISKING $%lu", static_cast<unsigned long>(payout));
    lv_label_set_text(risk_amount_label, buf);
    lv_label_set_text(status_label, "RISK MODE ACTIVE");
    set_status_palette(0x40250f, 0xfbbf24);
}

void show_risk_result(bool won, bool actual_red, uint32_t payout, uint8_t winning_mask)
{
    char buf[32];

    highlight_winning_mask(winning_mask);
    show_risk_overlay();
    set_risk_cards_result_style(won, actual_red);
    lv_label_set_text(risk_title_label, won ? "RISK WON" : "RISK LOST");
    std::snprintf(buf, sizeof(buf), "%c$%lu", won ? '+' : '-', static_cast<unsigned long>(payout));
    lv_label_set_text(risk_amount_label, buf);
    std::snprintf(buf, sizeof(buf), "%s WINS", actual_red ? "RED" : "BLUE");
    lv_label_set_text(status_label, buf);
    set_status_palette(won ? 0x123d28 : 0x3a1220, won ? 0x86efac : kLossAccentHex);
}
