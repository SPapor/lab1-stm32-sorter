#include "memmark.h"

void memmark_paint(uint32_t *lo, uint32_t *hi, uint32_t pattern)
{
    while (lo < hi)
        *lo++ = pattern;
}

uint32_t memmark_used_bytes(const uint32_t *bottom, const uint32_t *top, uint32_t pattern)
{
    const uint32_t *p = bottom;

    while (p < top && *p == pattern)
        p++;
    return (uint32_t)((size_t)(top - p) * sizeof(uint32_t));
}
