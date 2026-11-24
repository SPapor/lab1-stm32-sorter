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

/* Старт ядра для рядка STARTUP (лише PERF=1), мкс від скидання за perf_now:
 * api_us - перед першим викликом API ядра (rtos_hal_mark_api), start_us - коли
 * ядро запускає першу задачу (vPortSetupTimerInterrupt). Між ними переривання
 * вимкнені (docs/rtos-design.md, розділ 3) */
void rtos_hal_mark_api(void);
void rtos_hal_startup_us(uint32_t *api_us, uint32_t *start_us);

void rtos_hal_trace_task(unsigned slot, const void *tcb, uint16_t pin);

/* Версія ядра (tskKERNEL_VERSION_NUMBER) і configTOTAL_HEAP_SIZE для рядка BOOT */
const char *rtos_hal_kernel_version(void);
uint32_t rtos_hal_heap_total(void);

/* Останній configASSERT до скидання: 1 і номер рядка й адреса виклику, якщо запис
 * у .noinit цілий; запис після цього стирається. 0 — асерту не було. */
int rtos_hal_assert_take(uint32_t *line, uint32_t *pc);

/* На вході кожного обробника переривання NVIC у rtos: 1 - відкрита критична
 * секція ядра, обробник має одразу повернутися, нічого не скидаючи (збірка
 * SIM_CRIT=1 відкладе переривання до кінця секції, Platform/rtos/port_sim);
 * 0 - обробляти. З SIM_CRIT=0 завжди 0 */
int rtos_hal_irq_defer(int irqn);

/* Частота тактування таймерів APB (TIM14, TIM17), Гц */
uint32_t rtos_hal_timer_clk_hz(void);

/* Сирий вивід у USART2 опитуванням TXE, без HAL і без буфера рядка: для FATAL і
 * діагностики з переривань. Переплітається з рядком, який саме передає задача */
void rtos_hal_raw_puts(const char *s);
void rtos_hal_raw_putu(uint32_t v, unsigned base);

/* HardFault (stm32c0xx_it.c): рядок FATAL hardfault exc=<EXC_RETURN> pc=<адреса>
 * і зупинка, як у configASSERT. pc - зі стека задачі, якщо PSP вказує в RAM;
 * якщо збій стався на MSP (переривання або код до старту ядра), pc=0: обробник
 * CubeMX - функція C, і зсув її кадру на MSP невідомий. */
void rtos_hal_hardfault(uint32_t exc_return) __attribute__((noreturn));

void rtos_hal_fatal_create(const char *name) __attribute__((noreturn));

void rtos_hal_fatal_qp(const char *module, int32_t id) __attribute__((noreturn));

#endif /* RTOS_HAL_H */
