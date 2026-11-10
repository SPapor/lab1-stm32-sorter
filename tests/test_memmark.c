/* Тести stack painting (firmware/App/memmark.c) на масиві замість RAM. */

#include "unity.h"
#include "memmark.h"

#define WORDS 64u

static uint32_t ram[WORDS];      /* ram[0] — дно резерву, ram + WORDS — _estack */

void setUp(void)
{
    for (size_t i = 0; i < WORDS; i++)
        ram[i] = 0x12345678u;
}

void tearDown(void)
{
}

static void test_paint_fills_only_range(void)
{
    memmark_paint(ram + 4, ram + 10, MEMMARK_PATTERN);
    TEST_ASSERT_EQUAL_HEX32(0x12345678u, ram[3]);
    TEST_ASSERT_EQUAL_HEX32(MEMMARK_PATTERN, ram[4]);
    TEST_ASSERT_EQUAL_HEX32(MEMMARK_PATTERN, ram[9]);
    TEST_ASSERT_EQUAL_HEX32(0x12345678u, ram[10]);
    memmark_paint(ram + 10, ram + 10, MEMMARK_PATTERN);   /* порожній діапазон */
    memmark_paint(ram + 12, ram + 11, MEMMARK_PATTERN);   /* hi < lo */
    TEST_ASSERT_EQUAL_HEX32(0x12345678u, ram[11]);
}

/* Як у методичці: фарбуємо до SP - 16 слів, вище — живий кадр main */
static void test_used_after_paint(void)
{
    memmark_paint(ram, ram + WORDS - 16, MEMMARK_PATTERN);
    TEST_ASSERT_EQUAL_UINT32(16u * 4u, memmark_used_bytes(ram, ram + WORDS, MEMMARK_PATTERN));
}

/* Найнижче зіпсоване слово визначає глибину; «острівці» вище не впливають */
static void test_used_high_water(void)
{
    memmark_paint(ram, ram + WORDS - 16, MEMMARK_PATTERN);
    ram[40] = 0;
    TEST_ASSERT_EQUAL_UINT32(24u * 4u, memmark_used_bytes(ram, ram + WORDS, MEMMARK_PATTERN));
    ram[20] = 0x0BADF00Du;
    ram[44] = MEMMARK_PATTERN;
    TEST_ASSERT_EQUAL_UINT32(44u * 4u, memmark_used_bytes(ram, ram + WORDS, MEMMARK_PATTERN));
}

/* Шаблон зіпсовано до самого низу — used дорівнює резерву */
static void test_used_whole_reserve(void)
{
    memmark_paint(ram, ram + WORDS - 16, MEMMARK_PATTERN);
    ram[0] = 0;
    TEST_ASSERT_EQUAL_UINT32(WORDS * 4u, memmark_used_bytes(ram, ram + WORDS, MEMMARK_PATTERN));
}

/* Нічого не пофарбовано (наприклад, PERF=0 або фарбування не викликали) */
static void test_used_unpainted(void)
{
    TEST_ASSERT_EQUAL_UINT32(WORDS * 4u, memmark_used_bytes(ram, ram + WORDS, MEMMARK_PATTERN));
    TEST_ASSERT_EQUAL_UINT32(0, memmark_used_bytes(ram + 8, ram + 8, MEMMARK_PATTERN));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_paint_fills_only_range);
    RUN_TEST(test_used_after_paint);
    RUN_TEST(test_used_high_water);
    RUN_TEST(test_used_whole_reserve);
    RUN_TEST(test_used_unpainted);
    return UNITY_END();
}
