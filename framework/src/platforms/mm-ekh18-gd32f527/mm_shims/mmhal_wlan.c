/*
 * Copyright 2026 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * mmhal_wlan.c for mm-ekh18-gd32f527: MM8108 host interface (SPI1 + DMA0 Ch3/4).
 *
 * Adapted from mm-ekh18-gd32f470/mm_shims/mmhal_wlan.c:
 *  - SPI1 (not SPI0), RCU_SPI1
 *  - DMA0 (not DMA1), Ch3/4 (SPI1 RX/TX), SUBPERI0
 *  - EXTI2/PC2 (SPI_IRQ), EXTI3/PC3 (BUSY)
 *  - UID base address unchanged (0x1FFF7A10 -- same for GD32F4xx and GD32F5xx)
 */

#include "mmhal_wlan.h"
#include "mmlog.h"
#include "mmosal.h"
#include "mmutils.h"
#include "main.h"

#ifndef EXCLUDE_MMCONFIG
#include "mmconfig.h"
#endif

#define BYTE_TRAIN 16
MM_STATIC_ASSERT((BYTE_TRAIN >= 10), "BYTE_TRAIN must be at least 10.");

static struct mmosal_semb *dma_semb_handle;
#define DMA_WAIT_TMO (10)
#define DMA_TRANSFER_MIN_LENGTH (16)

static mmhal_irq_handler_t spi_irq_handler = NULL;
static mmhal_irq_handler_t busy_irq_handler = NULL;

static void wlan_hal_gpio_init(void)
{
    gpio_bit_reset(WAKE_GPIO_Port, WAKE_Pin);
    gpio_mode_set(WAKE_GPIO_Port, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, WAKE_Pin);
    gpio_output_options_set(WAKE_GPIO_Port, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, WAKE_Pin);

    gpio_bit_set(SPI_CS_GPIO_Port, SPI_CS_Pin);
    gpio_mode_set(SPI_CS_GPIO_Port, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, SPI_CS_Pin);
    gpio_output_options_set(SPI_CS_GPIO_Port, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, SPI_CS_Pin);

    /* EXTI source mapping set once in MX_GPIO_Init() -- not repeated here. */
    gpio_mode_set(SPI_IRQ_GPIO_Port, GPIO_MODE_INPUT, GPIO_PUPD_NONE, SPI_IRQ_Pin);
    gpio_mode_set(BUSY_GPIO_Port, GPIO_MODE_INPUT, GPIO_PUPD_PULLDOWN, BUSY_Pin);

    exti_init(SPI_IRQ_LINE, EXTI_INTERRUPT, EXTI_TRIG_FALLING);
    /* Rising edge only -- see comment in bsp/Core/Src/main.c:MX_GPIO_Init(). */
    exti_init(BUSY_IRQ_LINE, EXTI_INTERRUPT, EXTI_TRIG_RISING);

    nvic_irq_enable(SPI_IRQn, 15U, 0U);
    nvic_irq_enable(BUSY_IRQn, 15U, 0U);
}

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

