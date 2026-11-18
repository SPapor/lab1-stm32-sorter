#ifndef KSTRESS_PORT_H
#define KSTRESS_PORT_H

/* Лічильники порту для збірки TEST_KSTRESS (port_kstress.c) */

#include <stdint.h>

typedef struct {
    uint32_t isr_in_crit;        /* переривань усередині критичної секції */
    uint32_t pendsv_in_crit;     /* PendSV усередині критичної секції */
    uint32_t switches_in_crit;   /* з них - з перемиканням задачі */
    uint32_t switches;           /* усього перемикань задач */
} kstress_port_counters_t;

/* На початку обробників SysTick і TIM14 */
void kstress_note_isr(void);

/* traceTASK_SWITCHED_OUT / _IN (FreeRTOSConfig.h), tcb - pxCurrentTCB */
void kstress_switched_out(void *tcb);
void kstress_switched_in(void *tcb);

void kstress_port_counters(kstress_port_counters_t *c);

#endif /* KSTRESS_PORT_H */
