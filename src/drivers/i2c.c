/* I2C1 master, polled, I2C v1 IP (RM0090 27.3.3, AN2824 sequences for N=1, N=2, N>2).
 * Called from task context only. Every wait is bounded; on timeout the caller decides recovery. */
#include "drivers.h"

#define I2C_SPIN 20000u   /* ~ >1 ms of polling at 168 MHz: well above one byte at 400 kHz (25 us) */

static i2c_status_t wait_sr1(uint32_t mask)
{
    for (uint32_t i = 0; i < I2C_SPIN; i++) {
        uint32_t sr1 = I2C1->SR1;
        if (sr1 & I2C_SR1_AF) {
            I2C1->SR1 = ~I2C_SR1_AF;
            I2C1->CR1 |= I2C_CR1_STOP;
            return I2C_ERR_NACK;
        }
        if (sr1 & (I2C_SR1_BERR | I2C_SR1_ARLO)) {
            I2C1->SR1 = ~(I2C_SR1_BERR | I2C_SR1_ARLO);
            return I2C_ERR_BUS;
        }
        if (sr1 & mask) return I2C_OK;
    }
    return I2C_ERR_TIMEOUT;
}

static void clear_addr(void) { (void)I2C1->SR1; (void)I2C1->SR2; }

static i2c_status_t start_addr(uint8_t addr8)
{
    I2C1->CR1 |= I2C_CR1_START;
    i2c_status_t s = wait_sr1(I2C_SR1_SB);
    if (s != I2C_OK) return s;
    I2C1->DR = addr8;
    return wait_sr1(I2C_SR1_ADDR);
}

void i2c_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIODEN;
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;
    /* STM32F4DISCOVERY: the CS43L22 codec shares I2C1; hold it in reset (PD4 low, UM1472). */
    GPIOD->BSRR = 1u << (4 + 16);
    gpio_mode(GPIOD, 4, GPIO_MODE_OUT, 0);
    (void)RCC->APB1ENR;
    GPIOB->OTYPER |= (1u << 8) | (1u << 9);                       /* open drain */
    GPIOB->PUPDR = (GPIOB->PUPDR & ~((3u << 16) | (3u << 18))) | (1u << 16) | (1u << 18);
    gpio_mode(GPIOB, 8, GPIO_MODE_AF, 4);
    gpio_mode(GPIOB, 9, GPIO_MODE_AF, 4);

    I2C1->CR1 = I2C_CR1_SWRST;
    I2C1->CR1 = 0;
    I2C1->CR2 = PCLK1_HZ / 1000000u;                               /* FREQ = 42 */
    I2C1->CCR = I2C_CCR_FS | (PCLK1_HZ / (3u * 400000u));          /* fast mode 400 kHz, duty 2:1 */
    I2C1->TRISE = (PCLK1_HZ / 1000000u) * 300u / 1000u + 1u;       /* 300 ns max rise time */
    I2C1->CR1 = I2C_CR1_PE;
}

i2c_status_t i2c_write_reg(uint8_t addr7, uint8_t reg, uint8_t val)
{
    i2c_status_t s = start_addr((uint8_t)(addr7 << 1));
    if (s != I2C_OK) return s;
    clear_addr();
    I2C1->DR = reg;
    if ((s = wait_sr1(I2C_SR1_TXE)) != I2C_OK) return s;
    I2C1->DR = val;
    if ((s = wait_sr1(I2C_SR1_BTF)) != I2C_OK) return s;
    I2C1->CR1 |= I2C_CR1_STOP;
    return I2C_OK;
}

i2c_status_t i2c_read_reg(uint8_t addr7, uint8_t reg, uint8_t *dst, size_t n)
{
    if (n == 0u) return I2C_OK;
    i2c_status_t s = start_addr((uint8_t)(addr7 << 1));
    if (s != I2C_OK) return s;
    clear_addr();
    I2C1->DR = reg;
    if ((s = wait_sr1(I2C_SR1_BTF)) != I2C_OK) return s;

    if ((s = start_addr((uint8_t)((addr7 << 1) | 1u))) != I2C_OK) return s;
    if (n == 1u) {
        I2C1->CR1 &= ~I2C_CR1_ACK;
        clear_addr();
        I2C1->CR1 |= I2C_CR1_STOP;
        if ((s = wait_sr1(I2C_SR1_RXNE)) != I2C_OK) return s;
        dst[0] = (uint8_t)I2C1->DR;
        return I2C_OK;
    }
    if (n == 2u) {
        I2C1->CR1 |= I2C_CR1_POS;
        I2C1->CR1 &= ~I2C_CR1_ACK;
        clear_addr();
        if ((s = wait_sr1(I2C_SR1_BTF)) != I2C_OK) return s;
        I2C1->CR1 |= I2C_CR1_STOP;
        dst[0] = (uint8_t)I2C1->DR;
        dst[1] = (uint8_t)I2C1->DR;
        I2C1->CR1 &= ~I2C_CR1_POS;
        return I2C_OK;
    }
    I2C1->CR1 |= I2C_CR1_ACK;
    clear_addr();
    size_t i = 0;
    while (n - i > 3u) {
        if ((s = wait_sr1(I2C_SR1_RXNE)) != I2C_OK) return s;
        dst[i++] = (uint8_t)I2C1->DR;
    }
    /* last three bytes: N-2 in DR, N-1 in shift register when BTF */
    if ((s = wait_sr1(I2C_SR1_BTF)) != I2C_OK) return s;
    I2C1->CR1 &= ~I2C_CR1_ACK;
    dst[i++] = (uint8_t)I2C1->DR;
    if ((s = wait_sr1(I2C_SR1_BTF)) != I2C_OK) return s;
    I2C1->CR1 |= I2C_CR1_STOP;
    dst[i++] = (uint8_t)I2C1->DR;
    if ((s = wait_sr1(I2C_SR1_RXNE)) != I2C_OK) return s;
    dst[i] = (uint8_t)I2C1->DR;
    return I2C_OK;
}

void i2c_bus_recover(void)
{
    /* Release a slave holding SDA low: clock SCL 9 times as GPIO, then generate a STOP. */
    I2C1->CR1 = 0;
    gpio_mode(GPIOB, 8, GPIO_MODE_OUT, 0);
    gpio_mode(GPIOB, 9, GPIO_MODE_OUT, 0);
    GPIOB->BSRR = (1u << 8) | (1u << 9);
    for (int k = 0; k < 9; k++) {
        GPIOB->BSRR = 1u << (8 + 16);
        for (volatile int d = 0; d < 200; d++) {}
        GPIOB->BSRR = 1u << 8;
        for (volatile int d = 0; d < 200; d++) {}
    }
    GPIOB->BSRR = 1u << (9 + 16);       /* SDA low while SCL high ... */
    for (volatile int d = 0; d < 200; d++) {}
    GPIOB->BSRR = 1u << 9;              /* ... then SDA high = STOP */
    RCC->APB1RSTR |= RCC_APB1RSTR_I2C1RST;
    RCC->APB1RSTR &= ~RCC_APB1RSTR_I2C1RST;
    i2c_init();
}
