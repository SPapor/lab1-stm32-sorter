#ifndef PERF_TICK_H
#define PERF_TICK_H

/* Лічильник тіків для perf_now (Platform/perf_hal.c), ARCH=rtos. SysTick — тік
 * ядра, а uwTick HAL тепер рахує TIM17, у якого з SysTick немає спільної фази.
 * Тому номер тіку — власний лічильник, який збільшує SysTick_Handler
 * (Platform/rtos/rtos_hal.c) на кожне переривання, тобто той самий тік, від якого
 * SysTick->VAL рахує такти. Рахує і до старту ядра, і коли планувальник
 * призупинений (docs/rtos-design.md, розділ 3). */

#include <stdint.h>

extern volatile uint32_t rtos_hal_ticks;

#define perf_tick_count() (rtos_hal_ticks)

#endif /* PERF_TICK_H */
