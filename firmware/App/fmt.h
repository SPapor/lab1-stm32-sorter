#ifndef FMT_H
#define FMT_H

/* Форматування рядка в буфер без stdio newlib (docs/rtos-design.md, розділ 4).
 * Лише те, що є в рядках прошивки:
 *   %d %i %u %x %X %c %s %%, модифікатор l (long), ширина, прапорці '0' і '-'.
 * Точності ('.'), %llu і решти специфікаторів немає: невідомий специфікатор
 * виводиться як є, разом з '%'.
 * Атрибут format перевіряє типи аргументів, а що специфікатор узагалі є в цьому
 * списку - tools/tests/test_fmt_specs.py (make tools-test).
 * Семантика як у vsnprintf: у dst не більше cap - 1 символів і завжди '\0'
 * (якщо cap > 0); повертає довжину, яка вийшла б без обрізання. */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

size_t fmt_vbuf(char *dst, size_t cap, const char *fmt, va_list ap)
    __attribute__((format(printf, 3, 0)));
size_t fmt_buf(char *dst, size_t cap, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

/* Аргумент, збережений для форматування пізніше (черга журналу в rtos): ціле
 * будь-якого знаку чи вказівник на рядок, що живе довше за запис (літерал) */
typedef uintptr_t fmt_arg_t;

/* Бере з ap усі аргументи, яких потребує fmt, і пише перші max з них в out.
 * Повертає, скільки їх потрібно (більше за max - решта загублена). */
unsigned fmt_vargs(const char *fmt, va_list ap, fmt_arg_t *out, unsigned max);

/* Як fmt_vbuf, але аргументи - з масиву (n штук, з fmt_vargs). Відсутній
 * аргумент - 0, для %s - "(null)" */
size_t fmt_abuf(char *dst, size_t cap, const char *fmt, const fmt_arg_t *args, unsigned n);

#endif /* FMT_H */
