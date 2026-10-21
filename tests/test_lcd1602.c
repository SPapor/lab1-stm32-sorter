/* Тести драйвера LCD1602 через PCF8574 (firmware/Devices/lcd1602.c) на фейковій шині.
 * Байт порту PCF8574: P0 = RS, P1 = RW, P2 = EN, P3 = BL, P4..P7 = D4..D7. */

#include <string.h>
#include "unity.h"
#include "fake_i2c_bus.h"
#include "lcd1602.h"

#define ADDR 0x27u

static fake_i2c_t f;
static lcd1602_t lcd;

/* Виклики delay_ms: тривалість і кількість транзакцій на момент виклику */
static uint32_t delay_ms[16];
static size_t delay_after[16];
static size_t n_delays;

static void fake_delay(uint32_t ms)
{
    TEST_ASSERT_TRUE(n_delays < sizeof delay_ms / sizeof delay_ms[0]);
    delay_ms[n_delays] = ms;
    delay_after[n_delays] = f.count;
    n_delays++;
}

void setUp(void)
{
    fake_i2c_init_script(&f);
    n_delays = 0;
    /* драйвер у стані після lcd1602_init, без транзакцій */
    lcd.bus = &f.bus;
    lcd.addr = ADDR;
    lcd.backlight = 1u;
    lcd.delay_ms = fake_delay;
}

void tearDown(void)
{
}

static void expect_ports(const uint8_t *bytes, size_t n)
{
    for (size_t i = 0; i < n; i++)
        fake_i2c_expect_write(&f, ADDR, &bytes[i], 1, BUS_OK);
}

/* HD44780U, Figure 24 «4-Bit Interface»: три ніббли 0x3, нібл 0x2, далі Function set,
 * Display off, Display clear, Entry mode set; останнім драйвер вмикає дисплей. */
static const uint8_t init_bytes[] = {
    0x08,                     /* порт у відомий стан: лише BL; далі > 15 мс */
    0x3C, 0x38,               /* 0011 — 8 біт; далі > 4,1 мс */
    0x3C, 0x38,               /* 0011; далі > 100 мкс */
    0x3C, 0x38,               /* 0011 */
    0x2C, 0x28,               /* 0010 — 4 біти */
    0x2C, 0x28, 0x8C, 0x88,   /* Function set 0x28: N = 1 (2 рядки), F = 0 (5x8) */
    0x0C, 0x08, 0x8C, 0x88,   /* Display off 0x08 */
    0x0C, 0x08, 0x1C, 0x18,   /* Display clear 0x01 */
    0x0C, 0x08, 0x6C, 0x68,   /* Entry mode 0x06: I/D = 1, S = 0 */
    0x0C, 0x08, 0xCC, 0xC8,   /* Display on 0x0C: D = 1, C = B = 0 */
};
#define INIT_WRITES (sizeof init_bytes)

static void test_init_sequence_figure24(void)
{
    static const uint32_t want_ms[] = { 50, 5, 1, 1, 2 };
    static const size_t want_after[] = { 1, 3, 5, 7, 21 };   /* після 0x08, 1-го, 2-го, 3-го 0x3 і clear */
    lcd1602_t d;

    memset(&d, 0, sizeof d);
    expect_ports(init_bytes, INIT_WRITES);

    TEST_ASSERT_EQUAL(LCD_OK, lcd1602_init(&d, &f.bus, ADDR, fake_delay));
    fake_i2c_verify(&f);
    TEST_ASSERT_EQUAL_UINT8(1, d.backlight);
    TEST_ASSERT_EQUAL(5, n_delays);
    for (size_t i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_UINT32(want_ms[i], delay_ms[i]);
        TEST_ASSERT_EQUAL(want_after[i], delay_after[i]);
    }
}

/* Вектор із запису аналізатора: cap lcd, docs/lab_2/captures/all.vcd, 28,368 с
 * (measurements.md, скрін 23-cap-lcd): 'C' = 0x43 → 4D 49 3D 39 на 0x27. */
