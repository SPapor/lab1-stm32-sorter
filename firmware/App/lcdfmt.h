#ifndef LCDFMT_H
#define LCDFMT_H

/* Рядки для LCD без snprintf (docs/perf.md, розділ 8): snprintf тягне з newlib-nano
 * окрему рядкову копію printf (~1,3 КБ флешу), а для LCD потрібні лише текст і
 * беззнакові числа з вирівнюванням уліво. Результат той самий, що в snprintf з
 * "%-<w>s" і "%-<w>lu": поле доповнюється пробілами до width, довше — не обрізається,
 * а весь рядок обрізається до cap - 1 символів і завжди закінчується '\0'.
 *
 *   size_t n = lcdfmt_str(buf, 0, sizeof buf, "READY", 8);
 *   n = lcdfmt_str(buf, n, sizeof buf, " n=", 0);
 *   n = lcdfmt_u32(buf, n, sizeof buf, total, 5);       // як "%-8s n=%-5lu"
 */

#include <stddef.h>
#include <stdint.h>

/* Дописує s з позиції pos, доповнює до width; повертає нову позицію (може бути
 * ≥ cap — як «скільки вийшло б» у snprintf). cap > 0. */
size_t lcdfmt_str(char *dst, size_t pos, size_t cap, const char *s, unsigned width);

/* Те саме для десяткового v. */
size_t lcdfmt_u32(char *dst, size_t pos, size_t cap, uint32_t v, unsigned width);

#endif /* LCDFMT_H */
