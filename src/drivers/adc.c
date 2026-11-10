#include "drivers.h"

#define ADC_STREAM DMA2_Stream0         /* ADC1 = DMA2 Stream0 Channel0 */
#define S0_FLAGS (DMA_LIFCR_CTCIF0 | DMA_LIFCR_CHTIF0 | DMA_LIFCR_CTEIF0 | DMA_LIFCR_CDMEIF0 | DMA_LIFCR_CFEIF0)

void adc_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_DMA2EN;
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    (void)RCC->APB2ENR;
    gpio_mode(GPIOA, 1, GPIO_MODE_AN, 0);    /* IN1 */
    gpio_mode(GPIOB, 0, GPIO_MODE_AN, 0);    /* IN8 */

    ADC123_COMMON->CCR = ADC_CCR_ADCPRE_0 | ADC_CCR_TSVREFE;   /* ADCCLK = 84/4 = 21 MHz; temp + vref on */
    ADC1->CR1 = ADC_CR1_SCAN;
    /* IN16/IN17 need >= 10 us sampling (480 cycles @ 21 MHz = 22.9 us); IN1/IN8 56 cycles. */
    ADC1->SMPR1 = (7u << ADC_SMPR1_SMP16_Pos) | (7u << ADC_SMPR1_SMP17_Pos);
    ADC1->SMPR2 = (3u << ADC_SMPR2_SMP1_Pos) | (3u << ADC_SMPR2_SMP8_Pos);
    /* Sequence must be programmed before ADON (Renode latches the first channel at ADON). */
    ADC1->SQR1 = (ADC_NCH - 1u) << ADC_SQR1_L_Pos;
    ADC1->SQR3 = (1u << 0) | (8u << 5) | (16u << 10) | (17u << 15);
    /* DDS=1: request DMA after every conversion (Renode only raises requests with DDS set). */
    ADC1->CR2 = ADC_CR2_DMA | ADC_CR2_DDS | ADC_CR2_ADON;

    ADC_STREAM->CR = 0;
    while (ADC_STREAM->CR & DMA_SxCR_EN) {}
    ADC_STREAM->PAR = (uint32_t)&ADC1->DR;
    /* 32-bit transfers: DR's upper half reads 0 in independent mode on silicon, and Renode's
     * ADC model only implements 32-bit accesses (divergence D10). */
    ADC_STREAM->CR = (0u << DMA_SxCR_CHSEL_Pos) | DMA_SxCR_MSIZE_1 | DMA_SxCR_PSIZE_1 |
                     DMA_SxCR_MINC | DMA_SxCR_TCIE | DMA_SxCR_PL_1;   /* P->M, 32-bit, high priority */
    NVIC_SetPriority(DMA2_Stream0_IRQn, PRIO_ADC_DMA);
    NVIC_EnableIRQ(DMA2_Stream0_IRQn);
}

void adc_start_scan(volatile uint32_t *dst)
{
    DMA2->LIFCR = S0_FLAGS;
    ADC_STREAM->M0AR = (uint32_t)dst;
    ADC_STREAM->NDTR = ADC_NCH;
    ADC_STREAM->CR |= DMA_SxCR_EN;
    ADC1->CR2 |= ADC_CR2_SWSTART;
}

bool adc_dma_take_complete(void)
{
    bool done = (DMA2->LISR & DMA_LISR_TCIF0) != 0u;
    DMA2->LIFCR = S0_FLAGS;
    return done;
}
