/**
 * @file main.c
 */

extern "C"
{
#include "BTN.h"
#include "LED.h"
}
#include "BLE/BLE.h"

int main(void)
{
    if (0 > BTN_init())
    {
        return 0;
    }
    if (0 > LED_init())
    {
        return 0;
    }

    if (!ble::Init())
    {
        return 0;
    }

    while (1)
    {
        printk("here");
        k_msleep(200);
    }
    return 0;
}
