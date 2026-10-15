#include "mpu6050.h"

/* Читання регістрів: окремий запис адреси регістра і окреме читання, зі STOP
 * між ними. Repeated START (HAL_I2C_Mem_Read) у Wokwi C0 не працює
 * (docs/journal.md, проблема 7). */
static mpu6050_status_t read_regs(const mpu6050_t *dev, uint8_t reg, uint8_t *buf, size_t len)
{
    const i2c_bus_t *bus = dev->bus;

    if (bus->write(bus->ctx, dev->addr, &reg, 1) != BUS_OK)
        return MPU6050_ERR_BUS;
    if (bus->read(bus->ctx, dev->addr, buf, len) != BUS_OK)
        return MPU6050_ERR_BUS;
    return MPU6050_OK;
}

static bus_status_t write_reg(const mpu6050_t *dev, uint8_t reg, uint8_t val)
{
    const uint8_t b[2] = { reg, val };

    return dev->bus->write(dev->bus->ctx, dev->addr, b, sizeof b);
}

/* Два байти big-endian → знакове 16-бітне число. */
static int16_t be16(const uint8_t *p)
{
    return (int16_t)((uint16_t)p[0] << 8 | p[1]);
}

mpu6050_status_t mpu6050_init(mpu6050_t *dev, const i2c_bus_t *bus, uint8_t addr)
{
    dev->bus = bus;
    dev->addr = addr;
    dev->who_am_i = 0;
    dev->init_tries = 0;

    mpu6050_status_t st = read_regs(dev, MPU6050_REG_WHO_AM_I, &dev->who_am_i, 1);
    if (st != MPU6050_OK)
        return st;
    if (dev->who_am_i != MPU6050_WHO_AM_I_VALUE)
        return MPU6050_ERR_ID;

    /* Статус запису ігноруємо: у симуляторі HAL може повернути помилку, хоча
     * байт дійшов. Критерій успіху — зворотне читання PWR_MGMT_1. */
    for (uint8_t i = 1; i <= MPU6050_INIT_ATTEMPTS; i++) {
        dev->init_tries = i;
        (void)write_reg(dev, MPU6050_REG_PWR_MGMT_1, 0x00u);

        uint8_t pwr1 = MPU6050_PWR1_SLEEP;   /* невдале читання = ще спить */
        if (read_regs(dev, MPU6050_REG_PWR_MGMT_1, &pwr1, 1) == MPU6050_OK &&
            (pwr1 & MPU6050_PWR1_SLEEP) == 0)
            return MPU6050_OK;
    }
    return MPU6050_ERR_SLEEP;
}

mpu6050_status_t mpu6050_read_accel_raw(const mpu6050_t *dev, mpu6050_accel_raw_t *out)
{
    uint8_t b[6];

    mpu6050_status_t st = read_regs(dev, MPU6050_REG_ACCEL_XOUT_H, b, sizeof b);
    if (st != MPU6050_OK)
        return st;

    out->x = be16(&b[0]);
    out->y = be16(&b[2]);
    out->z = be16(&b[4]);
    return MPU6050_OK;
}

int32_t mpu6050_raw_to_mg(int16_t raw)
{
    return (int32_t)raw * 1000 / MPU6050_LSB_PER_G;
}
