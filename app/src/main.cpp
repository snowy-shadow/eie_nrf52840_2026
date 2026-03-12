/**
 * @file main.c
 */

extern "C"
{
#include "BTN.h"
#include "LED.h"
}
#include "Game/Graphic.h"
#include "BLE/BLE.h"
#include "BLE/AudioService/Audio.h"
#include "BLE/ControllerService/Controller.h"

namespace
{
// singletons must be stored as references; the copy constructors are deleted
auto& BLE_Audio = ble::AudioService::GetInstance();
auto& BLE_Controller = ble::Controller::GetInstance();
}

bool Init()
{
    return (0 == BTN_init()) && (0 == LED_init()) && ble::Init() && Graphic::Init();
}

int main(void)
{
    if (!Init())
    {
        return 0;
    }
    printk("booted");

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
        
        Graphic::Render();
        k_msleep(150);
    }
    return 0;
}
