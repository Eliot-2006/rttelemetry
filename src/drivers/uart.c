#include "drivers.h"

volatile uint32_t uart_rx_overruns;

__attribute__((weak)) void uart_tx_done_isr(void) {}
__attribute__((weak)) void uart_rx_byte_isr(uint8_t byte) { (void)byte; }

#define TX_STREAM DMA1_Stream6          /* USART2_TX = DMA1 Stream6 Channel4 (RM0090 Table 42) */

void uart_init(uint32_t baud)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_DMA1EN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    (void)RCC->APB1ENR;                                       /* clock-enable settle */
    gpio_mode(GPIOA, 2, GPIO_MODE_AF, 7);
    gpio_mode(GPIOA, 3, GPIO_MODE_AF, 7);
    GPIOA->PUPDR = (GPIOA->PUPDR & ~(3u << 6)) | (1u << 6);    /* RX pull-up: idle high when unplugged */

    USART2->CR1 = 0;
    USART2->BRR = (PCLK1_HZ + baud / 2u) / baud;              /* oversampling 16: BRR = fck / baud */
    USART2->CR3 = USART_CR3_DMAT;
    USART2->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;

    TX_STREAM->CR = 0;
    while (TX_STREAM->CR & DMA_SxCR_EN) {}
    TX_STREAM->PAR = (uint32_t)&USART2->DR;
    TX_STREAM->CR = (4u << DMA_SxCR_CHSEL_Pos) | DMA_SxCR_MINC | DMA_SxCR_DIR_0 | DMA_SxCR_TCIE;
    NVIC_SetPriority(DMA1_Stream6_IRQn, PRIO_UART_TX_DMA);
    NVIC_EnableIRQ(DMA1_Stream6_IRQn);
}

void uart_write_polled(const void *data, size_t len)
{
    const uint8_t *p = data;
    while (len--) {
        while (!(USART2->SR & USART_SR_TXE)) {}
        USART2->DR = *p++;
    }
    while (!(USART2->SR & USART_SR_TC)) {}
}

bool uart_tx_dma_start(const uint8_t *buf, uint16_t len)
{
    if (TX_STREAM->CR & DMA_SxCR_EN) return false;
    DMA1->HIFCR = DMA_HIFCR_CTCIF6 | DMA_HIFCR_CHTIF6 | DMA_HIFCR_CTEIF6 | DMA_HIFCR_CDMEIF6 | DMA_HIFCR_CFEIF6;
    TX_STREAM->M0AR = (uint32_t)buf;
    TX_STREAM->NDTR = len;
    USART2->SR = ~USART_SR_TC;                                /* TC is rc_w0 */
    TX_STREAM->CR |= DMA_SxCR_EN;
    return true;
}

void uart_tx_dma_abort(void)
{
    TX_STREAM->CR &= ~DMA_SxCR_EN;
    while (TX_STREAM->CR & DMA_SxCR_EN) {}
    DMA1->HIFCR = DMA_HIFCR_CTCIF6 | DMA_HIFCR_CHTIF6 | DMA_HIFCR_CTEIF6 | DMA_HIFCR_CDMEIF6 | DMA_HIFCR_CFEIF6;
}

void DMA1_Stream6_IRQHandler(void)
{
    if (DMA1->HISR & DMA_HISR_TCIF6) {
        DMA1->HIFCR = DMA_HIFCR_CTCIF6;
        uart_tx_done_isr();
    }
    DMA1->HIFCR = DMA_HIFCR_CHTIF6 | DMA_HIFCR_CTEIF6 | DMA_HIFCR_CDMEIF6 | DMA_HIFCR_CFEIF6;
}

void uart_rx_enable_irq(void)
{
    USART2->CR1 |= USART_CR1_RXNEIE;
    NVIC_SetPriority(USART2_IRQn, PRIO_UART_RX);
    NVIC_EnableIRQ(USART2_IRQn);
}

void USART2_IRQHandler(void)
{
    uint32_t sr = USART2->SR;
    if (sr & (USART_SR_RXNE | USART_SR_ORE)) {
        uint8_t b = (uint8_t)USART2->DR;                      /* SR then DR read clears ORE */
        if (sr & USART_SR_ORE) uart_rx_overruns++;
        if (sr & USART_SR_RXNE) uart_rx_byte_isr(b);
    }
}
