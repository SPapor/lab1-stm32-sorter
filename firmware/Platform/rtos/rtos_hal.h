#ifndef RTOS_HAL_H
#define RTOS_HAL_H

/* Платформна частина FreeRTOS (лише ARCH=rtos): база часу HAL на TIM17, тік ядра
 * на SysTick з самого HAL_Init, лічильник тіків для perf_now, хуки ядра і
 * діагностика (docs/rtos-design.md, розділ 3). Заголовок без FreeRTOS:
 * його включає main.c. */

#include <stdint.h>

/* Запускає ядро; з цього моменту тіки SysTick ідуть і порту. Не повертається.
 * Викликати після створення задач: з першого виклику API до старту ядра
 * переривання вимкнені. */
void rtos_hal_start_scheduler(void) __attribute__((noreturn));

/* Версія ядра (tskKERNEL_VERSION_NUMBER) і configTOTAL_HEAP_SIZE для рядка BOOT */
const char *rtos_hal_kernel_version(void);
uint32_t rtos_hal_heap_total(void);

/* Останній configASSERT до скидання: 1 і номер рядка й адреса виклику, якщо запис
 * у .noinit цілий; запис після цього стирається. 0 — асерту не було. */
int rtos_hal_assert_take(uint32_t *line, uint32_t *pc);

#endif /* RTOS_HAL_H */
