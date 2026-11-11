/* Тести форматування рядків LCD (firmware/App/lcdfmt.c): результат має бути
 * байт у байт таким, як у snprintf з тими самими форматами, що були в main.c. */

#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "lcdfmt.h"

#define ROW 17u   /* LCD1602_COLS + 1 */

void setUp(void)
{
}

void tearDown(void)
{
}

/* "%-8s n=%-5lu" — рядок 0: стан і всього посилок */
static void check_status(const char *state, uint32_t total)
{
    char want[ROW], got[ROW];
    size_t n;

    snprintf(want, sizeof want, "%-8s n=%-5lu", state, (unsigned long)total);
    memset(got, 'X', sizeof got);
    n = lcdfmt_str(got, 0, sizeof got, state, 8);
    n = lcdfmt_str(got, n, sizeof got, " n=", 0);
    lcdfmt_u32(got, n, sizeof got, total, 5);
    TEST_ASSERT_EQUAL_STRING(want, got);
}

/* "A%-4lu B%-4lu R%-3lu" — рядок 1: лічильники */
static void check_counts(uint32_t a, uint32_t b, uint32_t r)
{
    char want[ROW], got[ROW];
    size_t n;

    snprintf(want, sizeof want, "A%-4lu B%-4lu R%-3lu",
             (unsigned long)a, (unsigned long)b, (unsigned long)r);
    memset(got, 'X', sizeof got);
    n = lcdfmt_str(got, 0, sizeof got, "A", 0);
    n = lcdfmt_u32(got, n, sizeof got, a, 4);
    n = lcdfmt_str(got, n, sizeof got, " B", 0);
    n = lcdfmt_u32(got, n, sizeof got, b, 4);
    n = lcdfmt_str(got, n, sizeof got, " R", 0);
    lcdfmt_u32(got, n, sizeof got, r, 3);
    TEST_ASSERT_EQUAL_STRING(want, got);
}

static void test_status_row(void)
{
    check_status("READY", 0);
    check_status("SORTING", 12);
    check_status("ESTOP", 99999);
    check_status("FAULT", 4294967295u);   /* не вміщується — обрізається, як у snprintf */
    check_status("", 7);
}

static void test_counts_row(void)
{
    check_counts(0, 0, 0);
    check_counts(1, 22, 333);
    check_counts(9999, 9999, 999);
    check_counts(12345, 0, 7);            /* ширше за поле */
    check_counts(4294967295u, 4294967295u, 4294967295u);
}

/* "%-16s" — рядки FAULT */
static void test_padded_text(void)
{
    static const char *const texts[] = { "FAULT:", "MPU not found", "", "0123456789abcdefXYZ" };

    for (size_t i = 0; i < sizeof texts / sizeof texts[0]; i++) {
        char want[ROW], got[ROW];

        snprintf(want, sizeof want, "%-16s", texts[i]);
        memset(got, 'X', sizeof got);
        lcdfmt_str(got, 0, sizeof got, texts[i], 16);
        TEST_ASSERT_EQUAL_STRING(want, got);
    }
}

/* "ST ok=%u w=%u f=%u" — екран самоперевірки */
static void test_selftest_row(void)
{
    char want[ROW], got[ROW];
    size_t n;

    snprintf(want, sizeof want, "ST ok=%u w=%u f=%u", 4u, 1u, 0u);
    n = lcdfmt_str(got, 0, sizeof got, "ST ok=", 0);
    n = lcdfmt_u32(got, n, sizeof got, 4, 0);
    n = lcdfmt_str(got, n, sizeof got, " w=", 0);
    n = lcdfmt_u32(got, n, sizeof got, 1, 0);
    n = lcdfmt_str(got, n, sizeof got, " f=", 0);
    lcdfmt_u32(got, n, sizeof got, 0, 0);
    TEST_ASSERT_EQUAL_STRING(want, got);
}

/* Повертається позиція, «скільки вийшло б», і '\0' стоїть навіть без жодного символу */
static void test_positions_and_tiny_buffer(void)
{
    char b[4];

    TEST_ASSERT_EQUAL_UINT(5, lcdfmt_str(b, 0, sizeof b, "ab", 5));
    TEST_ASSERT_EQUAL_STRING("ab ", b);
    TEST_ASSERT_EQUAL_UINT(10, lcdfmt_u32(b, 0, sizeof b, 1234567890u, 0));
    TEST_ASSERT_EQUAL_STRING("123", b);
    TEST_ASSERT_EQUAL_UINT(1, lcdfmt_u32(b, 0, 1, 7, 0));
    TEST_ASSERT_EQUAL_CHAR('\0', b[0]);
    TEST_ASSERT_EQUAL_UINT(6, lcdfmt_str(b, 6, sizeof b, "", 0));   /* pos уже за межами */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_status_row);
    RUN_TEST(test_counts_row);
    RUN_TEST(test_padded_text);
    RUN_TEST(test_selftest_row);
    RUN_TEST(test_positions_and_tiny_buffer);
    return UNITY_END();
}
