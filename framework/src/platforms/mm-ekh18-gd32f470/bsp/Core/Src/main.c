/*
 * main.c - clock/peripheral bring-up for mm-ekh18-gd32f470.
 *
 * Modelled on the STM32CubeMX-generated main.c used by the other mm-ekh08-* platforms (see e.g.
 * framework/src/platforms/mm-ekh08-wb55/bsp/Core/Src/main.c), but written against GigaDevice's
 * real GD32F4xx_standard_peripheral driver (vendored unmodified in framework/src/bsps/gd32f4xx/)
 * instead of the STM32Cube HAL/LL.
 *
 * IMPORTANT: see this platform's readme.md. The clock tree below uses a 25MHz HXTAL crystal to
 * reach a 200MHz SYSCLK, and the GPIO pin assignments in main.h are wired to the MMECH18 module --
 * both are confirmed against the real schematic for the GD32F470VGT6 dev board this platform
 * targets (see readme.md). If you are using different hardware, re-verify both against your own
 * schematic before use.
 *
 * Copyright 2026 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "main.h"
#include "mmosal.h"
#include "mmport.h"

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
/* MX_SPI0_Init() is not static: it is declared in main.h (SPI_PERIPH_INIT) and re-invoked by
 * mmhal_wlan_init() (see mm_shims/mmhal_wlan.c) to re-initialise SPI0 after mmhal_wlan_deinit()
 * has torn it down. */
static void MX_USART0_Init(void);

void app_init(void);

int main(void)
{
    /* Configure the system clock before anything else touches a peripheral. */
    SystemClock_Config();

    /* Initialise all configured peripherals. */
    MX_GPIO_Init();
    MX_SPI0_Init();
    MX_USART0_Init();

    mmosal_main(app_init);

    /* mmosal_main() starts the scheduler and does not return; if it somehow does, halt here
     * rather than running off into undefined behaviour. */
    while (1)
    {
        MMPORT_BREAKPOINT();
    }
}

/**
 * @brief System Clock Configuration.
 *
 * Configures: HXTAL (25MHz, confirmed from the real schematic's X1 crystal designator) -> main
 * PLL -> SYSCLK = 200MHz, AHB = 200MHz, APB1 = 50MHz, APB2 = 100MHz.
 */
