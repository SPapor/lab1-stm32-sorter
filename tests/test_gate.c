/* Тести порогу gate з гістерезисом, FR-01 (firmware/App/gate.c). */

#include <stddef.h>
#include "unity.h"
#include "gate.h"

static gate_t g;

void setUp(void)
{
    gate_init(&g, GATE_ON_RAW, GATE_OFF_RAW);
}

void tearDown(void)
{
}

/* Подає послідовність відліків; повертає кількість подій IN і OUT. */
static void feed(const uint16_t *raw, size_t n, unsigned *in, unsigned *out)
{
    *in = 0;
    *out = 0;
    for (size_t i = 0; i < n; i++) {
        gate_event_t ev = gate_update(&g, raw[i]);
        if (ev == GATE_EV_IN)
            (*in)++;
        else if (ev == GATE_EV_OUT)
            (*out)++;
    }
}

static void test_thresholds_are_a_band(void)
{
    TEST_ASSERT_TRUE(GATE_OFF_RAW < GATE_ON_RAW);
    /* 500 лк (1001) — вільно, 10 лк (3412) — посилка */
    TEST_ASSERT_TRUE(1001u <= GATE_OFF_RAW);
    TEST_ASSERT_TRUE(3412u >= GATE_ON_RAW);
}

static void test_starts_empty(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, g.present);
    TEST_ASSERT_EQUAL_INT(GATE_EV_NONE, gate_update(&g, 1001));
}

static void test_in_at_on_threshold(void)
{
    TEST_ASSERT_EQUAL_INT(GATE_EV_NONE, gate_update(&g, GATE_ON_RAW - 1u));
    TEST_ASSERT_EQUAL_INT(GATE_EV_IN, gate_update(&g, GATE_ON_RAW));
    TEST_ASSERT_EQUAL_UINT8(1, g.present);
}

static void test_out_at_off_threshold(void)
{
    gate_update(&g, 3412);
    TEST_ASSERT_EQUAL_INT(GATE_EV_NONE, gate_update(&g, GATE_OFF_RAW + 1u));
    TEST_ASSERT_EQUAL_INT(GATE_EV_OUT, gate_update(&g, GATE_OFF_RAW));
    TEST_ASSERT_EQUAL_UINT8(0, g.present);
}

/* PRD, FR-01: затінення → рівно один PARCEL_IN, поки посилка стоїть */
static void test_one_event_per_parcel(void)
{
    static const uint16_t raw[] = { 1001, 1001, 3412, 3412, 3412, 3412, 1001, 1001 };
    unsigned in, out;

    feed(raw, sizeof raw / sizeof raw[0], &in, &out);
    TEST_ASSERT_EQUAL_UINT(1, in);
    TEST_ASSERT_EQUAL_UINT(1, out);
}

/* Шум навколо порогу ON у межах гістерезису не дає зайвих подій */
static void test_noise_inside_band_ignored(void)
{
    static const uint16_t raw[] = {
        2390, 2410, 2300, 2450, 1700, 2500, 1650, 2600, 1601, 2399, 3000, 1601, 2399,
    };
    unsigned in, out;

    feed(raw, sizeof raw / sizeof raw[0], &in, &out);
    TEST_ASSERT_EQUAL_UINT(1, in);
    TEST_ASSERT_EQUAL_UINT(0, out);
}

static void test_ten_parcels(void)
{
    unsigned in = 0, out = 0;

    for (int i = 0; i < 10; i++) {
        static const uint16_t raw[] = { 1001, 3412, 3412, 1001 };
        unsigned a, b;
        feed(raw, sizeof raw / sizeof raw[0], &a, &b);
        in += a;
        out += b;
    }
    TEST_ASSERT_EQUAL_UINT(10, in);
    TEST_ASSERT_EQUAL_UINT(10, out);
}

static void test_custom_thresholds(void)
{
    gate_init(&g, 200, 100);
    TEST_ASSERT_EQUAL_INT(GATE_EV_IN, gate_update(&g, 200));
    TEST_ASSERT_EQUAL_INT(GATE_EV_NONE, gate_update(&g, 150));
    TEST_ASSERT_EQUAL_INT(GATE_EV_OUT, gate_update(&g, 100));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_thresholds_are_a_band);
    RUN_TEST(test_starts_empty);
    RUN_TEST(test_in_at_on_threshold);
    RUN_TEST(test_out_at_off_threshold);
    RUN_TEST(test_one_event_per_parcel);
    RUN_TEST(test_noise_inside_band_ignored);
    RUN_TEST(test_ten_parcels);
    RUN_TEST(test_custom_thresholds);
    return UNITY_END();
}
