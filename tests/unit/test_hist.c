/* UT-HIST (PRD §19.2) */
#include <string.h>

#include "hist.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_bucket_boundaries(void)
{
    const uint32_t edges[HIST_BUCKETS] = {500, 1000, 2000, 5000, 10000, 20000, 50000, UINT32_MAX};
    TEST_ASSERT_EQUAL_UINT32_ARRAY(edges, HIST_EDGES_US, HIST_BUCKETS);
    for (int i = 0; i < HIST_BUCKETS; i++) {
        hist_t h;
        memset(&h, 0, sizeof h);
        hist_add(&h, edges[i]); /* upper edge is inclusive */
        TEST_ASSERT_EQUAL_UINT32(1, h.b[i]);
        if (i + 1 < HIST_BUCKETS) {
            hist_add(&h, edges[i] + 1);
            TEST_ASSERT_EQUAL_UINT32(1, h.b[i + 1]);
        }
    }
    hist_t h;
    memset(&h, 0, sizeof h);
    hist_add(&h, 0);
    TEST_ASSERT_EQUAL_UINT32(1, h.b[0]);
    TEST_ASSERT_EQUAL_UINT32(0, h.max);
}

static void test_max(void)
{
    hist_t h;
    memset(&h, 0, sizeof h);
    hist_add(&h, 700);
    hist_add(&h, 30);
    TEST_ASSERT_EQUAL_UINT32(700, h.max);
    hist_add(&h, UINT32_MAX);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, h.max);
}

static void test_saturation(void)
{
    hist_t h;
    memset(&h, 0, sizeof h);
    h.b[2] = UINT32_MAX - 1;
    hist_add(&h, 1500);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, h.b[2]);
    hist_add(&h, 1500);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, h.b[2]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_bucket_boundaries);
    RUN_TEST(test_max);
    RUN_TEST(test_saturation);
    return UNITY_END();
}
