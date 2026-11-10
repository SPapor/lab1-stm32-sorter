/* Тести оцінки самоперевірки при старті, FR-08 (firmware/App/health.c). */

#include "unity.h"
#include "health.h"

void setUp(void)
{
}

void tearDown(void)
{
}

/* SLEEP не знімається у Wokwi — WARN, а не FAULT; немає MPU або чужий ID — FAIL */
static void test_mpu(void)
{
    TEST_ASSERT_EQUAL_INT(HEALTH_OK, health_mpu(MPU6050_OK));
    TEST_ASSERT_EQUAL_INT(HEALTH_WARN, health_mpu(MPU6050_ERR_SLEEP));
    TEST_ASSERT_EQUAL_INT(HEALTH_FAIL, health_mpu(MPU6050_ERR_BUS));
    TEST_ASSERT_EQUAL_INT(HEALTH_FAIL, health_mpu(MPU6050_ERR_ID));
}

static void test_ldr(void)
{
    TEST_ASSERT_EQUAL_INT(HEALTH_OK, health_ldr(1001, 4095));
    TEST_ASSERT_EQUAL_INT(HEALTH_OK, health_ldr(1, 4095));
    TEST_ASSERT_EQUAL_INT(HEALTH_OK, health_ldr(4094, 4095));
    TEST_ASSERT_EQUAL_INT(HEALTH_WARN, health_ldr(0, 4095));
    TEST_ASSERT_EQUAL_INT(HEALTH_WARN, health_ldr(4095, 4095));
}

static void test_servo(void)
{
    TEST_ASSERT_EQUAL_INT(HEALTH_OK, health_servo(1500, 1000, 2000));
    TEST_ASSERT_EQUAL_INT(HEALTH_OK, health_servo(1000, 1000, 2000));
    TEST_ASSERT_EQUAL_INT(HEALTH_OK, health_servo(2000, 1000, 2000));
    TEST_ASSERT_EQUAL_INT(HEALTH_FAIL, health_servo(999, 1000, 2000));
    TEST_ASSERT_EQUAL_INT(HEALTH_FAIL, health_servo(2001, 1000, 2000));
    TEST_ASSERT_EQUAL_INT(HEALTH_FAIL, health_servo(0, 1000, 2000));
}

static void test_names(void)
{
    TEST_ASSERT_EQUAL_STRING("OK", health_name(HEALTH_OK));
    TEST_ASSERT_EQUAL_STRING("WARN", health_name(HEALTH_WARN));
    TEST_ASSERT_EQUAL_STRING("FAIL", health_name(HEALTH_FAIL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_mpu);
    RUN_TEST(test_ldr);
    RUN_TEST(test_servo);
    RUN_TEST(test_names);
    return UNITY_END();
}
