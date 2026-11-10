/* Health task (lowest app priority, 100 ms): watchdog supervisor, event log drain, 1 Hz HEALTH,
 * BOOT frame (PRD §13, §14.2). Lowest priority on purpose: if it starves, the system is
 * overloaded and the watchdog fires instead of hiding it. */
#include <string.h>
#include "app.h"
#include "drivers.h"
#include "events.h"
#include "fault.h"
#include "fault_inject.h"

static StaticQueue_t event_qs;
static uint8_t event_q_buf[EVENT_Q_LEN * sizeof(event_rec_t)];
static QueueHandle_t event_q;
static _Atomic uint32_t alive_mask;
static uint8_t payload[FRAME_PAYLOAD_MAX];

extern uint32_t __msp_limit;                     /* linker: bottom of the MSP reserve */

void events_init(void) { event_q = xQueueCreateStatic(EVENT_Q_LEN, sizeof(event_rec_t), event_q_buf, &event_qs); }

void log_event(uint16_t id, uint32_t arg)
{
    event_rec_t e = { .t_us = now_us(), .id = id, .arg = arg };
    if (xQueueSend(event_q, &e, 0) != pdTRUE) atomic_fetch_add(&ctr.event_drops, 1u);
}

void log_event_isr(uint16_t id, uint32_t arg, BaseType_t *woken)
{
    event_rec_t e = { .t_us = now_us(), .id = id, .arg = arg };
    if (xQueueSendFromISR(event_q, &e, woken) != pdTRUE) atomic_fetch_add(&ctr.event_drops, 1u);
}

void alive(unsigned tid)
{
    atomic_fetch_or(&alive_mask, 1u << tid);
    fault_checkin(tid, xTaskGetTickCount());
}

static uint16_t msp_free_bytes(void)
{
    const uint32_t *p = &__msp_limit;
    while (*p == 0xA5A5A5A5u) p++;
    return (uint16_t)((uintptr_t)p - (uintptr_t)&__msp_limit);
}

static uint16_t cpu_load_permille(void)
{
    static uint32_t last_idle, last_t;
    uint32_t idle = ulTaskGetIdleRunTimeCounter(), t = now_us();
    uint32_t dt = t - last_t, di = idle - last_idle;
    last_idle = idle; last_t = t;
    if (dt == 0 || di > dt) return 0;
    return (uint16_t)(1000u - (uint32_t)(((uint64_t)di * 1000u) / dt));
}

static void build_health(health_t *h)
{
    memset(h, 0, sizeof *h);
    h->uptime_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    h->triggered = atomic_load(&ctr.seq);
    h->delivered = atomic_load(&ctr.delivered);
    h->dropped_overrun = atomic_load(&ctr.dropped_overrun);
    h->dropped_ring = atomic_load(&sample_ring.drops);
    h->dropped_txpool = atomic_load(&ctr.dropped_txpool);
    h->frames_sent = atomic_load(&ctr.frames_sent);
    h->frames_dropped = atomic_load(&ctr.frames_dropped);
    h->rx_frames_ok = atomic_load(&ctr.rx_frames_ok);
    h->rx_frames_bad = atomic_load(&ctr.rx_frames_bad);
    h->rx_bytes_dropped = atomic_load(&rx_ring.drops);
    h->event_drops = atomic_load(&ctr.event_drops);
    h->tx_timeouts = atomic_load(&ctr.tx_timeouts);
    h->ring_occ = (uint16_t)ring_count(&sample_ring);
    h->ring_hwm = (uint16_t)atomic_load(&sample_ring.hwm);
    h->ring_capacity = RING_CAP;
    h->txpool_free_min = telemetry_pool_free_min();
    h->txpool_size = POOL_N;
    h->cpu_load_permille = cpu_load_permille();
    h->sample_rate_hz = (uint16_t)atomic_load(&ctr.rate_hz);
    h->sensor_status = atomic_exchange(&ctr.sensor_status, 0);
    h->fault_flags = fi_armed_mask();
    TaskHandle_t th[TID_COUNT] = { h_proc, h_tx, h_cmd, h_health, h_imu, xTaskGetIdleTaskHandle() };
    h->n_tasks = TID_COUNT;
    for (unsigned i = 0; i < TID_COUNT; i++)
        h->stack_free_min_words[i] = th[i] ? (uint16_t)uxTaskGetStackHighWaterMark(th[i]) : 0;
    h->msp_free_bytes = msp_free_bytes();
    xSemaphoreTake(stats_mutex, portMAX_DELAY);
    memcpy(h->l1_hist, l1_hist.b, sizeof h->l1_hist);
    h->l1_max_us = l1_hist.max;
    xSemaphoreGive(stats_mutex);
    h->l2_max_us = atomic_load(&ctr.l2_max_us);
    h->imu_samples = atomic_load(&imu_samples);
    h->imu_dropped = atomic_load(&imu_dropped);
    h->i2c_errors = atomic_load(&i2c_errors);
    h->imu_status = atomic_load(&imu_status);
}

