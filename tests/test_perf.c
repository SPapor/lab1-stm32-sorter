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

/* ---- періодична задача ---------------------------------------------------- */

#define MS(x)  ((uint32_t)(x) * CPT)
#define US(x)  ((uint32_t)(x) * CPU)

static void test_period_first_start_has_no_interval(void)
{
    perf_period_t p;

    perf_period_init(&p, 5000);
    perf_period_start(&p, MS(3), CPU);
    TEST_ASSERT_EQUAL_UINT32(0, p.n);
    TEST_ASSERT_EQUAL_UINT32(0, p.missed);
}

/* Знаковий джитер: інтервал 4,9 мс при періоді 5 мс — -100 мкс */
static void test_period_jitter_min_max(void)
{
    perf_period_t p;

    perf_period_init(&p, 5000);
    perf_period_start(&p, MS(0), CPU);
    perf_period_start(&p, MS(5) + US(300), CPU);    /* +300 */
    perf_period_start(&p, MS(10) + US(200), CPU);   /* -100 */
    perf_period_start(&p, MS(15) + US(200), CPU);   /* 0 */
    TEST_ASSERT_EQUAL_UINT32(3, p.n);
    TEST_ASSERT_EQUAL_INT32(-100, p.jit_min_us);
    TEST_ASSERT_EQUAL_INT32(300, p.jit_max_us);
    TEST_ASSERT_EQUAL_UINT32(0, p.missed);
    TEST_ASSERT_EQUAL_UINT32(1, p.hist[perf_hist_bin(300)]);
    TEST_ASSERT_EQUAL_UINT32(1, p.hist[perf_hist_bin(-100)]);
    TEST_ASSERT_EQUAL_UINT32(1, p.hist[perf_hist_bin(0)]);
}

/* Лише додатний джитер теж дає правильний мінімум (перший інтервал задає обидва) */
static void test_period_positive_only(void)
{
    perf_period_t p;

    perf_period_init(&p, 10000);
    perf_period_start(&p, MS(0), CPU);
    perf_period_start(&p, MS(10) + US(800), CPU);
    perf_period_start(&p, MS(20) + US(1300), CPU);  /* інтервал 10,5 мс */
    TEST_ASSERT_EQUAL_INT32(500, p.jit_min_us);
    TEST_ASSERT_EQUAL_INT32(800, p.jit_max_us);
}

/* Пропуск — інтервал понад 2 періоди; LCD 31,5 мс серед gate 5 мс */
static void test_period_missed(void)
{
    perf_period_t p;

    perf_period_init(&p, 5000);
    perf_period_start(&p, MS(0), CPU);
    perf_period_start(&p, MS(10), CPU);              /* рівно 2T — ще ні */
    perf_period_start(&p, MS(20) + US(1), CPU);      /* 2T + 1 мкс — пропуск */
    perf_period_start(&p, MS(56), CPU);              /* 35,999 мс */
    TEST_ASSERT_EQUAL_UINT32(2, p.missed);
    TEST_ASSERT_EQUAL_INT32(30999, p.jit_max_us);
    TEST_ASSERT_EQUAL_UINT32(3, p.hist[PERF_HIST_BINS - 1u]); /* 5000, 5001, 30999 — у «≥ 2000» */
}

static void test_hist_bins(void)
{
    TEST_ASSERT_EQUAL_UINT(0, perf_hist_bin(-1001));
    TEST_ASSERT_EQUAL_UINT(0, perf_hist_bin(INT32_MIN));
    TEST_ASSERT_EQUAL_UINT(1, perf_hist_bin(-1000));
    TEST_ASSERT_EQUAL_UINT(1, perf_hist_bin(-901));
    TEST_ASSERT_EQUAL_UINT(2, perf_hist_bin(-900));
    TEST_ASSERT_EQUAL_UINT(10, perf_hist_bin(-1));
    TEST_ASSERT_EQUAL_UINT(11, perf_hist_bin(0));
    TEST_ASSERT_EQUAL_UINT(11, perf_hist_bin(99));
    TEST_ASSERT_EQUAL_UINT(12, perf_hist_bin(100));
    TEST_ASSERT_EQUAL_UINT(30, perf_hist_bin(1999));
    TEST_ASSERT_EQUAL_UINT(31, perf_hist_bin(2000));
    TEST_ASSERT_EQUAL_UINT(31, perf_hist_bin(INT32_MAX));

    TEST_ASSERT_EQUAL_INT32(INT32_MIN, perf_hist_lo_us(0));
    TEST_ASSERT_EQUAL_INT32(-1000, perf_hist_lo_us(1));
    TEST_ASSERT_EQUAL_INT32(0, perf_hist_lo_us(11));
    TEST_ASSERT_EQUAL_INT32(2000, perf_hist_lo_us(31));
}

