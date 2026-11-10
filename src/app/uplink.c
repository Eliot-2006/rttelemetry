/* Command uplink: USART2 RX ISR -> rx SPSC ring -> cmd_task (deframe, validate, execute, ACK).
 * The uplink is the firmware's trust boundary: every byte is untrusted (FR-CMD-4, fuzzed). */
#include <string.h>
#include "app.h"
#include "drivers.h"
#include "frame.h"
#include "cmd.h"
#include "events.h"
#include "fault.h"
#include "fault_inject.h"

static uint8_t rx_storage[RX_RING_CAP];
ring_t rx_ring;
static deframer_t deframer;

void uart_rx_byte_isr(uint8_t b)
{
    if (ring_push(&rx_ring, &b) && b == 0x00u) {      /* wake on frame delimiter only */
        BaseType_t woken = pdFALSE;
        vTaskNotifyGiveFromISR(h_cmd, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

static void ack(uint16_t seq, uint8_t cmd, uint8_t status)
{
    uint8_t p[ACK_WIRE];
    size_t n = msg_put_ack(p, sizeof p, &(ack_t){.cmd_seq = seq, .cmd = cmd, .status = status});
    telemetry_send(MSG_ACK, p, n, now_us());
    if (status != ACK_OK) log_event(EVT_CMD_REJECTED, ((uint32_t)cmd << 8) | status);
}

static void execute(const cmd_t *c)
{
    switch (c->type) {
    case MSG_CMD_PING:           ack(c->seq, c->type, ACK_OK); break;
    case MSG_CMD_GET_STATUS:     health_request_status(); ack(c->seq, c->type, ACK_OK); break;
    case MSG_CMD_SET_RATE:
        acq_set_rate(c->rate_hz);
        log_event(EVT_RATE_CHANGED, c->rate_hz);
        ack(c->seq, c->type, ACK_OK);
        break;
    case MSG_CMD_RESET_COUNTERS:
        /* Diagnostics only (HWM, latency maxima). Loss-accounting counters are never reset:
         * conservation must hold over the whole run. */
        atomic_store(&sample_ring.hwm, ring_count(&sample_ring));
        atomic_store(&ctr.l2_max_us, 0);
        xSemaphoreTake(stats_mutex, portMAX_DELAY);
        memset(&l1_hist, 0, sizeof l1_hist);
        xSemaphoreGive(stats_mutex);
        ack(c->seq, c->type, ACK_OK);
        break;
    case MSG_CMD_REBOOT:
        ack(c->seq, c->type, ACK_OK);
        vTaskDelay(pdMS_TO_TICKS(20));                  /* let the ACK leave */
        fault_reset(RESET_CMD, 0, "cmd", NULL);
#if FAULT_INJECTION
    case MSG_CMD_FI_ARM:         ack(c->seq, c->type, fi_arm(c->fault_id, c->fault_param)); break;
    case MSG_CMD_FI_CLEAR:       fi_clear(); ack(c->seq, c->type, ACK_OK); break;
#endif
    default:                     ack(c->seq, c->type, ACK_UNSUPPORTED); break;
    }
}

void cmd_task(void *arg)
{
    (void)arg;
    configASSERT(ring_init(&rx_ring, rx_storage, RX_RING_CAP, 1));
    deframer_init(&deframer);
    uart_rx_enable_irq();
    static uint8_t scratch[FRAME_SCRATCH_LEN];
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
        alive(TID_CMD);
        fi_cmd_hook();
        uint8_t b;
        while (ring_pop(&rx_ring, &b)) {
            const uint8_t *wire;
            size_t wire_len;
            if (!deframer_push(&deframer, b, &wire, &wire_len)) continue;
            frame_hdr_t hdr;
            const uint8_t *payload;
            if (frame_decode(wire, wire_len, scratch, sizeof scratch, &hdr, &payload) != FRAME_OK) {
                atomic_fetch_add(&ctr.rx_frames_bad, 1u);
                continue;
            }
            cmd_t c;
            uint8_t st = cmd_parse(&hdr, payload, FAULT_INJECTION, &c);
            atomic_fetch_add(&ctr.rx_frames_ok, 1u);
            if (st != ACK_OK) ack(hdr.seq, hdr.type, st);
            else execute(&c);
        }
    }
}
