/* Payload encode/decode round trips for every message type. */
#include <string.h>

#include "msg.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static uint8_t buf[512];

static void test_sample_batch_roundtrip_and_layout(void)
{
    sample_rec_t s[SAMPLE_BATCH_MAX], o[SAMPLE_BATCH_MAX];
    memset(s, 0, sizeof s);
    for (uint32_t i = 0; i < SAMPLE_BATCH_MAX; i++) {
        s[i].seq = 0x01020304u + i;
        s[i].t_us = 0xA0B0C0D0u + i;
        for (int c = 0; c < 4; c++) s[i].raw[c] = (uint16_t)(0x1000 * c + i);
        s[i].status = (uint16_t)(0xF00F ^ i);
    }
    size_t n = msg_put_sample_batch(buf, sizeof buf, s, SAMPLE_BATCH_MAX, 0x0F);
    TEST_ASSERT_EQUAL_size_t(2 + 10 * SAMPLE_REC_WIRE, n);
    const uint8_t head[] = {10, 0x0F, 0x04, 0x03, 0x02, 0x01, 0xD0, 0xC0, 0xB0, 0xA0, 0x00, 0x00, 0x00, 0x10,
                            0x00, 0x20, 0x00, 0x30, 0x0F, 0xF0, 0x05, 0x03};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(head, buf, sizeof head);
    uint8_t count = 0, mask = 0;
    TEST_ASSERT_TRUE(msg_get_sample_batch(buf, n, o, &count, &mask));
    TEST_ASSERT_EQUAL_UINT8(10, count);
    TEST_ASSERT_EQUAL_UINT8(0x0F, mask);
    TEST_ASSERT_EQUAL_MEMORY(s, o, sizeof s);

    TEST_ASSERT_FALSE(msg_get_sample_batch(buf, n - 1, o, &count, &mask));
    TEST_ASSERT_TRUE(msg_get_sample_batch(buf, n + 5, o, &count, &mask)); /* longer accepted */
    TEST_ASSERT_EQUAL_size_t(0, msg_put_sample_batch(buf, n - 1, s, 10, 0x0F));
    TEST_ASSERT_EQUAL_size_t(0, msg_put_sample_batch(buf, sizeof buf, s, SAMPLE_BATCH_MAX + 1, 0x0F));
    TEST_ASSERT_EQUAL_size_t(2, msg_put_sample_batch(buf, sizeof buf, NULL, 0, 0x03));
    TEST_ASSERT_TRUE(msg_get_sample_batch(buf, 2, o, &count, &mask));
    TEST_ASSERT_EQUAL_UINT8(0, count);
    buf[0] = SAMPLE_BATCH_MAX + 1;
    TEST_ASSERT_FALSE(msg_get_sample_batch(buf, sizeof buf, o, &count, &mask));
}

static void test_imu_batch_roundtrip(void)
{
    imu_rec_t s[IMU_BATCH_MAX], o[IMU_BATCH_MAX];
    memset(s, 0, sizeof s);
    for (int i = 0; i < IMU_BATCH_MAX; i++) {
        s[i].seq = (uint32_t)i * 3u;
        s[i].t_us = 0xFFFFFFF0u + (uint32_t)i;
        for (int k = 0; k < 3; k++) {
            s[i].acc[k] = (int16_t)(-32768 + i * 100 + k);
            s[i].gyr[k] = (int16_t)(32767 - i * 100 - k);
        }
        s[i].temp = (int16_t)(-i);
    }
    size_t n = msg_put_imu_batch(buf, sizeof buf, s, IMU_BATCH_MAX);
    TEST_ASSERT_EQUAL_size_t(1 + 8 * IMU_REC_WIRE, n);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[1 + 8]);  /* acc[0] of rec 0 = -32768 -> 00 80 */
    TEST_ASSERT_EQUAL_HEX8(0x80, buf[1 + 9]);
    uint8_t count = 0;
    TEST_ASSERT_TRUE(msg_get_imu_batch(buf, n, o, &count));
    TEST_ASSERT_EQUAL_UINT8(8, count);
    TEST_ASSERT_EQUAL_MEMORY(s, o, sizeof s);
    TEST_ASSERT_FALSE(msg_get_imu_batch(buf, n - 1, o, &count));
    TEST_ASSERT_FALSE(msg_get_imu_batch(buf, 0, o, &count));
    TEST_ASSERT_EQUAL_size_t(0, msg_put_imu_batch(buf, n - 1, s, 8));
    TEST_ASSERT_EQUAL_size_t(0, msg_put_imu_batch(buf, sizeof buf, s, IMU_BATCH_MAX + 1));
    buf[0] = IMU_BATCH_MAX + 1;
    TEST_ASSERT_FALSE(msg_get_imu_batch(buf, sizeof buf, o, &count));
}

