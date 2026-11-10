/* Тести сортувальника: маршрут, лічильники, стани (firmware/App/sorter.c). */

#include <string.h>
#include "unity.h"
#include "sorter.h"

static sorter_t s;
static sorter_decision_t d;

void setUp(void)
{
    sorter_init(&s, 0);
    memset(&d, 0xA5, sizeof d);
}

void tearDown(void)
{
}

/* Повна посилка: PARCEL_IN з класом, потім PARCEL_OUT. */
static int parcel(parcel_class_t cls)
{
    int accepted = sorter_parcel_in(&s, cls, &d);

    sorter_parcel_out(&s);
    return accepted;
}

static void test_init_ready_or_fault(void)
{
    TEST_ASSERT_EQUAL_INT(SORTER_READY, s.state);
    TEST_ASSERT_EQUAL_UINT32(0, sorter_total(&s));
    TEST_ASSERT_EQUAL_UINT8(0, s.impact);

    sorter_init(&s, 1);
    TEST_ASSERT_EQUAL_INT(SORTER_FAULT, s.state);
}

/* FR-05: брак — удар або UNKNOWN */
static void test_route_table(void)
{
    TEST_ASSERT_EQUAL_INT(BIN_A, sorter_route(PARCEL_A, 0));
    TEST_ASSERT_EQUAL_INT(BIN_B, sorter_route(PARCEL_B, 0));
    TEST_ASSERT_EQUAL_INT(BIN_REJECT, sorter_route(PARCEL_UNKNOWN, 0));
    TEST_ASSERT_EQUAL_INT(BIN_REJECT, sorter_route(PARCEL_A, 1));
    TEST_ASSERT_EQUAL_INT(BIN_REJECT, sorter_route(PARCEL_B, 1));
    TEST_ASSERT_EQUAL_INT(BIN_REJECT, sorter_route(PARCEL_UNKNOWN, 1));
}

/* FR-03: A → 0°, B → 90°, брак → 180° */
static void test_bin_angles(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, sorter_bin_angle(BIN_A));
    TEST_ASSERT_EQUAL_UINT8(90, sorter_bin_angle(BIN_B));
    TEST_ASSERT_EQUAL_UINT8(180, sorter_bin_angle(BIN_REJECT));
}

static void test_decision_and_states(void)
{
    TEST_ASSERT_EQUAL_INT(1, sorter_parcel_in(&s, PARCEL_B, &d));
    TEST_ASSERT_EQUAL_INT(BIN_B, d.bin);
    TEST_ASSERT_EQUAL_UINT8(90, d.angle);
    TEST_ASSERT_EQUAL_UINT8(0, d.impact);
    TEST_ASSERT_EQUAL_UINT32(1, d.n);
    TEST_ASSERT_EQUAL_INT(SORTER_SORTING, s.state);

    /* Поки посилка перед gate, другий PARCEL_IN не приймається */
    TEST_ASSERT_EQUAL_INT(0, sorter_parcel_in(&s, PARCEL_A, &d));
    TEST_ASSERT_EQUAL_UINT32(1, sorter_total(&s));

    sorter_parcel_out(&s);
    TEST_ASSERT_EQUAL_INT(SORTER_READY, s.state);
    sorter_parcel_out(&s);   /* повторний OUT нічого не змінює */
    TEST_ASSERT_EQUAL_INT(SORTER_READY, s.state);
}

/* FR-04 + FR-05: удар до PARCEL_IN — брак, прапорець діє на одну посилку */
static void test_impact_rejects_next_parcel_only(void)
{
    TEST_ASSERT_EQUAL_INT(1, sorter_impact(&s));
    TEST_ASSERT_EQUAL_INT(0, sorter_impact(&s));   /* вже встановлено: без повторного IMPACT */

    TEST_ASSERT_EQUAL_INT(1, parcel(PARCEL_A));
    TEST_ASSERT_EQUAL_INT(BIN_REJECT, d.bin);
    TEST_ASSERT_EQUAL_UINT8(1, d.impact);
    TEST_ASSERT_EQUAL_UINT8(180, d.angle);

    TEST_ASSERT_EQUAL_INT(1, parcel(PARCEL_A));
    TEST_ASSERT_EQUAL_INT(BIN_A, d.bin);
    TEST_ASSERT_EQUAL_UINT8(0, d.impact);
}

/* Удар, поки попередня посилка ще перед gate, належить наступній */
static void test_impact_during_sorting_goes_to_next(void)
{
    TEST_ASSERT_EQUAL_INT(1, sorter_parcel_in(&s, PARCEL_A, &d));
    TEST_ASSERT_EQUAL_INT(BIN_A, d.bin);
    TEST_ASSERT_EQUAL_INT(1, sorter_impact(&s));
    sorter_parcel_out(&s);

    TEST_ASSERT_EQUAL_INT(1, parcel(PARCEL_B));
    TEST_ASSERT_EQUAL_INT(BIN_REJECT, d.bin);
}

