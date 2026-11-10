#ifndef SORTER_H
#define SORTER_H

/* Сортувальник: рішення про лоток (FR-03, FR-05), лічильники (FR-06), стани
 * READY / SORTING / ESTOP / FAULT (FR-07, FR-08). Лише логіка: нічого не знає
 * про АЦП, серво, час і журнал.
 *
 *   READY --PARCEL_IN--> SORTING --PARCEL_OUT--> READY
 *   READY, SORTING --E-STOP--> ESTOP --E-STOP--> READY
 *   FAULT (лише після самоперевірки при старті) — до перезапуску
 *
 * SORTING — посилка перед gate, рішення вже прийняте й заслінку виставлено. */

#include <stdint.h>
#include "classify.h"

typedef enum { SORTER_READY = 0, SORTER_SORTING, SORTER_ESTOP, SORTER_FAULT } sorter_state_t;

typedef enum { BIN_A = 0, BIN_B, BIN_REJECT, BIN_COUNT } sorter_bin_t;

typedef struct {
    sorter_state_t state;
    uint8_t impact;               /* удар з моменту попереднього рішення (FR-04) */
    uint32_t count[BIN_COUNT];    /* посилки за лотками (FR-06) */
} sorter_t;

typedef struct {
    sorter_bin_t bin;
    uint8_t impact;      /* прапорець удару, з яким прийнято рішення */
    uint8_t angle;       /* кут заслінки, ° */
    uint32_t n;          /* номер посилки: сума лічильників після рішення */
} sorter_decision_t;

/* fault != 0 — самоперевірка не пройшла (FR-08): стан FAULT, інакше READY.
 * Лічильники й прапорець удару — нулі. */
void sorter_init(sorter_t *s, int fault);

/* FR-05: удар або UNKNOWN — брак, інакше лоток за класом. */
sorter_bin_t sorter_route(parcel_class_t cls, uint8_t impact);

/* FR-03: A → 0°, B → 90°, брак → 180°. */
uint8_t sorter_bin_angle(sorter_bin_t bin);

/* PARCEL_IN з класом cls. Лише в READY: рішення за класом і прапорцем удару,
 * лічильник лотка +1, прапорець скидається, стан → SORTING; повертає 1 і
 * заповнює out. В інших станах посилка ігнорується (FR-07: в E-STOP нові входи
 * не приймаються) і повертається 0. */
int sorter_parcel_in(sorter_t *s, parcel_class_t cls, sorter_decision_t *out);

/* PARCEL_OUT: SORTING → READY; в інших станах нічого не змінює. */
void sorter_parcel_out(sorter_t *s);

/* FR-04: удар. У READY і SORTING прапорець запам'ятовується до наступного
 * рішення (посилка ще їде до gate); в ESTOP і FAULT конвеєр стоїть — ігнорується.
 * Повертає 1, лише якщо прапорець щойно встановлено (один рядок IMPACT на посилку).
 * Вихід з E-STOP прапорець не скидає. */
int sorter_impact(sorter_t *s);

/* FR-07: натискання E-STOP. READY, SORTING → ESTOP; ESTOP → READY; FAULT
 * лишається FAULT. Повертає новий стан. */
sorter_state_t sorter_estop_toggle(sorter_t *s);

/* Команда reset: лічильники в нуль; стан і прапорець удару не змінюються. */
void sorter_reset_counters(sorter_t *s);

uint32_t sorter_total(const sorter_t *s);

/* "READY", "SORTING", "ESTOP", "FAULT" */
const char *sorter_state_name(sorter_state_t st);

/* "A", "B", "REJECT" — значення ключа route= у журналі */
const char *sorter_bin_name(sorter_bin_t bin);

#endif /* SORTER_H */
