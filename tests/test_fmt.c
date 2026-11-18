/* Тести форматера (firmware/App/fmt.c): результат байт у байт такий, як у
 * vsnprintf хоста, для всіх специфікаторів з рядків прошивки і граничних значень.
 * Друга частина - рядки з реальних логів ЛР3 і ЛР4: ті самі формати й аргументи,
 * що в main.c та i2c_hal.c, мають дати той самий текст, тож парсери логів
 * (tools/stats_table.py, tools/evt_latency.py) бачать те саме. */

#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "fmt.h"

void setUp(void)
{
}

void tearDown(void)
{
}

/* Шлях черги журналу rtos: аргументи з va_list у масив (fmt_vargs), пізніше -
 * рядок з масиву (fmt_abuf). Має вийти те саме, що дав fmt_vbuf */
static void check_args(const char *want, size_t want_n, size_t cap, const char *fmt, va_list ap)
{
    fmt_arg_t arr[16];
    char got[256];
    unsigned n;
    size_t gn;

    memset(got, 'X', sizeof got);
    n = fmt_vargs(fmt, ap, arr, 16u);
    TEST_ASSERT_TRUE_MESSAGE(n <= 16u, fmt);
    gn = fmt_abuf(got, cap, fmt, arr, n);
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)want_n, (int)gn, fmt);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(want, got, sizeof got, fmt);
}

