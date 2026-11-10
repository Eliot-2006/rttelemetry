#include "msg.h"

#include <string.h>

#include "bytes.h"

static size_t done(const wbuf_t *w) { return w->err ? 0 : w->len; }

/* ---- SAMPLE_BATCH ---- */

size_t msg_put_sample_batch(uint8_t *out, size_t cap, const sample_rec_t *s, uint8_t count, uint8_t ch_mask)
{
    if (count > SAMPLE_BATCH_MAX) return 0;
    wbuf_t w = {out, cap, 0, false};
    wb_u8(&w, count);
    wb_u8(&w, ch_mask);
    for (uint8_t i = 0; i < count; i++) {
        wb_u32(&w, s[i].seq);
        wb_u32(&w, s[i].t_us);
        for (int c = 0; c < 4; c++) wb_u16(&w, s[i].raw[c]);
        wb_u16(&w, s[i].status);
    }
    return done(&w);
}

bool msg_get_sample_batch(const uint8_t *p, size_t len, sample_rec_t s[SAMPLE_BATCH_MAX], uint8_t *count,
                          uint8_t *ch_mask)
{
    rbuf_t r = {p, len, 0, false};
    *count = rb_u8(&r);
    *ch_mask = rb_u8(&r);
    if (*count > SAMPLE_BATCH_MAX) return false;
    for (uint8_t i = 0; i < *count; i++) {
        memset(&s[i], 0, sizeof s[i]);
        s[i].seq = rb_u32(&r);
        s[i].t_us = rb_u32(&r);
        for (int c = 0; c < 4; c++) s[i].raw[c] = rb_u16(&r);
        s[i].status = rb_u16(&r);
    }
    return !r.err;
}

/* ---- IMU_BATCH ---- */

size_t msg_put_imu_batch(uint8_t *out, size_t cap, const imu_rec_t *s, uint8_t count)
{
    if (count > IMU_BATCH_MAX) return 0;
    wbuf_t w = {out, cap, 0, false};
    wb_u8(&w, count);
    for (uint8_t i = 0; i < count; i++) {
        wb_u32(&w, s[i].seq);
        wb_u32(&w, s[i].t_us);
        for (int k = 0; k < 3; k++) wb_i16(&w, s[i].acc[k]);
        for (int k = 0; k < 3; k++) wb_i16(&w, s[i].gyr[k]);
        wb_i16(&w, s[i].temp);
    }
    return done(&w);
}

bool msg_get_imu_batch(const uint8_t *p, size_t len, imu_rec_t s[IMU_BATCH_MAX], uint8_t *count)
{
    rbuf_t r = {p, len, 0, false};
    *count = rb_u8(&r);
    if (*count > IMU_BATCH_MAX) return false;
    for (uint8_t i = 0; i < *count; i++) {
        memset(&s[i], 0, sizeof s[i]);
        s[i].seq = rb_u32(&r);
        s[i].t_us = rb_u32(&r);
        for (int k = 0; k < 3; k++) s[i].acc[k] = rb_i16(&r);
        for (int k = 0; k < 3; k++) s[i].gyr[k] = rb_i16(&r);
        s[i].temp = rb_i16(&r);
    }
    return !r.err;
}

/* ---- BOOT ---- */

size_t msg_put_boot(uint8_t *out, size_t cap, const boot_t *b)
{
    wbuf_t w = {out, cap, 0, false};
    wb_u8(&w, b->ver_major);
    wb_u8(&w, b->ver_minor);
    wb_u8(&w, b->ver_patch);
    wb_bytes(&w, b->git_sha, sizeof b->git_sha);
    wb_u8(&w, b->build_type);
    wb_u8(&w, b->fault_injection);
    wb_u8(&w, b->reset_reason);
    wb_u32(&w, b->boot_count);
    wb_u32(&w, b->detail);
    wb_u32(&w, b->pc);
    wb_u32(&w, b->lr);
    wb_u32(&w, b->psr);
    wb_u32(&w, b->cfsr);
    wb_u32(&w, b->hfsr);
    wb_u32(&w, b->mmfar);
    wb_u32(&w, b->bfar);
    wb_bytes(&w, b->task, sizeof b->task);
    wb_bytes(&w, b->file, sizeof b->file);
    wb_u16(&w, b->vrefint_cal);
    wb_u16(&w, b->ts_cal1);
    wb_u16(&w, b->ts_cal2);
    wb_u16(&w, b->sample_rate_hz);
    wb_u8(&w, b->batch);
    wb_u16(&w, b->ring_capacity);
    wb_u8(&w, b->pool_size);
    return done(&w);
}

