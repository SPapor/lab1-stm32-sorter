/* Тести арифметики вимірювань (firmware/App/perf.c). */

#include <string.h>
#include "unity.h"
#include "perf.h"

#define CPT  48000u          /* тактів на тік 1 мс при 48 МГц */
#define CPU  48u             /* тактів на мкс */

void setUp(void)
{
}

void tearDown(void)
{
}

/* Значення лічильника так, як його складає perf_now() */
static uint32_t cyc_at(uint32_t tick_ms, uint32_t in_tick)
{
    return tick_ms * CPT + in_tick;
}

static void test_elapsed_plain(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, perf_elapsed(1000, 1000));
    TEST_ASSERT_EQUAL_UINT32(4800, perf_elapsed(cyc_at(5, 100), cyc_at(5, 4900)));
    TEST_ASSERT_EQUAL_UINT32(CPT + 10u, perf_elapsed(cyc_at(7, 47995), cyc_at(9, 5)));
}

/* Переповнення 32 біт: 2^32 / 48000 = 89478,48 мс */
static void test_elapsed_across_32bit_wrap(void)
{
    uint32_t before = cyc_at(89478, 0);          /* 4 294 944 000 */
    uint32_t after = cyc_at(89479, 0);           /* вже за 2^32: 26 704 */

    TEST_ASSERT_TRUE(after < before);
    TEST_ASSERT_EQUAL_UINT32(CPT, perf_elapsed(before, after));
    TEST_ASSERT_EQUAL_UINT32(0x20u, perf_elapsed(0xFFFFFFF0u, 0x10u));
    TEST_ASSERT_EQUAL_UINT32(5u * CPT, perf_elapsed(cyc_at(89476, 123), cyc_at(89481, 123)));
}

/* Інтервал ≥ 2^32 тактів (≈ 89,5 с) «загортається»: 2^32 + 16 виглядає як 16 */
static void test_elapsed_ambiguous_after_89s(void)
{
    uint32_t start = cyc_at(1000, 0);
    uint32_t end = (uint32_t)((uint64_t)start + 0x100000010ull);

    TEST_ASSERT_EQUAL_UINT32(16u, perf_elapsed(start, end));
}

static void test_cyc_to_us_rounding(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, perf_cyc_to_us(0, CPU));
    TEST_ASSERT_EQUAL_UINT32(0, perf_cyc_to_us(23, CPU));
    TEST_ASSERT_EQUAL_UINT32(1, perf_cyc_to_us(24, CPU));
    TEST_ASSERT_EQUAL_UINT32(1, perf_cyc_to_us(48, CPU));
    TEST_ASSERT_EQUAL_UINT32(1000, perf_cyc_to_us(CPT, CPU));
    TEST_ASSERT_EQUAL_UINT32(31500, perf_cyc_to_us(31500u * CPU, CPU));
}

/* Біля 2^32 округлення не переповнюється */
static void test_cyc_to_us_near_max(void)
{
    TEST_ASSERT_EQUAL_UINT32(89478485u, perf_cyc_to_us(0xFFFFFFFFu, CPU));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, perf_cyc_to_us(0xFFFFFFFFu, 1));
    TEST_ASSERT_EQUAL_UINT32(0x80000000u, perf_cyc_to_us(0xFFFFFFFFu, 2));
}

static void test_add_min_max_sum(void)
{
    perf_stat_t s = { .name = "imu" };

    TEST_ASSERT_EQUAL_UINT32(0, perf_avg(&s));
    perf_add(&s, 300);
    perf_add(&s, 100);
    perf_add(&s, 200);
    TEST_ASSERT_EQUAL_UINT32(3, s.count);
    TEST_ASSERT_EQUAL_UINT32(100, s.min);
    TEST_ASSERT_EQUAL_UINT32(300, s.max);
    TEST_ASSERT_EQUAL_UINT64(600, s.sum);
    TEST_ASSERT_EQUAL_UINT32(200, perf_avg(&s));
    perf_add(&s, 0);                             /* нуль — теж мінімум */
    TEST_ASSERT_EQUAL_UINT32(0, s.min);
    TEST_ASSERT_EQUAL_UINT32(150, perf_avg(&s));
}

static void test_avg_rounds(void)
{
    perf_stat_t s = { .name = "x" };

    perf_add(&s, 1);
    perf_add(&s, 2);
    TEST_ASSERT_EQUAL_UINT32(2, perf_avg(&s));   /* 1,5 → 2 */
    perf_add(&s, 2);
    TEST_ASSERT_EQUAL_UINT32(2, perf_avg(&s));   /* 1,67 → 2 */
}

/* Сума понад 2^32 тактів: годину роботи циклу середнє рахується правильно */
static void test_sum_beyond_32bit(void)
{
    perf_stat_t s = { .name = "loop" };

    for (int i = 0; i < 100; i++)
        perf_add(&s, 2000000000u);
    TEST_ASSERT_TRUE(s.sum > 0xFFFFFFFFull);
    TEST_ASSERT_EQUAL_UINT32(2000000000u, perf_avg(&s));
    TEST_ASSERT_EQUAL_UINT32(2000000000u, s.max);
}

static void test_reset_keeps_name(void)
{
    perf_stat_t s = { .name = "gate" };

    perf_add(&s, 5);
    perf_stat_reset(&s);
    TEST_ASSERT_EQUAL_UINT32(0, s.count);
    TEST_ASSERT_EQUAL_UINT64(0, s.sum);
    TEST_ASSERT_EQUAL_STRING("gate", s.name);
    perf_add(&s, 7);
    TEST_ASSERT_EQUAL_UINT32(7, s.min);
}

static void test_udiv64(void)
{
    uint32_t rem;

    TEST_ASSERT_EQUAL_UINT32(0, perf_udiv64(0, 7, &rem));
    TEST_ASSERT_EQUAL_UINT32(0, rem);
    TEST_ASSERT_EQUAL_UINT32(14, perf_udiv64(100, 7, &rem));
    TEST_ASSERT_EQUAL_UINT32(2, rem);
    /* мітка EVT: 89 478 485 123 456 мкс → частини по 10^9 */
    TEST_ASSERT_EQUAL_UINT32(89478u, perf_udiv64(89478485123456ull, 1000000000u, &rem));
    TEST_ASSERT_EQUAL_UINT32(485123456u, rem);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, perf_udiv64(0x1FFFFFFFEull, 2u, NULL));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, perf_udiv64(0x200000000ull, 2u, NULL)); /* частка > 32 біт */
    TEST_ASSERT_EQUAL_UINT32(1u, perf_udiv64(0xFFFFFFFFull, 0xFFFFFFFFu, &rem));
    TEST_ASSERT_EQUAL_UINT32(0, rem);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_elapsed_plain);
    RUN_TEST(test_elapsed_across_32bit_wrap);
    RUN_TEST(test_elapsed_ambiguous_after_89s);
    RUN_TEST(test_cyc_to_us_rounding);
    RUN_TEST(test_cyc_to_us_near_max);
    RUN_TEST(test_add_min_max_sum);
    RUN_TEST(test_avg_rounds);
    RUN_TEST(test_sum_beyond_32bit);
    RUN_TEST(test_reset_keeps_name);
    RUN_TEST(test_udiv64);
    return UNITY_END();
}
