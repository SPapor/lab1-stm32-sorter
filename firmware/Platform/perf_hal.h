#ifndef PERF_HAL_H
#define PERF_HAL_H

/* Лічильник часу для вимірювань (docs/perf.md, методичка 5.1). У Cortex-M0+
 * немає DWT CYCCNT, тому такти складаються з лічильника тіків і SysTick:
 *
 *   perf_now() = tick * (SysTick->LOAD + 1) + (SysTick->LOAD - SysTick->VAL)
 *
 * tick — perf_tick_count() з Platform/<arch>/perf_tick.h: uwTick HAL у super loop
 * (тік 1 мс), лічильник із SysTick_Handler у FreeRTOS (тік 1/TICK_HZ).
 * При 48 МГц 1 такт ≈ 20,8 нс. 32 біти переповнюються раз на
 * 2^32 / 48e6 ≈ 89,5 с; беззнакова різниця end - start правильна для
 * інтервалів, коротших за це. */

#include <stdint.h>

/* Запам'ятовує тактів на тік (SysTick->LOAD + 1) і на мікросекунду, і
 * налаштовує службові піни PB13, PB14 як виходи (не з CubeMX, див. board.h).
 * Викликати після SystemClock_Config, коли SysTick уже переналаштований. */
void perf_hal_init(void);

/* Такти CPU, 32 біти. Читання в короткій критичній секції (PRIMASK
 * відновлюється як був), тож можна викликати і з переривання. */
uint32_t perf_now(void);

/* Такти від старту, 64 біти: лічильник run-time stats FreeRTOS (не переповнюється). */
uint64_t perf_now_cyc64(void);

/* Мікросекунди від старту, 64 біти: tick * (мкс у тіку) + частка тіку.
 * Для міток EVT: не переповнюється (сам 32-бітний tick при 1 кГц переповниться
 * через 49,7 доби). */
uint64_t perf_now_us64(void);

/* Тактова частота CPU, Гц (SystemCoreClock). */
uint32_t perf_hal_cpu_hz(void);

#endif /* PERF_HAL_H */
