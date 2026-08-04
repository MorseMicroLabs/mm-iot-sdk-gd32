/*
 * Copyright 2026 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * mmhal_app.c for mm-ekh18-gd32f527.
 *
 * LEDs:
 *   LED0 = PA2 (Red, used as error LED) -- active-LOW
 *   LED1 = PA3 (Green)                 -- active-LOW
 *   No blue LED available on this board.
 */

#include "mmhal_app.h"
#include "mm_hal_common.h"
#include "mmosal.h"
#include "mmutils.h"

#include "main.h"

static mmhal_button_state_cb_t mmhal_button_state_cb = NULL;

static time_t time_base_epoch = 0;
static uint32_t time_base_ms = 0;

void mmhal_set_led(uint8_t led, uint8_t level)
{
    switch (led)
    {
        case LED_GREEN:
            if (level)
            {
                gpio_bit_reset(LED_GREEN_GPIO_Port, LED_GREEN_Pin);
            }
            else
            {
                gpio_bit_set(LED_GREEN_GPIO_Port, LED_GREEN_Pin);
            }
            break;

        case LED_BLUE:
        case LED_RED:
        case LED_WHITE:
        default:
            /* Not available: this board has no GPIO-controllable blue, red, or white LED.
             * LED_RED is handled separately via mmhal_set_error_led(). */
            break;
    }
}

void mmhal_set_error_led(bool state)
{
    /* PA2 = LED0 (Red), active-LOW. This function may be called before main()'s BSP init,
     * so it enables the GPIO clock itself. */
    rcu_periph_clock_enable(RCU_GPIOA);
    gpio_mode_set(LED_ERROR_GPIO_Port, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_ERROR_Pin);
    gpio_output_options_set(LED_ERROR_GPIO_Port, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, LED_ERROR_Pin);

    if (state)
    {
        gpio_bit_reset(LED_ERROR_GPIO_Port, LED_ERROR_Pin);
    }
    else
    {
        gpio_bit_set(LED_ERROR_GPIO_Port, LED_ERROR_Pin);
    }
}

enum mmhal_button_state mmhal_get_button(enum mmhal_button_id button_id)
{
    MM_UNUSED(button_id);
    return BUTTON_RELEASED;
}

bool mmhal_set_button_callback(enum mmhal_button_id button_id,
                               mmhal_button_state_cb_t button_state_cb)
{
    if (button_id != BUTTON_ID_USER0)
    {
        return false;
    }

    mmhal_button_state_cb = button_state_cb;
    return true;
}

mmhal_button_state_cb_t mmhal_get_button_callback(enum mmhal_button_id button_id)
{
    if (button_id != BUTTON_ID_USER0)
    {
        return NULL;
    }

    return mmhal_button_state_cb;
}

time_t mmhal_get_time(void)
{
    uint32_t elapsed_ms = mmosal_get_time_ms() - time_base_ms;
    return time_base_epoch + (time_t)(elapsed_ms / 1000U);
}

void mmhal_set_time(time_t epoch)
{
    time_base_epoch = epoch;
    time_base_ms = mmosal_get_time_ms();
}

void mmhal_set_debug_pins(uint32_t mask, uint32_t values)
{
    MM_UNUSED(mask);
    MM_UNUSED(values);
}

bool mmhal_get_hardware_version(char *version_buffer, size_t version_buffer_length)
{
    return !mmosal_safer_strcpy(version_buffer, MMHAL_HARDWARE_VERSION, version_buffer_length);
}
