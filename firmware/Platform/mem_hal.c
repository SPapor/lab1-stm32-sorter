#include "mem_hal.h"
#include "stm32c0xx_hal.h"

extern uint32_t _estack;           /* вершина стека (кінець RAM) */
extern uint32_t _Min_Stack_Size;   /* абсолютний символ: його адреса = розмір */

uint32_t *mem_hal_stack_bottom(void)
{
    return (uint32_t *)((uintptr_t)&_estack - (uintptr_t)&_Min_Stack_Size);
}

uint32_t *mem_hal_stack_top(void)
{
    return &_estack;
}

uint32_t *mem_hal_sp(void)
{
    return (uint32_t *)__get_MSP();
}
