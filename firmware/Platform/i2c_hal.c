#include "i2c_hal.h"
#include <stdio.h>

static bus_status_t map_status(HAL_StatusTypeDef s, uint32_t err)
{
    if (s == HAL_OK)
        return BUS_OK;
    if (err & HAL_I2C_ERROR_AF)
        return BUS_ERR_NACK;
    if (s == HAL_TIMEOUT || (err & HAL_I2C_ERROR_TIMEOUT))
        return BUS_ERR_TIMEOUT;
    return BUS_ERR_OTHER;
}

static void print_recover(const i2c_hal_ctx_t *c)
{
    printf("I2C_RECOVER where=%s err=0x%02lX isr=0x%08lX\r\n",
           c->last_where, (unsigned long)c->last_err, (unsigned long)c->last_isr);
}

/* Модель I2C у Wokwi C0: при записі HAL не бачить TXIS для другого байта
 * (SCL лишається низьким, err=TIMEOUT, BUSY залипає). Після будь-якої помилки
 * повністю переініціалізуємо периферію, інакше наступні транзакції не пройдуть. */
static void recover(i2c_hal_ctx_t *c, const char *where)
{
    c->last_where = where;
    c->last_err = c->h->ErrorCode;
    c->last_isr = c->h->Instance->ISR;
    if (c->defer_log)
        c->log_pending = 1;
    else
        print_recover(c);

    HAL_I2C_DeInit(c->h);
    if (c->reinit)
        c->reinit();
    c->recover_count++;
}

/* Статус беремо до recover: HAL_I2C_DeInit скидає ErrorCode. */
static bus_status_t finish(i2c_hal_ctx_t *c, HAL_StatusTypeDef s, const char *where)
{
    bus_status_t st = map_status(s, c->h->ErrorCode);

    if (st != BUS_OK)
        recover(c, where);
    return st;
}

static bus_status_t hal_write(void *ctx, uint8_t addr7, const uint8_t *data, size_t len)
{
    i2c_hal_ctx_t *c = ctx;

    if (len > UINT16_MAX)
        return BUS_ERR_OTHER;
    HAL_StatusTypeDef s = HAL_I2C_Master_Transmit(c->h, (uint16_t)(addr7 << 1),
                                                  (uint8_t *)data, (uint16_t)len,
                                                  I2C_HAL_TIMEOUT_MS);
    return finish(c, s, "wr");
}

static bus_status_t hal_read(void *ctx, uint8_t addr7, uint8_t *data, size_t len)
{
    i2c_hal_ctx_t *c = ctx;

    if (len > UINT16_MAX)
        return BUS_ERR_OTHER;
    HAL_StatusTypeDef s = HAL_I2C_Master_Receive(c->h, (uint16_t)(addr7 << 1),
                                                 data, (uint16_t)len,
                                                 I2C_HAL_TIMEOUT_MS);
    return finish(c, s, "rd");
}

void i2c_hal_bus_init(i2c_bus_t *bus, i2c_hal_ctx_t *ctx, I2C_HandleTypeDef *h, void (*reinit)(void))
{
    ctx->h = h;
    ctx->reinit = reinit;
    ctx->recover_count = 0;
    ctx->last_where = "";
    ctx->last_err = 0;
    ctx->last_isr = 0;
    ctx->defer_log = 0;
    ctx->log_pending = 0;

    bus->write = hal_write;
    bus->read = hal_read;
    bus->ctx = ctx;
}

void i2c_hal_defer_log(i2c_hal_ctx_t *ctx, uint8_t on)
{
    ctx->defer_log = on;
    if (!on && ctx->log_pending) {
        print_recover(ctx);
        ctx->log_pending = 0;
    }
}
