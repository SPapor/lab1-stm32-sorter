#ifndef I2C_BUS_H
#define I2C_BUS_H

/* Абстрактна шина I²C: драйвери пристроїв працюють тільки через цей інтерфейс
 * і нічого не знають про HAL. Адреса — 7-бітна, без зсуву. */

#include <stddef.h>
#include <stdint.h>

typedef enum { BUS_OK = 0, BUS_ERR_NACK, BUS_ERR_TIMEOUT, BUS_ERR_OTHER } bus_status_t;

typedef struct {
    bus_status_t (*write)(void *ctx, uint8_t addr7, const uint8_t *data, size_t len); /* START, addr+W, data, STOP */
    bus_status_t (*read)(void *ctx, uint8_t addr7, uint8_t *data, size_t len);        /* START, addr+R, data, STOP */
    void *ctx;
} i2c_bus_t;

#endif /* I2C_BUS_H */
