/*
 * main.c - clock/peripheral bring-up for mm-ekh18-gd32f527.
 *
 * Modelled on mm-ekh18-gd32f470/bsp/Core/Src/main.c, adapted for the GD32F527ZMT7:
 *  - No fmc_wscnt_set(): GD32F5xx handles flash wait states automatically.
 *  - PMU high-drive mode required before 200MHz (see SystemClock_Config()).
 *  - SPI1 (PB13/14/15, APB1@50MHz, SPI_PSC_2=25MHz) instead of SPI0.
 *  - DMA0 Ch3/4 (SUBPERI0) for SPI1 RX/TX.
 *  - EXTI2/PC2 (SPI_IRQ) and EXTI3/PC3 (BUSY) instead of EXTI1/EXTI0.
 *
 * Copyright 2026 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "main.h"
#include "mmosal.h"
#include "mmport.h"

void SystemClock_Config(void);
static void ECC_PrimeNoinit(void);
static void MX_GPIO_Init(void);
static void MX_USART0_Init(void);

void app_init(void);

int main(void)
{
    /* MUST run before anything reads .noinit -- see ECC_PrimeNoinit(). */
    ECC_PrimeNoinit();

    SystemClock_Config();

    MX_GPIO_Init();
    MX_SPI1_Init();
    MX_USART0_Init();

    mmosal_main(app_init);

    while (1)
    {
        MMPORT_BREAKPOINT();
    }
}

/**
 * @brief Give the .noinit section a valid SRAM ECC syndrome.
 *
 * GD32F5xx SRAM is ECC-protected (FMC_OBCTL0.ECCEN, set by default), and the ECC syndrome bits are
 * undefined after a cold power-on. Reading a location that has never been written since power-up
 * therefore raises an *uncorrectable* (two-bit) ECC error, which this part reports as an NMI --
 * not a HardFault. NMI_Handler is a weak alias of Default_Handler (an infinite loop), so the board
 * hangs dead silent with the clocks, GPIOs and USART0 already fully initialised, which makes this
 * look like a UART bug rather than a memory bug.
 *
 * .noinit is exactly such a region: the zero-fill loop in startup_gd32f5xx.S stops at _ebss, which
 * is precisely where .noinit starts. mm_shims/mmosal_shim_freertos.c places preserved_failure_info
 * there and probes it with
 *     if (preserved_failure_info.magic != ASSERT_INFO_MAGIC)
 * -- that read is what fires the NMI, before the magic check can reject the garbage.
 *
 * Confirmed on GD32F527ZMT7 silicon: after the hang, SYSCFG_STAT read 0x00000001 (ECCMEIF0, "SRAM0
 * two bits no-correction event flag") and SYSCFG_SRAM0ECC read 0x61EA0007, i.e. ECCEADDR0 = 0x61EA
 * -> word 0x61EA -> 0x200187A8, which is the exact linked start address of .noinit. The NMI's
 * stacked return address was in vStartFirstTask, with CFSR = HFSR = 0 (proving it was a genuine
 * NMI, not an escalated fault).
 *
 * The mm-ekh18-gd32f470 sibling uses an equivalent linker script and startup and is unaffected,
 * because GD32F4xx SRAM has no ECC. This hazard is GD32F5xx-only.
 *
 * Prime on power-on/brown-out reset only, so .noinit can still do its job of carrying crash records
 * across warm resets (NRST, watchdog, NVIC_SystemReset). The reset-source flags are sticky, so they
 * are cleared afterwards -- otherwise every later warm reset would still look like a cold boot and
 * discard the records.
 *
 * .bss, the FreeRTOS heap (a static array in .bss) and the stack all get written before they are
 * read, so they establish valid ECC by themselves and need no priming. This was verified on
 * hardware: the firmware ran through SystemClock_Config(), the peripheral init and all of
 * xPortStartScheduler() -- heavy stack use -- without a single ECC event.
 */
static void ECC_PrimeNoinit(void)
{
    /* Provided by GD32F527ZM_FLASH.ld; these delimit the .noinit output section. */
    extern uint32_t _snoinit;
    extern uint32_t _enoinit;

    if ((RCU_RSTSCK & (RCU_RSTSCK_PORRSTF | RCU_RSTSCK_BORRSTF)) != 0U)
    {
        uint32_t *addr;
        for (addr = &_snoinit; addr < &_enoinit; addr++)
        {
            *addr = 0U;
        }
    }

    RCU_RSTSCK |= RCU_RSTSCK_RSTFC;
}