static boot_t make_boot(void)
{
    boot_t b;
    memset(&b, 0, sizeof b);
    b.ver_major = 1;
    b.ver_minor = 2;
    b.ver_patch = 3;
    memcpy(b.git_sha, "abcdef01", 8);
    b.build_type = 1;
    b.fault_injection = 1;
    b.reset_reason = RESET_HARDFAULT;
    b.boot_count = 42;
    b.detail = 0x11;
    b.pc = 0x08001234;
    b.lr = 0x08005679;
    b.psr = 0x21000000;
    b.cfsr = 0x00010000;
    b.hfsr = 0x40000000;
    b.mmfar = 0xE000ED34;
    b.bfar = 0xE000ED38;
    memcpy(b.task, "cmd", 3);
    memcpy(b.file, "src/app/fault_inject.c", 22);
    b.vrefint_cal = 1500;
    b.ts_cal1 = 940;
    b.ts_cal2 = 1200;
    b.sample_rate_hz = 1000;
    b.batch = 10;
    b.ring_capacity = 128;
    b.pool_size = 4;
    return b;
}

static void test_boot_roundtrip(void)
{
    boot_t b = make_boot(), o;
    size_t n = msg_put_boot(buf, sizeof buf, &b);
    TEST_ASSERT_EQUAL_size_t(BOOT_WIRE, n);
    TEST_ASSERT_EQUAL_MEMORY("abcdef01", buf + 3, 8);
    TEST_ASSERT_EQUAL_UINT8(RESET_HARDFAULT, buf[13]);
    TEST_ASSERT_EQUAL_UINT8(4, buf[BOOT_WIRE - 1]);
    memset(&o, 0xEE, sizeof o);
    TEST_ASSERT_TRUE(msg_get_boot(buf, n, &o));
    TEST_ASSERT_EQUAL_MEMORY(&b, &o, offsetof(boot_t, pool_size) + 1);
    TEST_ASSERT_FALSE(msg_get_boot(buf, n - 1, &o));
    TEST_ASSERT_TRUE(msg_get_boot(buf, n + 10, &o));
    TEST_ASSERT_EQUAL_size_t(0, msg_put_boot(buf, n - 1, &b));
}

static health_t make_health(uint8_t n_tasks)
{
    health_t h;
    memset(&h, 0, sizeof h);
    h.uptime_ms = 0x10000000;
    h.triggered = 0x20000001;
    h.delivered = 0x30000002;
    h.dropped_overrun = 0x40000003;
    h.dropped_ring = 0x50000004;
    h.dropped_txpool = 0x60000005;
    h.frames_sent = 0x70000006;
    h.frames_dropped = 0x80000007;
    h.rx_frames_ok = 0x90000008;
    h.rx_frames_bad = 0xA0000009;
    h.rx_bytes_dropped = 0xB000000A;
    h.event_drops = 0xC000000B;
    h.tx_timeouts = 0xD000000C;
    h.ring_occ = 3;
    h.ring_hwm = 17;
    h.ring_capacity = 128;
    h.txpool_free_min = 1;
    h.txpool_size = 4;
    h.cpu_load_permille = 37;
    h.sample_rate_hz = 8000;
    h.sensor_status = 0x0421;
    h.fault_flags = 0x8001;
    h.n_tasks = n_tasks;
    for (uint8_t i = 0; i < n_tasks; i++) h.stack_free_min_words[i] = (uint16_t)(100 + i);
    h.msp_free_bytes = 900;
    for (uint32_t i = 0; i < HIST_BUCKETS; i++) h.l1_hist[i] = 1000u * i;
    h.l1_max_us = 9500;
    h.l2_max_us = 150;
    h.imu_samples = 2080;
    h.imu_dropped = 1;
    h.i2c_errors = 2;
    h.imu_status = 3;
    return h;
}

