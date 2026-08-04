/*
 * Copyright 2026 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * mmhal_os.c for mm-ekh18-gd32f527.
 *
 * NOTE: unlike some of the other mm-ekh08-* platforms, this port does not implement MCU deep
 * sleep (e.g. GD32F5xx "Deep-sleep"/"Standby" modes with an LPTIM-equivalent wake timer). Getting
 * a low-power mode's wake timing and peripheral re-init exactly right is one of the areas most
 * likely to differ from the assumptions made elsewhere in this port, and an incorrect low power
 * implementation can be hard to debug (the MCU may fail to wake at all). Only WFI-based "shallow"
 * sleep (the RTOS tick keeps running) is implemented; deep sleep support can be added later once
 * the rest of the port is validated on real hardware.
 */

#include "mm_hal_common.h"
#include "mmhal_os.h"
#include "mmosal.h"
#include "mmutils.h"
#include "main.h"

/**
 * The MCU core (SYSCLK/AHB) clock frequency in Hz, as configured by SystemClock_Config() in
 * bsp/Core/Src/main.c. This is not declared in any mmhal header -- by convention every platform's
 * mm_shims/mmhal_os.c defines this global so that framework/src/freertos/mmiot/include/
 * FreeRTOSConfig.h (configCPU_CLOCK_HZ) and other consumers (e.g. the porting_assistant CPU/mem
 * benchmark) can pick up the actual configured clock rate.
 *
 * If you change HXTAL_VALUE or the PLL configuration in SystemClock_Config(), update this too.
 */
const uint32_t mmhal_system_clock = 200000000;

void mmhal_early_init(void)
{
    mmosal_disable_interrupts();
}

void mmhal_init(void)
{
    mmosal_enable_interrupts();
    mmhal_random_init();
}

enum mmhal_isr_state mmhal_get_isr_state(void)
{
    if (SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk)
    {
        return MMHAL_IN_ISR;
    }
    else
    {
        return MMHAL_NOT_IN_ISR;
    }
}

/* Log output via UART. Included by default, unless DISABLE_UART_LOG is defined. */
#ifndef DISABLE_UART_LOG

/** Size of the debug log buffer used by mmhal_log_write(). */
#define LOG_BUF_SIZE (512)

/**
 * Debug log buffer data structure. This is a simple circular buffer where empty is indicated
 * by (wr_idx == rd_idx) and full by (wr_idx == rd_idx-1) [mod LOG_BUF_SIZE].
 */
struct log_buf
{
    /** The place where the data goes. */
    uint8_t buf[LOG_BUF_SIZE];
    /** Write index into buf. */
    volatile size_t wr_idx;
    /** Read index into buf. */
    volatile size_t rd_idx;
    /** Flag to indicate if we've already sent a '\r' for the current '\n' */
    volatile bool newline_converted;
};

/** Log buffer instance. */
static struct log_buf log_buf = { 0 };

/**
 * Function to output a char from the log buffer.
 *
 * @param buf Reference to log buffer to take the char from
 *
 * @note This will automatically convert any instances of @c \n to @c \r\n
 *
 * @warning A @c usart_flag_get(LOG_USART, USART_FLAG_TBE) check must be made between consecutive
 *          calls of this function.
 */
static void mmhal_log_print_char(struct log_buf *buf)
{
    /* If we encounter a newline insert a carriage return first to maximize compatibility */
    if (buf->buf[buf->rd_idx] == '\n' && !buf->newline_converted)
    {
        buf->newline_converted = true;
        usart_data_transmit(LOG_USART, '\r');
        return;
    }
    buf->newline_converted = false;

    usart_data_transmit(LOG_USART, buf->buf[buf->rd_idx]);
    buf->rd_idx++;
    if (buf->rd_idx >= LOG_BUF_SIZE)
    {
        buf->rd_idx = 0;
    }
}

void LOG_USART_IRQ_HANDLER(void)
{
    /* Keep pumping data into the UART until it is not empty or we have no data left. */
    while (usart_flag_get(LOG_USART, USART_FLAG_TBE))
    {
        if (log_buf.wr_idx != log_buf.rd_idx)
        {
            mmhal_log_print_char(&log_buf);
        }
        else
        {
            break;
        }
    }

    /* If we emptied the buffer then we no longer want to be interrupted. It will be reenabled
     * once more data is added to the buffer. */
    if (log_buf.wr_idx == log_buf.rd_idx)
    {
        usart_interrupt_disable(LOG_USART, USART_INT_TBE);
    }
}

