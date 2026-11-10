/* XV generator: writes <outdir>/<name>.bin (complete wire frame incl. 0x00) and
 * <outdir>/<name>.json (expected decoding, same shape as tools/telemetry/frames.py
 * decode_frame()) for every downlink message type. Contents are fixed, so output is
 * byte-identical on every run; ctest xv_vectors_up_to_date diffs it against tests/vectors/out.
 * usage: gen_vectors <outdir> */
#include <stdio.h>
#include <string.h>

#include "frame.h"
#include "msg.h"

static FILE *js;
static int first;

static void jsep(void)
{
    if (!first) fputs(", ", js);
    first = 0;
}
static void ju(const char *k, unsigned long v)
{
    jsep();
    fprintf(js, "\"%s\": %lu", k, v);
}
static void ji(const char *k, long v)
{
    jsep();
    fprintf(js, "\"%s\": %ld", k, v);
}
static void jstr(const char *k, const char *s, size_t n) /* chars up to the first NUL */
{
    const char *nul = memchr(s, 0, n);
    jsep();
    fprintf(js, "\"%s\": \"%.*s\"", k, (int)(nul ? (size_t)(nul - s) : n), s);
}
static void jarr_u16(const char *k, const uint16_t *a, size_t n)
{
    jsep();
    fprintf(js, "\"%s\": [", k);
    for (size_t i = 0; i < n; i++) fprintf(js, "%s%u", i ? ", " : "", a[i]);
    fputc(']', js);
}
static void jarr_u32(const char *k, const uint32_t *a, size_t n)
{
    jsep();
    fprintf(js, "\"%s\": [", k);
    for (size_t i = 0; i < n; i++) fprintf(js, "%s%u", i ? ", " : "", a[i]);
    fputc(']', js);
}
static void jarr_i16(const char *k, const int16_t *a, size_t n)
{
    jsep();
    fprintf(js, "\"%s\": [", k);
    for (size_t i = 0; i < n; i++) fprintf(js, "%s%d", i ? ", " : "", a[i]);
    fputc(']', js);
}
static void jopen(const char *k, char brace) /* k == NULL: array element */
{
    if (k) {
        jsep();
        fprintf(js, "\"%s\": %c", k, brace);
    } else {
        jsep();
        fputc(brace, js);
    }
    first = 1;
}
static void jclose(char brace)
{
    fputc(brace, js);
    first = 0;
}

static const char *outdir;

/* Writes <name>.bin and opens <name>.json with the header fields; caller adds "payload". */
static int begin(const char *name, const char *type_name, uint8_t type, uint16_t seq, uint32_t t_us,
                 const uint8_t *payload, size_t len)
{
    uint8_t wire[FRAME_WIRE_MAX];
    size_t n = frame_encode(type, seq, t_us, payload, len, wire, sizeof wire);
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.bin", outdir, name);
    FILE *f = fopen(path, "wb");
    if (!f || n == 0 || fwrite(wire, 1, n, f) != n) return -1;
    fclose(f);
    snprintf(path, sizeof path, "%s/%s.json", outdir, name);
    js = fopen(path, "w");
    if (!js) return -1;
    first = 1;
    fputc('{', js);
    ju("version", FRAME_VERSION);
    ju("type", type);
    jsep();
    fprintf(js, "\"name\": \"%s\"", type_name);
    ju("seq", seq);
    ju("t_us", t_us);
    ju("payload_len", len);
    jopen("payload", '{');
    return 0;
}
static void end(void)
{
    jclose('}'); /* payload */
    jclose('}');
    fputc('\n', js);
    fclose(js);
}

static int sample_batch(void)
{
    sample_rec_t s[SAMPLE_BATCH_MAX];
    memset(s, 0, sizeof s);
    for (uint32_t i = 0; i < SAMPLE_BATCH_MAX; i++) {
        s[i].seq = 4294967290u + i; /* crosses the u32 wrap */
        s[i].t_us = 4294960000u + 1000u * i;
        s[i].raw[0] = (uint16_t)(i * 409u);
        s[i].raw[1] = (uint16_t)(4095u - i);
        s[i].raw[2] = (uint16_t)(940u + i);
        s[i].raw[3] = 1500;
        s[i].status = i == 3 ? (uint16_t)0x0021 : 0;
    }
    uint8_t p[FRAME_PAYLOAD_MAX];
    size_t n = msg_put_sample_batch(p, sizeof p, s, SAMPLE_BATCH_MAX, 0x0F);
    if (begin("sample_batch", "SAMPLE_BATCH", MSG_SAMPLE_BATCH, 0xFFFF, 1704u /* after the u32 wrap */, p, n)) return -1;
    ju("count", SAMPLE_BATCH_MAX);
    ju("ch_mask", 0x0F);
    jopen("samples", '[');
    for (int i = 0; i < SAMPLE_BATCH_MAX; i++) {
        jopen(NULL, '{');
        ju("seq", s[i].seq);
        ju("t_us", s[i].t_us);
        jarr_u16("raw", s[i].raw, 4);
        ju("status", s[i].status);
        jclose('}');
    }
    jclose(']');
    end();
    return 0;
}