/**
 * @brief System Clock Configuration.
 *
 * GD32F527ZMT7: HXTAL (25MHz) -> main PLL -> SYSCLK = 200MHz.
 * AHB = 200MHz, APB1 = 50MHz, APB2 = 100MHz.
 *
 * PMU high-drive mode must be enabled before the core clock exceeds a threshold (~168MHz).
 * Sequence (from GD32F5xx User Manual and gd32f5xx_pmu.h):
 *   1. Enable PMU clock
 *   2. Select LDO high voltage (PMU_LDOVS_HIGH)
 *   3. Start HXTAL and configure PLL (but do NOT switch sysclk to PLL yet)
 *   4. Enable PLL and wait for lock
 *   5. Enable high-driver mode (HDEN), wait for HDRF flag
 *   6. Switch to high-driver (HDS), wait for HDSRF flag
 *   7. Switch sysclk source to PLLP, verify with RCU_SCSS_PLLP
 */
void SystemClock_Config(void)
{
    rcu_periph_clock_enable(RCU_PMU);
    pmu_ldo_output_select(PMU_LDOVS_HIGH);

    rcu_osci_on(RCU_HXTAL);
    if (rcu_osci_stab_wait(RCU_HXTAL) != SUCCESS)
    {
        Error_Handler();
    }

    /* CK_PLLP = (HXTAL / pll_psc) * pll_n / pll_p = (25MHz / 25) * 400 / 2 = 200MHz.
     * NOTE: GigaDevice's own system_clock_200m_25m_hxtal() in system_gd32f5xx.c uses
     * pll_n=336, which gives 168MHz -- that is a GigaDevice copy-paste bug. We use 400. */
    if (rcu_pll_config(RCU_PLLSRC_HXTAL, 25U, 400U, 2U, 8U) != SUCCESS)
    {
        Error_Handler();
    }
    rcu_osci_on(RCU_PLL_CK);
    if (rcu_osci_stab_wait(RCU_PLL_CK) != SUCCESS)
    {
        Error_Handler();
    }

    rcu_ahb_clock_config(RCU_AHB_CKSYS_DIV1);    /* AHB  = SYSCLK / 1 = 200MHz */
    rcu_apb1_clock_config(RCU_APB1_CKAHB_DIV4);  /* APB1 = AHB / 4    =  50MHz */
    rcu_apb2_clock_config(RCU_APB2_CKAHB_DIV2);  /* APB2 = AHB / 2    = 100MHz */

    /* Enable high-driver mode before switching to PLL.
     * pmu_highdriver_mode_enable() sets PMU_CTL_HDEN (BIT(16)).
     * pmu_highdriver_switch_select(PMU_HIGHDR_SWITCH_EN) sets PMU_CTL_HDS (BIT(17)). */
    pmu_highdriver_mode_enable();
    while (!(PMU_CS & PMU_CS_HDRF))
    {
    }
    pmu_highdriver_switch_select(PMU_HIGHDR_SWITCH_EN);
    while (!(PMU_CS & PMU_CS_HDSRF))
    {
    }

    rcu_system_clock_source_config(RCU_CKSYSSRC_PLLP);
    /* NOTE: RCU_SCSS_PLLP (CFG0_SCSS(2) == 8) is the correct readback constant here --
     * NOT RCU_CKSYSSRC_PLLP (CFG0_SCS(2) == 2). They are different bit positions in the
     * same register (SCSS is a status field, SCS is a control field). Using the wrong one
     * always fails this check even when the clock switch succeeded -- see the analogous
     * comment in mm-ekh18-gd32f470/bsp/Core/Src/main.c. */
    if (rcu_system_clock_source_get() != RCU_SCSS_PLLP)
    {
        Error_Handler();
    }

    SystemCoreClockUpdate();

    nvic_priority_group_set(NVIC_PRIGROUP_PRE4_SUB0);
}

/**
 * @brief GPIO Initialization Function: sets up MM8108 control/status pins.
 */
static void MX_GPIO_Init(void)
{
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_SYSCFG);

    /* RESET_N (PC4), WAKE (PC5): push-pull outputs, initially low */
    gpio_bit_reset(RESET_N_GPIO_Port, RESET_N_Pin);
    gpio_bit_reset(WAKE_GPIO_Port, WAKE_Pin);
    gpio_mode_set(RESET_N_GPIO_Port, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, RESET_N_Pin | WAKE_Pin);
    gpio_output_options_set(RESET_N_GPIO_Port, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ,
                            RESET_N_Pin | WAKE_Pin);

    /* SPI_CS (PB12): push-pull output, soft NSS, initially deasserted (high) */
    gpio_bit_set(SPI_CS_GPIO_Port, SPI_CS_Pin);
    gpio_mode_set(SPI_CS_GPIO_Port, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, SPI_CS_Pin);
    gpio_output_options_set(SPI_CS_GPIO_Port, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, SPI_CS_Pin);

    /* SPI_IRQ (PC2), BUSY (PC3): inputs, EXTI configured for falling/rising edge. */
    gpio_mode_set(SPI_IRQ_GPIO_Port, GPIO_MODE_INPUT, GPIO_PUPD_NONE, SPI_IRQ_Pin);
    gpio_mode_set(BUSY_GPIO_Port, GPIO_MODE_INPUT, GPIO_PUPD_PULLDOWN, BUSY_Pin);

    syscfg_exti_line_config(EXTI_SOURCE_GPIOC, EXTI_SOURCE_PIN2);
    exti_init(SPI_IRQ_LINE, EXTI_INTERRUPT, EXTI_TRIG_FALLING);

    syscfg_exti_line_config(EXTI_SOURCE_GPIOC, EXTI_SOURCE_PIN3);
    /* Rising edge only (BUSY becoming asserted) -- NOT EXTI_TRIG_BOTH, which fires twice per
     * toggle and breaks the porting_assistant BUSY IRQ test (see mm-ekh18-gd32f470 for proof). */
    exti_init(BUSY_IRQ_LINE, EXTI_INTERRUPT, EXTI_TRIG_RISING);

    nvic_irq_enable(SPI_IRQn, 15U, 0U);
    nvic_irq_enable(BUSY_IRQn, 15U, 0U);
}

