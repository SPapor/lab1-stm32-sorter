#include "servo_hal.h"
#include "board.h"

/* TIM3: 48 МГц / (PSC + 1 = 48) = 1 МГц, ARR + 1 = 20000 → 50 Гц (pinout.md §3.3).
 * Один тік лічильника = 1 мкс, тому значення CCR дорівнює ширині імпульсу в мкс. */

void servo_hal_start(void)
{
    HAL_TIM_PWM_Start(BOARD_SERVO_TIM, BOARD_SERVO_CH);
}

void servo_hal_set_pulse_us(uint16_t us)
{
    __HAL_TIM_SET_COMPARE(BOARD_SERVO_TIM, BOARD_SERVO_CH, us);
}

void servo_hal_set_angle(uint8_t deg)
{
    if (deg > SERVO_HAL_MAX_DEG)
        deg = SERVO_HAL_MAX_DEG;

    uint32_t us = BOARD_SERVO_MIN_US +
                  (uint32_t)deg * (BOARD_SERVO_MAX_US - BOARD_SERVO_MIN_US) / SERVO_HAL_MAX_DEG;
    servo_hal_set_pulse_us((uint16_t)us);
}

uint16_t servo_hal_get_pulse_us(void)
{
    return (uint16_t)__HAL_TIM_GET_COMPARE(BOARD_SERVO_TIM, BOARD_SERVO_CH);
}
