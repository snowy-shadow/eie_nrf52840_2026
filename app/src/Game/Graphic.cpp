#include "Graphic.h"

#include <lvgl.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/printk.h>
#include "SlotMachine/slot_machine_logic.h"
#include "SlotMachine/slot_machine.h"

namespace Graphic
{
const device* display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static lv_obj_t* screen = nullptr;

bool Init()
{
    bool Status = device_is_ready(display_dev);
    screen = lv_screen_active();
    Status &= (screen != nullptr);

    if(!Status)
    {
        return false;
    }
    /* make sure the display isn't blanked (it defaults to blanked) */
    display_blanking_off(display_dev);
    printk("Graphic: create_gui\n");
    create_slot_machine_gui();
    printk("Graphic: sm_init\n");
    slot_machine_initialize(lv_tick_get());
    printk("Graphic: done\n");

    return true;
}

void Render()
{
    lv_timer_handler();
    slot_machine_step_animation(lv_tick_get());
}
}