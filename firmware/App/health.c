#include "health.h"

health_t health_mpu(mpu6050_status_t st)
{
    switch (st) {
    case MPU6050_OK:        return HEALTH_OK;
    case MPU6050_ERR_SLEEP: return HEALTH_WARN;
    default:                return HEALTH_FAIL;   /* ERR_BUS, ERR_ID */
    }
}

health_t health_ldr(uint16_t raw, uint16_t full)
{
    return (raw > 0u && raw < full) ? HEALTH_OK : HEALTH_WARN;
}

health_t health_servo(uint16_t pulse_us, uint16_t min_us, uint16_t max_us)
{
    return (pulse_us >= min_us && pulse_us <= max_us) ? HEALTH_OK : HEALTH_FAIL;
}

const char *health_name(health_t h)
{
    switch (h) {
    case HEALTH_OK:   return "OK";
    case HEALTH_WARN: return "WARN";
    default:          return "FAIL";
    }
}
