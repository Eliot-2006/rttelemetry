/* UT-RING (PRD §19.2) */
#include <string.h>

#include "ring.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static ring_t r;
static uint8_t storage[64 * 20];

static void push_u32(uint32_t v) { TEST_ASSERT_TRUE(ring_push(&r, &v)); }
static uint32_t pop_u32(void)
{
    uint32_t v = 0;
    TEST_ASSERT_TRUE(ring_pop(&r, &v));
    return v;
}

static void test_init_rejects_invalid(void)
{
    TEST_ASSERT_FALSE(ring_init(&r, storage, 0, 4));
    TEST_ASSERT_FALSE(ring_init(&r, storage, 3, 4));
    TEST_ASSERT_FALSE(ring_init(&r, storage, 12, 4));
    TEST_ASSERT_FALSE(ring_init(&r, storage, 0x80000001u, 4));
    TEST_ASSERT_FALSE(ring_init(&r, storage, 0xFFFFFFFFu, 4));
    TEST_ASSERT_FALSE(ring_init(&r, storage, 4, 0));
    TEST_ASSERT_FALSE(ring_init(&r, NULL, 4, 4));
    TEST_ASSERT_TRUE(ring_init(&r, storage, 0x80000000u, 1)); /* 2^31 allowed (buffer not touched) */
}

static void test_init_state(void)
{
    memset(&r, 0xA5, sizeof r);
    TEST_ASSERT_TRUE(ring_init(&r, storage, 8, 4));
    TEST_ASSERT_EQUAL_UINT32(0, ring_count(&r));
    TEST_ASSERT_EQUAL_UINT32(0, atomic_load(&r.drops));
    TEST_ASSERT_EQUAL_UINT32(0, atomic_load(&r.hwm));
    TEST_ASSERT_EQUAL_UINT32(0, atomic_load(&r.head));
    TEST_ASSERT_EQUAL_UINT32(0, atomic_load(&r.tail));
}

static void test_push_pop_single(void)
{
    ring_init(&r, storage, 8, 4);
    push_u32(0xDEADBEEF);
    TEST_ASSERT_EQUAL_UINT32(1, ring_count(&r));
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, pop_u32());
    TEST_ASSERT_EQUAL_UINT32(0, ring_count(&r));
}

static void test_pop_empty(void)
{
    uint32_t v = 7;
    ring_init(&r, storage, 8, 4);
    TEST_ASSERT_FALSE(ring_pop(&r, &v));
    TEST_ASSERT_EQUAL_UINT32(7, v); /* untouched */
}

static void test_fill_exactly_capacity_then_full(void)
{
    ring_init(&r, storage, 8, 4);
    for (uint32_t i = 0; i < 8; i++) push_u32(i);
    TEST_ASSERT_EQUAL_UINT32(8, ring_count(&r));
    uint32_t v = 99;
    TEST_ASSERT_FALSE(ring_push(&r, &v));
    TEST_ASSERT_EQUAL_UINT32(1, atomic_load(&r.drops));
    TEST_ASSERT_FALSE(ring_push(&r, &v));
    TEST_ASSERT_EQUAL_UINT32(2, atomic_load(&r.drops));
    for (uint32_t i = 0; i < 8; i++) TEST_ASSERT_EQUAL_UINT32(i, pop_u32()); /* newest dropped */
    TEST_ASSERT_FALSE(ring_pop(&r, &v));
}

static void test_fifo_across_wraparound(void)
{
    ring_init(&r, storage, 8, 4);
    uint32_t next_in = 0, next_out = 0;
    while (next_in < 3 * 8 + 5) { /* > 3 full laps, varying occupancy */
        for (int k = 0; k < 5; k++) push_u32(next_in++);
        for (int k = 0; k < 3; k++) TEST_ASSERT_EQUAL_UINT32(next_out++, pop_u32());
        while (ring_count(&r) > 3) TEST_ASSERT_EQUAL_UINT32(next_out++, pop_u32());
    }
    while (ring_count(&r)) TEST_ASSERT_EQUAL_UINT32(next_out++, pop_u32());
    TEST_ASSERT_EQUAL_UINT32(next_in, next_out);
    TEST_ASSERT_EQUAL_UINT32(0, atomic_load(&r.drops));
}

static void test_index_wrap_at_2_32(void)
{
    ring_init(&r, storage, 4, 4);
    atomic_store(&r.head, UINT32_MAX - 2);
    atomic_store(&r.tail, UINT32_MAX - 2);
    for (uint32_t i = 0; i < 4; i++) push_u32(100 + i); /* head crosses 2^32 */
    TEST_ASSERT_EQUAL_UINT32(4, ring_count(&r));
    uint32_t v = 0;
    TEST_ASSERT_FALSE(ring_push(&r, &v));
    TEST_ASSERT_EQUAL_UINT32(1, atomic_load(&r.drops));
    for (uint32_t i = 0; i < 4; i++) TEST_ASSERT_EQUAL_UINT32(100 + i, pop_u32());
    TEST_ASSERT_EQUAL_UINT32(0, ring_count(&r));
    TEST_ASSERT_EQUAL_UINT32(1, atomic_load(&r.head));
    for (uint32_t i = 0; i < 10; i++) {
        push_u32(i);
        TEST_ASSERT_EQUAL_UINT32(i, pop_u32());
    }
}