/* Інтервал між стартами через переповнення 32 біт */
static void test_period_across_32bit_wrap(void)
{
    perf_period_t p;
    uint32_t t0 = 0xFFFFFFFFu - US(1000) + 1u;

    perf_period_init(&p, 5000);
    perf_period_start(&p, t0, CPU);
    perf_period_start(&p, t0 + MS(5) + US(40), CPU);
    TEST_ASSERT_TRUE(t0 + MS(5) < t0);
    TEST_ASSERT_EQUAL_INT32(40, p.jit_max_us);
    TEST_ASSERT_EQUAL_UINT32(0, p.missed);
}

static void test_period_reset(void)
{
    perf_period_t p;

    perf_period_init(&p, 5000);
    perf_period_start(&p, MS(0), CPU);
    perf_period_start(&p, MS(50), CPU);
    perf_period_reset(&p);
    TEST_ASSERT_EQUAL_UINT32(0, p.n);
    TEST_ASSERT_EQUAL_UINT32(0, p.missed);
    TEST_ASSERT_EQUAL_UINT32(0, p.hist[PERF_HIST_BINS - 1u]);
    TEST_ASSERT_EQUAL_UINT32(5000, p.period_us);
    perf_period_start(&p, MS(500), CPU);     /* знову перший — без інтервалу */
    TEST_ASSERT_EQUAL_UINT32(0, p.n);
}

/* ---- завантаження ---------------------------------------------------------- */

static void test_load_window(void)
{
    perf_load_t l;

    perf_load_init(&l, MS(1000));
    TEST_ASSERT_EQUAL_INT(0, perf_load_poll(&l, MS(0)));      /* відкриває вікно */
    perf_load_busy(&l, MS(100));
    perf_load_busy(&l, MS(150) + US(500));
    TEST_ASSERT_EQUAL_INT(0, perf_load_poll(&l, MS(999)));
    TEST_ASSERT_EQUAL_INT(1, perf_load_poll(&l, MS(1000)));
    TEST_ASSERT_EQUAL_UINT32(1, l.windows);
    TEST_ASSERT_EQUAL_UINT32(MS(1000), l.last_win);
    TEST_ASSERT_EQUAL_UINT32(25, l.last_pct);                 /* 25,05 % */
    TEST_ASSERT_EQUAL_UINT32(251, l.last_pm);
    TEST_ASSERT_EQUAL_UINT32(0, l.busy);

    /* наступне вікно — від моменту закриття, може бути довшим */
    perf_load_busy(&l, MS(30));
    TEST_ASSERT_EQUAL_INT(0, perf_load_poll(&l, MS(1999)));
    TEST_ASSERT_EQUAL_INT(1, perf_load_poll(&l, MS(2500)));
    TEST_ASSERT_EQUAL_UINT32(MS(1500), l.last_win);
    TEST_ASSERT_EQUAL_UINT32(2, l.last_pct);
    TEST_ASSERT_EQUAL_UINT32(20, l.last_pm);
}

static void test_load_full_and_idle(void)
{
    perf_load_t l;

    perf_load_init(&l, MS(1000));
    perf_load_poll(&l, MS(10));
    perf_load_busy(&l, MS(1000));
    TEST_ASSERT_EQUAL_INT(1, perf_load_poll(&l, MS(1010)));
    TEST_ASSERT_EQUAL_UINT32(100, l.last_pct);
    TEST_ASSERT_EQUAL_UINT32(1000, l.last_pm);
    TEST_ASSERT_EQUAL_INT(1, perf_load_poll(&l, MS(2010)));
    TEST_ASSERT_EQUAL_UINT32(0, l.last_pct);
}

/* Вікно через переповнення лічильника */
static void test_load_across_32bit_wrap(void)
{
    perf_load_t l;
    uint32_t t0 = 0xFFFFFFFFu - MS(300) + 1u;

    perf_load_init(&l, MS(1000));
    perf_load_poll(&l, t0);
    perf_load_busy(&l, MS(50));
    TEST_ASSERT_EQUAL_INT(0, perf_load_poll(&l, t0 + MS(999)));
    TEST_ASSERT_EQUAL_INT(1, perf_load_poll(&l, t0 + MS(1000)));
    TEST_ASSERT_EQUAL_UINT32(5, l.last_pct);
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
    RUN_TEST(test_period_first_start_has_no_interval);
    RUN_TEST(test_period_jitter_min_max);
    RUN_TEST(test_period_positive_only);
    RUN_TEST(test_period_missed);
    RUN_TEST(test_hist_bins);
    RUN_TEST(test_period_across_32bit_wrap);
    RUN_TEST(test_period_reset);
    RUN_TEST(test_load_window);
    RUN_TEST(test_load_full_and_idle);
    RUN_TEST(test_load_across_32bit_wrap);
    return UNITY_END();
}
