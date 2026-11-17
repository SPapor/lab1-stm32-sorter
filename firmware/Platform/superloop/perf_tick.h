#ifndef PERF_TICK_H
#define PERF_TICK_H

/* Лічильник тіків для perf_now (Platform/perf_hal.c), super loop: uwTick HAL.
 * Його збільшує SysTick_Handler, тобто те саме переривання, від якого SysTick->VAL
 * рахує такти в тіку: номер тіку й такти — одна узгоджена пара. */

#include "stm32c0xx_hal.h"

#define perf_tick_count() (uwTick)

#endif /* PERF_TICK_H */
