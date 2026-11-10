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
