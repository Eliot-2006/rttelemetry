/* Register-level drivers (CMSIS only, no HAL). Each function documents its context. */
#ifndef DRIVERS_H
#define DRIVERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "board.h"

/* ---- clock.c ---- */
/* HSE 8 MHz -> PLL -> 168 MHz; APB1 42, APB2 84; flash 5 WS + caches. False if a ready flag times out. */
bool clock_init(void);
void markers_init(void);   /* PD12..15 outputs */

/* ---- uart.c (USART2, PA2/PA3) ---- */
void uart_init(uint32_t baud);
void uart_write_polled(const void *data, size_t len);          /* any context, busy-waits */
/* Start DMA1_Stream6 transmit. Buffer must be in DMA-capable SRAM and stay valid until
 * uart_tx_done_isr() runs. Returns false if a transfer is already in flight. */
bool uart_tx_dma_start(const uint8_t *buf, uint16_t len);
void uart_tx_dma_abort(void);
void uart_rx_enable_irq(void);
/* Application hooks (weak defaults do nothing), called from ISR context. */
void uart_tx_done_isr(void);
void uart_rx_byte_isr(uint8_t byte);
extern volatile uint32_t uart_rx_overruns;

/* ---- tim.c ---- */
void     tim2_timebase_init(void);                    /* free-running 1 MHz, 32-bit */
static inline uint32_t now_us(void) { return TIM2->CNT; }
void     tim3_trigger_init(uint32_t rate_hz);         /* update IRQ at rate_hz (0 = stopped) */
void     tim3_set_rate(uint32_t rate_hz);             /* glitch-free reconfigure, task context */

/* ---- adc.c (ADC1 scan IN1, IN8, IN16 temp, IN17 vrefint -> DMA2_Stream0) ---- */
#define ADC_NCH 4u
void adc_init(void);
/* ISR context (TIM3): arm DMA for one scan into dst[ADC_NCH] (32-bit words, value in bits 0-11)
 * and software-start the ADC. */
void adc_start_scan(volatile uint32_t *dst);
/* ISR context (DMA2_Stream0): returns true if the scan completed (and clears the flag). */
bool adc_dma_take_complete(void);
/* ISR context (TIM3): if the scan's DMA transfers stopped short, abort it and re-arm the ADC
 * (returns true). Returns false if all transfers are done: the DMA ISR owns that scan. */
bool adc_abort_stalled_scan(void);

/* ---- iwdg.c ---- */
void iwdg_start(uint32_t timeout_ms);                 /* 1 ms resolution, max 4095 ms */
static inline void iwdg_kick(void) { IWDG->KR = 0xAAAAu; }

/* ---- i2c.c (I2C1, PB8 SCL / PB9 SDA) ---- */
typedef enum { I2C_OK = 0, I2C_ERR_NACK, I2C_ERR_TIMEOUT, I2C_ERR_BUS } i2c_status_t;
void i2c_init(void);
i2c_status_t i2c_read_reg(uint8_t addr7, uint8_t reg, uint8_t *dst, size_t n);
i2c_status_t i2c_write_reg(uint8_t addr7, uint8_t reg, uint8_t val);
void i2c_bus_recover(void);                           /* 9 SCL pulses + STOP, then re-init */

#endif
