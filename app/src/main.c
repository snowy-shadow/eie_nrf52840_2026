/**
 * @file main.c
 */

#include <inttypes.h>
#ifdef CONFIG_BOARD_NATIVE_SIM
    #include <stdlib.h>
#endif

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "BTN.h"
#include "LED.h"

#define SLEEP_MS 500

int entry(void)
{
    if (BTN_init() < 0)
    {
        return 0;
    }

    if (LED_init() < 0)
    {
        return 0;
    }

    LED_pwm(LED0, 25);
    LED_pwm(LED1, 50);
    LED_pwm(LED2, 75);
    LED_pwm(LED3, 100);

    while (!BTN_is_pressed(BTN0))
    {
        k_msleep(SLEEP_MS);
    }

    return 0;
}

int main(void)
{
    int ret = entry();

#ifdef CONFIG_BOARD_NATIVE_SIM
    exit(ret);
#else
    return ret;
#endif
}
