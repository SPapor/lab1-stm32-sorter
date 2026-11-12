#ifndef I2C_HAL_H
#define I2C_HAL_H

/* Реалізація абстрактної шини i2c_bus_t поверх STM32 HAL. */

#include <stdint.h>
#include "stm32c0xx_hal.h"
#include "i2c_bus.h"

#define I2C_HAL_TIMEOUT_MS 5u     /* найдовша робоча транзакція (6 байтів MPU) ~0,7 мс */
#define I2C_HAL_MAX_TRIES  3u     /* FR-09: транзакція + до 2 повторів, після кожної помилки — відновлення */

typedef struct {
    I2C_HandleTypeDef *h;
    void (*reinit)(void);     /* повторна ініціалізація периферії (MX_I2Cx_Init) */
    uint8_t max_tries;        /* спроб на одну транзакцію (I2C_HAL_MAX_TRIES) */
    uint32_t recover_count;   /* скільки разів шину відновлювали після помилки */
    const char *last_where;   /* остання помилка: "wr" або "rd" */
    uint8_t last_addr;        /*   7-бітна адреса */
    uint8_t last_try;         /*   номер спроби 1..max_tries */
    uint32_t last_err;        /*   ErrorCode HAL */
    uint32_t last_isr;        /*   регістр ISR до відновлення */
    uint8_t defer_log;        /* 1 — рядок I2C_RECOVER відкладається */
    uint8_t log_pending;      /* є відкладений рядок I2C_RECOVER */
} i2c_hal_ctx_t;

/* Заповнює bus функціями write/read, що працюють через хендл h.
 * FR-09: після помилки транзакції периферія переініціалізується (HAL_I2C_DeInit +
 * reinit), у UART іде рядок "<мс> I2C_RECOVER try=n/N ...", і транзакція
 * повторюється — усього до max_tries спроб; далі помилка повертається драйверу.
 * Кожна спроба блокує до I2C_HAL_TIMEOUT_MS, а після NACK HAL ще генерує STOP і
 * чекає STOPF до I2C_TIMEOUT_STOPF (25 мс, константа HAL) - у Wokwi це ~34 мс на спробу. */
void i2c_hal_bus_init(i2c_bus_t *bus, i2c_hal_ctx_t *ctx, I2C_HandleTypeDef *h, void (*reinit)(void));

/* Кількість спроб на транзакцію (0 вважається 1). Команди cap … ставлять 1:
 * на записі аналізатора має бути рівно одна транзакція. */
void i2c_hal_set_tries(i2c_hal_ctx_t *ctx, uint8_t tries);

/* on = 1: рядок I2C_RECOVER не друкується під час транзакції, а запам'ятовується;
 * on = 0: друк знову негайний, а відкладений рядок (якщо був) друкується зараз.
 * Потрібно, щоб у записі аналізатора UART не накладався на транзакцію I²C. */
void i2c_hal_defer_log(i2c_hal_ctx_t *ctx, uint8_t on);

#endif /* I2C_HAL_H */
