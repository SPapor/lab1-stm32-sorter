#ifndef MEMMARK_H
#define MEMMARK_H

/* Stack painting (методичка 5.4, docs/perf.md): на старті резерв стека
 * заповнюємо шаблоном, а пізніше шукаємо найнижче слово, де шаблон зіпсовано.
 * Стек росте вниз від top, тож усе вище за це слово колись було зайняте —
 * це найбільша глибина стека (high-water mark). Лише логіка над масивом слів:
 * тестується на хості. */

#include <stddef.h>
#include <stdint.h>

#define MEMMARK_PATTERN 0xDEADBEEFu

/* Заповнює [lo, hi) шаблоном; hi ≤ lo — нічого. */
void memmark_paint(uint32_t *lo, uint32_t *hi, uint32_t pattern);

/* Скільки байтів стека використано: від top до найнижчого слова в [bottom, top),
 * де шаблон зіпсовано. Якщо результат дорівнює (top - bottom) * 4, шаблон
 * зіпсовано до самого низу — стек, найімовірніше, вийшов за межі резерву. */
uint32_t memmark_used_bytes(const uint32_t *bottom, const uint32_t *top, uint32_t pattern);

#endif /* MEMMARK_H */
