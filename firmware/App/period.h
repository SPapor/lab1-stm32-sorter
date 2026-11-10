#ifndef PERIOD_H
#define PERIOD_H

/* Перевірка «настав час періодичної задачі» для super loop. Час — мілісекунди
 * з HAL_GetTick (uint32_t, переповнюється через ~49,7 доби); беззнакова різниця
 * now - last лишається правильною і через переповнення. */

#include <stdint.h>

/* 1 — від *last минуло щонайменше period мс, тоді *last = now; інакше 0.
 * Запуски, пропущені через довге блокування, не «доганяються» пачкою:
 * наступний буде через period від now. */
int period_due(uint32_t *last, uint32_t now, uint32_t period);

#endif /* PERIOD_H */
