/*
 * Copyright 2026 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * mmhal_wlan.c for mm-ekh18-gd32f470: MM8108 host interface (GPIO control lines + polled SPI,
 * plus full GPIO/SPI teardown on disable -- see mmhal_wlan_init()/mmhal_wlan_deinit() below).
 *
 * NOTE: unlike some of the other mm-ekh08-* platforms, this implementation always uses polled
 * (non-DMA) SPI transfers, even for large buffers. This trades away some throughput/CPU headroom
 * in exchange for removing a whole category of risk (getting the DMA controller/stream/channel
 * request-mapping wrong -- see framework/src/bsps/gd32f4xx/README-PROVENANCE.md) while this port
 * is unverified against real hardware. DMA can be added later once the rest of the port is
 * validated.
 */

#include "mmhal_wlan.h"
#include "mmosal.h"
#include "mmconfig.h"
#include "mmutils.h"

#include "main.h"

/**
 * The number of bytes to send as part of @ref mmhal_wlan_send_training_seq for SD over SPI.
 * This must be at least 74 bits (>= 10 bytes), see section 6.4.1.1 of the SD spec
 * "Physical Layer Simplified Specification Version 9.10" for more detail.
 */
#define BYTE_TRAIN 16

MM_STATIC_ASSERT((BYTE_TRAIN >= 10), "BYTE_TRAIN must be at least 10.");

/** SPI hw interrupt handler. Must be set before enabling irq */
static mmhal_irq_handler_t spi_irq_handler = NULL;

/** busy interrupt handler. Must be set before enabling irq */
static mmhal_irq_handler_t busy_irq_handler = NULL;

/**
 * Re-initialise the MM8108 control/status GPIOs (WAKE, SPI_CS, SPI_IRQ, BUSY) that
 * wlan_gpio_deinit() tears down. Mirrors the relevant subset of MX_GPIO_Init() in
 * bsp/Core/Src/main.c.
 *
 * NOTE: RESET_N is deliberately not touched here (or in wlan_gpio_deinit()): it is independently
 * managed by mmhal_wlan_assert_reset()/mmhal_wlan_hard_reset() and must remain a stable output
 * across mmhal_wlan_init()/mmhal_wlan_deinit() calls, not be glitched by GPIO re-init. This
 * matches the pattern used by mm-mm8108-ekh05's wlan_hal_gpio_init()/wlan_gpio_deinit().
 */
static void wlan_hal_gpio_init(void)
{
    /* WAKE, SPI_CS: push-pull outputs, initially deasserted. */
    gpio_bit_reset(WAKE_GPIO_Port, WAKE_Pin);
    gpio_mode_set(WAKE_GPIO_Port, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, WAKE_Pin);
    gpio_output_options_set(WAKE_GPIO_Port, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, WAKE_Pin);

    gpio_bit_set(SPI_CS_GPIO_Port, SPI_CS_Pin);
    gpio_mode_set(SPI_CS_GPIO_Port, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, SPI_CS_Pin);
    gpio_output_options_set(SPI_CS_GPIO_Port, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, SPI_CS_Pin);

    /* SPI_IRQ, BUSY: EXTI inputs. The EXTI source-to-pin mapping (syscfg_exti_line_config) is
     * only set once, in MX_GPIO_Init() -- it is unaffected by wlan_gpio_deinit()'s plain GPIO
     * reconfiguration, so it does not need to be repeated here. */
    gpio_mode_set(SPI_IRQ_GPIO_Port, GPIO_MODE_INPUT, GPIO_PUPD_NONE, SPI_IRQ_Pin);
    gpio_mode_set(BUSY_GPIO_Port, GPIO_MODE_INPUT, GPIO_PUPD_PULLDOWN, BUSY_Pin);

    exti_init(SPI_IRQ_LINE, EXTI_INTERRUPT, EXTI_TRIG_FALLING);
    /* Rising edge only -- see the matching comment in bsp/Core/Src/main.c:MX_GPIO_Init(). */
    exti_init(BUSY_IRQ_LINE, EXTI_INTERRUPT, EXTI_TRIG_RISING);

    nvic_irq_enable(SPI_IRQn, 15U, 0U);
    nvic_irq_enable(BUSY_IRQn, 15U, 0U);
}