static int imu_batch(void)
{
    imu_rec_t s[IMU_BATCH_MAX];
    memset(s, 0, sizeof s);
    for (int i = 0; i < IMU_BATCH_MAX; i++) {
        s[i].seq = (uint32_t)(100 + i);
        s[i].t_us = (uint32_t)(5000000 + i * 4808);
        s[i].acc[0] = (int16_t)(-32768 + i);
        s[i].acc[1] = (int16_t)(i * 1000);
        s[i].acc[2] = 8192;
        s[i].gyr[0] = (int16_t)(32767 - i);
        s[i].gyr[1] = (int16_t)(-i * 7);
        s[i].gyr[2] = 0;
        s[i].temp = (int16_t)(-1200 + i);
    }
    uint8_t p[FRAME_PAYLOAD_MAX];
    size_t n = msg_put_imu_batch(p, sizeof p, s, IMU_BATCH_MAX);
    if (begin("imu_batch", "IMU_BATCH", MSG_IMU_BATCH, 2, 5040000, p, n)) return -1;
    ju("count", IMU_BATCH_MAX);
    jopen("samples", '[');
    for (int i = 0; i < IMU_BATCH_MAX; i++) {
        jopen(NULL, '{');
        ju("seq", s[i].seq);
        ju("t_us", s[i].t_us);
        jarr_i16("acc", s[i].acc, 3);
        jarr_i16("gyr", s[i].gyr, 3);
        ji("temp", s[i].temp);
        jclose('}');
    }
    jclose(']');
    end();
    return 0;
}

static int boot(void)
{
    boot_t b;
    memset(&b, 0, sizeof b);
    b.ver_major = 0;
    b.ver_minor = 1;
    b.ver_patch = 0;
    memcpy(b.git_sha, "0123abcd", 8);
    b.build_type = 0;
    b.fault_injection = 1;
    b.reset_reason = RESET_HARDFAULT;
    b.boot_count = 3;
    b.detail = 0;
    b.pc = 0x080012A4u;
    b.lr = 0x08000F11u;
    b.psr = 0x01000000u;
    b.cfsr = 0x00010000u;
    b.hfsr = 0x40000000u;
    b.mmfar = 0xE000ED34u;
    b.bfar = 0xE000ED38u;
    memcpy(b.task, "cmd", 3);
    memcpy(b.file, "fault_inject.c", 14);
    b.vrefint_cal = 1655;
    b.ts_cal1 = 941;
    b.ts_cal2 = 1206;
    b.sample_rate_hz = 1000;
    b.batch = 10;
    b.ring_capacity = 128;
    b.pool_size = 4;
    uint8_t p[FRAME_PAYLOAD_MAX];
    size_t n = msg_put_boot(p, sizeof p, &b);
    if (begin("boot", "BOOT", MSG_BOOT, 0, 1234, p, n)) return -1;
    ju("ver_major", b.ver_major);
    ju("ver_minor", b.ver_minor);
    ju("ver_patch", b.ver_patch);
    jstr("git_sha", b.git_sha, sizeof b.git_sha);
    ju("build_type", b.build_type);
    ju("fault_injection", b.fault_injection);
    ju("reset_reason", b.reset_reason);
    ju("boot_count", b.boot_count);
    ju("detail", b.detail);
    ju("pc", b.pc);
    ju("lr", b.lr);
    ju("psr", b.psr);
    ju("cfsr", b.cfsr);
    ju("hfsr", b.hfsr);
    ju("mmfar", b.mmfar);
    ju("bfar", b.bfar);
    jstr("task", b.task, sizeof b.task);
    jstr("file", b.file, sizeof b.file);
    ju("vrefint_cal", b.vrefint_cal);
    ju("ts_cal1", b.ts_cal1);
    ju("ts_cal2", b.ts_cal2);
    ju("sample_rate_hz", b.sample_rate_hz);
    ju("batch", b.batch);
    ju("ring_capacity", b.ring_capacity);
    ju("pool_size", b.pool_size);
    end();
    return 0;
}