/**
 * @brief SPI1 Initialization Function: MM8108 host interface.
 *
 * Not static: also called from mm_shims/mmhal_wlan.c (via SPI_PERIPH_INIT()) to re-initialise
 * SPI1 in mmhal_wlan_init() after mmhal_wlan_deinit() has torn it down.
 *
 * SPI1 is on APB1 (50MHz). SPI_PSC_2 gives 50MHz/2 = 25MHz SPI clock.
 */
void MX_SPI1_Init(void)
{
    spi_parameter_struct spi_init_struct;

    rcu_periph_clock_enable(RCU_SPI1);

    /* DMA0 clock and RX-channel IRQ only need enabling once but are harmless to re-enable. */
    rcu_periph_clock_enable(RCU_DMA0);
    nvic_irq_enable(SPI_RX_DMA_IRQn, 15U, 0U);

    gpio_mode_set(SPI_SCK_GPIO_Port, GPIO_MODE_AF, GPIO_PUPD_NONE,
                 SPI_SCK_Pin | SPI_MISO_Pin | SPI_MOSI_Pin);
    gpio_output_options_set(SPI_SCK_GPIO_Port, GPIO_OTYPE_PP, GPIO_OSPEED_MAX,
                            SPI_SCK_Pin | SPI_MISO_Pin | SPI_MOSI_Pin);
    gpio_af_set(SPI_SCK_GPIO_Port, GPIO_AF_5, SPI_SCK_Pin | SPI_MISO_Pin | SPI_MOSI_Pin);

    spi_struct_para_init(&spi_init_struct);
    spi_init_struct.device_mode = SPI_MASTER;
    spi_init_struct.trans_mode = SPI_TRANSMODE_FULLDUPLEX;
    spi_init_struct.frame_size = SPI_FRAMESIZE_8BIT;
    spi_init_struct.nss = SPI_NSS_SOFT;
    spi_init_struct.endian = SPI_ENDIAN_MSB;
    spi_init_struct.clock_polarity_phase = SPI_CK_PL_LOW_PH_1EDGE;
    /* APB1=50MHz, SPI_PSC_2 -> 25MHz SPI clock */
    spi_init_struct.prescale = SPI_PSC_2;
    spi_init(SPI_PERIPH, &spi_init_struct);
    spi_nss_output_disable(SPI_PERIPH);

    spi_enable(SPI_PERIPH);
}

/**
 * @brief USART0 Initialization Function: log console, 115200 8N1.
 */
static void MX_USART0_Init(void)
{
    rcu_periph_clock_enable(RCU_USART0);

    gpio_mode_set(LOG_USART_TX_GPIO_Port, GPIO_MODE_AF, GPIO_PUPD_NONE,
                 LOG_USART_TX_Pin | LOG_USART_RX_Pin);
    gpio_output_options_set(LOG_USART_TX_GPIO_Port, GPIO_OTYPE_PP, GPIO_OSPEED_10MHZ,
                            LOG_USART_TX_Pin | LOG_USART_RX_Pin);
    gpio_af_set(LOG_USART_TX_GPIO_Port, GPIO_AF_7, LOG_USART_TX_Pin | LOG_USART_RX_Pin);

    usart_baudrate_set(LOG_USART, 115200U);
    usart_word_length_set(LOG_USART, USART_WL_8BIT);
    usart_stop_bit_set(LOG_USART, USART_STB_1BIT);
    usart_parity_config(LOG_USART, USART_PM_NONE);
    usart_receive_config(LOG_USART, USART_RECEIVE_ENABLE);
    usart_transmit_config(LOG_USART, USART_TRANSMIT_ENABLE);

    nvic_irq_enable(LOG_USART_IRQ, 15U, 0U);

    usart_enable(LOG_USART);
}

/**
 * @brief This function is executed in case of a fatal setup error (e.g. clock failed to lock).
 */
void Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
        MMPORT_BREAKPOINT();
    }
}
