/* Downlink payloads (PRD §12.2). All fields little-endian, in declaration order, no padding.
 * msg_put_* return the payload length, or 0 if `cap` is too small or a count is out of range.
 * msg_get_* return false if the payload is too short or a count is out of range; longer
 * payloads are accepted and the tail ignored (versioning rule: fields are only appended). */
#ifndef CORE_MSG_H
#define CORE_MSG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hist.h"

/* ---- SAMPLE_BATCH: u8 count, u8 ch_mask, count x sample_rec (18 B) ---- */
#define SAMPLE_BATCH_MAX 10
#define SAMPLE_REC_WIRE 18u
typedef struct {
    uint32_t seq, t_us;
    uint16_t raw[4];
    uint16_t status;
} sample_rec_t;
size_t msg_put_sample_batch(uint8_t *out, size_t cap, const sample_rec_t *s, uint8_t count, uint8_t ch_mask);
bool msg_get_sample_batch(const uint8_t *p, size_t len, sample_rec_t s[SAMPLE_BATCH_MAX], uint8_t *count,
                          uint8_t *ch_mask);

/* ---- IMU_BATCH: u8 count, count x imu_rec (22 B) ---- */
#define IMU_BATCH_MAX 8
#define IMU_REC_WIRE 22u
typedef struct {
    uint32_t seq, t_us;
    int16_t acc[3], gyr[3], temp;
} imu_rec_t;
size_t msg_put_imu_batch(uint8_t *out, size_t cap, const imu_rec_t *s, uint8_t count);
bool msg_get_imu_batch(const uint8_t *p, size_t len, imu_rec_t s[IMU_BATCH_MAX], uint8_t *count);

/* ---- BOOT (102 B) ---- */
enum {
    RESET_POWER_ON = 0,
    RESET_CMD = 1,
    RESET_WDT_TASK_STALL = 2,
    RESET_HARDFAULT = 3,
    RESET_STACK_OVF = 4,
    RESET_ASSERT = 5,
    RESET_UNKNOWN_WDT = 6,
};
#define BOOT_WIRE 102u
typedef struct {
    uint8_t ver_major, ver_minor, ver_patch;
    char git_sha[8]; /* not NUL-terminated */
    uint8_t build_type; /* 0 debug, 1 release */
    uint8_t fault_injection;
    uint8_t reset_reason; /* RESET_* */
    uint32_t boot_count;
    uint32_t detail, pc, lr, psr, cfsr, hfsr, mmfar, bfar;
    char task[16]; /* NUL-padded */
    char file[24]; /* NUL-padded */
    uint16_t vrefint_cal, ts_cal1, ts_cal2;
    uint16_t sample_rate_hz;
    uint8_t batch;
    uint16_t ring_capacity;
    uint8_t pool_size;
} boot_t;
size_t msg_put_boot(uint8_t *out, size_t cap, const boot_t *b);
bool msg_get_boot(const uint8_t *p, size_t len, boot_t *b);

/* ---- HEALTH (124 B + 2 B per task) ---- */
#define HEALTH_MAX_TASKS 8
#define HEALTH_WIRE(n_tasks) (124u + 2u * (n_tasks))
typedef struct {
    uint32_t uptime_ms, triggered, delivered, dropped_overrun, dropped_ring, dropped_txpool, frames_sent,
        frames_dropped, rx_frames_ok, rx_frames_bad, rx_bytes_dropped, event_drops, tx_timeouts;
    uint16_t ring_occ, ring_hwm, ring_capacity;
    uint8_t txpool_free_min, txpool_size;
    uint16_t cpu_load_permille, sample_rate_hz, sensor_status, fault_flags;
    uint8_t n_tasks;                               /* <= HEALTH_MAX_TASKS */
    uint16_t stack_free_min_words[HEALTH_MAX_TASKS]; /* only n_tasks entries on the wire */
    uint16_t msp_free_bytes;
    uint32_t l1_hist[HIST_BUCKETS];
    uint32_t l1_max_us, l2_max_us;
    uint32_t imu_samples, imu_dropped, i2c_errors;
    uint8_t imu_status;
} health_t;
size_t msg_put_health(uint8_t *out, size_t cap, const health_t *h);
bool msg_get_health(const uint8_t *p, size_t len, health_t *h);

/* ---- EVENT: u8 count, count x {u32 t_us, u16 id, u16 rsv=0, u32 arg} (12 B) ---- */
#define EVENT_BATCH_MAX 8
#define EVENT_REC_WIRE 12u
typedef struct {
    uint32_t t_us;
    uint16_t id; /* EVT_* (events.h) */
    uint32_t arg;
} event_rec_t;
size_t msg_put_event(uint8_t *out, size_t cap, const event_rec_t *e, uint8_t count);
bool msg_get_event(const uint8_t *p, size_t len, event_rec_t e[EVENT_BATCH_MAX], uint8_t *count);

/* ---- ACK: u16 cmd_seq, u8 cmd, u8 status (4 B) ---- */
enum {
    ACK_OK = 0,
    ACK_ERR_UNKNOWN = 1,
    ACK_ERR_LEN = 2,
    ACK_ERR_RANGE = 3,
    ACK_UNSUPPORTED = 4,
    ACK_BUSY = 5,
};
#define ACK_WIRE 4u
typedef struct {
    uint16_t cmd_seq;
    uint8_t cmd, status;
} ack_t;
size_t msg_put_ack(uint8_t *out, size_t cap, const ack_t *a);
bool msg_get_ack(const uint8_t *p, size_t len, ack_t *a);

#endif
