/**
 * @file main.c
 */

extern "C"
{
#include "BTN.h"
}
#include "Game/Graphic.h"
#include "BLE/BLE.h"
#include "BLE/AudioService/Audio.h"
#include "BLE/ControllerService/Controller.h"
#include "assets/slot_spin_audio.h"
#include "assets/slot_win_audio.h"
#include "Game/SlotMachine/slot_machine_logic.h"
#include "lvgl.h"

namespace
{
// singletons must be stored as references; the copy constructors are deleted
auto& BLE_Audio      = ble::AudioService::GetInstance();
auto& BLE_Controller = ble::Controller::GetInstance();

void increase_multiplier_async(void*) { slot_machine_increase_multiplier(); }
void decrease_multiplier_async(void*) { slot_machine_decrease_multiplier(); }
void start_spin_async(void*)
{
    const bool was_animating = slot_machine_is_animating();

    slot_machine_start_spin(lv_tick_get());

    if (!was_animating && slot_machine_is_animating())
    {
        BLE_Audio.Start(slot_spin_audio, slot_spin_audio_len);
    }
}
void risk_red_async(void*) { slot_machine_resolve_risk(true); }
void risk_blue_async(void*) { slot_machine_resolve_risk(false); }
void collect_risk_async(void*) { slot_machine_collect_risk(); }
void reset_machine_async(void*) { slot_machine_reset(); }

bool Init() { return (0 == BTN_init()) && ble::Init() && Graphic::Init(); }

void input_listener(const char C)
{
    switch (C)
    {
        case 'q':
        case 'Q':
            lv_async_call(increase_multiplier_async, nullptr);
            break;

        case 'w':
        case 'W':
            lv_async_call(decrease_multiplier_async, nullptr);
            break;

        case 'e':
        case 'E':
            lv_async_call(start_spin_async, nullptr);
            break;

        case 'a':
        case 'A':
            lv_async_call(risk_red_async, nullptr);
            break;

        case 'd':
        case 'D':
            lv_async_call(risk_blue_async, nullptr);
            break;

        case 'c':
        case 'C':
            lv_async_call(collect_risk_async, nullptr);
            break;

        case 'r':
        case 'R':
            lv_async_call(reset_machine_async, nullptr);
            break;

        default:
            // ignore other keys
            break;
    }
}
} // namespace

int main(void)
{
    if (!Init())
    {
        return 0;
    }
    printk("booted");

    while (true)
    {
        if (BTN_check_clear_pressed(BTN0))
        {
            printk("BTN0 pressed");
            BLE_Audio.Start(slot_spin_audio, slot_spin_audio_len);
        }
        const auto KeyPress = BLE_Controller.TakeKeyPress();
        if (KeyPress.has_value())
        {
            printk("KeyPress : %d", KeyPress.value_or(-1));
            input_listener(KeyPress.value());
        }

        Graphic::Render();
        if (slot_machine_consume_win_audio_request())
        {
            BLE_Audio.Start(slot_win_audio, slot_win_audio_len);
        }
        k_msleep(12);  // ~60 FPS for smooth animation
    }
    return 0;
}