void health_request_status(void) { xTaskNotifyGive(h_health); }

static void send_health(void)
{
    static health_t h;            /* static: keep the ~170-byte struct off the task stack */
    build_health(&h);
    size_t len = msg_put_health(payload, sizeof payload, &h);
    configASSERT(len > 0);
    telemetry_send(MSG_HEALTH, payload, len, now_us());
}

static void send_boot(void)
{
    boot_t b;
    memset(&b, 0, sizeof b);
    b.ver_major = FW_VERSION_MAJOR; b.ver_minor = FW_VERSION_MINOR; b.ver_patch = FW_VERSION_PATCH;
    memcpy(b.git_sha, FW_GIT_SHA, sizeof b.git_sha);
    b.build_type = FW_BUILD_RELEASE;
    b.fault_injection = FAULT_INJECTION;
    const reset_info_t *r = &fault_last_reset;
    b.reset_reason = (uint8_t)r->reason;
    b.boot_count = r->boot_count;
    b.detail = r->detail; b.pc = r->pc; b.lr = r->lr; b.psr = r->psr;
    b.cfsr = r->cfsr; b.hfsr = r->hfsr; b.mmfar = r->mmfar; b.bfar = r->bfar;
    memcpy(b.task, r->task, sizeof b.task);
    memcpy(b.file, r->file, sizeof b.file);
    /* Factory calibration (RM0090/datasheet addresses). Renode returns 0 (divergence D7). */
    b.vrefint_cal = *(const volatile uint16_t *)0x1FFF7A2Au;
    b.ts_cal1 = *(const volatile uint16_t *)0x1FFF7A2Cu;
    b.ts_cal2 = *(const volatile uint16_t *)0x1FFF7A2Eu;
    b.sample_rate_hz = (uint16_t)atomic_load(&ctr.rate_hz);
    b.batch = BATCH_N;
    b.ring_capacity = RING_CAP;
    b.pool_size = POOL_N;
    size_t len = msg_put_boot(payload, sizeof payload, &b);
    configASSERT(len > 0);
    telemetry_send(MSG_BOOT, payload, len, now_us());
    log_event(EVT_BOOT, r->boot_count);
    log_event(EVT_RESET_REASON, r->reason);
}

/* EVENT batches go out through the same frame pool as everything else. If the pool is empty
 * (e.g. the TXPOOL_EMPTY_ON episode itself) the batch stays pending and is retried on the next
 * slot instead of being lost; the event queue buffers newer events meanwhile. */
static void drain_events(void)
{
    static event_rec_t ev[EVENT_BATCH_MAX];
    static unsigned n;
    for (;;) {
        while (n < EVENT_BATCH_MAX && xQueueReceive(event_q, &ev[n], 0) == pdTRUE) n++;
        if (n == 0u) return;
        size_t len = msg_put_event(payload, sizeof payload, ev, (uint8_t)n);
        configASSERT(len > 0);
        if (!telemetry_send(MSG_EVENT, payload, len, now_us())) return;
        bool full = n == EVENT_BATCH_MAX;
        n = 0;
        if (!full) return;
    }
}

void health_task(void *arg)
{
    (void)arg;
    send_boot();
    iwdg_start(WDT_TIMEOUT_MS);
    bool healthy = true;
    unsigned cycle = 0;
    TickType_t period = pdMS_TO_TICKS(100), next = xTaskGetTickCount() + period, window_start = next - period;
    for (;;) {
        /* Wait for the next 100 ms slot; a GET_STATUS notification is answered at once without
         * disturbing the cadence. A late slot (next already passed) runs immediately. */
        TickType_t left = next - xTaskGetTickCount();
        if (left <= period && ulTaskNotifyTake(pdTRUE, left)) {
            send_health();
            continue;
        }
        next += period;
        cycle++;
        fault_checkin(TID_HEALTH, xTaskGetTickCount());
        if (xTaskGetTickCount() - window_start >= WDT_WINDOW_TICKS) {
            window_start = xTaskGetTickCount();
            uint32_t seen = atomic_exchange(&alive_mask, 0);
            uint32_t missing = WDT_MONITORED & ~seen;
            if (missing && healthy) {
                healthy = false;                 /* stop kicking: the IWDG resets us */
                fault_note_stall(missing);
                log_event(EVT_WDT_STALL, missing);
            }
        }
        if (healthy) iwdg_kick();
        drain_events();
        if (cycle % 10u == 0u) send_health();
    }
}
