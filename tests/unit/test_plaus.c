/* UT-PLAUS (PRD §19.2) */
#include "plaus.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static const plaus_cfg_t cfg = {.min = 100, .max = 4000, .roc_max = 500, .stuck_n = 50};

static void test_range_inclusive_boundaries(void)
{
    plaus_state_t st = {0};
    TEST_ASSERT_EQUAL_UINT8(0, plaus_check(&cfg, &st, 100));
    st = (plaus_state_t){0};
    TEST_ASSERT_EQUAL_UINT8(PLAUS_RANGE, plaus_check(&cfg, &st, 99));
    st = (plaus_state_t){0};
    TEST_ASSERT_EQUAL_UINT8(0, plaus_check(&cfg, &st, 4000));
    st = (plaus_state_t){0};
    TEST_ASSERT_EQUAL_UINT8(PLAUS_RANGE, plaus_check(&cfg, &st, 4001));
    st = (plaus_state_t){0};
    TEST_ASSERT_EQUAL_UINT8(PLAUS_RANGE, plaus_check(&cfg, &st, 0));
    st = (plaus_state_t){0};
    TEST_ASSERT_EQUAL_UINT8(PLAUS_RANGE, plaus_check(&cfg, &st, 0xFFFF));
}

static void test_stuck_at_n_minus_1_and_n(void)
{
    plaus_state_t st = {0};
    for (int i = 1; i <= 49; i++) TEST_ASSERT_EQUAL_UINT8(0, plaus_check(&cfg, &st, 2000)); /* N-1 identical */
    TEST_ASSERT_EQUAL_UINT8(PLAUS_STUCK, plaus_check(&cfg, &st, 2000));                   /* N-th */
    for (int i = 0; i < 70000; i++) TEST_ASSERT_EQUAL_UINT8(PLAUS_STUCK, plaus_check(&cfg, &st, 2000)); /* saturates */
    TEST_ASSERT_EQUAL_UINT8(0, plaus_check(&cfg, &st, 2001)); /* clears on change */
    for (int i = 2; i <= 49; i++) TEST_ASSERT_EQUAL_UINT8(0, plaus_check(&cfg, &st, 2001));
    TEST_ASSERT_EQUAL_UINT8(PLAUS_STUCK, plaus_check(&cfg, &st, 2001));
}

static void test_roc_thresholds(void)
{
    plaus_state_t st = {0};
    TEST_ASSERT_EQUAL_UINT8(0, plaus_check(&cfg, &st, 1000));      /* first sample: no ROC */
    TEST_ASSERT_EQUAL_UINT8(0, plaus_check(&cfg, &st, 1500));      /* +500 == max: ok */
    TEST_ASSERT_EQUAL_UINT8(PLAUS_ROC, plaus_check(&cfg, &st, 2001)); /* +501 */
    TEST_ASSERT_EQUAL_UINT8(0, plaus_check(&cfg, &st, 1501));      /* -500 */
    TEST_ASSERT_EQUAL_UINT8(PLAUS_ROC, plaus_check(&cfg, &st, 1000)); /* -501 */
    TEST_ASSERT_EQUAL_UINT8(0, plaus_check(&cfg, &st, 1000));      /* clears */
    /* extremes do not overflow */
    const plaus_cfg_t wide = {.min = 0, .max = 0xFFFF, .roc_max = 0xFFFE, .stuck_n = 0xFFFF};
    st = (plaus_state_t){0};
    plaus_check(&wide, &st, 0);
    TEST_ASSERT_EQUAL_UINT8(PLAUS_ROC, plaus_check(&wide, &st, 0xFFFF));
    TEST_ASSERT_EQUAL_UINT8(PLAUS_ROC, plaus_check(&wide, &st, 0));
}

static void test_combined_flags(void)
{
    plaus_state_t st = {0};
    plaus_check(&cfg, &st, 3800);
    TEST_ASSERT_EQUAL_UINT8(PLAUS_RANGE | PLAUS_ROC, plaus_check(&cfg, &st, 50));
    const plaus_cfg_t c2 = {.min = 100, .max = 200, .roc_max = 10, .stuck_n = 2};
    st = (plaus_state_t){0};
    TEST_ASSERT_EQUAL_UINT8(PLAUS_RANGE, plaus_check(&c2, &st, 5));
    TEST_ASSERT_EQUAL_UINT8(PLAUS_RANGE | PLAUS_STUCK, plaus_check(&c2, &st, 5));
}

static void test_status_packing(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x0001, PLAUS_STATUS(0, PLAUS_RANGE));
    TEST_ASSERT_EQUAL_HEX16(0x0020, PLAUS_STATUS(1, PLAUS_STUCK));
    TEST_ASSERT_EQUAL_HEX16(0x0400, PLAUS_STATUS(2, PLAUS_ROC));
    TEST_ASSERT_EQUAL_HEX16(0x7000, PLAUS_STATUS(3, PLAUS_RANGE | PLAUS_STUCK | PLAUS_ROC));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_range_inclusive_boundaries);
    RUN_TEST(test_stuck_at_n_minus_1_and_n);
    RUN_TEST(test_roc_thresholds);
    RUN_TEST(test_combined_flags);
    RUN_TEST(test_status_packing);
    return UNITY_END();
}
