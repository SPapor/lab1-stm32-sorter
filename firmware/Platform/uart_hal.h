#ifndef UART_HAL_H
#define UART_HAL_H

/* Прийом з USART2 (VCP) опитуванням, без переривань і DMA. */

#include <stdint.h>

/* Повертає прийнятий символ 0..255 або -1, якщо нового символу немає.
 * Прапорець ORE скидається: інакше після переповнення прийом зупиняється. */
int uart_hal_getc(void);

/* Лічильники для сценарію «потік команд» (docs/perf.md): rx — прийняті байти,
 * ore — скільки разів приймач переповнювався (кожен раз загублено ≥ 1 байт).
 * Точна кількість загублених = надіслано хостом - rx. */
void uart_hal_counters(uint32_t *rx, uint32_t *ore);
void uart_hal_counters_reset(void);

#endif /* UART_HAL_H */
