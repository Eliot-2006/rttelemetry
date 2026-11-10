#include "drivers.h"

#define SPIN_LIMIT 1000000u

static bool wait_set(const volatile uint32_t *reg, uint32_t mask, uint32_t want)
{
    for (uint32_t i = 0; i < SPIN_LIMIT; i++) {
        if ((*reg & mask) == want) return true;
    }
    return false;
}

bool clock_init(void)
{
    RCC->CR |= RCC_CR_HSEON;
    if (!wait_set(&RCC->CR, RCC_CR_HSERDY, RCC_CR_HSERDY)) return false;

    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    PWR->CR |= PWR_CR_VOS;                                   /* scale 1 for 168 MHz */

    RCC->CFGR = RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV4 | RCC_CFGR_PPRE2_DIV2;
    /* VCO = 8 MHz / M(8) * N(336) = 336 MHz; SYSCLK = /P(2) = 168 MHz; USB = /Q(7) = 48 MHz */
    RCC->PLLCFGR = (8u << RCC_PLLCFGR_PLLM_Pos) | (336u << RCC_PLLCFGR_PLLN_Pos) |
                   (0u << RCC_PLLCFGR_PLLP_Pos) | RCC_PLLCFGR_PLLSRC_HSE |
                   (7u << RCC_PLLCFGR_PLLQ_Pos);
    RCC->CR |= RCC_CR_PLLON;
    if (!wait_set(&RCC->CR, RCC_CR_PLLRDY, RCC_CR_PLLRDY)) return false;

    FLASH->ACR = FLASH_ACR_LATENCY_5WS | FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    if (!wait_set(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_PLL)) return false;

    SystemCoreClock = HCLK_HZ;
    return true;
}

void markers_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN;
    for (unsigned p = MARK_ISR; p <= MARK_FLT; p++) gpio_mode(MARK_PORT, p, GPIO_MODE_OUT, 0);
}
