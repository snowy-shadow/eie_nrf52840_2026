#include "Graphic.h"

#include <lvgl.h>
#include <zephyr/drivers/display.h>
#include "lv_data_obj.h"

namespace Graphic
{
const device* display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static lv_obj_t* screen = nullptr;

bool Init()
{
    lv_init();
    bool Status = device_is_ready(display_dev);
    screen = lv_screen_active();
    Status &= (screen != nullptr);

    if(Status)
    {
        /* make sure the display isn't blanked (it defaults to blanked) */
        display_blanking_off(display_dev);
    }
    return Status;
}

void Render()
{
    lv_timer_handler();
}
}