#include "period.h"

int period_due(uint32_t *last, uint32_t now, uint32_t period)
{
    if (now - *last < period)
        return 0;
    *last = now;
    return 1;
}