static void test_print_C_matches_capture(void)
{
    expect_ports((const uint8_t[]){ 0x4D, 0x49, 0x3D, 0x39 }, 4);

    TEST_ASSERT_EQUAL(LCD_OK, lcd1602_print(&lcd, "C"));
    fake_i2c_verify(&f);
}

/* Той самий запис: перед "CAP" — set_cursor(0, 1) = команда 0xC0 → CC C8 0C 08. */
static void test_set_cursor_row1_is_C0(void)
{
    expect_ports((const uint8_t[]){ 0xCC, 0xC8, 0x0C, 0x08 }, 4);

    TEST_ASSERT_EQUAL(LCD_OK, lcd1602_set_cursor(&lcd, 0, 1));
    fake_i2c_verify(&f);
}

static void test_set_cursor_row0_and_column_clamp(void)
{
    expect_ports((const uint8_t[]){ 0x8C, 0x88, 0x5C, 0x58 }, 4);   /* 0x85 */
    expect_ports((const uint8_t[]){ 0xCC, 0xC8, 0xFC, 0xF8 }, 4);   /* 0xCF: col 20 → 15 */

    TEST_ASSERT_EQUAL(LCD_OK, lcd1602_set_cursor(&lcd, 5, 0));
    TEST_ASSERT_EQUAL(LCD_OK, lcd1602_set_cursor(&lcd, 20, 1));
    fake_i2c_verify(&f);
}

static void test_clear_waits_after_command(void)
{
    expect_ports((const uint8_t[]){ 0x0C, 0x08, 0x1C, 0x18 }, 4);

    TEST_ASSERT_EQUAL(LCD_OK, lcd1602_clear(&lcd));
    fake_i2c_verify(&f);
    TEST_ASSERT_EQUAL(1, n_delays);
    TEST_ASSERT_EQUAL_UINT32(2, delay_ms[0]);
    TEST_ASSERT_EQUAL(4, delay_after[0]);
}

static void test_print_without_backlight(void)
{
    lcd.backlight = 0u;
    expect_ports((const uint8_t[]){ 0x45, 0x41, 0x35, 0x31 }, 4);

    TEST_ASSERT_EQUAL(LCD_OK, lcd1602_print(&lcd, "C"));
    fake_i2c_verify(&f);
}

/* Обмеження Wokwi C0 (docs/journal.md, проблема 8): кожен запис на шину — 1 байт. */
static void test_every_write_is_one_byte(void)
{
    lcd1602_t d;

    fake_i2c_init_regs(&f, ADDR);
    TEST_ASSERT_EQUAL(LCD_OK, lcd1602_init(&d, &f.bus, ADDR, fake_delay));
    TEST_ASSERT_EQUAL(LCD_OK, lcd1602_set_cursor(&d, 0, 1));
    TEST_ASSERT_EQUAL(LCD_OK, lcd1602_print(&d, "CAP"));
    TEST_ASSERT_EQUAL(LCD_OK, lcd1602_clear(&d));

    TEST_ASSERT_EQUAL(INIT_WRITES + 4 + 3 * 4 + 4, f.count);
    for (size_t i = 0; i < f.count; i++) {
        TEST_ASSERT_EQUAL(FAKE_I2C_WRITE, f.log[i].op);
        TEST_ASSERT_EQUAL_HEX8(ADDR, f.log[i].addr);
        TEST_ASSERT_EQUAL(1, f.log[i].len);
    }
}

/* Помилка на будь-якому записі ініціалізації → LCD_ERR_BUS, далі ні транзакцій, ні затримок. */
static void test_init_bus_error_at_each_write(void)
{
    for (size_t n = 1; n <= INIT_WRITES; n++) {
        lcd1602_t d;

        fake_i2c_init_regs(&f, ADDR);
        fake_i2c_fail_at(&f, n, n % 2 ? BUS_ERR_NACK : BUS_ERR_TIMEOUT);
        n_delays = 0;

        TEST_ASSERT_EQUAL_MESSAGE(LCD_ERR_BUS, lcd1602_init(&d, &f.bus, ADDR, fake_delay), "status");
        TEST_ASSERT_EQUAL_MESSAGE(n, f.count, "transactions after error");
        for (size_t i = 0; i < n_delays; i++)
            TEST_ASSERT_TRUE_MESSAGE(delay_after[i] < n, "delay after error");
    }
}

