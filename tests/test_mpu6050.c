/* Тести драйвера MPU-6050 (firmware/Devices/mpu6050.c) на фейковій шині. */

#include "unity.h"
#include "fake_i2c_bus.h"
#include "mpu6050.h"

#define ADDR MPU6050_ADDR_DEFAULT

/* Номери транзакцій mpu6050_init: 1 — запис вказівника 0x75, 2 — читання WHO_AM_I;
 * спроба k (з 1): 3k — запис PWR_MGMT_1 = 0, 3k+1 — запис вказівника 0x6B, 3k+2 — читання. */
#define TXN_WR_PWR(k)  (3u * (k))
#define TXN_PTR_PWR(k) (3u * (k) + 1u)
#define TXN_RD_PWR(k)  (3u * (k) + 2u)
#define TXN_INIT_MAX   (TXN_RD_PWR(MPU6050_INIT_ATTEMPTS))

static fake_i2c_t f;
static mpu6050_t dev;

void setUp(void)
{
    fake_i2c_init_regs(&f, ADDR);
    f.regs[MPU6050_REG_WHO_AM_I] = MPU6050_WHO_AM_I_VALUE;
    f.regs[MPU6050_REG_PWR_MGMT_1] = MPU6050_PWR1_SLEEP;   /* стан після скидання */
}

void tearDown(void)
{
}

static void expect_wr(const uint8_t *b, size_t n, bus_status_t st)
{
    fake_i2c_expect_write(&f, ADDR, b, n, st);
}

static void expect_rd(const uint8_t *b, size_t n, bus_status_t st)
{
    fake_i2c_expect_read(&f, ADDR, b, n, st);
}

/* ---- mpu6050_init ---- */

static void test_init_transactions(void)
{
    fake_i2c_init_script(&f);
    expect_wr((const uint8_t[]){ 0x75 }, 1, BUS_OK);
    expect_rd((const uint8_t[]){ 0x68 }, 1, BUS_OK);
    expect_wr((const uint8_t[]){ 0x6B, 0x00 }, 2, BUS_OK);
    expect_wr((const uint8_t[]){ 0x6B }, 1, BUS_OK);
    expect_rd((const uint8_t[]){ 0x00 }, 1, BUS_OK);

    TEST_ASSERT_EQUAL(MPU6050_OK, mpu6050_init(&dev, &f.bus, ADDR));
    fake_i2c_verify(&f);
}

static void test_init_whoami_ok_wakes_on_first_try(void)
{
    TEST_ASSERT_EQUAL(MPU6050_OK, mpu6050_init(&dev, &f.bus, ADDR));
    TEST_ASSERT_EQUAL_HEX8(0x68, dev.who_am_i);
    TEST_ASSERT_EQUAL_UINT8(1, dev.init_tries);
    TEST_ASSERT_EQUAL_HEX8(0x00, f.regs[MPU6050_REG_PWR_MGMT_1]);
    TEST_ASSERT_EQUAL(TXN_RD_PWR(1), f.count);
}

static void test_init_foreign_id(void)
{
    f.regs[MPU6050_REG_WHO_AM_I] = 0x70;

    TEST_ASSERT_EQUAL(MPU6050_ERR_ID, mpu6050_init(&dev, &f.bus, ADDR));
    TEST_ASSERT_EQUAL_HEX8(0x70, dev.who_am_i);
    TEST_ASSERT_EQUAL(2, f.count);   /* у PWR_MGMT_1 не писали */
    TEST_ASSERT_EQUAL_HEX8(MPU6050_PWR1_SLEEP, f.regs[MPU6050_REG_PWR_MGMT_1]);
}

static void test_init_bus_error_on_whoami_write(void)
{
    fake_i2c_fail_at(&f, 1, BUS_ERR_NACK);

    TEST_ASSERT_EQUAL(MPU6050_ERR_BUS, mpu6050_init(&dev, &f.bus, ADDR));
    TEST_ASSERT_EQUAL(1, f.count);
    TEST_ASSERT_EQUAL_UINT8(0, dev.init_tries);
}

static void test_init_bus_error_on_whoami_read(void)
{
    fake_i2c_fail_at(&f, 2, BUS_ERR_TIMEOUT);

    TEST_ASSERT_EQUAL(MPU6050_ERR_BUS, mpu6050_init(&dev, &f.bus, ADDR));
    TEST_ASSERT_EQUAL(2, f.count);
}

static void test_init_absent_device(void)
{
    TEST_ASSERT_EQUAL(MPU6050_ERR_BUS, mpu6050_init(&dev, &f.bus, 0x69));
    TEST_ASSERT_EQUAL(BUS_ERR_NACK, f.log[0].status);
}