/* PRD, FR-06: 10 посилок → сума лічильників рівно 10 */
static void test_ten_parcels_counted(void)
{
    static const parcel_class_t cls[10] = {
        PARCEL_A, PARCEL_B, PARCEL_A, PARCEL_UNKNOWN, PARCEL_B,
        PARCEL_A, PARCEL_A, PARCEL_UNKNOWN, PARCEL_B, PARCEL_A,
    };

    for (unsigned i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL_INT(1, parcel(cls[i]));
        TEST_ASSERT_EQUAL_UINT32(i + 1u, d.n);
    }
    TEST_ASSERT_EQUAL_UINT32(5, s.count[BIN_A]);
    TEST_ASSERT_EQUAL_UINT32(3, s.count[BIN_B]);
    TEST_ASSERT_EQUAL_UINT32(2, s.count[BIN_REJECT]);
    TEST_ASSERT_EQUAL_UINT32(10, sorter_total(&s));
}

static void test_reset_counters_keeps_state_and_impact(void)
{
    parcel(PARCEL_A);
    sorter_impact(&s);
    sorter_estop_toggle(&s);

    sorter_reset_counters(&s);
    TEST_ASSERT_EQUAL_UINT32(0, sorter_total(&s));
    TEST_ASSERT_EQUAL_INT(SORTER_ESTOP, s.state);
    TEST_ASSERT_EQUAL_UINT8(1, s.impact);
}

/* PRD, FR-07: E-STOP на ходу — нові входи ігноруються; повторне натискання — READY */
static void test_estop_blocks_parcels(void)
{
    TEST_ASSERT_EQUAL_INT(SORTER_ESTOP, sorter_estop_toggle(&s));
    TEST_ASSERT_EQUAL_INT(0, parcel(PARCEL_A));
    TEST_ASSERT_EQUAL_UINT32(0, sorter_total(&s));
    TEST_ASSERT_EQUAL_INT(SORTER_ESTOP, s.state);

    TEST_ASSERT_EQUAL_INT(SORTER_READY, sorter_estop_toggle(&s));
    TEST_ASSERT_EQUAL_INT(1, parcel(PARCEL_A));
    TEST_ASSERT_EQUAL_UINT32(1, sorter_total(&s));
}

/* E-STOP, поки посилка перед gate: вихід — у READY, а не назад у SORTING */
static void test_estop_during_sorting(void)
{
    sorter_parcel_in(&s, PARCEL_B, &d);
    TEST_ASSERT_EQUAL_INT(SORTER_ESTOP, sorter_estop_toggle(&s));
    sorter_parcel_out(&s);
    TEST_ASSERT_EQUAL_INT(SORTER_ESTOP, s.state);
    TEST_ASSERT_EQUAL_INT(SORTER_READY, sorter_estop_toggle(&s));
}

/* В E-STOP конвеєр стоїть: удар не запам'ятовується; набутий раніше — лишається */
static void test_impact_in_estop(void)
{
    sorter_estop_toggle(&s);
    TEST_ASSERT_EQUAL_INT(0, sorter_impact(&s));
    TEST_ASSERT_EQUAL_UINT8(0, s.impact);
    sorter_estop_toggle(&s);

    sorter_impact(&s);
    sorter_estop_toggle(&s);
    sorter_estop_toggle(&s);
    TEST_ASSERT_EQUAL_INT(1, parcel(PARCEL_A));
    TEST_ASSERT_EQUAL_INT(BIN_REJECT, d.bin);
}

/* FR-08: FAULT не виходить ні з E-STOP, ні з посилкою, ні з ударом */
static void test_fault_is_sticky(void)
{
    sorter_init(&s, 1);
    TEST_ASSERT_EQUAL_INT(SORTER_FAULT, sorter_estop_toggle(&s));
    TEST_ASSERT_EQUAL_INT(0, parcel(PARCEL_A));
    TEST_ASSERT_EQUAL_INT(0, sorter_impact(&s));
    TEST_ASSERT_EQUAL_INT(SORTER_FAULT, s.state);
    TEST_ASSERT_EQUAL_UINT32(0, sorter_total(&s));
}

static void test_names(void)
{
    TEST_ASSERT_EQUAL_STRING("READY", sorter_state_name(SORTER_READY));
    TEST_ASSERT_EQUAL_STRING("SORTING", sorter_state_name(SORTER_SORTING));
    TEST_ASSERT_EQUAL_STRING("ESTOP", sorter_state_name(SORTER_ESTOP));
    TEST_ASSERT_EQUAL_STRING("FAULT", sorter_state_name(SORTER_FAULT));
    TEST_ASSERT_EQUAL_STRING("A", sorter_bin_name(BIN_A));
    TEST_ASSERT_EQUAL_STRING("B", sorter_bin_name(BIN_B));
    TEST_ASSERT_EQUAL_STRING("REJECT", sorter_bin_name(BIN_REJECT));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_ready_or_fault);
    RUN_TEST(test_route_table);
    RUN_TEST(test_bin_angles);
    RUN_TEST(test_decision_and_states);
    RUN_TEST(test_impact_rejects_next_parcel_only);
    RUN_TEST(test_impact_during_sorting_goes_to_next);
    RUN_TEST(test_ten_parcels_counted);
    RUN_TEST(test_reset_counters_keeps_state_and_impact);
    RUN_TEST(test_estop_blocks_parcels);
    RUN_TEST(test_estop_during_sorting);
    RUN_TEST(test_impact_in_estop);
    RUN_TEST(test_fault_is_sticky);
    RUN_TEST(test_names);
    return UNITY_END();
}
