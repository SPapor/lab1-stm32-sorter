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
    const char *last_where;   /* остання помилка: "wr" або "rd" */
    uint32_t last_err;        /*   ErrorCode HAL */
    uint32_t last_isr;        /*   регістр ISR до відновлення */
    uint8_t defer_log;        /* 1 — рядок I2C_RECOVER відкладається */
    uint8_t log_pending;      /* є відкладений рядок I2C_RECOVER */
} i2c_hal_ctx_t;

/* Заповнює bus функціями write/read, що працюють через хендл h. */
void i2c_hal_bus_init(i2c_bus_t *bus, i2c_hal_ctx_t *ctx, I2C_HandleTypeDef *h, void (*reinit)(void));

/* on = 1: рядок I2C_RECOVER не друкується під час транзакції, а запам'ятовується;
 * on = 0: друк знову негайний, а відкладений рядок (якщо був) друкується зараз.
 * Потрібно, щоб у записі аналізатора UART не накладався на транзакцію I²C. */
void i2c_hal_defer_log(i2c_hal_ctx_t *ctx, uint8_t on);

#endif /* I2C_HAL_H */
