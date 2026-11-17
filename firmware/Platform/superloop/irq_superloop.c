/* Системні обробники super loop. CubeMX їх не генерує (firmware.ioc, NVIC → Code
 * generation): у ARCH=rtos ці вектори належать порту FreeRTOS, тож обидва варіанти
 * визначені тут, у Platform/<arch>/. SVC і PendSV у super loop не викликаються. */

#include "stm32c0xx_hal.h"
#ifdef TEST_BOOTMARK
#include "perf_hal.h"
#endif

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

#ifdef TEST_BOOTMARK
/* Дослід TEST_BOOTMARK: той самий код, що слабкий HAL_InitTick у stm32c0xx_hal.c,
 * плюс мітка стану SysTick перед його перезапуском (rtos_hal.c робить так само) */
HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
    perf_bootmark('i');
    if ((uint32_t)uwTickFreq == 0u)
        return HAL_ERROR;
    if (HAL_SYSTICK_Config(SystemCoreClock / (1000u / (uint32_t)uwTickFreq)) != 0u)
        return HAL_ERROR;
    if (TickPriority >= (1UL << __NVIC_PRIO_BITS))
        return HAL_ERROR;
    HAL_NVIC_SetPriority(SysTick_IRQn, TickPriority, 0u);
    uwTickPrio = TickPriority;
    return HAL_OK;
}
#endif
