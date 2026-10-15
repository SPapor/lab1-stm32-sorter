#ifndef MPU6050_H
#define MPU6050_H

/* Драйвер акселерометра MPU-6050 поверх абстрактної шини i2c_bus_t. */

#include <stdint.h>
#include "i2c_bus.h"

#define MPU6050_ADDR_DEFAULT     0x68u   /* AD0 = 0 */
#define MPU6050_WHO_AM_I_VALUE   0x68u
#define MPU6050_INIT_ATTEMPTS    3u

#define MPU6050_REG_ACCEL_XOUT_H 0x3Bu
#define MPU6050_REG_PWR_MGMT_1   0x6Bu
#define MPU6050_REG_WHO_AM_I     0x75u

#define MPU6050_PWR1_SLEEP       0x40u   /* біт SLEEP, встановлений після скидання */
#define MPU6050_LSB_PER_G        16384   /* ±2 g */

typedef enum { MPU6050_OK = 0, MPU6050_ERR_BUS, MPU6050_ERR_ID, MPU6050_ERR_SLEEP } mpu6050_status_t;

typedef struct {
    const i2c_bus_t *bus;
    uint8_t addr;        /* 7-бітна адреса */
    uint8_t who_am_i;    /* прочитане значення WHO_AM_I */
    uint8_t init_tries;  /* номер спроби, на якій знято SLEEP (або остання) */
} mpu6050_t;

typedef struct { int16_t x, y, z; } mpu6050_accel_raw_t;

/* Перевіряє WHO_AM_I і виводить датчик зі сну (до MPU6050_INIT_ATTEMPTS спроб). */
mpu6050_status_t mpu6050_init(mpu6050_t *dev, const i2c_bus_t *bus, uint8_t addr);

/* Читає сирі значення прискорення X, Y, Z одним пакетом з 6 байтів. */
mpu6050_status_t mpu6050_read_accel_raw(const mpu6050_t *dev, mpu6050_accel_raw_t *out);

/* Переводить сире значення в мілі-g для діапазону ±2 g: raw*1000/16384. */
int32_t mpu6050_raw_to_mg(int16_t raw);

#endif /* MPU6050_H */