/**
 * De-initialise the MM8108 control/status GPIOs (WAKE, SPI_CS, SPI_IRQ, BUSY) to a safe,
 * output-low state, to prevent current leakage into a powered-down MM8108. Both IRQs are
 * disabled first. RESET_N is intentionally left alone -- see wlan_hal_gpio_init().
 */
static void wlan_gpio_deinit(void)
{
    mmhal_wlan_set_busy_irq_enabled(false);
    mmhal_wlan_set_spi_irq_enabled(false);

    gpio_bit_reset(BUSY_GPIO_Port, BUSY_Pin);
    gpio_mode_set(BUSY_GPIO_Port, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, BUSY_Pin);
    gpio_output_options_set(BUSY_GPIO_Port, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, BUSY_Pin);

    gpio_bit_reset(SPI_IRQ_GPIO_Port, SPI_IRQ_Pin);
    gpio_mode_set(SPI_IRQ_GPIO_Port, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, SPI_IRQ_Pin);
    gpio_output_options_set(SPI_IRQ_GPIO_Port, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, SPI_IRQ_Pin);

    gpio_bit_reset(SPI_CS_GPIO_Port, SPI_CS_Pin);
    gpio_mode_set(SPI_CS_GPIO_Port, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, SPI_CS_Pin);
    gpio_output_options_set(SPI_CS_GPIO_Port, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, SPI_CS_Pin);

    gpio_bit_reset(WAKE_GPIO_Port, WAKE_Pin);
    gpio_mode_set(WAKE_GPIO_Port, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, WAKE_Pin);
    gpio_output_options_set(WAKE_GPIO_Port, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, WAKE_Pin);
}

/**
 * Disable the SPI0 peripheral and set its pins to a safe output-low state, to prevent current
 * leakage into a powered-down MM8108. This platform always uses polled (non-DMA) SPI transfers
 * (see file header), so unlike the STM32 mm-mm8108-ekh05 reference this was ported from, there is
 * no DMA channel/stream to tear down here.
 */
static void wlan_hal_spi_deinit(void)
{
    NVIC_DisableIRQ(SPI_PERIPH_IRQn);

    spi_disable(SPI_PERIPH);
    spi_i2s_deinit(SPI_PERIPH);

    gpio_bit_reset(SPI_SCK_GPIO_Port, SPI_SCK_Pin);
    gpio_bit_reset(SPI_MISO_GPIO_Port, SPI_MISO_Pin);
    gpio_bit_reset(SPI_MOSI_GPIO_Port, SPI_MOSI_Pin);
    gpio_mode_set(SPI_SCK_GPIO_Port, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE,
                 SPI_SCK_Pin | SPI_MISO_Pin | SPI_MOSI_Pin);
    gpio_output_options_set(SPI_SCK_GPIO_Port, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ,
                            SPI_SCK_Pin | SPI_MISO_Pin | SPI_MOSI_Pin);

    rcu_periph_clock_disable(RCU_SPI0);
}

void mmhal_wlan_hard_reset(void)
{
    mmhal_wlan_assert_reset(true);
    mmosal_task_sleep(5);
    mmhal_wlan_assert_reset(false);
    mmosal_task_sleep(20);
}

void mmhal_wlan_assert_reset(bool assert_reset)
{
    if (assert_reset)
    {
        gpio_bit_reset(RESET_N_GPIO_Port, RESET_N_Pin);
    }
    else
    {
        gpio_bit_set(RESET_N_GPIO_Port, RESET_N_Pin);
    }
}

#if defined(ENABLE_EXT_XTAL_INIT) && ENABLE_EXT_XTAL_INIT
bool mmhal_wlan_ext_xtal_init_is_required(void)
{
    return true;
}
#endif

void mmhal_wlan_spi_cs_assert(void)
{
    gpio_bit_reset(SPI_CS_GPIO_Port, SPI_CS_Pin);
}

void mmhal_wlan_spi_cs_deassert(void)
{
    gpio_bit_set(SPI_CS_GPIO_Port, SPI_CS_Pin);
}

uint8_t mmhal_wlan_spi_rw(uint8_t data)
{
    while (!spi_i2s_flag_get(SPI_PERIPH, SPI_FLAG_TBE))
    {
    }
    spi_i2s_data_transmit(SPI_PERIPH, data);
    while (!spi_i2s_flag_get(SPI_PERIPH, SPI_FLAG_RBNE))
    {
    }
    return (uint8_t)spi_i2s_data_receive(SPI_PERIPH);
}

