/* portasm.c порту ARM_CM0 для збірки TEST_KSTRESS: без змін, плюс лічильник
 * критичних секцій ISR (taskENTER_CRITICAL_FROM_ISR). ulSetInterruptMask і
 * vClearInterruptMask порту перейменовано, ядро кличе обгортки нижче
 * (port_kstress.c - про те, що рахується). У продукт не входить. */
#define ulSetInterruptMask  port_ulSetInterruptMask
#define vClearInterruptMask port_vClearInterruptMask
#include "portasm.c"
#undef ulSetInterruptMask
#undef vClearInterruptMask
#include "kstress_port.h"

uint32_t ulSetInterruptMask(void);
void vClearInterruptMask(uint32_t ulMask);

uint32_t ulSetInterruptMask(void)
{
    uint32_t mask = port_ulSetInterruptMask();

    kstress_isr_mask_enter();
    return mask;
}

void vClearInterruptMask(uint32_t ulMask)
{
    kstress_isr_mask_exit();
    port_vClearInterruptMask(ulMask);
}
