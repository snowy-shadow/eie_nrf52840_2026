/**
 * @file main.c
 */

extern "C"
{
#include "BTN.h"
#include "LED.h"
}
#include <lvgl.h>
#include <zephyr/drivers/display.h>
#include "lv_data_obj.h"
#include "BLE/BLE.h"
#include "BLE/AudioService/Audio.h"
#include "BLE/ControllerService/Controller.h"

namespace
{
const device* display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
// singletons must be stored as references; the copy constructors are deleted
auto& BLE_Audio = ble::AudioService::GetInstance();
auto& BLE_Controller = ble::Controller::GetInstance();
}

bool Init()
{
    bool Status = (0 == BTN_init()) && (0 == LED_init()) && ble::Init() && device_is_ready(display_dev);
    lv_init();

    return Status;
}

int main(void)
{
    if (!Init())
    {
        return 0;
    }
    printk("booted");
    static auto* screen = lv_screen_active();

    lv_obj_t* label = lv_label_create(screen);
    lv_label_set_text(label, "Hello world");

    /* make sure the display isn't blanked (it defaults to blanked) */
    display_blanking_off(display_dev);
    while (true)
    {
        if(BTN_check_clear_pressed(BTN0))
        {
            printk("BTN0 pressed");
            BLE_Audio.Start();
        }
        const auto KeyPress = BLE_Controller.TakeKeyPress();
        if(KeyPress.has_value())
        {
            printk("KeyPress : %d", KeyPress.value_or(-1));
            switch(KeyPress.value())
            {
                case 'n':
                    BLE_Audio.Start();
                    break;
                default:
                    break;
            }
        }
        
        lv_timer_handler();
        k_msleep(100);
    }
    return 0;
}
