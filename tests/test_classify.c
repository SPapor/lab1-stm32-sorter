/* Тести класифікації посилки, FR-02 (firmware/App/classify.c). */

#include "unity.h"
#include "classify.h"

void setUp(void)
{
}

void tearDown(void)
{
}

/* PRD, FR-02: три рівні освітлення class-LDR (коди моделі Wokwi) */
static void test_three_lux_levels(void)
{
    TEST_ASSERT_EQUAL_INT(PARCEL_A, classify(1001));        /* 500 лк */
    TEST_ASSERT_EQUAL_INT(PARCEL_UNKNOWN, classify(2045));  /* 100 лк */
    TEST_ASSERT_EQUAL_INT(PARCEL_B, classify(3412));        /* 10 лк */
}

static void test_boundaries(void)
{
    TEST_ASSERT_EQUAL_INT(PARCEL_A, classify(CLASS_A_MAX_RAW));
    TEST_ASSERT_EQUAL_INT(PARCEL_UNKNOWN, classify(CLASS_A_MAX_RAW + 1u));
    TEST_ASSERT_EQUAL_INT(PARCEL_UNKNOWN, classify(CLASS_B_MIN_RAW - 1u));
    TEST_ASSERT_EQUAL_INT(PARCEL_B, classify(CLASS_B_MIN_RAW));
}

static void test_model_extremes(void)
{
    TEST_ASSERT_EQUAL_INT(PARCEL_A, classify(32));     /* 100 000 лк */
    TEST_ASSERT_EQUAL_INT(PARCEL_B, classify(4063));   /* 0,1 лк */
}

/* Обрив або замикання датчика — не клас, а брак */
static void test_rails_are_unknown(void)
{
    TEST_ASSERT_EQUAL_INT(PARCEL_UNKNOWN, classify(0));
    TEST_ASSERT_EQUAL_INT(PARCEL_UNKNOWN, classify(CLASS_FULL_RAW));
    TEST_ASSERT_EQUAL_INT(PARCEL_UNKNOWN, classify(0xFFFF));
}

static void test_names(void)
{
    TEST_ASSERT_EQUAL_STRING("A", classify_name(PARCEL_A));
    TEST_ASSERT_EQUAL_STRING("B", classify_name(PARCEL_B));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", classify_name(PARCEL_UNKNOWN));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_three_lux_levels);
    RUN_TEST(test_boundaries);
    RUN_TEST(test_model_extremes);
    RUN_TEST(test_rails_are_unknown);
    RUN_TEST(test_names);
    return UNITY_END();
}