bool msg_get_boot(const uint8_t *p, size_t len, boot_t *b)
{
    rbuf_t r = {p, len, 0, false};
    memset(b, 0, sizeof *b);
    b->ver_major = rb_u8(&r);
    b->ver_minor = rb_u8(&r);
    b->ver_patch = rb_u8(&r);
    rb_bytes(&r, b->git_sha, sizeof b->git_sha);
    b->build_type = rb_u8(&r);
    b->fault_injection = rb_u8(&r);
    b->reset_reason = rb_u8(&r);
    b->boot_count = rb_u32(&r);
    b->detail = rb_u32(&r);
    b->pc = rb_u32(&r);
    b->lr = rb_u32(&r);
    b->psr = rb_u32(&r);
    b->cfsr = rb_u32(&r);
    b->hfsr = rb_u32(&r);
    b->mmfar = rb_u32(&r);
    b->bfar = rb_u32(&r);
    rb_bytes(&r, b->task, sizeof b->task);
    rb_bytes(&r, b->file, sizeof b->file);
    b->vrefint_cal = rb_u16(&r);
    b->ts_cal1 = rb_u16(&r);
    b->ts_cal2 = rb_u16(&r);
    b->sample_rate_hz = rb_u16(&r);
    b->batch = rb_u8(&r);
    b->ring_capacity = rb_u16(&r);
    b->pool_size = rb_u8(&r);
    return !r.err;
}

/* ---- HEALTH ---- */

size_t msg_put_health(uint8_t *out, size_t cap, const health_t *h)
{
    if (h->n_tasks > HEALTH_MAX_TASKS) return 0;
    wbuf_t w = {out, cap, 0, false};
    wb_u32(&w, h->uptime_ms);
    wb_u32(&w, h->triggered);
    wb_u32(&w, h->delivered);
    wb_u32(&w, h->dropped_overrun);
    wb_u32(&w, h->dropped_ring);
    wb_u32(&w, h->dropped_txpool);
    wb_u32(&w, h->frames_sent);
    wb_u32(&w, h->frames_dropped);
    wb_u32(&w, h->rx_frames_ok);
    wb_u32(&w, h->rx_frames_bad);
    wb_u32(&w, h->rx_bytes_dropped);
    wb_u32(&w, h->event_drops);
    wb_u32(&w, h->tx_timeouts);
    wb_u16(&w, h->ring_occ);
    wb_u16(&w, h->ring_hwm);
    wb_u16(&w, h->ring_capacity);
    wb_u8(&w, h->txpool_free_min);
    wb_u8(&w, h->txpool_size);
    wb_u16(&w, h->cpu_load_permille);
    wb_u16(&w, h->sample_rate_hz);
    wb_u16(&w, h->sensor_status);
    wb_u16(&w, h->fault_flags);
    wb_u8(&w, h->n_tasks);
    for (uint8_t i = 0; i < h->n_tasks; i++) wb_u16(&w, h->stack_free_min_words[i]);
    wb_u16(&w, h->msp_free_bytes);
    for (int i = 0; i < HIST_BUCKETS; i++) wb_u32(&w, h->l1_hist[i]);
    wb_u32(&w, h->l1_max_us);
    wb_u32(&w, h->l2_max_us);
    wb_u32(&w, h->imu_samples);
    wb_u32(&w, h->imu_dropped);
    wb_u32(&w, h->i2c_errors);
    wb_u8(&w, h->imu_status);
    return done(&w);
}

