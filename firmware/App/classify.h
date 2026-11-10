#ifndef CLASSIFY_H
#define CLASSIFY_H

/* FR-02: клас посилки за фоторезистором class. Світла посилка відбиває більше
 * світла: більше люксів — менший код АЦП (модель LDR — див. gate.h).
 * Перевірка з PRD: 500 лк → A (1001), 100 лк → UNKNOWN (2045), 10 лк → B (3412). */

#include <stdint.h>

#define CLASS_A_MAX_RAW  1300u   /* ≤ — A, світла (≈ 300 лк і світліше) */
#define CLASS_B_MIN_RAW  2700u   /* ≥ — B, темна (≈ 39 лк і темніше) */
#define CLASS_FULL_RAW   4095u   /* повна шкала 12-бітного АЦП */

typedef enum { PARCEL_A = 0, PARCEL_B, PARCEL_UNKNOWN } parcel_class_t;

/* Код між порогами — UNKNOWN. Код на межі шкали (0 або CLASS_FULL_RAW) — обрив
 * чи замикання датчика (модель Wokwi дає 32…4063), теж UNKNOWN. */
parcel_class_t classify(uint16_t raw);

/* "A", "B", "UNKNOWN" — значення ключа class= у журналі. */
const char *classify_name(parcel_class_t c);

#endif /* CLASSIFY_H */