static void test_health_roundtrip_all_task_counts(void)
{
    for (uint8_t nt = 0; nt <= HEALTH_MAX_TASKS; nt++) {
        health_t h = make_health(nt), o;
        size_t n = msg_put_health(buf, sizeof buf, &h);
        TEST_ASSERT_EQUAL_size_t(HEALTH_WIRE(nt), n);
        TEST_ASSERT_EQUAL_UINT8(nt, buf[68]);
        TEST_ASSERT_EQUAL_UINT8(3, buf[n - 1]);
        memset(&o, 0xEE, sizeof o);
        TEST_ASSERT_TRUE(msg_get_health(buf, n, &o));
        TEST_ASSERT_EQUAL_MEMORY(&h, &o, sizeof h); /* unused stack slots zeroed */
        TEST_ASSERT_FALSE(msg_get_health(buf, n - 1, &o));
        TEST_ASSERT_TRUE(msg_get_health(buf, n + 3, &o));
        TEST_ASSERT_EQUAL_size_t(0, msg_put_health(buf, n - 1, &h));
    }
    health_t h = make_health(HEALTH_MAX_TASKS);
    h.n_tasks = HEALTH_MAX_TASKS + 1;
    TEST_ASSERT_EQUAL_size_t(0, msg_put_health(buf, sizeof buf, &h));
    h.n_tasks = HEALTH_MAX_TASKS;
    size_t n = msg_put_health(buf, sizeof buf, &h);
    buf[68] = HEALTH_MAX_TASKS + 1;
    health_t o;
    TEST_ASSERT_FALSE(msg_get_health(buf, n + 2, &o));
}

static void test_event_roundtrip(void)
{
    event_rec_t e[EVENT_BATCH_MAX], o[EVENT_BATCH_MAX];
    memset(e, 0, sizeof e);
    for (int i = 0; i < EVENT_BATCH_MAX; i++) {
        e[i].t_us = 1000000u * (uint32_t)i;
        e[i].id = (uint16_t)(i + 1);
        e[i].arg = 0xCAFE0000u + (uint32_t)i;
    }
    size_t n = msg_put_event(buf, sizeof buf, e, EVENT_BATCH_MAX);
    TEST_ASSERT_EQUAL_size_t(1 + 8 * EVENT_REC_WIRE, n);
    TEST_ASSERT_EQUAL_HEX8(0, buf[1 + 6]); /* rsv */
    TEST_ASSERT_EQUAL_HEX8(0, buf[1 + 7]);
    uint8_t count = 0;
    TEST_ASSERT_TRUE(msg_get_event(buf, n, o, &count));
    TEST_ASSERT_EQUAL_UINT8(8, count);
    TEST_ASSERT_EQUAL_MEMORY(e, o, sizeof e);
    TEST_ASSERT_FALSE(msg_get_event(buf, n - 1, o, &count));
    TEST_ASSERT_EQUAL_size_t(0, msg_put_event(buf, n - 1, e, 8));
    TEST_ASSERT_EQUAL_size_t(0, msg_put_event(buf, sizeof buf, e, EVENT_BATCH_MAX + 1));
    buf[0] = EVENT_BATCH_MAX + 1;
    TEST_ASSERT_FALSE(msg_get_event(buf, sizeof buf, o, &count));
}

static void test_ack_roundtrip(void)
{
    ack_t a = {.cmd_seq = 0xBEEF, .cmd = 0x82, .status = ACK_ERR_RANGE}, o;
    size_t n = msg_put_ack(buf, sizeof buf, &a);
    TEST_ASSERT_EQUAL_size_t(ACK_WIRE, n);
    const uint8_t exp[] = {0xEF, 0xBE, 0x82, 0x03};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(exp, buf, 4);
    TEST_ASSERT_TRUE(msg_get_ack(buf, n, &o));
    TEST_ASSERT_EQUAL_UINT16(0xBEEF, o.cmd_seq);
    TEST_ASSERT_EQUAL_UINT8(0x82, o.cmd);
    TEST_ASSERT_EQUAL_UINT8(ACK_ERR_RANGE, o.status);
    TEST_ASSERT_FALSE(msg_get_ack(buf, 3, &o));
    TEST_ASSERT_EQUAL_size_t(0, msg_put_ack(buf, 3, &a));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sample_batch_roundtrip_and_layout);
    RUN_TEST(test_imu_batch_roundtrip);
    RUN_TEST(test_boot_roundtrip);
    RUN_TEST(test_health_roundtrip_all_task_counts);
    RUN_TEST(test_event_roundtrip);
    RUN_TEST(test_ack_roundtrip);
    return UNITY_END();
}
