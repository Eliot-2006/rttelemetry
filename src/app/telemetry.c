/* Frame pool and the TX task that owns USART2 TX DMA (PRD §7.4, §15.3).
 *
 * Ownership (I-POOL-1): every pool index is in exactly one place: free_q, a producer between
 * telemetry_send()'s receive and send, or tx_q. Both queues have depth POOL_N, so neither can
 * overflow. The tx task COBS/CRC-encodes into its own DMA buffer (tx_wire) and returns the
 * pool entry immediately, so frame_seq is assigned in wire order. */
#include <string.h>
#include "app.h"
#include "drivers.h"
#include "frame.h"
#include "fault_inject.h"

typedef struct {
    uint8_t type;
    uint16_t len;
    uint32_t t_ready;
    uint8_t payload[FRAME_PAYLOAD_MAX];
} pool_entry_t;

static pool_entry_t pool[POOL_N];
static uint8_t tx_wire[FRAME_WIRE_MAX];              /* DMA source: SRAM1 */
static StaticQueue_t free_qs, tx_qs;
static uint8_t free_q_buf[POOL_N], tx_q_buf[POOL_N];
static QueueHandle_t free_q, tx_q;
static _Atomic uint8_t pool_free_min = POOL_N;

void telemetry_init(void)
{
    configASSERT(DMA_ADDR_OK(tx_wire));
    free_q = xQueueCreateStatic(POOL_N, 1, free_q_buf, &free_qs);
    tx_q = xQueueCreateStatic(POOL_N, 1, tx_q_buf, &tx_qs);
    for (uint8_t i = 0; i < POOL_N; i++) xQueueSend(free_q, &i, 0);
}

uint8_t telemetry_pool_free_min(void) { return atomic_exchange(&pool_free_min, (uint8_t)uxQueueMessagesWaiting(free_q)); }

bool telemetry_send(uint8_t type, const uint8_t *payload, size_t len, uint32_t t_ready)
{
    uint8_t i;
    if (len > FRAME_PAYLOAD_MAX || xQueueReceive(free_q, &i, 0) != pdTRUE) {
        atomic_fetch_add(&ctr.frames_dropped, 1u);
        return false;
    }
    uint8_t free_now = (uint8_t)uxQueueMessagesWaiting(free_q);
    uint8_t m = atomic_load(&pool_free_min);
    while (free_now < m && !atomic_compare_exchange_weak(&pool_free_min, &m, free_now)) {}
    pool[i].type = type;
    pool[i].len = (uint16_t)len;
    pool[i].t_ready = t_ready;
    memcpy(pool[i].payload, payload, len);
    configASSERT(xQueueSend(tx_q, &i, 0) == pdTRUE);   /* cannot fail: depth == POOL_N */
    return true;
}

void uart_tx_done_isr(void)
{
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(h_tx, &woken);
    portYIELD_FROM_ISR(woken);
}

void tx_task(void *arg)
{
    (void)arg;
    uint16_t frame_seq = 0;
    for (;;) {
        uint8_t i;
        alive(TID_TX);
        if (xQueueReceive(tx_q, &i, pdMS_TO_TICKS(100)) != pdTRUE) continue;
        size_t n = frame_encode(pool[i].type, frame_seq++, pool[i].t_ready,
                                pool[i].payload, pool[i].len, tx_wire, sizeof tx_wire);
        uint32_t l2 = now_us() - pool[i].t_ready;
        configASSERT(xQueueSend(free_q, &i, 0) == pdTRUE);
        configASSERT(n > 0);
        uint32_t m = atomic_load(&ctr.l2_max_us);
        if (l2 > m) atomic_store(&ctr.l2_max_us, l2);

        fi_tx_hook(n);                                     /* FI-02: emulate a slow link */
        (void)ulTaskNotifyTake(pdTRUE, 0);                 /* drop any stale completion */
        mark_hi(MARK_TX);
        configASSERT(uart_tx_dma_start(tx_wire, (uint16_t)n));
        /* bytes * 10 bits / baud, x2 + 5 ms margin */
        TickType_t timeout = pdMS_TO_TICKS((n * 20000u) / TELEM_BAUD + 5u);
        if (ulTaskNotifyTake(pdTRUE, timeout) == 0) {
            uart_tx_dma_abort();
            atomic_fetch_add(&ctr.tx_timeouts, 1u);
            log_event(EVT_TX_TIMEOUT, (uint32_t)n);
        } else {
            atomic_fetch_add(&ctr.frames_sent, 1u);
        }
        mark_lo(MARK_TX);
    }
}
