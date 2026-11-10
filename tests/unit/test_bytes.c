/* bytes.h: LE writer/reader never touch memory out of bounds and latch errors. */
#include <string.h>

#include "bytes.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_le_layout_and_readback(void)
{
    uint8_t b[16];
    wbuf_t w = {b, sizeof b, 0, false};
    wb_u8(&w, 0x01);
    wb_u16(&w, 0x0302);
    wb_u32(&w, 0x07060504u);
    wb_i16(&w, -2);
    wb_bytes(&w, "xy", 2);
    TEST_ASSERT_FALSE(w.err);
    TEST_ASSERT_EQUAL_size_t(11, w.len);
    const uint8_t exp[] = {1, 2, 3, 4, 5, 6, 7, 0xFE, 0xFF, 'x', 'y'};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(exp, b, sizeof exp);

    rbuf_t r = {b, w.len, 0, false};
    TEST_ASSERT_EQUAL_HEX8(0x01, rb_u8(&r));
    TEST_ASSERT_EQUAL_HEX16(0x0302, rb_u16(&r));
    TEST_ASSERT_EQUAL_HEX32(0x07060504u, rb_u32(&r));
    TEST_ASSERT_EQUAL_INT16(-2, rb_i16(&r));
    char s[2];
    rb_bytes(&r, s, 2);
    TEST_ASSERT_EQUAL_MEMORY("xy", s, 2);
    TEST_ASSERT_FALSE(r.err);
    TEST_ASSERT_EQUAL_UINT8(0, rb_u8(&r)); /* past end */
    TEST_ASSERT_TRUE(r.err);
}

static void test_writer_overflow_latches(void)
{
    uint8_t b[8];
    memset(b, 0xCC, sizeof b);
    wbuf_t w = {b, 3, 0, false};
    wb_u16(&w, 0xAAAA);
    wb_u16(&w, 0xBBBB); /* does not fit: nothing written */
    TEST_ASSERT_TRUE(w.err);
    TEST_ASSERT_EQUAL_size_t(2, w.len);
    wb_u8(&w, 0xDD); /* would fit, but error is latched */
    TEST_ASSERT_EQUAL_size_t(2, w.len);
    TEST_ASSERT_EQUAL_HEX8(0xCC, b[2]);
    TEST_ASSERT_EQUAL_HEX8(0xCC, b[3]);
}

static void test_reader_short_zero_fills(void)
{
    const uint8_t b[3] = {1, 2, 3};
    rbuf_t r = {b, 3, 0, false};
    TEST_ASSERT_EQUAL_HEX32(0, rb_u32(&r));
    TEST_ASSERT_TRUE(r.err);
    TEST_ASSERT_EQUAL_UINT8(0, rb_u8(&r)); /* latched */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_le_layout_and_readback);
    RUN_TEST(test_writer_overflow_latches);
    RUN_TEST(test_reader_short_zero_fills);
    return UNITY_END();
}
