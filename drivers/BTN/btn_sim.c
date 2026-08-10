#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <errno.h>

#include "BTN.h"

LOG_MODULE_REGISTER(btn_sim, LOG_LEVEL_INF);

/* -------------------------------------------------------------------------- */
/* Configuration                                                              */
/* -------------------------------------------------------------------------- */

#define BTN_THREAD_STACK_SIZE 1024
#define BTN_THREAD_PRIORITY   5
#define BTN_INPUT_SLEEP_MS    10

/* -------------------------------------------------------------------------- */
/* Button state                                                                */
/* -------------------------------------------------------------------------- */

static atomic_t buttons[NUM_BTNS];

/* -------------------------------------------------------------------------- */
/* Keyboard mapping                                                            */
/* -------------------------------------------------------------------------- */

struct btn_key_map
{
    char key;
    btn_id btn;
};

static const struct btn_key_map key_map[] = {
    {'q', BTN0},
    {'w', BTN1},
    {'a', BTN2},
    {'s', BTN3},
};

/* -------------------------------------------------------------------------- */
/* Terminal handling                                                           */
/* -------------------------------------------------------------------------- */

static struct termios original_terminal;
static int original_stdin_flags;
static bool terminal_configured;

static void terminal_restore(void)
{
    if (!terminal_configured)
    {
        return;
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &original_terminal);
    fcntl(STDIN_FILENO, F_SETFL, original_stdin_flags);

    terminal_configured = false;
}

static bool terminal_configure(void)
{
    struct termios terminal;

    if (tcgetattr(STDIN_FILENO, &original_terminal) != 0)
    {
        LOG_ERR("Failed to get terminal settings");
        return false;
    }

    original_stdin_flags = fcntl(STDIN_FILENO, F_GETFL);

    if (original_stdin_flags < 0)
    {
        LOG_ERR("Failed to get stdin flags");
        return false;
    }

    terminal = original_terminal;

    /*
     * Deliver characters immediately instead of waiting for Enter.
     */
    terminal.c_lflag &= ~(ICANON | ECHO);

    /*
     * Treat Ctrl+C as a normal character.
     *
     * The keyboard thread handles it and raises SIGINT after restoring
     * the terminal.
     */
    terminal.c_lflag &= ~ISIG;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &terminal) != 0)
    {
        LOG_ERR("Failed to configure terminal");
        return false;
    }

    /*
     * IMPORTANT:
     *
     * Never block the native_sim host thread in read()/poll().
     * Make stdin non-blocking and let the Zephyr thread sleep instead.
     */
    if (fcntl(STDIN_FILENO, F_SETFL, original_stdin_flags | O_NONBLOCK) != 0)
    {
        LOG_ERR("Failed to make stdin non-blocking");

        tcsetattr(STDIN_FILENO, TCSANOW, &original_terminal);

        return false;
    }

    terminal_configured = true;

    return true;
}

/* -------------------------------------------------------------------------- */
/* Keyboard thread                                                             */
/* -------------------------------------------------------------------------- */

static void btn_keyboard_thread(void* p1, void* p2, void* p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    if (!terminal_configure())
    {
        return;
    }

    printk("Keyboard input started\n");
    printk("Press q/w/a/s for BTN0/BTN1/BTN2/BTN3\n");
    printk("Press Ctrl+C to exit\n");

    while (true)
    {
        char c;

        /*
         * stdin is non-blocking, so this can never stall native_sim.
         */
        ssize_t ret = read(STDIN_FILENO, &c, 1);

        if (ret == 1)
        {
            /*
             * Ctrl+C
             */
            if (c == 0x03)
            {
                terminal_restore();

                /*
                 * Let native_sim terminate normally.
                 */
                raise(SIGINT);

                return;
            }

            for (size_t i = 0; i < ARRAY_SIZE(key_map); i++)
            {
                if (c == key_map[i].key)
                {
                    btn_id btn = key_map[i].btn;

                    atomic_set(&buttons[btn], true);

                    LOG_INF("BTN%d pressed", btn);

                    break;
                }
            }
        }
        else if (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        {
            LOG_ERR("stdin read failed: errno=%d", errno);
            break;
        }

        /*
         * Always yield to the rest of the simulated system.
         */
        k_msleep(BTN_INPUT_SLEEP_MS);
    }

    terminal_restore();
}

/* -------------------------------------------------------------------------- */
/* Thread                                                                      */
/* -------------------------------------------------------------------------- */

K_THREAD_STACK_DEFINE(btn_keyboard_stack, BTN_THREAD_STACK_SIZE);

static struct k_thread btn_keyboard_tid;

/* -------------------------------------------------------------------------- */
/* Public API                                                                  */
/* -------------------------------------------------------------------------- */

int BTN_init(void)
{
    for (int i = 0; i < NUM_BTNS; i++)
    {
        atomic_clear(&buttons[i]);
    }

    k_thread_create(&btn_keyboard_tid,
                    btn_keyboard_stack,
                    BTN_THREAD_STACK_SIZE,
                    btn_keyboard_thread,
                    NULL,
                    NULL,
                    NULL,
                    BTN_THREAD_PRIORITY,
                    0,
                    K_NO_WAIT);

    LOG_INF("Button simulator initialized");

    return 0;
}

bool BTN_is_pressed(btn_id btn)
{
    if (btn >= NUM_BTNS)
    {
        return false;
    }

    return atomic_get(&buttons[btn]) != 0;
}

bool BTN_check_pressed(btn_id btn)
{
    if (btn >= NUM_BTNS)
    {
        return false;
    }

    return atomic_get(&buttons[btn]) != 0;
}

bool BTN_check_clear_pressed(btn_id btn)
{
    if (btn >= NUM_BTNS)
    {
        return false;
    }

    return atomic_clear(&buttons[btn]) != 0;
}

void BTN_clear_pressed(btn_id btn)
{
    if (btn >= NUM_BTNS)
    {
        return;
    }

    atomic_clear(&buttons[btn]);
}
