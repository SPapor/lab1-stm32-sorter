#ifndef FMT_H
#define FMT_H

/* Форматування рядка в буфер без stdio newlib (docs/rtos-design.md, розділ 4).
 * Лише те, що є в рядках прошивки:
 *   %d %i %u %x %X %c %s %%, модифікатор l (long), ширина, прапорці '0' і '-'.
 * Точності ('.'), %llu і решти специфікаторів немає: невідомий специфікатор
 * виводиться як є, разом з '%'.
 * Семантика як у vsnprintf: у dst не більше cap - 1 символів і завжди '\0'
 * (якщо cap > 0); повертає довжину, яка вийшла б без обрізання. */

#include <stdarg.h>
#include <stddef.h>

size_t fmt_vbuf(char *dst, size_t cap, const char *fmt, va_list ap);
size_t fmt_buf(char *dst, size_t cap, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

#endif /* FMT_H */
