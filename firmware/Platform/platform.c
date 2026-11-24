#include "platform.h"
#include "stm32c0xx_hal.h"
#include "board.h"

void platform_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

#define I2C_CLEAR_PULSES  9u
#define I2C_CLEAR_HALF    40u

static void i2c_clear_step(uint32_t bsrr)
{
    for (volatile uint32_t n = 0; n < I2C_CLEAR_HALF; n++) {
    }
    BOARD_I2C_PORT->BSRR = bsrr;
}

void platform_i2c_reinit(void)
{
    const uint32_t mode_mask = (3u << (2u * __builtin_ctz(BOARD_I2C_SCL_PIN))) |
                               (3u << (2u * __builtin_ctz(BOARD_I2C_SDA_PIN)));
    const uint32_t mode_out = (1u << (2u * __builtin_ctz(BOARD_I2C_SCL_PIN))) |
                              (1u << (2u * __builtin_ctz(BOARD_I2C_SDA_PIN)));

    BOARD_I2C_PORT->BSRR = BOARD_I2C_SCL_PIN | BOARD_I2C_SDA_PIN;
    BOARD_I2C_PORT->OTYPER |= BOARD_I2C_SCL_PIN | BOARD_I2C_SDA_PIN;
    BOARD_I2C_PORT->PUPDR = (BOARD_I2C_PORT->PUPDR & ~mode_mask) | mode_out;
    BOARD_I2C_PORT->MODER = (BOARD_I2C_PORT->MODER & ~mode_mask) | mode_out;
    for (uint32_t i = 0; i < I2C_CLEAR_PULSES && !(BOARD_I2C_PORT->IDR & BOARD_I2C_SDA_PIN); i++) {
        i2c_clear_step((uint32_t)BOARD_I2C_SCL_PIN << 16);
        i2c_clear_step(BOARD_I2C_SCL_PIN);
    }
    i2c_clear_step((uint32_t)BOARD_I2C_SCL_PIN << 16);
    i2c_clear_step((uint32_t)BOARD_I2C_SDA_PIN << 16);
    i2c_clear_step(BOARD_I2C_SCL_PIN);
    i2c_clear_step(BOARD_I2C_SDA_PIN);
    MX_I2C1_Init();
}
