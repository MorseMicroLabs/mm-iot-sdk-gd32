/*
 * main.h - board/pin definitions for mm-ekh18-gd32f470.
 *
 * The pin assignments below are for a GD32F470VGT6 (LQFP100) core board, confirmed against the
 * real schematic packaged with that board ("SCH_LQFP100-14_14-V03_ARM_25M"). That board is a bare
 * GPIO breakout (all pins broken out sequentially on a 50-pin "H2" header) rather than an
 * Arduino-shield-compatible board, so there is no mechanically-mating MMECH18 connector -- the
 * pins below were chosen from H2's free/unused pins (avoiding PA0-PA2, which drive the board's
 * onboard status LEDs, and PA9/PA10, which are the board's own USART0 TX/RX broken out to H2) and
 * must be wired to the MMECH18 module with jumper wires. See
 * framework/src/platforms/mm-ekh18-gd32f470/readme.md for the full pin table and H2 pin numbers.
 * If you are using a different GD32F470 board, update the "Private defines" section below to
 * match your actual schematic.
 *
 * Copyright 2026 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MAIN_H
#define MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* gd32f4xx.h pulls in gd32f4xx_libopt.h, which in turn includes every peripheral driver header
 * (rcu/gpio/spi/usart/exti/syscfg/fmc/misc/trng/...) -- see bsp/Core/Inc/gd32f4xx_libopt.h. */
#include "gd32f4xx.h"

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* MX_SPI0_Init() is defined in main.c; it is not static so that mmhal_wlan_init() (see
 * mm_shims/mmhal_wlan.c) can call it via SPI_PERIPH_INIT() below to re-initialise SPI0 after a
 * teardown in mmhal_wlan_deinit(). */
void MX_SPI0_Init(void);

/* Exported macros: peripheral/IRQ selection --------------------------------*/
#define SPI_PERIPH          (SPI0)
#define SPI_PERIPH_IRQn      (SPI0_IRQn)
#define SPI_PERIPH_INIT       MX_SPI0_Init

/* SPI_IRQ (PB1) and BUSY (PB0) each get their own dedicated single-line EXTI vector rather than
 * sharing the EXTI9_5/EXTI10_15 group vectors used on some other mm-ekh08-* platforms. */
#define SPI_IRQn            (EXTI1_IRQn)
#define SPI_IRQ_LINE        (EXTI_1)
#define SPI_IRQ_HANDLER     EXTI1_IRQHandler

#define BUSY_IRQn           (EXTI0_IRQn)
#define BUSY_IRQ_LINE       (EXTI_0)
#define BUSY_IRQ_HANDLER    EXTI0_IRQHandler

#define LOG_USART               (USART0)
#define LOG_USART_IRQ            (USART0_IRQn)
#define LOG_USART_IRQ_HANDLER    USART0_IRQHandler

/* LOG_USART_RX GPIO (PA10) is used as a deep sleep one-shot wake interrupt, shared with EXTI10-15 */
#define LOG_USART_RX_IRQn           (EXTI10_15_IRQn)
#define LOG_USART_RX_IRQ_LINE       (EXTI_10)
#define LOG_USART_RX_IRQ_HANDLER    EXTI10_15_IRQHandler
#define LOG_USART_RX_EXTI_Port      (EXTI_SOURCE_GPIOA)
#define LOG_USART_RX_EXTI_Line      (EXTI_SOURCE_PIN10)

/* Private defines: MMECH18 <-> GD32F470VGT6 pin mapping ---------------------------------------
 *
 * Confirmed against the real schematic ("SCH_LQFP100-14_14-V03_ARM_25M"), all taken from the
 * board's "H2" GPIO breakout header (H2 pin numbers noted below; see readme.md for the full
 * table). These must be wired to the MMECH18 module with jumper wires -- H2 is not a
 * shield-compatible connector. */
#define RESET_N_Pin         GPIO_PIN_4
#define RESET_N_GPIO_Port   GPIOC          /* H2 pin 9 */
#define WAKE_Pin            GPIO_PIN_5
#define WAKE_GPIO_Port      GPIOC          /* H2 pin 10 */
#define BUSY_Pin            GPIO_PIN_0
#define BUSY_GPIO_Port      GPIOB          /* H2 pin 11 */
#define SPI_IRQ_Pin         GPIO_PIN_1
#define SPI_IRQ_GPIO_Port   GPIOB          /* H2 pin 12 */

/* SPI0's default (non-remapped) pins happen to land on four consecutive H2 pins (5-8). */
#define SPI_CS_Pin          GPIO_PIN_4
#define SPI_CS_GPIO_Port    GPIOA          /* H2 pin 5 */
#define SPI_SCK_Pin         GPIO_PIN_5
#define SPI_SCK_GPIO_Port   GPIOA          /* H2 pin 6 */
#define SPI_MISO_Pin        GPIO_PIN_6
#define SPI_MISO_GPIO_Port  GPIOA          /* H2 pin 7 */
#define SPI_MOSI_Pin        GPIO_PIN_7
#define SPI_MOSI_GPIO_Port  GPIOA          /* H2 pin 8 */

/* PA9/PA10 are the board's own USART0 TX/RX, silkscreen-labelled on the schematic itself
 * ("串口打印TTL电平: PA9--TXD, PA10--RXD"), broken out to H2 pins 48 and 46 respectively. */
#define LOG_USART_TX_Pin        GPIO_PIN_9
#define LOG_USART_TX_GPIO_Port  GPIOA      /* H2 pin 48 */
#define LOG_USART_RX_Pin        GPIO_PIN_10
#define LOG_USART_RX_GPIO_Port  GPIOA      /* H2 pin 46 */

/* Onboard status LEDs (from the real schematic): LED2 (green, PA0), LED3 (blue, PA1), and LED4
 * (yellow, PA2) are each wired VCC -> LED -> 1k resistor -> GPIO, i.e. active-LOW (driving the pin
 * low sinks current through the LED and turns it on). There is no GPIO-controllable red or white
 * LED (LED1 is a fixed, always-on red power indicator wired directly across VCC/GND, not a GPIO).
 * See mm_shims/mmhal_app.c: mmhal_set_led() drives LED_GREEN/LED_BLUE from these; the yellow LED
 * is repurposed as the error LED since no true red LED is available via GPIO. */
#define LED_GREEN_Pin           GPIO_PIN_0
#define LED_GREEN_GPIO_Port     GPIOA      /* H2 pin 1 */
#define LED_BLUE_Pin            GPIO_PIN_1
#define LED_BLUE_GPIO_Port      GPIOA      /* H2 pin 2 */
#define LED_ERROR_Pin           GPIO_PIN_2
#define LED_ERROR_GPIO_Port     GPIOA      /* H2 pin 3 */

/** Debug pins are not wired to a known GPIO on this board. mmhal_set_debug_pins() is therefore a
 *  no-op -- add pin definitions here and update mm_shims/mmhal_app.c if you need debug pins. */

#ifdef __cplusplus
}
#endif

#endif /* MAIN_H */