/* fmt_vbuf проти vsnprintf: текст, довжина і '\0' при різних cap; те саме через масив */
static void check_cap(size_t cap, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void check_cap(size_t cap, const char *fmt, ...)
{
    char want[256], got[256];
    va_list ap, ap2, ap3;
    int wn;
    size_t gn;

    TEST_ASSERT_TRUE(cap <= sizeof got);
    memset(want, 'X', sizeof want);
    memset(got, 'X', sizeof got);
    va_start(ap, fmt);
    va_copy(ap2, ap);
    va_copy(ap3, ap);
    wn = vsnprintf(want, cap, fmt, ap);
    gn = fmt_vbuf(got, cap, fmt, ap2);
    check_args(want, (size_t)wn, cap, fmt, ap3);
    va_end(ap3);
    va_end(ap2);
    va_end(ap);
    TEST_ASSERT_EQUAL_INT_MESSAGE(wn, (int)gn, fmt);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(want, got, sizeof got, fmt);
}

#define check(...) check_cap(256u, __VA_ARGS__)

/* Рядок логу: текст має збігтися з еталоном з логу і з snprintf хоста */
static void check_line(const char *expected, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void check_line(const char *expected, const char *fmt, ...)
{
    char want[256], got[256];
    va_list ap, ap2, ap3;
    int wn;

    memset(want, 'X', sizeof want);
    va_start(ap, fmt);
    va_copy(ap2, ap);
    va_copy(ap3, ap);
    wn = vsnprintf(want, sizeof want, fmt, ap);
    fmt_vbuf(got, sizeof got, fmt, ap2);
    check_args(want, (size_t)wn, sizeof want, fmt, ap3);
    va_end(ap3);
    va_end(ap2);
    va_end(ap);
    TEST_ASSERT_EQUAL_STRING(want, got);
    TEST_ASSERT_EQUAL_STRING(expected, got);
}

/* --- Специфікатори, які є в прошивці ------------------------------------- */

static void test_lu(void)
{
    check("%lu", 0ul);
    check("%lu", 1ul);
    check("%lu", 4294967295ul);   /* макс. uint32 */
    check("t_us=%lu name=%s", 7047ul, "boot");
}

static void test_u(void)
{
    check("%u", 0u);
    check("%u", 65535u);
    check("%u", UINT_MAX);
    check("%4u", 7u);              /* ширина більша за значення */
    check("%4u", 12345u);          /* ширина менша: не обрізається */
}

static void test_d_ld(void)
{
    check("%d", 0);
    check("%d", -1);
    check("%d", INT_MIN);
    check("%d", INT_MAX);
    check("%ld", -962l);
    check("%ld", (long)INT32_MIN);
    check("%ld", LONG_MIN);        /* модуль не вміщується в long */
    check("%ld", LONG_MAX);
    check("%5ld", -42l);
    check("%5ld", 123456l);
    check("%6d", -32768);
    check("%6d", 0);
    check("%05d", -42);            /* знак перед нулями */
    check("%-5d|", -42);
}

static void test_hex(void)
{
    check("%02X", 0u);
    check("%02X", 0x8u);
    check("%02X", 0xFFu);
    check("%02X", 0x1234u);        /* ширина менша за значення */
    check("%08lX", 0ul);
    check("%08lX", 0x8003ul);
    check("%08lX", 0xFFFFFFFFul);
    check("%02lX", 0x24ul);
    check("%x", 0xabcdu);
    check("%X", 0xabcdu);
}

static void test_09lu(void)
{
    check("%lu%09lu", 4ul, 152895418ul);
    check("%lu%09lu", 1ul, 0ul);
    check("%09lu", 999999999ul);
}

static void test_s_c(void)
{
    check("%s", "");
    check("[%s]", "");
    check("%s", "READY");
    check("%-8s|", "READY");
    check("%-8s|", "LONGER_THAN_8");
    check("%8s|", "ab");
    check("%-16s|", "");
    check("%c", 'A');
    check("%3c|%-3c|", 'x', 'y');
    check("100%%");
    check("%s=%u %s", "a", 1u, "b");
}

static void test_left_and_zero(void)
{
    check("%-4lu|", 7ul);
    check("%-3lu|", 1234ul);
    check("%-5lu|", 0ul);
    check("%010lu", 42ul);
}

/* Обрізання: як у vsnprintf - не більше cap - 1 символів, '\0', повна довжина */
static void test_truncation(void)
{
    check_cap(1u, "%s", "abc");
    check_cap(2u, "%s", "abc");
    check_cap(4u, "%s", "abc");
    check_cap(5u, "%08lX", 0x8003ul);
    check_cap(10u, "%lu SELFTEST dev=%s", 160ul, "lcd1602");
}

static void test_cap_zero(void)
{
    char buf[4] = "zzz";

    TEST_ASSERT_EQUAL_UINT(5u, fmt_buf(buf, 0u, "%s", "hello"));
    TEST_ASSERT_EQUAL_STRING("zzz", buf);    /* при cap = 0 буфер не чіпається */
}

/* Без перевірки формату компілятором: для форматів, на які -Wformat свариться */
static size_t raw(char *dst, size_t cap, const char *fmt, ...)
{
    va_list ap;
    size_t n;

    va_start(ap, fmt);
    n = fmt_vbuf(dst, cap, fmt, ap);
    va_end(ap);
    return n;
}

/* Невідомий специфікатор виводиться як є (у прошивці таких немає) */
static void test_unknown_spec(void)
{
    char buf[32];

    raw(buf, sizeof buf, "a%qb");
    TEST_ASSERT_EQUAL_STRING("a%qb", buf);
    raw(buf, sizeof buf, "end%");
    TEST_ASSERT_EQUAL_STRING("end%", buf);
}

/* '-' сильніший за '0', як у printf */
static void test_minus_beats_zero(void)
{
    char buf[32];

    raw(buf, sizeof buf, "%-010lu|", 42ul);
    TEST_ASSERT_EQUAL_STRING("42        |", buf);
}

/* --- Рядки з реальних логів (формати - з main.c та i2c_hal.c) ------------ */

static void test_log_lines(void)
{
    /* BOOT (ЛР4, обидві ARCH, без доповнення - його дописує uart_hal_line_pad) */
    check_line("2 BOOT fw=lab4-rtos git=50939149 perf=1 arch=rtos lto=1 tick_hz=1000 kernel=V11.3.3 os_heap=5120",
               "%lu %s fw=%s git=%s perf=%u arch=%s lto=%u tick_hz=%u kernel=%s os_heap=%lu", 2ul, "BOOT",
               "lab4-rtos", "50939149", 1u, "rtos", 1u, 1000u, "V11.3.3", 5120ul);
    check_line("2 BOOT fw=lab3-superloop git=50939149 perf=1 arch=superloop lto=1 tick_hz=1000",
               "%lu %s fw=%s git=%s perf=%u arch=%s lto=%u tick_hz=%u", 2ul, "BOOT",
               "lab3-superloop", "50939149", 1u, "superloop", 1u, 1000u);
    check_line("10 KNOBS lcd_ms=500 lcd_chars=0 imu_ms=10 log=2",
               "%lu %s lcd_ms=%u lcd_chars=%u imu_ms=%u log=%u", 10ul, "KNOBS", 500u, 0u, 10u, 2u);
    check_line("16 PERF cpu_hz=48000000 now_cyc=440 pair_cyc=1153",
               "%lu %s cpu_hz=%lu now_cyc=%lu pair_cyc=%lu", 16ul, "PERF", 48000000ul, 440ul, 1153ul);
    check_line("72701 PERF git=cad75f82 now_cyc=526 pair_cyc=1298 cyc_per_us=48 arch=superloop lto=1 tick_hz=1000",
               "%lu %s git=%s now_cyc=%lu pair_cyc=%lu cyc_per_us=%lu arch=%s lto=%u tick_hz=%u",
               72701ul, "PERF", "cad75f82", 526ul, 1298ul, 48ul, "superloop", 1u, 1000u);
    check_line("85 SELFTEST dev=mpu6050 status=WARN who=0x68 tries=3 err=SLEEP",
               "%lu SELFTEST dev=%s status=%s who=0x%02X tries=%u err=%s", 85ul, "mpu6050", "WARN",
               0x68u, 3u, "SLEEP");
    check_line("161 SELFTEST dev=lcd1602 status=OK addr=0x27 rb=0x08",
               "%lu SELFTEST dev=%s status=%s addr=0x%02X rb=0x%02X", 161ul, "lcd1602", "OK", 0x27u, 0x08u);
    check_line("181 SELFTEST_END ok=4 warn=1 fail=0",
               "%lu SELFTEST_END ok=%u warn=%u fail=%u", 181ul, 4u, 1u, 0u);
    check_line("221 STATE state=READY", "%lu %s state=%s", 221ul, "STATE", "READY");
    check_line("224 STACK used=824 reserved=1024 free=200 overflow=0",
               "%lu %s used=%lu reserved=%lu free=%lu overflow=%u", 224ul, "STACK",
               824ul, 1024ul, 200ul, 0u);
    check_line("230 HEAP arena=436 used=436 free=0",
               "%lu %s arena=%u used=%u free=%u", 230ul, "HEAP", 436u, 436u, 0u);
    check_line("7 EVT t_us=7047 name=boot", "%lu %s t_us=%lu name=%s", 7ul, "EVT", 7047ul, "boot");
    check_line("4153083 EVT t_us=4152895418 name=cmd_rx",
               "%lu %s t_us=%lu%09lu name=%s", 4153083ul, "EVT", 4ul, 152895418ul, "cmd_rx");
    check_line("67679 STARTUP boot_us=11681 gate_us=171890 class_us=176532 imu_us=234307 lcd_us=751718 "
               "os_api_us=232513 os_start_us=233078",
               "%lu %s boot_us=%lu gate_us=%lu class_us=%lu imu_us=%lu lcd_us=%lu "
               "os_api_us=%lu os_start_us=%lu", 67679ul, "STARTUP", 11681ul, 171890ul, 176532ul,
               234307ul, 751718ul, 232513ul, 233078ul);
    check_line("72632 STAT name=gate n=9072 min_us=113 avg_us=115 max_us=135 period_us=5000 "
               "jit_min_us=-962 jit_max_us=89792 miss=683",
               "%lu %s name=%s n=%lu min_us=%lu avg_us=%lu max_us=%lu period_us=%lu "
               "jit_min_us=%ld jit_max_us=%ld miss=%lu", 72632ul, "STAT", "gate", 9072ul, 113ul,
               115ul, 135ul, 5000ul, -962l, 89792l, 683ul);
    check_line("225513 LOAD pct=20 pm=195 win_cyc=48002400 busy_cyc=9365622 windows=225",
               "%lu %s pct=%lu pm=%lu win_cyc=%lu busy_cyc=%lu windows=%lu", 225513ul, "LOAD",
               20ul, 195ul, 48002400ul, 9365622ul, 225ul);
    check_line("72764 UART rx=37 ore=0", "%lu %s rx=%lu ore=%lu", 72764ul, "UART", 37ul, 0ul);
    check_line("77552 HIST name=lcd period_us=100000 n=662 step_us=100",
               "%lu %s name=%s period_us=%lu n=%lu step_us=%d", 77552ul, "HIST", "lcd", 100000ul,
               662ul, 100);
    check_line("77558 HIST name=lcd lo_us=-700 hi_us=-600 n=1",
               "%lu %s name=%s lo_us=%ld hi_us=%ld n=%lu", 77558ul, "HIST", "lcd", -700l, -600l, 1ul);
    check_line("77622 HIST name=lcd lo_us=2000 hi_us=inf n=5",
               "%lu %s name=%s lo_us=%ld hi_us=inf n=%lu", 77622ul, "HIST", "lcd", 2000l, 5ul);
    check_line("244330 STATUS state=READY a=0 b=0 reject=0 total=0 impact=0 log=1 fault=NONE",
               "%lu %s state=%s a=%lu b=%lu reject=%lu total=%lu impact=%u log=%u fault=%s",
               244330ul, "STATUS", "READY", 0ul, 0ul, 0ul, 0ul, 0u, 1u, "NONE");
    check_line("249704 I2C_RECOVER try=1/3 where=wr addr=0x27 err=0x24 isr=0x00000001",
               "%lu I2C_RECOVER try=%u/%u where=%s addr=0x%02X err=0x%02lX isr=0x%08lX",
               249704ul, 1u, 3u, "wr", 0x27u, 0x24ul, 0x1ul);
    check_line("30 I2C_RECOVER try=1/1 where=wr addr=0x68 err=0x20 isr=0x00008003",
               "%lu I2C_RECOVER try=%u/%u where=%s addr=0x%02X err=0x%02lX isr=0x%08lX",
               30ul, 1u, 1u, "wr", 0x68u, 0x20ul, 0x8003ul);
    check_line("244128 LOG on=1", "%lu %s on=%u", 244128ul, "LOG", 1u);
    check_line("3945 STATS reset=1", "%lu %s reset=1", 3945ul, "STATS");
    check_line("160880 CLI st=BAD", "%lu %s st=BAD", 160880ul, "CLI");
}

/* Масив: аргументів більше за місце - решта загублена, кількість повертається;
 * відсутні в масиві - 0 і "(null)" */
static void args_take(fmt_arg_t *arr, unsigned max, unsigned *n, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    *n = fmt_vargs(fmt, ap, arr, max);
    va_end(ap);
}

static void test_args_overflow_and_missing(void)
{
    fmt_arg_t arr[3] = { 0, 0, 0 };
    char got[64];
    unsigned n;

    args_take(arr, 2u, &n, "%u %s %ld %c", 7u, "ab", -5L, 'z');
    TEST_ASSERT_EQUAL_UINT(4u, n);
    TEST_ASSERT_EQUAL_UINT(0u, (unsigned)arr[2]);   /* третього місця не чіпали */
    fmt_abuf(got, sizeof got, "%u %s %ld %c", arr, 2u);
    TEST_ASSERT_EQUAL_STRING("7 ab 0 \0", got);    /* %c з 0 - символ '\0' обриває рядок */
    fmt_abuf(got, sizeof got, "%u %s", arr, 1u);
    TEST_ASSERT_EQUAL_STRING("7 (null)", got);
    args_take(arr, 3u, &n, "no args %% here");
    TEST_ASSERT_EQUAL_UINT(0u, n);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_lu);
    RUN_TEST(test_u);
    RUN_TEST(test_d_ld);
    RUN_TEST(test_hex);
    RUN_TEST(test_09lu);
    RUN_TEST(test_s_c);
    RUN_TEST(test_left_and_zero);
    RUN_TEST(test_truncation);
    RUN_TEST(test_cap_zero);
    RUN_TEST(test_unknown_spec);
    RUN_TEST(test_minus_beats_zero);
    RUN_TEST(test_log_lines);
    RUN_TEST(test_args_overflow_and_missing);
    return UNITY_END();
}
