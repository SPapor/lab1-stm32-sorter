#ifndef HEALTH_H
#define HEALTH_H

/* FR-08: оцінка результатів самоперевірки при старті. FAIL хоча б одного
 * пристрою — сортувальник у стані FAULT; WARN — працює далі, але з позначкою
 * в журналі. */

#include <stdint.h>
#include "mpu6050.h"

typedef enum { HEALTH_OK = 0, HEALTH_WARN, HEALTH_FAIL } health_t;

/* MPU-6050: немає відповіді або WHO_AM_I ≠ 0x68 — FAIL. WHO_AM_I правильний,
 * але SLEEP не знявся — WARN: так поводиться модель Wokwi (docs/journal.md,
 * проблема 8), а прискорення при цьому читається. */
health_t health_mpu(mpu6050_status_t st);

/* LDR: код на межі шкали (0 або full) — обрив, замикання або засвітка: WARN. */
health_t health_ldr(uint16_t raw, uint16_t full);

/* Серво: зворотне читання ширини імпульсу поза [min_us, max_us] — FAIL. */
health_t health_servo(uint16_t pulse_us, uint16_t min_us, uint16_t max_us);

/* "OK", "WARN", "FAIL" — значення ключа status= у журналі */
const char *health_name(health_t h);

#endif /* HEALTH_H */
