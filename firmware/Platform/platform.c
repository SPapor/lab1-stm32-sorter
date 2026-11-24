#include "platform.h"
#include "stm32c0xx_hal.h"
#include "board.h"

void platform_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

#define I2C_CLEAR_PULSES  9u
#define I2C_CLEAR_HALF    40u

static void i2c_half_period(void)
{
    for (volatile uint32_t n = 0; n < I2C_CLEAR_HALF; n++) {
    }
}

static void i2c_line(uint16_t pin, GPIO_PinState v)
{
    HAL_GPIO_WritePin(BOARD_I2C_PORT, pin, v);
    i2c_half_period();
}

void platform_i2c_reinit(void)
{
    GPIO_InitTypeDef g = {0};

    HAL_GPIO_WritePin(BOARD_I2C_PORT, BOARD_I2C_SCL_PIN | BOARD_I2C_SDA_PIN, GPIO_PIN_SET);
    g.Pin = BOARD_I2C_SCL_PIN | BOARD_I2C_SDA_PIN;
    g.Mode = GPIO_MODE_OUTPUT_OD;
    g.Pull = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BOARD_I2C_PORT, &g);
    i2c_half_period();
    for (uint32_t i = 0; i < I2C_CLEAR_PULSES; i++) {
        i2c_line(BOARD_I2C_SCL_PIN, GPIO_PIN_RESET);
        i2c_line(BOARD_I2C_SCL_PIN, GPIO_PIN_SET);
    }
    i2c_line(BOARD_I2C_SDA_PIN, GPIO_PIN_RESET);
    i2c_line(BOARD_I2C_SDA_PIN, GPIO_PIN_SET);
    MX_I2C1_Init();
}
