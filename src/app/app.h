/* Application-wide configuration, shared objects and counters (PRD §8, §17, §18). */
#ifndef APP_H
#define APP_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "ring.h"
#include "msg.h"
#include "frame.h"
#include "events.h"
#include "hist.h"

#define FW_VERSION_MAJOR 1
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 0

#define DEFAULT_RATE_HZ   1000u
#define BATCH_N           SAMPLE_BATCH_MAX      /* 10 samples per SAMPLE_BATCH frame */
#define FLUSH_US          20000u                /* emit a partial batch once its oldest sample is this old */
#define RING_CAP          128u                  /* sample records, power of two */
#define RX_RING_CAP       512u                  /* bytes; holds two max-size wire frames */
#define POOL_N            4u
#define EVENT_Q_LEN       32u
#define WDT_TIMEOUT_MS    1000u
#define WDT_WINDOW_TICKS  pdMS_TO_TICKS(500)

/* Task ids (index into HEALTH stack array and the watchdog liveness table) and priorities. */
enum { TID_PROC, TID_TX, TID_CMD, TID_HEALTH, TID_IMU, TID_IDLE, TID_COUNT };
#define PRIO_IMU     6
#define PRIO_PROC    5
#define PRIO_TX      4
#define PRIO_CMD     3
#define PRIO_HEALTH  2
#define WDT_MONITORED ((1u << TID_PROC) | (1u << TID_TX) | (1u << TID_CMD))

/* ---- counters (single writer each unless noted; all read by health) ---- */
typedef struct {
    _Atomic uint32_t seq;               /* triggered samples = next sample seq (TIM3 ISR) */
    _Atomic uint32_t dropped_overrun;   /* TIM3 ISR */
    _Atomic uint32_t dropped_txpool;    /* proc */
    _Atomic uint32_t delivered;         /* proc: samples handed to tx inside frames */
    _Atomic uint32_t frames_sent;       /* tx */
    _Atomic uint32_t frames_dropped;    /* send attempts refused by an empty pool (EVENT batches are retried) */
    _Atomic uint32_t rx_frames_ok, rx_frames_bad;   /* cmd */
    _Atomic uint32_t event_drops;       /* any context (atomic add) */
    _Atomic uint32_t tx_timeouts;       /* tx */
    _Atomic uint32_t l2_max_us;         /* tx */
    _Atomic uint16_t sensor_status;     /* proc: OR of sample status since last HEALTH */
    _Atomic uint32_t rate_hz;           /* cmd */
} counters_t;
extern counters_t ctr;

/* Multi-word statistics guarded by stats_mutex (priority inheritance). */
extern SemaphoreHandle_t stats_mutex;
extern hist_t l1_hist;

extern ring_t sample_ring, rx_ring;
extern TaskHandle_t h_proc, h_tx, h_cmd, h_health;

/* acq.c */
void acq_start(uint32_t rate_hz);
void acq_set_rate(uint32_t rate_hz);

/* telemetry.c: frame pool + tx task */
void telemetry_init(void);
/* Copy a payload into a pool buffer and queue it; false (and counted) if the pool is empty. */
bool telemetry_send(uint8_t type, const uint8_t *payload, size_t len, uint32_t t_ready);
uint8_t telemetry_pool_free_min(void);
void tx_task(void *arg);

/* proc.c */
void proc_task(void *arg);
/* health.c */
void health_task(void *arg);
void health_request_status(void);   /* GET_STATUS: HEALTH within 100 ms */
void log_event(uint16_t id, uint32_t arg);
void log_event_isr(uint16_t id, uint32_t arg, BaseType_t *woken);
void events_init(void);
void alive(unsigned tid);
/* uplink.c */
void cmd_task(void *arg);

#endif
