#ifndef SERVO_HAL_H
#define SERVO_HAL_H

/* Серво на TIM3_CH1 поверх STM32 HAL. */

#include <stdint.h>

#define SERVO_HAL_MAX_DEG 180u

/* Запускає ШИМ 50 Гц на каналі серво. */
void servo_hal_start(void);

/* Ширина імпульсу в мкс напряму в CCR, без обмеження BOARD_SERVO_MIN/MAX_US —
 * щоб можна було виміряти реальні межі серво (pinout.md §3.3). */
void servo_hal_set_pulse_us(uint16_t us);

/* Кут 0..180° лінійно в BOARD_SERVO_MIN_US..BOARD_SERVO_MAX_US; більший кут
 * обмежується до 180°. */
void servo_hal_set_angle(uint8_t deg);

/* Зворотне читання CCR — фактична ширина імпульсу в мкс. */
uint16_t servo_hal_get_pulse_us(void);

#endif /* SERVO_HAL_H */
