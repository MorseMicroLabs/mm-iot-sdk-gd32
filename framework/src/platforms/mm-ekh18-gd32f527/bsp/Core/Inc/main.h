/*
 * main.h - board/pin definitions for mm-ekh18-gd32f527.
 *
 * The pin assignments below are for a GD32F527ZMT7 (LQFP144) core board, confirmed against
 * the board schematic.  The board uses SPI1 (PB13/14/15) for the MM8108 WiFi HaLow module,
 * with control/interrupt signals on GPIOB (RESET_N, WAKE) and GPIOC (SPI_IRQ=PC2, BUSY=PC3).
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

/* gd32f5xx.h pulls in gd32f5xx_libopt.h, which includes every peripheral driver header. */
#include "gd32f5xx.h"

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);
void MX_SPI1_Init(void);

/* Exported macros: peripheral/IRQ selection --------------------------------*/
#define SPI_PERIPH          (SPI1)
#define SPI_PERIPH_IRQn      (SPI1_IRQn)
#define SPI_PERIPH_INIT       MX_SPI1_Init

/* SPI1 DMA request mapping: confirmed against GD32F5xx User Manual, Table 10-4
 * "Peripheral requests to DMA0", PERIEN[2:0] = 0b000 (DMA_SUBPERI0):
 *   DMA0 Channel 3 = SPI1_RX
 *   DMA0 Channel 4 = SPI1_TX */
#define SPI_DMA_PERIPH       (DMA0)
#define SPI_RX_DMA_CHANNEL   (DMA_CH3)
#define SPI_TX_DMA_CHANNEL   (DMA_CH4)
#define SPI_RX_DMA_SUBPERI   (DMA_SUBPERI0)
#define SPI_TX_DMA_SUBPERI   (DMA_SUBPERI0)
#define SPI_RX_DMA_IRQn         (DMA0_Channel3_IRQn)
#define SPI_RX_DMA_IRQ_HANDLER  DMA0_Channel3_IRQHandler

/* SPI_IRQ (PC2) and BUSY (PC3) each get their own dedicated single-line EXTI vector. */
#define SPI_IRQn            (EXTI2_IRQn)
#define SPI_IRQ_LINE        (EXTI_2)
#define SPI_IRQ_HANDLER     EXTI2_IRQHandler

#define BUSY_IRQn           (EXTI3_IRQn)
#define BUSY_IRQ_LINE       (EXTI_3)
#define BUSY_IRQ_HANDLER    EXTI3_IRQHandler

#define LOG_USART               (USART0)
#define LOG_USART_IRQ            (USART0_IRQn)
#define LOG_USART_IRQ_HANDLER    USART0_IRQHandler

/* LOG_USART_RX GPIO (PA10) is used as a deep sleep one-shot wake interrupt */
#define LOG_USART_RX_IRQn           (EXTI10_15_IRQn)
#define LOG_USART_RX_IRQ_LINE       (EXTI_10)
#define LOG_USART_RX_IRQ_HANDLER    EXTI10_15_IRQHandler
#define LOG_USART_RX_EXTI_Port      (EXTI_SOURCE_GPIOA)
#define LOG_USART_RX_EXTI_Line      (EXTI_SOURCE_PIN10)

/* Private defines: MMECH18 <-> GD32F527ZMT7 pin mapping -------------------------------------- */
#define RESET_N_Pin         GPIO_PIN_4
#define RESET_N_GPIO_Port   GPIOC
#define WAKE_Pin            GPIO_PIN_5
#define WAKE_GPIO_Port      GPIOC
#define BUSY_Pin            GPIO_PIN_3
#define BUSY_GPIO_Port      GPIOC
#define SPI_IRQ_Pin         GPIO_PIN_2
#define SPI_IRQ_GPIO_Port   GPIOC

/* SPI1 default pins: PB12(NSS/CS), PB13(SCK), PB14(MISO), PB15(MOSI), AF5 */
#define SPI_CS_Pin          GPIO_PIN_12
#define SPI_CS_GPIO_Port    GPIOB
#define SPI_SCK_Pin         GPIO_PIN_13
#define SPI_SCK_GPIO_Port   GPIOB
#define SPI_MISO_Pin        GPIO_PIN_14
#define SPI_MISO_GPIO_Port  GPIOB
#define SPI_MOSI_Pin        GPIO_PIN_15
#define SPI_MOSI_GPIO_Port  GPIOB

/* USART0: PA9=TX, PA10=RX, AF7 */
#define LOG_USART_TX_Pin        GPIO_PIN_9
#define LOG_USART_TX_GPIO_Port  GPIOA
#define LOG_USART_RX_Pin        GPIO_PIN_10
#define LOG_USART_RX_GPIO_Port  GPIOA

/* Onboard status LEDs (active-LOW: VCC -> LED -> resistor -> GPIO):
 *   LED0 = PA2 (Red, used as error LED)
 *   LED1 = PA3 (Green) */
#define LED_ERROR_Pin           GPIO_PIN_2
#define LED_ERROR_GPIO_Port     GPIOA
#define LED_GREEN_Pin           GPIO_PIN_3
#define LED_GREEN_GPIO_Port     GPIOA

#ifdef __cplusplus
}
#endif

#endif /* MAIN_H */
