/* UT-CRC (PRD §19.2) */
#include "crc16.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_check_value(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16_ccitt(CRC16_INIT, "123456789", 9));
}

static void test_empty_returns_init(void)
{
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, crc16_ccitt(CRC16_INIT, "", 0));
    TEST_ASSERT_EQUAL_HEX16(0x1234, crc16_ccitt(0x1234, NULL, 0));
}

static void test_incremental_equals_one_shot(void)
{
    uint8_t buf[300];
    for (unsigned i = 0; i < sizeof buf; i++) buf[i] = (uint8_t)(i * 31u + 7u);
    uint16_t whole = crc16_ccitt(CRC16_INIT, buf, sizeof buf);
    for (size_t split = 0; split <= sizeof buf; split += 13) {
        uint16_t c = crc16_ccitt(CRC16_INIT, buf, split);
        TEST_ASSERT_EQUAL_HEX16(whole, crc16_ccitt(c, buf + split, sizeof buf - split));
    }
}

static void test_known_vectors(void)
{
    /* Cross-checked with Python binascii.crc_hqx(data, 0xFFFF). */
    const uint8_t a[] = {0x00};
    const uint8_t ff[] = {0xFF, 0xFF};
    TEST_ASSERT_EQUAL_HEX16(0xE1F0, crc16_ccitt(CRC16_INIT, a, 1));
    TEST_ASSERT_EQUAL_HEX16(0x0000, crc16_ccitt(CRC16_INIT, ff, 2));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_check_value);
    RUN_TEST(test_empty_returns_init);
    RUN_TEST(test_incremental_equals_one_shot);
    RUN_TEST(test_known_vectors);
    return UNITY_END();
}
