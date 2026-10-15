#ifndef I2C_HAL_H
#define I2C_HAL_H

/* Реалізація абстрактної шини i2c_bus_t поверх STM32 HAL. */

#include <stdint.h>
#include "stm32c0xx_hal.h"
#include "i2c_bus.h"

#define I2C_HAL_TIMEOUT_MS 100u

typedef struct {
    I2C_HandleTypeDef *h;
    void (*reinit)(void);     /* повторна ініціалізація периферії (MX_I2Cx_Init) */
    uint32_t recover_count;   /* скільки разів шину відновлювали після помилки */
} i2c_hal_ctx_t;

/* Заповнює bus функціями write/read, що працюють через хендл h. */
void i2c_hal_bus_init(i2c_bus_t *bus, i2c_hal_ctx_t *ctx, I2C_HandleTypeDef *h, void (*reinit)(void));

#endif /* I2C_HAL_H */
