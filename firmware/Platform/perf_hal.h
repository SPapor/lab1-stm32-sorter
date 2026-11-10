#ifndef PERF_HAL_H
#define PERF_HAL_H

/* Лічильник часу для вимірювань (docs/perf.md, методичка 5.1). У Cortex-M0+
 * немає DWT CYCCNT, тому такти складаються з лічильника тіків HAL і SysTick:
 *
 *   perf_now() = uwTick * (SystemCoreClock / 1000) + (SysTick->LOAD - SysTick->VAL)
 *
 * При 48 МГц 1 такт ≈ 20,8 нс. 32 біти переповнюються раз на
 * 2^32 / 48e6 ≈ 89,5 с; беззнакова різниця end - start правильна для
 * інтервалів, коротших за це. */

#include <stdint.h>

/* Запам'ятовує тактів на тік (SystemCoreClock / 1000) і на мікросекунду.
 * Викликати після SystemClock_Config, коли SysTick уже переналаштований. */
void perf_hal_init(void);

/* Такти CPU, 32 біти. Читання в короткій критичній секції (PRIMASK
 * відновлюється як був), тож можна викликати і з переривання. */
uint32_t perf_now(void);

/* Мікросекунди від старту, 64 біти: uwTick * 1000 + частка мілісекунди.
 * Для міток EVT: не переповнюється (uwTick сам переповниться через 49,7 доби). */
uint64_t perf_now_us64(void);

/* Тактова частота CPU, Гц (SystemCoreClock). */
uint32_t perf_hal_cpu_hz(void);

#endif /* PERF_HAL_H */
