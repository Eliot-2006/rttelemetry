#include "drivers.h"

void tim2_timebase_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    (void)RCC->APB1ENR;
    TIM2->CR1 = 0;
    TIM2->PSC = TIM_APB1_HZ / 1000000u - 1u;   /* 1 MHz tick */
    TIM2->ARR = 0xFFFFFFFFu;
    TIM2->EGR = TIM_EGR_UG;                    /* load PSC now */
    TIM2->CR1 = TIM_CR1_CEN;
}

static void tim3_program(uint32_t rate_hz)
{
    TIM3->CR1 = 0;
    TIM3->DIER = 0;
    if (rate_hz == 0u) return;
    TIM3->PSC = TIM_APB1_HZ / 1000000u - 1u;   /* 1 MHz tick */
    TIM3->ARR = 1000000u / rate_hz - 1u;       /* rates <= 8 kHz -> ARR >= 124, fits 16 bits for >= 16 Hz */
    TIM3->CNT = 0;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->SR = 0;
    TIM3->DIER = TIM_DIER_UIE;
    TIM3->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
}

void tim3_trigger_init(uint32_t rate_hz)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
    (void)RCC->APB1ENR;
    NVIC_SetPriority(TIM3_IRQn, PRIO_SAMPLE_TRIGGER);
    tim3_program(rate_hz);
    NVIC_EnableIRQ(TIM3_IRQn);
}

void tim3_set_rate(uint32_t rate_hz)
{
    NVIC_DisableIRQ(TIM3_IRQn);                /* no partially-updated PSC/ARR seen by the ISR */
    tim3_program(rate_hz);
    NVIC_ClearPendingIRQ(TIM3_IRQn);
    NVIC_EnableIRQ(TIM3_IRQn);
}
