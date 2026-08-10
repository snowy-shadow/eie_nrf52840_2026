#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <stdint.h>
#include <errno.h>

#include "LED.h"

LOG_MODULE_REGISTER(led_sim, LOG_LEVEL_DBG);

/* --------------------------------------------------------------------------
 * LED state
 * -------------------------------------------------------------------------- */

static led_state led_states[NUM_LEDS];

static struct k_mutex led_mutex;

/* --------------------------------------------------------------------------
 * PWM configuration
 * -------------------------------------------------------------------------- */

struct led_pwm_config
{
    led_frequency frequency;
    uint8_t duty_cycle;
};

static struct led_pwm_config pwm_configs[NUM_LEDS];

/* --------------------------------------------------------------------------
 * LED threads
 * -------------------------------------------------------------------------- */

#define LED_THREAD_STACK_SIZE 512
#define LED_THREAD_PRIORITY   5

K_THREAD_STACK_ARRAY_DEFINE(led_thread_stacks, NUM_LEDS, LED_THREAD_STACK_SIZE);

static struct k_thread led_threads[NUM_LEDS];

/* --------------------------------------------------------------------------
 * LED thread
 * -------------------------------------------------------------------------- */

static void led_thread(void* p1, void* p2, void* p3)
{
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    led_id led = (led_id) (uintptr_t) p1;

    while (true)
    {
        led_frequency frequency;
        uint8_t duty_cycle;

        k_mutex_lock(&led_mutex, K_FOREVER);

        frequency  = pwm_configs[led].frequency;
        duty_cycle = pwm_configs[led].duty_cycle;

        k_mutex_unlock(&led_mutex);

        /*
         * 0% duty cycle -> always OFF.
         */
        if (duty_cycle == 0)
        {
            k_mutex_lock(&led_mutex, K_FOREVER);
            led_states[led] = LED_OFF;
            k_mutex_unlock(&led_mutex);

            k_msleep(100);
            continue;
        }

        /*
         * 100% duty cycle -> always ON.
         */
        if (duty_cycle >= 100)
        {
            k_mutex_lock(&led_mutex, K_FOREVER);
            led_states[led] = LED_ON;
            k_mutex_unlock(&led_mutex);

            k_msleep(100);
            continue;
        }

        /*
         * Calculate PWM period.
         *
         * 1 Hz  -> 1000 ms
         * 2 Hz  ->  500 ms
         * 4 Hz  ->  250 ms
         * 8 Hz  ->  125 ms
         * 16 Hz ->   62 ms
         */
        uint32_t period_ms = 1000U / frequency;

        uint32_t on_ms = (period_ms * duty_cycle) / 100U;

        uint32_t off_ms = period_ms - on_ms;

        /*
         * Avoid zero-length sleeps.
         */
        if (on_ms == 0)
        {
            on_ms = 1;
        }

        if (off_ms == 0)
        {
            off_ms = 1;
        }

        /*
         * ON
         */
        k_mutex_lock(&led_mutex, K_FOREVER);
        led_states[led] = LED_ON;
        k_mutex_unlock(&led_mutex);

        LOG_INF("LED%d -> ON", led);

        k_msleep(on_ms);

        /*
         * OFF
         */
        k_mutex_lock(&led_mutex, K_FOREVER);
        led_states[led] = LED_OFF;
        k_mutex_unlock(&led_mutex);

        LOG_INF("LED%d -> OFF", led);

        k_msleep(off_ms);
    }
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

int LED_init(void)
{
    k_mutex_init(&led_mutex);

    for (int i = 0; i < NUM_LEDS; i++)
    {
        led_states[i] = LED_OFF;

        /*
         * Default configuration.
         */
        pwm_configs[i].frequency  = LED_1HZ;
        pwm_configs[i].duty_cycle = 0;

        /*
         * Start one thread per LED.
         */
        k_thread_create(&led_threads[i],
                        led_thread_stacks[i],
                        LED_THREAD_STACK_SIZE,
                        led_thread,
                        (void*) (uintptr_t) i,
                        NULL,
                        NULL,
                        LED_THREAD_PRIORITY,
                        0,
                        K_NO_WAIT);
    }

    LOG_INF("LED simulator initialized");

    return 0;
}

int LED_toggle(led_id led)
{
    if (led >= NUM_LEDS)
    {
        return -EINVAL;
    }

    k_mutex_lock(&led_mutex, K_FOREVER);

    led_states[led] = (led_states[led] == LED_ON) ? LED_OFF : LED_ON;

    led_state state = led_states[led];

    k_mutex_unlock(&led_mutex);

    LOG_INF("LED%d -> %s", led, state == LED_ON ? "ON" : "OFF");

    return 0;
}

int LED_set(led_id led, led_state new_state)
{
    if (led >= NUM_LEDS || new_state > LED_ON)
    {
        return -EINVAL;
    }

    k_mutex_lock(&led_mutex, K_FOREVER);

    led_states[led] = new_state;

    k_mutex_unlock(&led_mutex);

    LOG_INF("LED%d -> %s", led, new_state == LED_ON ? "ON" : "OFF");

    return 0;
}

int LED_pwm(led_id led, uint8_t duty_cycle)
{
    if (led >= NUM_LEDS || duty_cycle > 100)
    {
        return -EINVAL;
    }

    k_mutex_lock(&led_mutex, K_FOREVER);

    pwm_configs[led].duty_cycle = duty_cycle;

    k_mutex_unlock(&led_mutex);

    LOG_INF("LED%d PWM duty = %u%%", led, duty_cycle);

    return 0;
}

void LED_blink(led_id led, led_frequency frequency)
{
    if (led >= NUM_LEDS)
    {
        return;
    }

    if (frequency < LED_1HZ || frequency > LED_16HZ)
    {
        return;
    }

    k_mutex_lock(&led_mutex, K_FOREVER);

    pwm_configs[led].frequency  = frequency;
    pwm_configs[led].duty_cycle = 50;

    k_mutex_unlock(&led_mutex);

    LOG_INF("LED%d blinking at %d Hz", led, frequency);
}
