#ifndef PERF_H
#define PERF_H

/* Арифметика вимірювань (docs/perf.md, методичка 5.1). Лише логіка: такти
 * приходять ззовні (Platform/perf_hal), тож модуль збирається й тестується на хості.
 *
 * Лічильник тактів 32-бітний і при 48 МГц переповнюється раз на ≈ 89,5 с.
 * Інтервал — беззнакова різниця end - start: вона правильна і через одне
 * переповнення, поки сам інтервал коротший за 2^32 тактів. Довший інтервал
 * «загортається» і стає меншим на 2^32 — такі тут не міряються. */

#include <stdint.h>

/* Статистика одного вимірюваного інтервалу, як у методичці */
typedef struct {
    const char *name;
    uint32_t count;
    uint32_t min, max;   /* такти */
    uint64_t sum;        /* такти; 64 біти — не переповнюється */
} perf_stat_t;

/* Тактів від start до end з урахуванням переповнення 32 біт. */
uint32_t perf_elapsed(uint32_t start, uint32_t end);

/* Такти → мікросекунди з округленням до найближчого; cyc_per_us > 0. */
uint32_t perf_cyc_to_us(uint32_t cyc, uint32_t cyc_per_us);

/* Один вимір cyc тактів. */
void perf_add(perf_stat_t *s, uint32_t cyc);

/* Обнуляє лічильники, назва лишається. */
void perf_stat_reset(perf_stat_t *s);

/* Середнє в тактах з округленням; 0, якщо вимірів не було. */
uint32_t perf_avg(const perf_stat_t *s);

/* num / den і остача без 64-бітного ділення з libgcc (__aeabi_uldivmod +
 * __udivmoddi4 ≈ 470 Б флешу): двійкове ділення зсувом. Частка має
 * вміститися в 32 біти (інакше повертається 0xFFFFFFFF); den > 0; rem може бути NULL. */
uint32_t perf_udiv64(uint64_t num, uint32_t den, uint32_t *rem);

#endif /* PERF_H */
