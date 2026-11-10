/* Acquisition: TIM3 trigger ISR -> ADC1 scan via DMA2_Stream0 -> DMA ISR -> SPSC ring (PRD §7.3).
 *
 * Invariant I-ACQ-1: t_trig/cur_seq are written only by the TIM3 ISR while in_flight == 0 and
 * read only by the DMA ISR while in_flight == 1; the DMA ISR clears in_flight after reading
 * them (release). TIM3 (prio 4) may preempt the DMA ISR (prio 5); this ordering makes it safe.
 * TIM3 also aborts a scan still in flight after SCAN_TIMEOUT_US, but only while its DMA transfers
 * are unfinished (NDTR != 0): the DMA ISR runs only after transfer complete, so it never owns
 * a scan that TIM3 aborts. */
#include "app.h"
#include "drivers.h"
#include "fault_inject.h"

static volatile uint32_t scan_buf[ADC_NCH];       /* DMA target: .bss in SRAM1 (never CCM, D4) */
static uint32_t t_trig, cur_seq;
static atomic_bool in_flight;

static sample_rec_t ring_storage[RING_CAP];
ring_t sample_ring;

void acq_start(uint32_t rate_hz)
{
    configASSERT(DMA_ADDR_OK(scan_buf));
    configASSERT(ring_init(&sample_ring, ring_storage, RING_CAP, sizeof(sample_rec_t)));
    atomic_store(&ctr.rate_hz, rate_hz);
    tim3_trigger_init(rate_hz);
}

void acq_set_rate(uint32_t rate_hz)
{
    tim3_set_rate(rate_hz);
    atomic_store(&ctr.rate_hz, rate_hz);
}

/* Priority 4: above configMAX_SYSCALL_INTERRUPT_PRIORITY -> no RTOS calls allowed here. */
void TIM3_IRQHandler(void)
{
    TIM3->SR = ~TIM_SR_UIF;
    mark_hi(MARK_ISR);
    uint32_t s = atomic_fetch_add_explicit(&ctr.seq, 1u, memory_order_relaxed);
    if (atomic_load_explicit(&in_flight, memory_order_acquire) &&
        now_us() - t_trig > SCAN_TIMEOUT_US && adc_abort_stalled_scan()) {
        /* The stalled scan's sample is lost: count it, then start this trigger's scan. */
        atomic_fetch_add_explicit(&ctr.dropped_overrun, 1u, memory_order_relaxed);
        atomic_store_explicit(&in_flight, false, memory_order_relaxed);
    }
    if (atomic_load_explicit(&in_flight, memory_order_acquire)) {
        atomic_fetch_add_explicit(&ctr.dropped_overrun, 1u, memory_order_relaxed);
    } else {
        t_trig = now_us();
        cur_seq = s;
        atomic_store_explicit(&in_flight, true, memory_order_relaxed);
        adc_start_scan(scan_buf);
    }
    mark_lo(MARK_ISR);
}

void DMA2_Stream0_IRQHandler(void)
{
    if (!adc_dma_take_complete()) return;
    fi_dma_isr_hook();
    sample_rec_t r = {
        .seq = cur_seq, .t_us = t_trig, .status = 0,
        .raw = { (uint16_t)scan_buf[0], (uint16_t)scan_buf[1], (uint16_t)scan_buf[2], (uint16_t)scan_buf[3] },
    };
    atomic_store_explicit(&in_flight, false, memory_order_release);
    if (ring_push(&sample_ring, &r)) {
        BaseType_t woken = pdFALSE;
        vTaskNotifyGiveFromISR(h_proc, &woken);
        portYIELD_FROM_ISR(woken);
    }
}
