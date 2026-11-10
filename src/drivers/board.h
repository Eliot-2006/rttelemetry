/* Board-level constants for STM32F407VG on STM32F4DISCOVERY (see docs/hardware.md).
 * One definition of clocks, pins and NVIC priorities shared by every driver. */
#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>
#include "stm32f4xx.h"

#define HCLK_HZ        168000000u
#define PCLK1_HZ        42000000u
#define PCLK2_HZ        84000000u
#define TIM_APB1_HZ     84000000u   /* APB1 timer clock = 2 x PCLK1 (APB1 prescaler != 1) */

#define TELEM_BAUD      921600u

/* NVIC priorities (4 bits, lower = more urgent). FreeRTOS masks >= 5 (configMAX_SYSCALL...). */
#define PRIO_SAMPLE_TRIGGER  4u   /* TIM3: never calls the RTOS, never masked by kernel */
#define PRIO_ADC_DMA         5u   /* DMA2_Stream0: ring push + notify */
#define PRIO_IMU_EXTI        6u
#define PRIO_I2C             6u
#define PRIO_UART_TX_DMA     7u   /* DMA1_Stream6 */
#define PRIO_UART_RX         7u   /* USART2 */

/* DMA cannot reach CCM on silicon (Renode allows it: divergence D4). */
#define DMA_ADDR_OK(p) ((uintptr_t)(p) >= 0x20000000u && (uintptr_t)(p) < 0x20020000u)

/* GPIO helpers */
static inline void gpio_mode(GPIO_TypeDef *g, unsigned pin, unsigned mode, unsigned af)
{
    g->MODER = (g->MODER & ~(3u << (pin * 2))) | (mode << (pin * 2));
    if (mode == 2u) {
        volatile uint32_t *afr = &g->AFR[pin >> 3];
        unsigned sh = (pin & 7u) * 4u;
        *afr = (*afr & ~(0xFu << sh)) | (af << sh);
    }
}
#define GPIO_MODE_IN   0u
#define GPIO_MODE_OUT  1u
#define GPIO_MODE_AF   2u
#define GPIO_MODE_AN   3u

/* Timing markers on the Discovery LEDs (logic-analyzer probes in M9). */
#define MARK_PORT GPIOD
#define MARK_ISR  12u
#define MARK_PROC 13u
#define MARK_TX   14u
#define MARK_FLT  15u
static inline void mark_hi(unsigned pin) { MARK_PORT->BSRR = 1u << pin; }
static inline void mark_lo(unsigned pin) { MARK_PORT->BSRR = 1u << (pin + 16u); }

#endif
