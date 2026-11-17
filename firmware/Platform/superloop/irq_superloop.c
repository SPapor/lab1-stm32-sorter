/* Системні обробники super loop. CubeMX їх не генерує (firmware.ioc, NVIC → Code
 * generation): у ARCH=rtos ці вектори належать порту FreeRTOS, тож обидва варіанти
 * визначені тут, у Platform/<arch>/. SVC і PendSV у super loop не викликаються. */

#include "stm32c0xx_hal.h"

void SVC_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);

void SVC_Handler(void)
{
}

void PendSV_Handler(void)
{
}

/* Тік HAL 1 мс: HAL_GetTick, тайм-аути HAL і perf_now (perf_tick.h) */
void SysTick_Handler(void)
{
    HAL_IncTick();
}
