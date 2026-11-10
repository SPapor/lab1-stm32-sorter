/* Тести детектора удару, FR-04 (firmware/App/impact.c). */

#include "unity.h"
#include "impact.h"

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_rest_is_not_impact(void)
{
    uint32_t m2 = impact_mag2(0, 0, 1000);   /* 1 g по Z */

    TEST_ASSERT_EQUAL_UINT32(1000000u, m2);
    TEST_ASSERT_FALSE(impact_over(m2));
}

/* Поріг строгий: рівно 2 g — ще не удар */
static void test_threshold_is_strict(void)
{
    TEST_ASSERT_FALSE(impact_over(impact_mag2(2000, 0, 0)));
    TEST_ASSERT_TRUE(impact_over(impact_mag2(2001, 0, 0)));
    TEST_ASSERT_TRUE(impact_over(impact_mag2(0, 0, -2001)));
    TEST_ASSERT_FALSE(impact_over(4000000u));
    TEST_ASSERT_TRUE(impact_over(4000001u));
}

/* ±2 g: вісь насичується на 1999 mg, удар — лише сумою осей */
static void test_saturated_axes_sum(void)
{
    TEST_ASSERT_FALSE(impact_over(impact_mag2(1999, 0, 0)));
    TEST_ASSERT_TRUE(impact_over(impact_mag2(1999, 0, 1000)));       /* ≈ 2,24 g */
    TEST_ASSERT_TRUE(impact_over(impact_mag2(-1500, 1500, 0)));      /* ≈ 2,12 g */
    TEST_ASSERT_FALSE(impact_over(impact_mag2(1000, 1000, 1000)));   /* ≈ 1,73 g */
}

/* Обмеження осі: без нього сума квадратів переповнила б uint32_t */
static void test_axis_clamped_no_overflow(void)
{
    uint32_t max = 3u * 32767u * 32767u;

    TEST_ASSERT_EQUAL_UINT32(max, impact_mag2(100000, -100000, 40000));
    TEST_ASSERT_EQUAL_UINT32(max, impact_mag2(INT32_MAX, INT32_MIN + 1, 32767));
    TEST_ASSERT_TRUE(impact_over(max));
}

static void test_isqrt(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, impact_isqrt(0));
    TEST_ASSERT_EQUAL_UINT32(1, impact_isqrt(1));
    TEST_ASSERT_EQUAL_UINT32(1, impact_isqrt(3));
    TEST_ASSERT_EQUAL_UINT32(2, impact_isqrt(4));
    TEST_ASSERT_EQUAL_UINT32(1000, impact_isqrt(1000000u));
    TEST_ASSERT_EQUAL_UINT32(1999, impact_isqrt(3999999u));
    TEST_ASSERT_EQUAL_UINT32(2000, impact_isqrt(4000000u));
    TEST_ASSERT_EQUAL_UINT32(2235, impact_isqrt(impact_mag2(1999, 0, 1000)));
    TEST_ASSERT_EQUAL_UINT32(65535, impact_isqrt(UINT32_MAX));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rest_is_not_impact);
    RUN_TEST(test_threshold_is_strict);
    RUN_TEST(test_saturated_axes_sum);
    RUN_TEST(test_axis_clamped_no_overflow);
    RUN_TEST(test_isqrt);
    return UNITY_END();
}