static int health(void)
{
    health_t h;
    memset(&h, 0, sizeof h);
    h.uptime_ms = 5000;
    h.triggered = 5000;
    h.delivered = 4960;
    h.dropped_overrun = 1;
    h.dropped_ring = 2;
    h.dropped_txpool = 10;
    h.frames_sent = 503;
    h.frames_dropped = 1;
    h.rx_frames_ok = 4;
    h.rx_frames_bad = 2;
    h.rx_bytes_dropped = 0;
    h.event_drops = 0;
    h.tx_timeouts = 0;
    h.ring_occ = 3;
    h.ring_hwm = 14;
    h.ring_capacity = 128;
    h.txpool_free_min = 2;
    h.txpool_size = 4;
    h.cpu_load_permille = 41;
    h.sample_rate_hz = 1000;
    h.sensor_status = 0x0400;
    h.fault_flags = 0;
    h.n_tasks = 5;
    const uint16_t st[5] = {201, 150, 260, 270, 90};
    memcpy(h.stack_free_min_words, st, sizeof st);
    h.msp_free_bytes = 812;
    const uint32_t hist[HIST_BUCKETS] = {0, 0, 0, 0, 480, 16, 0, 0};
    memcpy(h.l1_hist, hist, sizeof hist);
    h.l1_max_us = 10950;
    h.l2_max_us = 37;
    h.imu_samples = 1040;
    h.imu_dropped = 0;
    h.i2c_errors = 1;
    h.imu_status = 1;
    uint8_t p[FRAME_PAYLOAD_MAX];
    size_t n = msg_put_health(p, sizeof p, &h);
    if (begin("health", "HEALTH", MSG_HEALTH, 7, 5000123, p, n)) return -1;
    ju("uptime_ms", h.uptime_ms);
    ju("triggered", h.triggered);
    ju("delivered", h.delivered);
    ju("dropped_overrun", h.dropped_overrun);
    ju("dropped_ring", h.dropped_ring);
    ju("dropped_txpool", h.dropped_txpool);
    ju("frames_sent", h.frames_sent);
    ju("frames_dropped", h.frames_dropped);
    ju("rx_frames_ok", h.rx_frames_ok);
    ju("rx_frames_bad", h.rx_frames_bad);
    ju("rx_bytes_dropped", h.rx_bytes_dropped);
    ju("event_drops", h.event_drops);
    ju("tx_timeouts", h.tx_timeouts);
    ju("ring_occ", h.ring_occ);
    ju("ring_hwm", h.ring_hwm);
    ju("ring_capacity", h.ring_capacity);
    ju("txpool_free_min", h.txpool_free_min);
    ju("txpool_size", h.txpool_size);
    ju("cpu_load_permille", h.cpu_load_permille);
    ju("sample_rate_hz", h.sample_rate_hz);
    ju("sensor_status", h.sensor_status);
    ju("fault_flags", h.fault_flags);
    ju("n_tasks", h.n_tasks);
    jarr_u16("stack_free_min_words", h.stack_free_min_words, h.n_tasks);
    ju("msp_free_bytes", h.msp_free_bytes);
    jarr_u32("l1_hist", h.l1_hist, HIST_BUCKETS);
    ju("l1_max_us", h.l1_max_us);
    ju("l2_max_us", h.l2_max_us);
    ju("imu_samples", h.imu_samples);
    ju("imu_dropped", h.imu_dropped);
    ju("i2c_errors", h.i2c_errors);
    ju("imu_status", h.imu_status);
    end();
    return 0;
}

static int event(void)
{
    const event_rec_t e[3] = {{10, 1, 3}, {20, 2, 3}, {4294967295u, 3, 0xFFFFFFFFu}};
    uint8_t p[FRAME_PAYLOAD_MAX];
    size_t n = msg_put_event(p, sizeof p, e, 3);
    if (begin("event", "EVENT", MSG_EVENT, 8, 4294967295u, p, n)) return -1;
    ju("count", 3);
    jopen("events", '[');
    for (int i = 0; i < 3; i++) {
        jopen(NULL, '{');
        ju("t_us", e[i].t_us);
        ju("id", e[i].id);
        ju("arg", e[i].arg);
        jclose('}');
    }
    jclose(']');
    end();
    return 0;
}

static int ack(void)
{
    const ack_t a = {.cmd_seq = 3, .cmd = MSG_CMD_SET_RATE, .status = ACK_ERR_RANGE};
    uint8_t p[FRAME_PAYLOAD_MAX];
    size_t n = msg_put_ack(p, sizeof p, &a);
    if (begin("ack", "ACK", MSG_ACK, 9, 777, p, n)) return -1;
    ju("cmd_seq", a.cmd_seq);
    ju("cmd", a.cmd);
    ju("status", a.status);
    end();
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <outdir>\n", argv[0]);
        return 2;
    }
    outdir = argv[1];
    if (sample_batch() || imu_batch() || boot() || health() || event() || ack()) {
        fprintf(stderr, "gen_vectors: failed writing to %s\n", outdir);
        return 1;
    }
    return 0;
}