static void test_init_wakes_on_third_try(void)
{
    fake_i2c_fail_at(&f, TXN_WR_PWR(1), BUS_ERR_TIMEOUT);
    fake_i2c_fail_at(&f, TXN_WR_PWR(2), BUS_ERR_TIMEOUT);

    TEST_ASSERT_EQUAL(MPU6050_OK, mpu6050_init(&dev, &f.bus, ADDR));
    TEST_ASSERT_EQUAL_UINT8(3, dev.init_tries);
    TEST_ASSERT_EQUAL(TXN_INIT_MAX, f.count);
}

static void test_init_err_sleep_after_three_tries(void)
{
    for (unsigned k = 1; k <= MPU6050_INIT_ATTEMPTS; k++)
        fake_i2c_fail_at(&f, TXN_WR_PWR(k), BUS_ERR_TIMEOUT);

    TEST_ASSERT_EQUAL(MPU6050_ERR_SLEEP, mpu6050_init(&dev, &f.bus, ADDR));
    TEST_ASSERT_EQUAL_UINT8(MPU6050_INIT_ATTEMPTS, dev.init_tries);
    TEST_ASSERT_EQUAL(TXN_INIT_MAX, f.count);   /* четвертої спроби немає */
    TEST_ASSERT_EQUAL_HEX8(MPU6050_PWR1_SLEEP, f.regs[MPU6050_REG_PWR_MGMT_1]);
}

/* Невдале зворотне читання = датчик ще спить, хоча запис дійшов. */
static void test_init_failed_readback_retries(void)
{
    fake_i2c_fail_at(&f, TXN_RD_PWR(1), BUS_ERR_TIMEOUT);

    TEST_ASSERT_EQUAL(MPU6050_OK, mpu6050_init(&dev, &f.bus, ADDR));
    TEST_ASSERT_EQUAL_UINT8(2, dev.init_tries);
}

static void test_init_failed_readback_pointer_retries(void)
{
    fake_i2c_fail_at(&f, TXN_PTR_PWR(1), BUS_ERR_NACK);

    TEST_ASSERT_EQUAL(MPU6050_OK, mpu6050_init(&dev, &f.bus, ADDR));
    TEST_ASSERT_EQUAL_UINT8(2, dev.init_tries);
}

/* Wokwi C0 (docs/journal.md, проблема 8): запис повертає помилку — рішення
 * приймає лише зворотне читання. Тут байт дійшов, тож датчик прокинувся з 1-ї спроби. */
static void test_init_ignores_write_status_trusts_readback(void)
{
    fake_i2c_init_script(&f);
    expect_wr((const uint8_t[]){ 0x75 }, 1, BUS_OK);
    expect_rd((const uint8_t[]){ 0x68 }, 1, BUS_OK);
    expect_wr((const uint8_t[]){ 0x6B, 0x00 }, 2, BUS_ERR_TIMEOUT);
    expect_wr((const uint8_t[]){ 0x6B }, 1, BUS_OK);
    expect_rd((const uint8_t[]){ 0x00 }, 1, BUS_OK);

    TEST_ASSERT_EQUAL(MPU6050_OK, mpu6050_init(&dev, &f.bus, ADDR));
    TEST_ASSERT_EQUAL_UINT8(1, dev.init_tries);
    fake_i2c_verify(&f);
}

/* ---- mpu6050_read_accel_raw ---- */

static void use_device(void)
{
    dev.bus = &f.bus;
    dev.addr = ADDR;
}

/* Вектор із запису аналізатора: cap accel, docs/lab_2/captures/all.vcd, 16,6338 с
 * (measurements.md, скрін 20-cap-accel): S 68W A 3B A P — S 68R A 00 A 00 A 00 A 00 A 40 A 00 N P. */
static void test_read_accel_capture_vector(void)
{
    mpu6050_accel_raw_t a;

    fake_i2c_init_script(&f);
    use_device();
    expect_wr((const uint8_t[]){ 0x3B }, 1, BUS_OK);
    expect_rd((const uint8_t[]){ 0x00, 0x00, 0x00, 0x00, 0x40, 0x00 }, 6, BUS_OK);

    TEST_ASSERT_EQUAL(MPU6050_OK, mpu6050_read_accel_raw(&dev, &a));
    fake_i2c_verify(&f);
    TEST_ASSERT_EQUAL_INT16(0, a.x);
    TEST_ASSERT_EQUAL_INT16(0, a.y);
    TEST_ASSERT_EQUAL_INT16(16384, a.z);
    TEST_ASSERT_EQUAL_INT32(0, mpu6050_raw_to_mg(a.x));
    TEST_ASSERT_EQUAL_INT32(0, mpu6050_raw_to_mg(a.y));
    TEST_ASSERT_EQUAL_INT32(1000, mpu6050_raw_to_mg(a.z));
}

