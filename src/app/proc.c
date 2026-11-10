/* Processing task: drain the sample ring, flag implausible values, batch, hand frames to TX
 * (PRD §11). Never blocks on downstream: a full frame pool drops the batch, counted. */
#include "app.h"
#include "drivers.h"
#include "plaus.h"
#include "events.h"
#include "fault_inject.h"

/* Per-channel plausibility (raw 12-bit counts). ch0/ch1 external inputs: rails mean a broken
 * wire or short; ch2 temperature sensor; ch3 VREFINT. STUCK only on external inputs, since
 * VREFINT is legitimately constant. */
static const plaus_cfg_t plaus_cfg[ADC_NCH] = {
    { .min = 50,  .max = 4045, .roc_max = 1000, .stuck_n = 50 },
    { .min = 50,  .max = 4045, .roc_max = 1000, .stuck_n = 50 },
    { .min = 600, .max = 1400, .roc_max = 50,   .stuck_n = 0 },
    { .min = 1300, .max = 1700, .roc_max = 100, .stuck_n = 0 },
};
static plaus_state_t plaus_st[ADC_NCH];
static uint8_t flagged[ADC_NCH];                  /* flags at the last onset, 0 = clear */

static sample_rec_t batch[BATCH_N];
static uint8_t batch_n;
static uint8_t payload[FRAME_PAYLOAD_MAX];

static void emit(void)
{
    static bool pool_empty;
    uint32_t now = now_us();
    size_t len = msg_put_sample_batch(payload, sizeof payload, batch, batch_n, 0x0Fu);
    configASSERT(len > 0);
    if (telemetry_send(MSG_SAMPLE_BATCH, payload, len, now)) {
        atomic_fetch_add(&ctr.delivered, batch_n);
        xSemaphoreTake(stats_mutex, portMAX_DELAY);
        hist_add(&l1_hist, now - batch[0].t_us);
        xSemaphoreGive(stats_mutex);
        if (pool_empty) { pool_empty = false; log_event(EVT_TXPOOL_EMPTY_OFF, 0); }
    } else {
        atomic_fetch_add(&ctr.dropped_txpool, batch_n);
        if (!pool_empty) { pool_empty = true; log_event(EVT_TXPOOL_EMPTY_ON, batch[0].seq); }
    }
    batch_n = 0;
}

/* Edge-triggered events for counters owned by ISRs (they cannot log themselves). */
static void watch_isr_counters(void)
{
    static uint32_t last_ring_drops, last_overrun;
    static bool ring_ovf;
    uint32_t d = atomic_load(&sample_ring.drops);
    if (d != last_ring_drops && !ring_ovf) { ring_ovf = true; log_event(EVT_RING_OVF_ON, d); }
    else if (d == last_ring_drops && ring_ovf) { ring_ovf = false; log_event(EVT_RING_OVF_OFF, d); }
    last_ring_drops = d;
    uint32_t o = atomic_load(&ctr.dropped_overrun);
    if (o != last_overrun) log_event(EVT_ADC_OVERRUN, o);
    last_overrun = o;
}

void proc_task(void *arg)
{
    (void)arg;
    acq_start(DEFAULT_RATE_HZ);
    TickType_t last_watch = xTaskGetTickCount();
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
        alive(TID_PROC);
        fi_proc_hook();
        mark_hi(MARK_PROC);
        sample_rec_t r;
        uint16_t status_or = 0;
        while (ring_pop(&sample_ring, &r)) {
            for (unsigned c = 0; c < ADC_NCH; c++) {
                uint8_t f = plaus_check(&plaus_cfg[c], &plaus_st[c], r.raw[c]);
                r.status |= PLAUS_STATUS(c, f);
                if ((f != 0) != (flagged[c] != 0)) {   /* edge-triggered: one event per onset/clear */
                    log_event(f ? EVT_SENSOR_FLAG_ON : EVT_SENSOR_FLAG_OFF, (c << 8) | (f ? f : flagged[c]));
                    flagged[c] = f;
                }
            }
            status_or |= r.status;
            batch[batch_n++] = r;
            if (batch_n == BATCH_N) emit();
        }
        if (batch_n && now_us() - batch[0].t_us >= FLUSH_US) emit();
        if (status_or) atomic_fetch_or(&ctr.sensor_status, status_or);
        if (xTaskGetTickCount() - last_watch >= pdMS_TO_TICKS(50)) {
            last_watch = xTaskGetTickCount();
            watch_isr_counters();
        }
        mark_lo(MARK_PROC);
    }
}