static void mmhal_log_flush_uart(void)
{
    /* We don't know whether interrupts will be enabled or not, so let's just disable and do
     * this the manual way. */
    usart_interrupt_disable(LOG_USART, USART_INT_TBE);
    while (log_buf.wr_idx != log_buf.rd_idx)
    {
        while (!usart_flag_get(LOG_USART, USART_FLAG_TBE))
        {
        }

        mmhal_log_print_char(&log_buf);
    }
}

static void mmhal_log_write_uart(const uint8_t *data, size_t length)
{
    while (length > 0)
    {
        size_t rd_idx = log_buf.rd_idx;
        size_t wr_idx = log_buf.wr_idx;

        /* Calculate maximum length we can copy in one go. */
        size_t space;
        if (wr_idx < rd_idx)
        {
            space = rd_idx - wr_idx - 1;
        }
        else
        {
            space = LOG_BUF_SIZE - wr_idx;
            /* Constrain to size-1 so we don't wrap. */
            if (!rd_idx)
            {
                space--;
            }
        }

        MMOSAL_ASSERT(space < LOG_BUF_SIZE);

        if (space == 0)
        {
            mmosal_task_yield();
            continue;
        }

        size_t copy_len = space < length ? space : length;
        memcpy(log_buf.buf + log_buf.wr_idx, data, copy_len);
        log_buf.wr_idx += copy_len;
        if (log_buf.wr_idx >= LOG_BUF_SIZE)
        {
            log_buf.wr_idx -= LOG_BUF_SIZE;
        }
        data += copy_len;
        length -= copy_len;

        /* Enable TX empty interrupt. */
        usart_interrupt_enable(LOG_USART, USART_INT_TBE);
        NVIC_EnableIRQ(LOG_USART_IRQ);
    }
}

#else

static void mmhal_log_write_uart(const uint8_t *data, size_t length)
{
    /* UART logging is disabled... send the message to a black hole. */
    MM_UNUSED(data);
    MM_UNUSED(length);
}

static void mmhal_log_flush_uart(void)
{
    /* UART logging is disabled... no action required. */
}

#endif

/* Log output via ITM/SWO. Only enabled if ENABLE_ITM_LOG is defined. */
#ifdef ENABLE_ITM_LOG

static void mmhal_log_write_itm(const uint8_t *data, size_t length)
{
    while (length-- > 0)
    {
        ITM_SendChar(*data++);
    }
}

#else

/* ITM/SWO logging is disabled... send the message to a black hole. */
static void mmhal_log_write_itm(const uint8_t *data, size_t length)
{
    MM_UNUSED(data);
    MM_UNUSED(length);
}

#endif

void mmhal_log_write(const uint8_t *data, size_t length)
{
    mmhal_log_write_uart(data, length);
    mmhal_log_write_itm(data, length);
}

void mmhal_log_flush(void)
{
    mmhal_log_flush_uart();
}

void mmhal_reset(void)
{
    NVIC_SystemReset();
    while (1)
    {
    }
}

enum mmhal_sleep_state mmhal_sleep_prepare(uint32_t expected_idle_time_ms)
{
    MM_UNUSED(expected_idle_time_ms);

    /* Deep sleep is not implemented on this platform (see file header comment); we always
     * request shallow sleep (WFI with the RTOS tick still running), unless deep sleep vetoes
     * indicate we should not sleep at all. */
    if (mmhal_get_deep_sleep_veto())
    {
        return MMHAL_SLEEP_DISABLED;
    }

    __disable_irq();
    __DSB();
    __ISB();

    return MMHAL_SLEEP_SHALLOW;
}

uint32_t mmhal_sleep(enum mmhal_sleep_state sleep_state, uint32_t expected_idle_time_ms)
{
    MM_UNUSED(expected_idle_time_ms);

    if (sleep_state == MMHAL_SLEEP_SHALLOW)
    {
        __WFI();
    }

    /* We do not track elapsed sleep time separately from the RTOS tick, since we never stop the
     * tick (no deep sleep support), so always report 0ms of "extra" elapsed time. */
    return 0;
}

void mmhal_sleep_abort(enum mmhal_sleep_state sleep_state)
{
    MM_UNUSED(sleep_state);
    __enable_irq();
}

void mmhal_sleep_cleanup(void)
{
    __enable_irq();
}