static void test_read_accel_negative_big_endian(void)
{
    static const uint8_t raw[6] = { 0xFC, 0x18, 0xC0, 0x00, 0x80, 0x00 };
    mpu6050_accel_raw_t a;

    use_device();
    for (unsigned i = 0; i < sizeof raw; i++)
        f.regs[MPU6050_REG_ACCEL_XOUT_H + i] = raw[i];

    TEST_ASSERT_EQUAL(MPU6050_OK, mpu6050_read_accel_raw(&dev, &a));
    TEST_ASSERT_EQUAL_INT16(-1000, a.x);
    TEST_ASSERT_EQUAL_INT16(-16384, a.y);
    TEST_ASSERT_EQUAL_INT16(-32768, a.z);
    TEST_ASSERT_EQUAL_INT32(-61, mpu6050_raw_to_mg(a.x));
    TEST_ASSERT_EQUAL_INT32(-1000, mpu6050_raw_to_mg(a.y));
    TEST_ASSERT_EQUAL_INT32(-2000, mpu6050_raw_to_mg(a.z));
}

static void test_read_accel_max_positive(void)
{
    mpu6050_accel_raw_t a;

    use_device();
    for (unsigned i = 0; i < 6; i += 2) {
        f.regs[MPU6050_REG_ACCEL_XOUT_H + i] = 0x7F;
        f.regs[MPU6050_REG_ACCEL_XOUT_H + i + 1] = 0xFF;
    }

    TEST_ASSERT_EQUAL(MPU6050_OK, mpu6050_read_accel_raw(&dev, &a));
    TEST_ASSERT_EQUAL_INT16(32767, a.x);
    TEST_ASSERT_EQUAL_INT16(32767, a.y);
    TEST_ASSERT_EQUAL_INT16(32767, a.z);
}

static void test_read_accel_bus_error_keeps_output(void)
{
    const mpu6050_accel_raw_t sentinel = { 111, 222, 333 };

    for (size_t n = 1; n <= 2; n++) {
        mpu6050_accel_raw_t a = sentinel;

        setUp();
        use_device();
        fake_i2c_fail_at(&f, n, BUS_ERR_TIMEOUT);
        TEST_ASSERT_EQUAL(MPU6050_ERR_BUS, mpu6050_read_accel_raw(&dev, &a));
        TEST_ASSERT_EQUAL(n, f.count);
        TEST_ASSERT_EQUAL_INT16(sentinel.x, a.x);
        TEST_ASSERT_EQUAL_INT16(sentinel.y, a.y);
        TEST_ASSERT_EQUAL_INT16(sentinel.z, a.z);
    }
}

/* ---- mpu6050_raw_to_mg: raw * 1000 / 16384, ділення з відкиданням дробу до нуля ---- */

static void test_raw_to_mg_bounds(void)
{
    TEST_ASSERT_EQUAL_INT32(0, mpu6050_raw_to_mg(0));
    TEST_ASSERT_EQUAL_INT32(1000, mpu6050_raw_to_mg(16384));
    TEST_ASSERT_EQUAL_INT32(-1000, mpu6050_raw_to_mg(-16384));
    TEST_ASSERT_EQUAL_INT32(500, mpu6050_raw_to_mg(8192));
    TEST_ASSERT_EQUAL_INT32(1999, mpu6050_raw_to_mg(32767));
    TEST_ASSERT_EQUAL_INT32(-2000, mpu6050_raw_to_mg(-32768));
    TEST_ASSERT_EQUAL_INT32(0, mpu6050_raw_to_mg(16));     /* 0,98 mg */
    TEST_ASSERT_EQUAL_INT32(0, mpu6050_raw_to_mg(-16));
    TEST_ASSERT_EQUAL_INT32(1, mpu6050_raw_to_mg(17));     /* 1,04 mg */
    TEST_ASSERT_EQUAL_INT32(-1, mpu6050_raw_to_mg(-17));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_transactions);
    RUN_TEST(test_init_whoami_ok_wakes_on_first_try);
    RUN_TEST(test_init_foreign_id);
    RUN_TEST(test_init_bus_error_on_whoami_write);
    RUN_TEST(test_init_bus_error_on_whoami_read);
    RUN_TEST(test_init_absent_device);
    RUN_TEST(test_init_wakes_on_third_try);
    RUN_TEST(test_init_err_sleep_after_three_tries);
    RUN_TEST(test_init_failed_readback_retries);
    RUN_TEST(test_init_failed_readback_pointer_retries);
    RUN_TEST(test_init_ignores_write_status_trusts_readback);
    RUN_TEST(test_read_accel_capture_vector);
    RUN_TEST(test_read_accel_negative_big_endian);
    RUN_TEST(test_read_accel_max_positive);
    RUN_TEST(test_read_accel_bus_error_keeps_output);
    RUN_TEST(test_raw_to_mg_bounds);
    return UNITY_END();
}