static void wlan_hal_spi_deinit(void)
{
    NVIC_DisableIRQ(SPI_PERIPH_IRQn);
    NVIC_DisableIRQ(SPI_RX_DMA_IRQn);

    dma_channel_disable(SPI_DMA_PERIPH, SPI_RX_DMA_CHANNEL);
    dma_channel_disable(SPI_DMA_PERIPH, SPI_TX_DMA_CHANNEL);

    spi_disable(SPI_PERIPH);
    spi_i2s_deinit(SPI_PERIPH);

    gpio_bit_reset(SPI_SCK_GPIO_Port, SPI_SCK_Pin);
    gpio_bit_reset(SPI_MISO_GPIO_Port, SPI_MISO_Pin);
    gpio_bit_reset(SPI_MOSI_GPIO_Port, SPI_MOSI_Pin);
    gpio_mode_set(SPI_SCK_GPIO_Port, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE,
                 SPI_SCK_Pin | SPI_MISO_Pin | SPI_MOSI_Pin);
    gpio_output_options_set(SPI_SCK_GPIO_Port, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ,
                            SPI_SCK_Pin | SPI_MISO_Pin | SPI_MOSI_Pin);

    rcu_periph_clock_disable(RCU_SPI1);
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

static void mmhal_wlan_spi_dma_transfer(void *mem_addr, unsigned len, bool mem_is_rx,
                                        uint32_t *dummy_word)
{
    dma_single_data_parameter_struct dma_init_struct;

    dma_channel_disable(SPI_DMA_PERIPH, SPI_RX_DMA_CHANNEL);
    dma_channel_disable(SPI_DMA_PERIPH, SPI_TX_DMA_CHANNEL);

    dma_deinit(SPI_DMA_PERIPH, SPI_RX_DMA_CHANNEL);
    dma_deinit(SPI_DMA_PERIPH, SPI_TX_DMA_CHANNEL);

    dma_single_data_para_struct_init(&dma_init_struct);
    dma_init_struct.periph_addr = (uint32_t)&SPI_DATA(SPI_PERIPH);
    dma_init_struct.periph_inc = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_8BIT;
    dma_init_struct.circular_mode = DMA_CIRCULAR_MODE_DISABLE;
    dma_init_struct.number = len;
    dma_init_struct.priority = DMA_PRIORITY_HIGH;

    /* RX channel: MM8108 -> buf (read) or MM8108 -> dummy (write). */
    dma_init_struct.direction = DMA_PERIPH_TO_MEMORY;
    dma_init_struct.memory0_addr = mem_is_rx ? (uint32_t)mem_addr : (uint32_t)dummy_word;
    dma_init_struct.memory_inc = mem_is_rx ? DMA_MEMORY_INCREASE_ENABLE : DMA_MEMORY_INCREASE_DISABLE;
    dma_single_data_mode_init(SPI_DMA_PERIPH, SPI_RX_DMA_CHANNEL, &dma_init_struct);
    dma_channel_subperipheral_select(SPI_DMA_PERIPH, SPI_RX_DMA_CHANNEL, SPI_RX_DMA_SUBPERI);

    /* TX channel: buf -> MM8108 (write) or fixed 0xFF dummy -> MM8108 (read).
     * IMPORTANT: must explicitly set DMA_MEMORY_TO_PERIPH -- see F470 provenance notes for why
     * leaving this unset silently configures the wrong direction and breaks DMA transfers. */
    *dummy_word = 0xFFU;
    dma_init_struct.direction = DMA_MEMORY_TO_PERIPH;
    dma_init_struct.memory0_addr = mem_is_rx ? (uint32_t)dummy_word : (uint32_t)mem_addr;
    dma_init_struct.memory_inc = mem_is_rx ? DMA_MEMORY_INCREASE_DISABLE : DMA_MEMORY_INCREASE_ENABLE;
    dma_single_data_mode_init(SPI_DMA_PERIPH, SPI_TX_DMA_CHANNEL, &dma_init_struct);
    dma_channel_subperipheral_select(SPI_DMA_PERIPH, SPI_TX_DMA_CHANNEL, SPI_TX_DMA_SUBPERI);

    /* Only RX raises a completion IRQ -- SPI is full-duplex so RX and TX finish together.
     * Use DMA_INT_FTF (CHXCTL bit) not DMA_INT_FLAG_FTF (INTF bit) -- they are different
     * bit positions (see mm-ekh18-gd32f470/mm_shims/mmhal_wlan.c for the full explanation). */
    dma_interrupt_enable(SPI_DMA_PERIPH, SPI_RX_DMA_CHANNEL, DMA_INT_FTF);

    dma_channel_enable(SPI_DMA_PERIPH, SPI_RX_DMA_CHANNEL);
    dma_channel_enable(SPI_DMA_PERIPH, SPI_TX_DMA_CHANNEL);

    spi_dma_enable(SPI_PERIPH, SPI_DMA_RECEIVE);
    spi_dma_enable(SPI_PERIPH, SPI_DMA_TRANSMIT);

    bool ok = mmosal_semb_wait(dma_semb_handle, DMA_WAIT_TMO);
    MMOSAL_ASSERT(ok);
}

void mmhal_wlan_spi_read_buf(uint8_t *buf, unsigned len)
{
    if (len < DMA_TRANSFER_MIN_LENGTH)
    {
        unsigned ii;
        for (ii = 0; ii < len; ii++)
        {
            *buf++ = mmhal_wlan_spi_rw(0xFF);
        }
    }
    else
    {
        uint32_t dummy_word;
        mmhal_wlan_spi_dma_transfer(buf, len, true, &dummy_word);
    }
}

void mmhal_wlan_spi_write_buf(const uint8_t *buf, unsigned len)
{
    if (len < DMA_TRANSFER_MIN_LENGTH)
    {
        unsigned ii;
        for (ii = 0; ii < len; ii++)
        {
            (void)mmhal_wlan_spi_rw(*buf++);
        }
    }
    else
    {
        uint32_t dummy_word;
        mmhal_wlan_spi_dma_transfer((void *)(uintptr_t)buf, len, false, &dummy_word);
    }
}

void mmhal_wlan_send_training_seq(void)
{
    uint8_t i;

    mmhal_wlan_spi_cs_deassert();

    gpio_mode_set(SPI_MOSI_GPIO_Port, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, SPI_MOSI_Pin);
    gpio_bit_set(SPI_MOSI_GPIO_Port, SPI_MOSI_Pin);

    for (i = 0; i < BYTE_TRAIN; i++)
    {
        (void)mmhal_wlan_spi_rw(0xFF);
    }

    while (spi_i2s_flag_get(SPI_PERIPH, SPI_FLAG_TRANS))
    {
    }

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
    dma_semb_handle = mmosal_semb_create("mmhal_wlan_dma_semb");
    wlan_hal_gpio_init();
    SPI_PERIPH_INIT();

    mmhal_wlan_assert_reset(false);
}

void mmhal_wlan_deinit(void)
{
    mmhal_wlan_assert_reset(true);

    wlan_hal_spi_deinit();
    wlan_gpio_deinit();
    mmosal_semb_delete(dma_semb_handle);
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

void SPI_RX_DMA_IRQ_HANDLER(void)
{
    if (dma_interrupt_flag_get(SPI_DMA_PERIPH, SPI_RX_DMA_CHANNEL, DMA_INT_FLAG_FTF))
    {
        dma_interrupt_flag_clear(SPI_DMA_PERIPH, SPI_RX_DMA_CHANNEL, DMA_INT_FLAG_FTF);
        mmosal_semb_give_from_isr(dma_semb_handle);
    }
}

static void generate_stable_mac_addr_from_uid(uint8_t *mac_addr)
{
    /* 0x1FFF7A10 is confirmed as the GD32F5xx unique device ID base address -- same as GD32F4xx
     * and STM32F4 (GigaDevice preserved this address across the F4xx->F5xx family). */
    #define GD32F5XX_UID_BASE (0x1FFF7A10UL)
    const volatile uint32_t *uid = (const volatile uint32_t *)GD32F5XX_UID_BASE;

    uint32_t uid_word = uid[0] ^ uid[1] ^ uid[2];

    mac_addr[0] = 0x02;
    mac_addr[1] = 0x00;
    memcpy(&mac_addr[2], &uid_word, sizeof(uid_word));
    MMOSAL_DEV_ASSERT(mm_mac_addr_is_valid_sta(mac_addr));
}

void mmhal_read_mac_addr(uint8_t *mac_addr)
{
#ifndef EXCLUDE_MMCONFIG
    /* Read from mmconfig into a temporary as we only override the value in mac_addr once the
     * mmconfig MAC has been validated in case mac_addr already contains a MAC address. */
    uint8_t macval[MMWLAN_MAC_ADDR_LEN];
    if (mmconfig_read_mac_addr("wlan.macaddr", macval) == MMCONFIG_OK)
    {
        if (mm_mac_addr_is_valid_sta(macval))
        {
            memcpy(mac_addr, macval, MMWLAN_MAC_ADDR_LEN);
            return;
        }
        MMLOG_ERR("Rejecting invalid MAC address from mmconfig wlan.macaddr: " MM_MAC_ADDR_FMT "\n",
                  MM_MAC_ADDR_VAL(macval));
    }
#endif

    if (mm_mac_addr_is_valid_sta(mac_addr))
    {
        return;
    }

    generate_stable_mac_addr_from_uid(mac_addr);
}

const struct mmhal_chip *mmhal_get_chip(void)
{
    return &MMHAL_CHIP_TYPE;
}
