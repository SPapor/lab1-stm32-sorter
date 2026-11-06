#include "impact.h"

static uint32_t sq_axis(int32_t v)
{
    if (v > IMPACT_AXIS_MAX_MG)
        v = IMPACT_AXIS_MAX_MG;
    else if (v < -IMPACT_AXIS_MAX_MG)
        v = -IMPACT_AXIS_MAX_MG;
    return (uint32_t)(v * v);
}

uint32_t impact_mag2(int32_t x_mg, int32_t y_mg, int32_t z_mg)
{
    return sq_axis(x_mg) + sq_axis(y_mg) + sq_axis(z_mg);
}

int impact_over(uint32_t mag2)
{
    return mag2 > (uint32_t)IMPACT_THRESHOLD_MG * IMPACT_THRESHOLD_MG;
}

/* Побітовий корінь: по одному біту результату за крок, без ділення. */
uint32_t impact_isqrt(uint32_t v)
{
    uint32_t root = 0;
    uint32_t bit = 1ul << 30;

    while (bit > v)
        bit >>= 2;
    while (bit != 0u) {
        if (v >= root + bit) {
            v -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}