void mmhal_wlan_spi_read_buf(uint8_t *buf, unsigned len)
{
    unsigned ii;
    for (ii = 0; ii < len; ii++)
    {
        *buf++ = mmhal_wlan_spi_rw(0xFF);
    }
}

void mmhal_wlan_spi_write_buf(const uint8_t *buf, unsigned len)
{
    unsigned ii;
    for (ii = 0; ii < len; ii++)
    {
        (void)mmhal_wlan_spi_rw(*buf++);
    }
}

void mmhal_wlan_send_training_seq(void)
{
    uint8_t i;

    mmhal_wlan_spi_cs_deassert();

    /* SPI MOSI line to GPIO out */
    gpio_mode_set(SPI_MOSI_GPIO_Port, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, SPI_MOSI_Pin);
    gpio_bit_set(SPI_MOSI_GPIO_Port, SPI_MOSI_Pin);

    /* Send >74 clock pulses to card to stabilize CLK, see section 6.4.1.1 of the SD spec. */
    for (i = 0; i < BYTE_TRAIN; i++)
    {
        (void)mmhal_wlan_spi_rw(0xFF);
    }

    while (spi_i2s_flag_get(SPI_PERIPH, SPI_FLAG_TRANS))
    {
    }

    /* Configure SPI MOSI line back to original config (AF selection is preserved by the GPIO
     * peripheral even while the pin was temporarily switched to plain output mode). */
    gpio_bit_reset(SPI_MOSI_GPIO_Port, SPI_MOSI_Pin);
    gpio_mode_set(SPI_MOSI_GPIO_Port, GPIO_MODE_AF, GPIO_PUPD_NONE, SPI_MOSI_Pin);
}

void mmhal_wlan_register_spi_irq_handler(mmhal_irq_handler_t handler)
{
    spi_irq_handler = handler;
}

bool mmhal_wlan_spi_irq_is_asserted(void)
{
    return !gpio_input_bit_get(SPI_IRQ_GPIO_Port, SPI_IRQ_Pin);
}

void mmhal_wlan_set_spi_irq_enabled(bool enabled)
{
    if (enabled)
    {
        exti_interrupt_enable(SPI_IRQ_LINE);

        /* The transceiver will hold the IRQ line low if there is additional information
         * to be retrieved. Ideally the interrupt pin would be configured as a low level
         * interrupt. */
        if (mmhal_wlan_spi_irq_is_asserted())
        {
            if (spi_irq_handler != NULL)
            {
                spi_irq_handler();
            }
        }

        NVIC_EnableIRQ(SPI_IRQn);
    }
    else
    {
        NVIC_DisableIRQ(SPI_IRQn);
        exti_interrupt_disable(SPI_IRQ_LINE);
    }
}

void mmhal_wlan_init(void)
{
    /* Re-initialise connections to the MM8108 before enabling the chip -- undoes the teardown
     * performed by mmhal_wlan_deinit(). */
    wlan_hal_gpio_init();
    SPI_PERIPH_INIT();

    mmhal_wlan_assert_reset(false);
}

void mmhal_wlan_deinit(void)
{
    mmhal_wlan_assert_reset(true);

    /* Disable connections to the MM8108 once disabled, to prevent current leakage into the
     * powered-down chip. */
    wlan_hal_spi_deinit();
    wlan_gpio_deinit();
}

void mmhal_wlan_wake_assert(void)
{
    gpio_bit_set(WAKE_GPIO_Port, WAKE_Pin);
}

void mmhal_wlan_wake_deassert(void)
{
    gpio_bit_reset(WAKE_GPIO_Port, WAKE_Pin);
}

bool mmhal_wlan_busy_is_asserted(void)
{
    return gpio_input_bit_get(BUSY_GPIO_Port, BUSY_Pin);
}

void mmhal_wlan_register_busy_irq_handler(mmhal_irq_handler_t handler)
{
    busy_irq_handler = handler;
}

void mmhal_wlan_set_busy_irq_enabled(bool enabled)
{
    if (enabled)
    {
        NVIC_EnableIRQ(BUSY_IRQn);
    }
    else
    {
        NVIC_DisableIRQ(BUSY_IRQn);
    }
}

/**
 * @brief This function handles BUSY interrupt.
 */
void BUSY_IRQ_HANDLER(void)
{
    if (exti_interrupt_flag_get(BUSY_IRQ_LINE))
    {
        exti_interrupt_flag_clear(BUSY_IRQ_LINE);
        if (busy_irq_handler != NULL)
        {
            busy_irq_handler();
        }
    }
}

