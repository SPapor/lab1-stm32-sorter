#include "perf.h"
#include <stddef.h>

uint32_t perf_elapsed(uint32_t start, uint32_t end)
{
    return end - start;
}

/* Частка й остача окремо: cyc + cyc_per_us / 2 могло б переповнитися біля 2^32 */
uint32_t perf_cyc_to_us(uint32_t cyc, uint32_t cyc_per_us)
{
    uint32_t q = cyc / cyc_per_us;
    uint32_t r = cyc - q * cyc_per_us;

    return (2u * r >= cyc_per_us) ? q + 1u : q;
}

void perf_add(perf_stat_t *s, uint32_t cyc)
{
    if (s->count == 0u || cyc < s->min)
        s->min = cyc;
    if (cyc > s->max)
        s->max = cyc;
    s->sum += cyc;
    s->count++;
}

void perf_stat_reset(perf_stat_t *s)
{
    s->count = 0;
    s->min = 0;
    s->max = 0;
    s->sum = 0;
}

uint32_t perf_udiv64(uint64_t num, uint32_t den, uint32_t *rem)
{
    uint64_t r = 0;
    uint64_t q = 0;

    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((num >> i) & 1u);
        q <<= 1;
        if (r >= den) {
            r -= den;
            q |= 1u;
        }
    }
    if (rem != NULL)
        *rem = (uint32_t)r;
    return (q >> 32) ? 0xFFFFFFFFu : (uint32_t)q;
}

uint32_t perf_avg(const perf_stat_t *s)
{
    if (s->count == 0u)
        return 0;
    return perf_udiv64(s->sum + s->count / 2u, s->count, NULL);
}

void perf_period_init(perf_period_t *p, uint32_t period_us)
{
    p->period_us = period_us;
    perf_period_reset(p);
}

void perf_period_reset(perf_period_t *p)
{
    p->t_prev = 0;
    p->has_prev = 0;
    p->n = 0;
    p->jit_min_us = 0;
    p->jit_max_us = 0;
    p->missed = 0;
    for (size_t i = 0; i < PERF_HIST_BINS; i++)
        p->hist[i] = 0;
}

/* Верхню межу перевіряємо до віднімання: jitter - PERF_HIST_LO_US біля INT32_MAX
 * переповнило б int32 */
#define PERF_HIST_HI_US (PERF_HIST_LO_US + (int32_t)(PERF_HIST_BINS - 2u) * PERF_HIST_STEP_US)

unsigned perf_hist_bin(int32_t jitter_us)
{
    if (jitter_us < PERF_HIST_LO_US)
        return 0;
    if (jitter_us >= PERF_HIST_HI_US)
        return PERF_HIST_BINS - 1u;
    return (unsigned)((jitter_us - PERF_HIST_LO_US) / PERF_HIST_STEP_US) + 1u;
}

int32_t perf_hist_lo_us(unsigned bin)
{
    if (bin == 0u)
        return INT32_MIN;
    return PERF_HIST_LO_US + (int32_t)(bin - 1u) * PERF_HIST_STEP_US;
}

/* Інтервал у мкс обмежується INT32_MAX: довші за 35 хв тут не трапляються */
void perf_period_start(perf_period_t *p, uint32_t now, uint32_t cyc_per_us)
{
    if (p->has_prev) {
        uint32_t dt_us = perf_cyc_to_us(now - p->t_prev, cyc_per_us);
        int32_t dt = dt_us > (uint32_t)INT32_MAX ? INT32_MAX : (int32_t)dt_us;
        int32_t jitter = dt - (int32_t)p->period_us;

        if (p->n == 0u || jitter < p->jit_min_us)
            p->jit_min_us = jitter;
        if (p->n == 0u || jitter > p->jit_max_us)
            p->jit_max_us = jitter;
        if (dt_us > 2u * p->period_us)
            p->missed++;
        p->hist[perf_hist_bin(jitter)]++;
        p->n++;
    }
    p->t_prev = now;
    p->has_prev = 1;
}

void perf_load_init(perf_load_t *l, uint32_t win_cyc)
{
    l->win_cyc = win_cyc;
    l->start = 0;
    l->open = 0;
    l->busy = 0;
    l->windows = 0;
    l->last_win = 0;
    l->last_busy = 0;
    l->last_pct = 0;
    l->last_pm = 0;
}

void perf_load_busy(perf_load_t *l, uint32_t cyc)
{
    l->busy += cyc;
}

/* busy * 100 переповнило б 32 біти (вікно 1 с = 48e6 тактів), тож ділимо на
 * вікно / 100 (вікно / 1000); відносна похибка — до 100 / вікно, тобто ~2e-6 */
int perf_load_poll(perf_load_t *l, uint32_t now)
{
    uint32_t win;

    if (!l->open) {
        l->open = 1;
        l->start = now;
        l->busy = 0;
        return 0;
    }
    win = now - l->start;
    if (win < l->win_cyc)
        return 0;
    l->last_win = win;
    l->last_busy = l->busy;
    l->last_pct = perf_cyc_to_us(l->busy, win / 100u);
    l->last_pm = perf_cyc_to_us(l->busy, win / 1000u);
    l->windows++;
    l->busy = 0;
    l->start = now;
    return 1;
}
