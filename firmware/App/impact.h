#ifndef IMPACT_H
#define IMPACT_H

/* FR-04: удар — модуль прискорення понад 2 g. Рішення — порівнянням квадратів,
 * без кореня: |a|² > (2000 mg)².
 *
 * MPU-6050 працює в діапазоні ±2 g (ACCEL_CONFIG не записати: 2-байтові записи
 * у Wokwi обриваються, docs/journal.md, проблема 8), одна вісь насичується на
 * ≈ 2000 mg. Тому понад 2 g модуль виходить лише сумою осей, напр. X = 2 g, Z = 1 g
 * → |a| ≈ 2,24 g. */

#include <stdint.h>

#define IMPACT_THRESHOLD_MG  2000
#define IMPACT_AXIS_MAX_MG   32767   /* обмеження осі: 3 · 32767² < 2³² */

/* |a|² у mg². Кожна вісь спершу обмежується ±IMPACT_AXIS_MAX_MG. */
uint32_t impact_mag2(int32_t x_mg, int32_t y_mg, int32_t z_mg);

/* 1 — |a| > IMPACT_THRESHOLD_MG. */
int impact_over(uint32_t mag2);

/* ⌊√v⌋: |a| у mg для журналу. */
uint32_t impact_isqrt(uint32_t v);

#endif /* IMPACT_H */