bool msg_get_health(const uint8_t *p, size_t len, health_t *h)
{
    rbuf_t r = {p, len, 0, false};
    memset(h, 0, sizeof *h);
    h->uptime_ms = rb_u32(&r);
    h->triggered = rb_u32(&r);
    h->delivered = rb_u32(&r);
    h->dropped_overrun = rb_u32(&r);
    h->dropped_ring = rb_u32(&r);
    h->dropped_txpool = rb_u32(&r);
    h->frames_sent = rb_u32(&r);
    h->frames_dropped = rb_u32(&r);
    h->rx_frames_ok = rb_u32(&r);
    h->rx_frames_bad = rb_u32(&r);
    h->rx_bytes_dropped = rb_u32(&r);
    h->event_drops = rb_u32(&r);
    h->tx_timeouts = rb_u32(&r);
    h->ring_occ = rb_u16(&r);
    h->ring_hwm = rb_u16(&r);
    h->ring_capacity = rb_u16(&r);
    h->txpool_free_min = rb_u8(&r);
    h->txpool_size = rb_u8(&r);
    h->cpu_load_permille = rb_u16(&r);
    h->sample_rate_hz = rb_u16(&r);
    h->sensor_status = rb_u16(&r);
    h->fault_flags = rb_u16(&r);
    h->n_tasks = rb_u8(&r);
    if (h->n_tasks > HEALTH_MAX_TASKS) return false;
    for (uint8_t i = 0; i < h->n_tasks; i++) h->stack_free_min_words[i] = rb_u16(&r);
    h->msp_free_bytes = rb_u16(&r);
    for (int i = 0; i < HIST_BUCKETS; i++) h->l1_hist[i] = rb_u32(&r);
    h->l1_max_us = rb_u32(&r);
    h->l2_max_us = rb_u32(&r);
    h->imu_samples = rb_u32(&r);
    h->imu_dropped = rb_u32(&r);
    h->i2c_errors = rb_u32(&r);
    h->imu_status = rb_u8(&r);
    return !r.err;
}

/* ---- EVENT ---- */

size_t msg_put_event(uint8_t *out, size_t cap, const event_rec_t *e, uint8_t count)
{
    if (count > EVENT_BATCH_MAX) return 0;
    wbuf_t w = {out, cap, 0, false};
    wb_u8(&w, count);
    for (uint8_t i = 0; i < count; i++) {
        wb_u32(&w, e[i].t_us);
        wb_u16(&w, e[i].id);
        wb_u16(&w, 0); /* reserved */
        wb_u32(&w, e[i].arg);
    }
    return done(&w);
}

bool msg_get_event(const uint8_t *p, size_t len, event_rec_t e[EVENT_BATCH_MAX], uint8_t *count)
{
    rbuf_t r = {p, len, 0, false};
    *count = rb_u8(&r);
    if (*count > EVENT_BATCH_MAX) return false;
    for (uint8_t i = 0; i < *count; i++) {
        memset(&e[i], 0, sizeof e[i]);
        e[i].t_us = rb_u32(&r);
        e[i].id = rb_u16(&r);
        (void)rb_u16(&r); /* reserved: ignored */
        e[i].arg = rb_u32(&r);
    }
    return !r.err;
}

/* ---- ACK ---- */

size_t msg_put_ack(uint8_t *out, size_t cap, const ack_t *a)
{
    wbuf_t w = {out, cap, 0, false};
    wb_u16(&w, a->cmd_seq);
    wb_u8(&w, a->cmd);
    wb_u8(&w, a->status);
    return done(&w);
}

bool msg_get_ack(const uint8_t *p, size_t len, ack_t *a)
{
    rbuf_t r = {p, len, 0, false};
    a->cmd_seq = rb_u16(&r);
    a->cmd = rb_u8(&r);
    a->status = rb_u8(&r);
    return !r.err;
}
