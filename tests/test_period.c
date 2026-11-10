/* Тести перевірки періоду задач super loop (firmware/App/period.c). */

#include "unity.h"
#include "period.h"

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_not_due_before_period(void)
{
    uint32_t last = 100;

    TEST_ASSERT_EQUAL_INT(0, period_due(&last, 100, 5));
    TEST_ASSERT_EQUAL_INT(0, period_due(&last, 104, 5));
    TEST_ASSERT_EQUAL_UINT32(100, last);
}

static void test_due_exactly_at_period(void)
{
    uint32_t last = 100;

    TEST_ASSERT_EQUAL_INT(1, period_due(&last, 105, 5));
    TEST_ASSERT_EQUAL_UINT32(105, last);
    TEST_ASSERT_EQUAL_INT(0, period_due(&last, 109, 5));
    TEST_ASSERT_EQUAL_INT(1, period_due(&last, 110, 5));
}

/* Після блокування на 600 мс — один запуск, а не 120 запусків пачкою */
static void test_no_catch_up_after_long_block(void)
{
    uint32_t last = 0;

    TEST_ASSERT_EQUAL_INT(1, period_due(&last, 600, 5));
    TEST_ASSERT_EQUAL_UINT32(600, last);
    TEST_ASSERT_EQUAL_INT(0, period_due(&last, 601, 5));
}

/* HAL_GetTick переповнюється через ~49,7 доби */
static void test_tick_wraparound(void)
{
    uint32_t last = 0xFFFFFFFEu;

    TEST_ASSERT_EQUAL_INT(0, period_due(&last, 0xFFFFFFFFu, 5));
    TEST_ASSERT_EQUAL_INT(0, period_due(&last, 2u, 5));       /* минуло 4 мс */
    TEST_ASSERT_EQUAL_INT(1, period_due(&last, 3u, 5));       /* минуло 5 мс */
    TEST_ASSERT_EQUAL_UINT32(3u, last);
}

static void test_zero_period_always_due(void)
{
    uint32_t last = 7;

    TEST_ASSERT_EQUAL_INT(1, period_due(&last, 7, 0));
    TEST_ASSERT_EQUAL_INT(1, period_due(&last, 7, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_not_due_before_period);
    RUN_TEST(test_due_exactly_at_period);
    RUN_TEST(test_no_catch_up_after_long_block);
    RUN_TEST(test_tick_wraparound);
    RUN_TEST(test_zero_period_always_due);
    return UNITY_END();
}