static void test_hwm(void)
{
    ring_init(&r, storage, 16, 4);
    push_u32(1);
    push_u32(2);
    push_u32(3);
    TEST_ASSERT_EQUAL_UINT32(3, atomic_load(&r.hwm));
    pop_u32();
    pop_u32();
    push_u32(4);
    TEST_ASSERT_EQUAL_UINT32(3, atomic_load(&r.hwm)); /* occupancy 2, hwm stays */
    for (uint32_t i = 0; i < 14; i++) push_u32(i);
    TEST_ASSERT_EQUAL_UINT32(16, atomic_load(&r.hwm));
    uint32_t v = 0;
    TEST_ASSERT_FALSE(ring_push(&r, &v));
    TEST_ASSERT_EQUAL_UINT32(16, atomic_load(&r.hwm));
}

static void test_elem_size_1_and_20(void)
{
    ring_init(&r, storage, 4, 1);
    for (uint8_t i = 0; i < 4; i++) TEST_ASSERT_TRUE(ring_push(&r, &i));
    for (uint8_t i = 0; i < 4; i++) {
        uint8_t b = 0xFF;
        TEST_ASSERT_TRUE(ring_pop(&r, &b));
        TEST_ASSERT_EQUAL_UINT8(i, b);
    }

    uint8_t in[20], out[20];
    ring_init(&r, storage, 32, 20);
    for (uint8_t n = 0; n < 40; n++) { /* wraps a 32-slot ring of 20-byte records */
        for (uint8_t k = 0; k < 20; k++) in[k] = (uint8_t)(n * 20 + k);
        TEST_ASSERT_TRUE(ring_push(&r, in));
        TEST_ASSERT_TRUE(ring_pop(&r, out));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(in, out, 20);
    }
}

static void test_capacity_1(void)
{
    ring_init(&r, storage, 1, 4);
    push_u32(5);
    uint32_t v = 6;
    TEST_ASSERT_FALSE(ring_push(&r, &v));
    TEST_ASSERT_EQUAL_UINT32(1, atomic_load(&r.drops));
    TEST_ASSERT_EQUAL_UINT32(5, pop_u32());
    TEST_ASSERT_FALSE(ring_pop(&r, &v));
    push_u32(7);
    TEST_ASSERT_EQUAL_UINT32(7, pop_u32());
    TEST_ASSERT_EQUAL_UINT32(1, atomic_load(&r.hwm));
}

static void test_capacity_2(void)
{
    ring_init(&r, storage, 2, 4);
    for (uint32_t lap = 0; lap < 5; lap++) {
        push_u32(lap * 2);
        push_u32(lap * 2 + 1);
        uint32_t v = 0;
        TEST_ASSERT_FALSE(ring_push(&r, &v));
        TEST_ASSERT_EQUAL_UINT32(lap * 2, pop_u32());
        TEST_ASSERT_EQUAL_UINT32(lap * 2 + 1, pop_u32());
    }
    TEST_ASSERT_EQUAL_UINT32(5, atomic_load(&r.drops));
    TEST_ASSERT_EQUAL_UINT32(2, atomic_load(&r.hwm));
}

static void test_count_invariant_pushed_eq_popped_plus_count(void)
{
    uint32_t pushed = 0, popped = 0, v = 0;
    ring_init(&r, storage, 8, 4);
    for (uint32_t i = 0; i < 1000; i++) {
        if ((i * 7u) % 3u) pushed += ring_push(&r, &i);
        else popped += ring_pop(&r, &v);
        TEST_ASSERT_TRUE(ring_count(&r) <= 8);
        TEST_ASSERT_EQUAL_UINT32(pushed, popped + ring_count(&r)); /* I-R3 */
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_rejects_invalid);
    RUN_TEST(test_init_state);
    RUN_TEST(test_push_pop_single);
    RUN_TEST(test_pop_empty);
    RUN_TEST(test_fill_exactly_capacity_then_full);
    RUN_TEST(test_fifo_across_wraparound);
    RUN_TEST(test_index_wrap_at_2_32);
    RUN_TEST(test_hwm);
    RUN_TEST(test_elem_size_1_and_20);
    RUN_TEST(test_capacity_1);
    RUN_TEST(test_capacity_2);
    RUN_TEST(test_count_invariant_pushed_eq_popped_plus_count);
    return UNITY_END();
}
