/* UT-CMD (PRD §19.2) */
#include <string.h>

#include "cmd.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static cmd_t c;

static uint8_t parse(uint8_t type, const uint8_t *p, uint16_t n, bool fi)
{
    frame_hdr_t h = {.version = FRAME_VERSION, .type = type, .seq = 0x1234, .t_us = 0, .payload_len = n};
    memset(&c, 0xEE, sizeof c);
    uint8_t st = cmd_parse(&h, p, fi, &c);
    TEST_ASSERT_EQUAL_UINT8(type, c.type);
    TEST_ASSERT_EQUAL_UINT16(0x1234, c.seq);
    return st;
}

static void test_no_arg_commands(void)
{
    const uint8_t types[] = {MSG_CMD_PING, MSG_CMD_GET_STATUS, MSG_CMD_RESET_COUNTERS, MSG_CMD_REBOOT};
    const uint8_t x[1] = {0};
    for (size_t i = 0; i < sizeof types; i++) {
        TEST_ASSERT_EQUAL_UINT8(ACK_OK, parse(types[i], NULL, 0, false));
        TEST_ASSERT_EQUAL_UINT16(0, c.rate_hz);
        TEST_ASSERT_EQUAL_UINT8(0, c.fault_id);
        TEST_ASSERT_EQUAL_UINT32(0, c.fault_param);
        TEST_ASSERT_EQUAL_UINT8(ACK_OK, parse(types[i], NULL, 0, true));
        TEST_ASSERT_EQUAL_UINT8(ACK_ERR_LEN, parse(types[i], x, 1, false));
    }
}

static void test_set_rate(void)
{
    const uint16_t ok[] = {0, 100, 250, 500, 1000, 2000, 4000, 8000};
    const uint16_t bad[] = {1, 99, 101, 249, 999, 1001, 3000, 7999, 8001, 16000, 0xFFFF};
    for (size_t i = 0; i < sizeof ok / sizeof ok[0]; i++) {
        const uint8_t p[2] = {(uint8_t)ok[i], (uint8_t)(ok[i] >> 8)};
        TEST_ASSERT_EQUAL_UINT8(ACK_OK, parse(MSG_CMD_SET_RATE, p, 2, false));
        TEST_ASSERT_EQUAL_UINT16(ok[i], c.rate_hz);
    }
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        const uint8_t p[2] = {(uint8_t)bad[i], (uint8_t)(bad[i] >> 8)};
        TEST_ASSERT_EQUAL_UINT8(ACK_ERR_RANGE, parse(MSG_CMD_SET_RATE, p, 2, false));
    }
    const uint8_t p3[3] = {0xE8, 0x03, 0};
    TEST_ASSERT_EQUAL_UINT8(ACK_ERR_LEN, parse(MSG_CMD_SET_RATE, p3, 3, false));
    TEST_ASSERT_EQUAL_UINT8(ACK_ERR_LEN, parse(MSG_CMD_SET_RATE, p3, 1, false));
    TEST_ASSERT_EQUAL_UINT8(ACK_ERR_LEN, parse(MSG_CMD_SET_RATE, NULL, 0, false));
}

static void test_fi_arm(void)
{
    for (uint8_t id = FAULT_STALL_PROC; id <= FAULT_I2C_NACK; id++) {
        const uint8_t p[5] = {id, 0x78, 0x56, 0x34, 0x12};
        TEST_ASSERT_EQUAL_UINT8(ACK_OK, parse(MSG_CMD_FI_ARM, p, 5, true));
        TEST_ASSERT_EQUAL_UINT8(id, c.fault_id);
        TEST_ASSERT_EQUAL_HEX32(0x12345678, c.fault_param);
        TEST_ASSERT_EQUAL_UINT8(ACK_UNSUPPORTED, parse(MSG_CMD_FI_ARM, p, 5, false));
    }
    const uint8_t id0[5] = {0}, id9[5] = {9}, idff[5] = {0xFF};
    TEST_ASSERT_EQUAL_UINT8(ACK_ERR_RANGE, parse(MSG_CMD_FI_ARM, id0, 5, true));
    TEST_ASSERT_EQUAL_UINT8(ACK_ERR_RANGE, parse(MSG_CMD_FI_ARM, id9, 5, true));
    TEST_ASSERT_EQUAL_UINT8(ACK_ERR_RANGE, parse(MSG_CMD_FI_ARM, idff, 5, true));
    const uint8_t p6[6] = {1};
    TEST_ASSERT_EQUAL_UINT8(ACK_ERR_LEN, parse(MSG_CMD_FI_ARM, p6, 6, true));
    TEST_ASSERT_EQUAL_UINT8(ACK_ERR_LEN, parse(MSG_CMD_FI_ARM, p6, 4, true));
    TEST_ASSERT_EQUAL_UINT8(ACK_ERR_LEN, parse(MSG_CMD_FI_ARM, NULL, 0, true));
    /* release builds: UNSUPPORTED regardless of length/range */
    TEST_ASSERT_EQUAL_UINT8(ACK_UNSUPPORTED, parse(MSG_CMD_FI_ARM, p6, 6, false));
    TEST_ASSERT_EQUAL_UINT8(ACK_UNSUPPORTED, parse(MSG_CMD_FI_ARM, id9, 5, false));
}

static void test_fi_clear(void)
{
    const uint8_t x[1] = {0};
    TEST_ASSERT_EQUAL_UINT8(ACK_OK, parse(MSG_CMD_FI_CLEAR, NULL, 0, true));
    TEST_ASSERT_EQUAL_UINT8(ACK_ERR_LEN, parse(MSG_CMD_FI_CLEAR, x, 1, true));
    TEST_ASSERT_EQUAL_UINT8(ACK_UNSUPPORTED, parse(MSG_CMD_FI_CLEAR, NULL, 0, false));
    TEST_ASSERT_EQUAL_UINT8(ACK_UNSUPPORTED, parse(MSG_CMD_FI_CLEAR, x, 1, false));
}

static void test_unknown_types(void)
{
    const uint8_t unknown[] = {0x00, MSG_SAMPLE_BATCH, MSG_HEALTH, MSG_ACK, 0x7F, 0x85, 0x8F, 0x92, 0xFF};
    for (size_t i = 0; i < sizeof unknown; i++) {
        TEST_ASSERT_EQUAL_UINT8(ACK_ERR_UNKNOWN, parse(unknown[i], NULL, 0, true));
        TEST_ASSERT_EQUAL_UINT8(ACK_ERR_UNKNOWN, parse(unknown[i], NULL, 0, false));
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_no_arg_commands);
    RUN_TEST(test_set_rate);
    RUN_TEST(test_fi_arm);
    RUN_TEST(test_fi_clear);
    RUN_TEST(test_unknown_types);
    return UNITY_END();
}