void SystemClock_Config(void)
{
    /* Increase flash wait states before increasing SYSCLK. 5 wait states matches GigaDevice's
     * "AHB clock frequency vs FMC_WS" table for VDD 2.7-3.6V at ~200MHz on comparable GD32F4xx
     * high-density parts -- confirm against the GD32F470 datasheet if you change SYSCLK. */
    fmc_wscnt_set(WS_WSCNT_5);

    rcu_osci_on(RCU_HXTAL);
    if (rcu_osci_stab_wait(RCU_HXTAL) != SUCCESS)
    {
        Error_Handler();
    }

    /* CK_PLLP = (HXTAL / pll_psc) * pll_n / pll_p = (25MHz / 25) * 400 / 2 = 200MHz.
     * pll_q is only used to derive the 48MHz USB/SDIO/TRNG clock (400MHz / 8 = 50MHz, close
     * enough given USB/SDIO are not used on this platform; TRNG does not require CK48M). */
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

    rcu_system_clock_source_config(RCU_CKSYSSRC_PLLP);
    /* NOTE: rcu_system_clock_source_get() reads back the CFG0.SCSS status field (bits[3:2]), which
     * is a *different* bit position from CFG0.SCS (bits[1:0]) used to request the source above --
     * RCU_CKSYSSRC_PLLP (CFG0_SCS(2) == 2) is only valid as input to
     * rcu_system_clock_source_config(); comparing the readback against it will always fail even on
     * a successful switch. Use RCU_SCSS_PLLP (CFG0_SCSS(2) == 8) here instead. Confirmed via GDB:
     * with the old (wrong) comparison, HXTAL stabilized and the PLL configured and locked
     * successfully, but this check still always spuriously failed and landed in Error_Handler(),
     * so the app never got past clock setup on real hardware despite the clock tree actually being
     * correct. */
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

    /* RESET_N, WAKE: push-pull outputs, initially low (reset asserted / wake deasserted) */
    gpio_bit_reset(RESET_N_GPIO_Port, RESET_N_Pin);
    gpio_bit_reset(WAKE_GPIO_Port, WAKE_Pin);
    gpio_mode_set(RESET_N_GPIO_Port, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, RESET_N_Pin | WAKE_Pin);
    gpio_output_options_set(RESET_N_GPIO_Port, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ,
                            RESET_N_Pin | WAKE_Pin);

    /* SPI_CS: push-pull output, bit-banged (soft NSS), initially deasserted (high) */
    gpio_bit_set(SPI_CS_GPIO_Port, SPI_CS_Pin);
    gpio_mode_set(SPI_CS_GPIO_Port, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, SPI_CS_Pin);
    gpio_output_options_set(SPI_CS_GPIO_Port, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, SPI_CS_Pin);

    /* SPI_IRQ, BUSY: inputs, EXTI configured for falling/rising edge respectively. Both are on
     * GPIOB (PB1/PB0), each landing on its own dedicated single-line EXTI vector. */
    gpio_mode_set(SPI_IRQ_GPIO_Port, GPIO_MODE_INPUT, GPIO_PUPD_NONE, SPI_IRQ_Pin);
    gpio_mode_set(BUSY_GPIO_Port, GPIO_MODE_INPUT, GPIO_PUPD_PULLDOWN, BUSY_Pin);

    syscfg_exti_line_config(EXTI_SOURCE_GPIOB, EXTI_SOURCE_PIN1);
    exti_init(SPI_IRQ_LINE, EXTI_INTERRUPT, EXTI_TRIG_FALLING);

    syscfg_exti_line_config(EXTI_SOURCE_GPIOB, EXTI_SOURCE_PIN0);
    /* Rising edge only (BUSY becoming asserted), matching mm-mm8108-ekh05's
     * LL_EXTI_TRIGGER_RISING -- NOT EXTI_TRIG_BOTH, which double-fires (once per assert, once per
     * deassert) and was confirmed via real hardware testing to break the porting_assistant BUSY
     * IRQ test (expected 2 invocations for 2 toggle cycles, got 4). */
    exti_init(BUSY_IRQ_LINE, EXTI_INTERRUPT, EXTI_TRIG_RISING);

    nvic_irq_enable(SPI_IRQn, 15U, 0U);
    nvic_irq_enable(BUSY_IRQn, 15U, 0U);

    /* Interrupts are enabled later via mmhal_wlan_set_spi_irq_enabled()/set_busy_irq_enabled(),
     * not here -- see mm_shims/mmhal_wlan.c. */
}

/**
 * @brief SPI0 Initialization Function: MM8108 host interface (polled for small transfers, DMA for
 * bulk transfers -- see mm_shims/mmhal_wlan.c).
 *
 * Not static: also called from mm_shims/mmhal_wlan.c (via SPI_PERIPH_INIT(), see main.h) to
 * re-initialise SPI0 in mmhal_wlan_init(), after mmhal_wlan_deinit() has torn it down.
 */
void MX_SPI0_Init(void)
{
    spi_parameter_struct spi_init_struct;

    rcu_periph_clock_enable(RCU_SPI0);

    /* DMA1 clock and the RX-channel transfer-complete IRQ only need enabling once (they aren't
     * touched by wlan_hal_spi_deinit()'s teardown -- see mm_shims/mmhal_wlan.c), but doing it here
     * unconditionally on every (re-)init is harmless and keeps this function self-contained. Only
     * the RX channel's completion IRQ is used: SPI is full-duplex, so RX and TX finish together,
     * and waiting on one is sufficient (matching the mm-ekh18-wb55 reference). */
    rcu_periph_clock_enable(RCU_DMA1);
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
    /* APB2 / 2 = 50MHz SPI clock at the 100MHz APB2 configured above. Requested rate was 40MHz,
     * but the GD32F470 SPI prescaler only divides by powers of 2 (2/4/8/...) off a fixed 100MHz
     * APB2, so 40MHz isn't reachable without reworking the whole clock tree (APB2 would need to
     * be 80MHz, which cascades into AHB/APB1 and therefore USART baud rate calculation and flash
     * wait states); 50MHz is the closest achievable rate above 40MHz. Adjust to suit your board's
     * signal integrity if you see SPI errors at this rate. */
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
    gpio_output_options_set(LOG_USART_TX_GPIO_Port, GPIO_OTYPE_PP, GPIO_OSPEED_25MHZ,
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