/**
 * @brief This function handles SPI IRQ interrupt.
 */
void SPI_IRQ_HANDLER(void)
{
    if (exti_interrupt_flag_get(SPI_IRQ_LINE))
    {
        exti_interrupt_flag_clear(SPI_IRQ_LINE);
        if (spi_irq_handler != NULL)
        {
            spi_irq_handler();
        }
    }
}

/**
 * Generate a stable, device-unique MAC address based on the MCU UID. This address is not globally
 * unique, but is consistent across boots on the same device and marked as locally administered
 * (0x02 prefix).
 *
 * NOTE: 0x1FFF7A10 is confirmed as the correct GD32F4xx unique device ID base address (it matches
 * GigaDevice's own DEVICE_ID1/2/3 definitions in the vendored
 * GD32F4xx_usb_library/device/core/Include/usbd_enum.h), and happens to be identical to STM32F4's.
 *
 * @param mac_addr Location where the MAC address will be stored.
 */
static void generate_stable_mac_addr_from_uid(uint8_t *mac_addr)
{
    #define GD32F4XX_UID_BASE (0x1FFF7A10UL)
    const volatile uint32_t *uid = (const volatile uint32_t *)GD32F4XX_UID_BASE;

    /* Shorten the UID */
    uint32_t uid_word = uid[0] ^ uid[1] ^ uid[2];

    /* Set MAC address as locally administered */
    mac_addr[0] = 0x02;
    mac_addr[1] = 0x00;
    memcpy(&mac_addr[2], &uid_word, sizeof(uid_word));
}

/**
 * Attempts to read a MAC address from the "wlan.macaddr" key in mmconfig persistent configuration.
 *
 * @param mac_addr Location where the MAC address will be stored if there is a valid MAC address in
 *                 mmconfig persistent storage.
 */
static void get_mmconfig_mac_addr(uint8_t *mac_addr)
{
    char strval[32];
    if (mmconfig_read_string("wlan.macaddr", strval, sizeof(strval)) > 0)
    {
        /* Need to provide an array of ints to sscanf otherwise it will overflow */
        int temp[MMWLAN_MAC_ADDR_LEN];
        uint8_t validated_mac[MMWLAN_MAC_ADDR_LEN];
        int i;

        int ret = sscanf(strval,
                         "%x:%x:%x:%x:%x:%x",
                         &temp[0],
                         &temp[1],
                         &temp[2],
                         &temp[3],
                         &temp[4],
                         &temp[5]);
        if (ret == MMWLAN_MAC_ADDR_LEN)
        {
            for (i = 0; i < MMWLAN_MAC_ADDR_LEN; i++)
            {
                if (temp[i] > UINT8_MAX || temp[i] < 0)
                {
                    /* Invalid value, ignore and exit without updating mac_addr */
                    printf("Invalid MAC address found in [wlan.macaddr], rejecting!\n");
                    return;
                }
                validated_mac[i] = (uint8_t)temp[i];
            }
            /* We only override the value in mac_addr once the entire mmconfig MAC has been
             * validated in case mac_addr already contains a MAC address. */
            memcpy(mac_addr, validated_mac, MMWLAN_MAC_ADDR_LEN);
        }
    }
}

void mmhal_read_mac_addr(uint8_t *mac_addr)
{
    /*
     * MAC address is determined using the following precedence:
     *
     * 1. The value of the `wlan.macaddr` setting in persistent storage, if present and valid.
     *
     * 2. The MAC address in transceiver OTP (i.e., the value of mac_addr passed into this function,
     *    if non-zero).
     *
     * 3. A stable MAC address generated from the MCU's hardware UID. This value is consistent
     *    across boots for the same device, but unique to each MCU.
     *
     * 4. Failing all of the above, the value of mac_addr will remain zero on return from this
     *    function, in which case the driver will generate a random MAC address.
     */

    get_mmconfig_mac_addr(mac_addr);

    if (!mm_mac_addr_is_zero(mac_addr))
    {
        return;
    }

    generate_stable_mac_addr_from_uid(mac_addr);
}

const struct mmhal_chip *mmhal_get_chip(void)
{
    /* This is a define that is set by the build system in the platform-xxx.mk file to select the
     * Morse chip type. See the API documentation for mmhal_get_chip() for more information. */
    return &MMHAL_CHIP_TYPE;
}