static void test_init_absent_device(void)
{
    lcd1602_t d;

    fake_i2c_init_regs(&f, 0x3F);
    TEST_ASSERT_EQUAL(LCD_ERR_BUS, lcd1602_init(&d, &f.bus, ADDR, fake_delay));
    TEST_ASSERT_EQUAL(1, f.count);
    TEST_ASSERT_EQUAL(BUS_ERR_NACK, f.log[0].status);
}

static void test_print_bus_error_at_each_write(void)
{
    for (size_t n = 1; n <= 8; n++) {   /* два символи по 4 записи */
        fake_i2c_init_regs(&f, ADDR);
        fake_i2c_fail_at(&f, n, BUS_ERR_NACK);

        TEST_ASSERT_EQUAL(LCD_ERR_BUS, lcd1602_print(&lcd, "CA"));
        TEST_ASSERT_EQUAL(n, f.count);
    }
}

static void test_set_cursor_and_clear_bus_error(void)
{
    for (size_t n = 1; n <= 4; n++) {
        fake_i2c_init_regs(&f, ADDR);
        fake_i2c_fail_at(&f, n, BUS_ERR_TIMEOUT);
        TEST_ASSERT_EQUAL(LCD_ERR_BUS, lcd1602_set_cursor(&lcd, 0, 1));
        TEST_ASSERT_EQUAL(n, f.count);

        fake_i2c_init_regs(&f, ADDR);
        fake_i2c_fail_at(&f, n, BUS_ERR_TIMEOUT);
        n_delays = 0;
        TEST_ASSERT_EQUAL(LCD_ERR_BUS, lcd1602_clear(&lcd));
        TEST_ASSERT_EQUAL(n, f.count);
        TEST_ASSERT_EQUAL(0, n_delays);
    }
}

/* ---- lcd1602_probe: запис 0x08 і читання порту назад ---- */

static void test_probe_reads_port_back(void)
{
    uint8_t rb = 0;

    fake_i2c_expect_write(&f, ADDR, (const uint8_t[]){ 0x08 }, 1, BUS_OK);
    fake_i2c_expect_read(&f, ADDR, (const uint8_t[]){ 0x08 }, 1, BUS_OK);

    TEST_ASSERT_EQUAL(LCD_OK, lcd1602_probe(&f.bus, ADDR, &rb));
    fake_i2c_verify(&f);
    TEST_ASSERT_EQUAL_HEX8(0x08, rb);
}

static void test_probe_bus_errors(void)
{
    uint8_t rb = 0;

    fake_i2c_init_regs(&f, ADDR);
    fake_i2c_fail_at(&f, 1, BUS_ERR_NACK);
    TEST_ASSERT_EQUAL(LCD_ERR_BUS, lcd1602_probe(&f.bus, ADDR, &rb));
    TEST_ASSERT_EQUAL(1, f.count);

    fake_i2c_init_regs(&f, ADDR);
    fake_i2c_fail_at(&f, 2, BUS_ERR_TIMEOUT);
    TEST_ASSERT_EQUAL(LCD_ERR_BUS, lcd1602_probe(&f.bus, ADDR, &rb));
    TEST_ASSERT_EQUAL(2, f.count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_sequence_figure24);
    RUN_TEST(test_print_C_matches_capture);
    RUN_TEST(test_set_cursor_row1_is_C0);
    RUN_TEST(test_set_cursor_row0_and_column_clamp);
    RUN_TEST(test_clear_waits_after_command);
    RUN_TEST(test_print_without_backlight);
    RUN_TEST(test_every_write_is_one_byte);
    RUN_TEST(test_init_bus_error_at_each_write);
    RUN_TEST(test_init_absent_device);
    RUN_TEST(test_print_bus_error_at_each_write);
    RUN_TEST(test_set_cursor_and_clear_bus_error);
    RUN_TEST(test_probe_reads_port_back);
    RUN_TEST(test_probe_bus_errors);
    return UNITY_END();
}
