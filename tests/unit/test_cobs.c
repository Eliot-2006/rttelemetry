/* UT-COBS (PRD §19.2) */
#include <string.h>

#include "cobs.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static uint8_t in[1100], enc[1200], dec[1200];

static void fill(size_t n, unsigned seed)
{
    /* deterministic mix with plenty of zeros */
    unsigned x = seed * 2654435761u + 1u;
    for (size_t i = 0; i < n; i++) {
        x = x * 1103515245u + 12345u;
        in[i] = (x >> 16) % 5u == 0 ? 0 : (uint8_t)(x >> 8);
    }
}

static void roundtrip(size_t n)
{
    size_t e = cobs_encode(in, n, enc, sizeof enc);
    TEST_ASSERT_TRUE(e >= 1);
    TEST_ASSERT_TRUE(e <= n + (n + 253) / 254 + 1); /* <= n + ceil(n/254) + 1 */
    TEST_ASSERT_TRUE(e <= COBS_MAX_ENCODED(n));
    TEST_ASSERT_NULL(memchr(enc, 0, e));
    size_t d = cobs_decode(enc, e, dec, sizeof dec);
    TEST_ASSERT_EQUAL_size_t(n, d);
    if (n) TEST_ASSERT_EQUAL_UINT8_ARRAY(in, dec, n);
}

static void test_roundtrip_lengths_0_to_1024(void)
{
    for (size_t n = 0; n <= 1024; n++) {
        fill(n, (unsigned)n);
        roundtrip(n);
    }
}

static void test_nonzero_runs_at_block_boundaries(void)
{
    const size_t lens[] = {1, 253, 254, 255, 507, 508, 509, 1024};
    for (size_t i = 0; i < sizeof lens / sizeof lens[0]; i++) {
        memset(in, 0x5A, lens[i]);
        roundtrip(lens[i]);
        /* also surrounded by zeros */
        in[0] = 0;
        in[lens[i] - 1] = 0;
        roundtrip(lens[i]);
    }
}

static void test_exact_encodings(void)
{
    const uint8_t e0[] = {0x01};
    TEST_ASSERT_EQUAL_size_t(1, cobs_encode(in, 0, enc, sizeof enc));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(e0, enc, 1);

    const uint8_t i1[] = {0x11, 0x22, 0x00, 0x33}, e1[] = {0x03, 0x11, 0x22, 0x02, 0x33};
    TEST_ASSERT_EQUAL_size_t(5, cobs_encode(i1, 4, enc, sizeof enc));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(e1, enc, 5);

    const uint8_t i2[] = {0x00, 0x00}, e2[] = {0x01, 0x01, 0x01};
    TEST_ASSERT_EQUAL_size_t(3, cobs_encode(i2, 2, enc, sizeof enc));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(e2, enc, 3);

    /* 254 non-zero bytes: single 0xFF block, no trailing code */
    memset(in, 0x01, 254);
    TEST_ASSERT_EQUAL_size_t(255, cobs_encode(in, 254, enc, sizeof enc));
    TEST_ASSERT_EQUAL_HEX8(0xFF, enc[0]);
    /* 255 non-zero bytes: 0xFF block then a 0x02 block */
    memset(in, 0x01, 255);
    TEST_ASSERT_EQUAL_size_t(257, cobs_encode(in, 255, enc, sizeof enc));
    TEST_ASSERT_EQUAL_HEX8(0x02, enc[255]);
}

static void test_all_zeros(void)
{
    for (size_t n = 1; n <= 600; n += 37) {
        memset(in, 0, n);
        roundtrip(n);
        TEST_ASSERT_EQUAL_size_t(n + 1, cobs_encode(in, n, enc, sizeof enc));
    }
}

static void test_encode_cap_too_small(void)
{
    fill(100, 3);
    size_t e = cobs_encode(in, 100, enc, sizeof enc);
    for (size_t cap = 0; cap < e; cap++) TEST_ASSERT_EQUAL_size_t(0, cobs_encode(in, 100, enc, cap));
    TEST_ASSERT_EQUAL_size_t(e, cobs_encode(in, 100, enc, e));
    memset(in, 7, 254);
    TEST_ASSERT_EQUAL_size_t(0, cobs_encode(in, 254, enc, 254));
}

static void test_decode_rejects_malformed(void)
{
    const uint8_t zero_inside[] = {0x03, 0x11, 0x00, 0x02, 0x33};
    const uint8_t zero_code[] = {0x02, 0x11, 0x00};
    const uint8_t overrun[] = {0x05, 0x11, 0x22};
    const uint8_t overrun_ff[] = {0xFF, 0x11};
    TEST_ASSERT_EQUAL_size_t(0, cobs_decode(zero_inside, sizeof zero_inside, dec, sizeof dec));
    TEST_ASSERT_EQUAL_size_t(0, cobs_decode(zero_code, sizeof zero_code, dec, sizeof dec));
    TEST_ASSERT_EQUAL_size_t(0, cobs_decode(overrun, sizeof overrun, dec, sizeof dec));
    TEST_ASSERT_EQUAL_size_t(0, cobs_decode(overrun_ff, sizeof overrun_ff, dec, sizeof dec));
    TEST_ASSERT_EQUAL_size_t(0, cobs_decode(overrun, 0, dec, sizeof dec));
}

static void test_decode_cap(void)
{
    fill(300, 9);
    size_t e = cobs_encode(in, 300, enc, sizeof enc);
    for (size_t cap = 0; cap < 300; cap += 7) TEST_ASSERT_EQUAL_size_t(0, cobs_decode(enc, e, dec, cap));
    TEST_ASSERT_EQUAL_size_t(300, cobs_decode(enc, e, dec, 300));
    /* trailing implicit zero must also fit */
    const uint8_t two[] = {0x02, 0x41, 0x01};
    TEST_ASSERT_EQUAL_size_t(0, cobs_decode(two, 3, dec, 1));
    TEST_ASSERT_EQUAL_size_t(2, cobs_decode(two, 3, dec, 2));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_roundtrip_lengths_0_to_1024);
    RUN_TEST(test_nonzero_runs_at_block_boundaries);
    RUN_TEST(test_exact_encodings);
    RUN_TEST(test_all_zeros);
    RUN_TEST(test_encode_cap_too_small);
    RUN_TEST(test_decode_rejects_malformed);
    RUN_TEST(test_decode_cap);
    return UNITY_END();
}
