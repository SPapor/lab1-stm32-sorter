/* portasm.c порту ARM_CM0 для збірки TEST_KSTRESS: без змін. У порту із сабмодуля
 * (SIM_CRIT=0) тут визначено ulSetInterruptMask і vClearInterruptMask - їх
 * перейменовано, а обгортки з лічильником - у port_kstress.c. У port_sim
 * (SIM_CRIT=1) вони в port.c, і перейменування тут нічого не зачіпає.
 * У продукт не входить. */
#define ulSetInterruptMask  port_ulSetInterruptMask
#define vClearInterruptMask port_vClearInterruptMask
#include "portasm.c"
