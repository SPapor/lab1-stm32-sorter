#ifndef MEM_HAL_H
#define MEM_HAL_H

/* Межі стека зі скрипту лінкера STM32C031xx_FLASH.ld (методичка 5.4):
 *
 *   | .data | .bss | купа -> ...   | резерв стека _Min_Stack_Size | <- _estack
 *
 * _estack — вершина стека (кінець RAM), _Min_Stack_Size — абсолютний символ:
 * його адреса і є розміром резерву. */

#include <stdint.h>

/* Дно резерву: _estack - _Min_Stack_Size. */
uint32_t *mem_hal_stack_bottom(void);

/* Вершина стека: _estack. */
uint32_t *mem_hal_stack_top(void);

/* Поточний SP (MSP). */
uint32_t *mem_hal_sp(void);

#endif /* MEM_HAL_H */
