#ifndef GATE_H
#define GATE_H

/* FR-01: поява посилки на вході за фоторезистором gate — поріг АЦП з гістерезисом.
 * Посилка затіняє LDR, і код АЦП росте. Модель wokwi-photoresistor-sensor при
 * живленні 3,3 В: 500 лк → 1001 (виміряно в ЛР2), 100 лк → 2045, 10 лк → 3412. */

#include <stdint.h>

#define GATE_ON_RAW   2400u   /* ≥ — посилка є (≈ 60 лк і темніше) */
#define GATE_OFF_RAW  1600u   /* ≤ — посилки немає (≈ 190 лк і світліше) */

typedef enum { GATE_EV_NONE = 0, GATE_EV_IN, GATE_EV_OUT } gate_event_t;

typedef struct {
    uint16_t on_raw;
    uint16_t off_raw;
    uint8_t present;    /* 1 — посилка перед датчиком */
} gate_t;

/* Пороги off_raw < on_raw, між ними — смуга гістерезису. Початковий стан —
 * «посилки немає». */
void gate_init(gate_t *g, uint16_t on_raw, uint16_t off_raw);

/* Один відлік АЦП. GATE_EV_IN — перехід «немає → є» (raw ≥ on_raw),
 * GATE_EV_OUT — «є → немає» (raw ≤ off_raw). Відлік у смузі гістерезису стан
 * не змінює, тож шум навколо одного порогу не дає зайвих подій. */
gate_event_t gate_update(gate_t *g, uint16_t raw);

#endif /* GATE_H */
