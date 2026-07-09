/*
 * Copyright 2026 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * mmhal_app.c for mm-ekh18-gd32f470.
 *
 * NOTE: buttons and debug pins are not implemented (stubbed) -- there is no user button or debug
 * GPIO available on this board (see framework/src/platforms/mm-ekh18-gd32f470/readme.md). LEDs
 * *are* implemented using the board's real onboard green/blue/yellow LEDs (see main.h).
 *
 * There is also no battery-backed RTC peripheral wired up on this port (see
 * framework/src/bsps/gd32f4xx/README-PROVENANCE.md for why the GD32F4xx RTC itself was not
 * implemented); mmhal_get_time()/mmhal_set_time() instead use a simple software clock derived
 * from the RTOS tick, which is reset to the epoch every power cycle unless mmhal_set_time() is
 * called again (e.g. via SNTP) after each boot.
 */

#include "mmhal_app.h"
#include "mm_hal_common.h"
#include "mmosal.h"
#include "mmutils.h"

#include "main.h"

static mmhal_button_state_cb_t mmhal_button_state_cb = NULL;

/** time(2) value corresponding to @c time_base_ms (see mmhal_get_time()/mmhal_set_time()). */
static time_t time_base_epoch = 0;
/** mmosal_get_time_ms() value at which @c time_base_epoch was recorded. */
static uint32_t time_base_ms = 0;

/* All three onboard LEDs are wired VCC -> LED -> resistor -> GPIO (active-LOW: driving the pin low
 * sinks current and turns the LED on). See main.h for the pin definitions and provenance. */

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
            if (level)
            {
                gpio_bit_reset(LED_BLUE_GPIO_Port, LED_BLUE_Pin);
            }
            else
            {
                gpio_bit_set(LED_BLUE_GPIO_Port, LED_BLUE_Pin);
            }
            break;

        case LED_RED:
        case LED_WHITE:
        default:
            /* Not available: this board has no GPIO-controllable red or white LED (see main.h).
             * LED_RED is used instead as the dedicated error LED -- see mmhal_set_error_led(). */
            break;
    }
}

void mmhal_set_error_led(bool state)
{
    /* This board has no GPIO-controllable red LED, so the yellow LED is repurposed as the error
     * indicator (see main.h). Per the API contract this function may be called by the bootloader
     * before main()'s usual BSP init has run, so it must do its own clock/GPIO setup here. */
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
    /* Not implemented on this platform */
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
    /* Not implemented: no debug GPIOs defined for this board yet. */
    MM_UNUSED(mask);
    MM_UNUSED(values);
}

bool mmhal_get_hardware_version(char *version_buffer, size_t version_buffer_length)
{
    /* Note: You need to identify the correct hardware and or version
     *       here using whatever means available (GPIO's, version number stored in EEPROM, etc)
     *       and return the correct string here. */
    return !mmosal_safer_strcpy(version_buffer, MMHAL_HARDWARE_VERSION, version_buffer_length);
}
