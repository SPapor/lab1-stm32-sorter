#ifndef ADC_HAL_H
#define ADC_HAL_H

/* Одиночне перетворення ADC1 по одному каналу поверх STM32 HAL. */

#include <stdint.h>

#define ADC_HAL_TIMEOUT_MS 10u
#define ADC_HAL_MAX_RAW    4095u   /* повна шкала 12-бітного АЦП */

/* Перетворює один канал (BOARD_ADC_CH_*) і повертає сирий код 0..ADC_HAL_MAX_RAW.
 * На час перетворення ставить маркер ADC_MARK (PA15) у 1. */
uint16_t adc_hal_read(uint32_t channel);

#endif /* ADC_HAL_H */
