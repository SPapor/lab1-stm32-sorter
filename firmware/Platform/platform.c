#include "platform.h"
#include "stm32c0xx_hal.h"

void platform_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}
