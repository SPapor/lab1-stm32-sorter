#ifndef UART_HAL_H
#define UART_HAL_H

/* Консоль на USART2 (VCP): у super loop прийом опитуванням, у rtos - перериванням
 * RXNE (обробник - Tasks/sorter_tasks.c, читає тим самим uart_hal_getc); без DMA; передача
 * блокувальна (HAL_UART_Transmit), ≈ 87 мкс на байт при 115200 8N1. */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include "fmt.h"

/* Найдовший рядок разом з "\r\n"; довший текст обрізається */
#define UART_HAL_LINE_MAX   192u

/* Повертає прийнятий символ 0..255 або -1, якщо нового символу немає.
 * Прапорець ORE скидається: інакше після переповнення прийом зупиняється. */
int uart_hal_getc(void);

/* Лічильники для сценарію «потік команд» (docs/perf.md): rx — прийняті байти,
 * ore — скільки разів приймач переповнювався (кожен раз загублено ≥ 1 байт).
 * Точна кількість загублених = надіслано хостом - rx. */
void uart_hal_counters(uint32_t *rx, uint32_t *ore);
void uart_hal_counters_reset(void);

/* Право на UART для кількох задач (rtos): lock/unlock викликаються навколо
 * uart_hal_write і навколо рядка від першого uart_hal_line_*add до
 * uart_hal_line_end. Мають допускати вкладений виклик з тієї самої задачі.
 * NULL (за замовчуванням) - без захисту. uart_hal_lock/unlock - те саме право
 * явно, щоб тримати його довше за один рядок */
void uart_hal_set_lock(void (*lock)(void), void (*unlock)(void));
void uart_hal_lock(void);
void uart_hal_unlock(void);

/* Переривання USART2 на прийом (RXNE; ORE теж його дає) */
void uart_hal_rx_irq_enable(void);

/* Сирі байти, одним викликом HAL_UART_Transmit */
void uart_hal_write(const char *s, size_t n);

/* Рядок збирається в буфері (формат - App/fmt.h, без stdio newlib) і йде в UART
 * одним викликом uart_hal_write у uart_hal_line_end, разом з "\r\n".
 *   uart_hal_line_add("%lu %s ", ms, event);
 *   uart_hal_line_vadd(fmt, ap);
 *   uart_hal_line_end();
 * Буфер один: рядок збирає і закінчує один виконавець (у rtos - під lock). */
void uart_hal_line_add(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void uart_hal_line_vadd(const char *fmt, va_list ap) __attribute__((format(printf, 1, 0)));
/* Те саме з аргументами з масиву (запис черги журналу, App/fmt.h) */
void uart_hal_line_aadd(const char *fmt, const fmt_arg_t *args, unsigned n);
size_t uart_hal_line_len(void);
/* Доповнює рядок пробілами до len символів (без "\r\n") */
void uart_hal_line_pad(size_t len);
void uart_hal_line_end(void);

/* Цілий рядок за форматом, "\r\n" додається */
void uart_hal_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* UART_HAL_H */
