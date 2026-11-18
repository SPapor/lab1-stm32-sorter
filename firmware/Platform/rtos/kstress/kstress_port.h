#ifndef KSTRESS_PORT_H
#define KSTRESS_PORT_H

/* Лічильники порту для збірки TEST_KSTRESS (port_kstress.c, port_kstress_asm.c).
 * Усе - з моменту старту ядра; на залізі всі *_in_* мали б бути 0. */

#include <stdint.h>

typedef struct {
    uint32_t crit_task;              /* входів у taskENTER_CRITICAL */
    uint32_t crit_isr;               /* входів у taskENTER_CRITICAL_FROM_ISR */
    uint32_t switches;               /* перемикань задач */
    uint32_t pendsv_in_task_crit;    /* PendSV усередині критичної секції задачі */
    uint32_t switches_in_task_crit;  /* з них - зі справжнім перемиканням */
    uint32_t pendsv_in_isr_crit;     /* PendSV усередині критичної секції ISR */
    uint32_t switches_in_isr_crit;
    uint32_t tick_in_task_crit;      /* тік SysTick у критичній секції задачі */
    uint32_t tick_in_isr_crit;
    uint32_t tim14_in_task_crit;     /* переривання TIM14 у критичній секції задачі */
    uint32_t tim14_in_isr_crit;
} kstress_port_counters_t;

typedef enum {
    KSTRESS_SRC_TICK,                /* SysTick_Handler (Platform/rtos/rtos_hal.c) */
    KSTRESS_SRC_TIM14                /* TIM14_IRQHandler (Tasks/kstress.c) */
} kstress_src_t;

/* ulCriticalNesting порту як є (0xaaaaaaaa + k - до старту ядра) */
uint32_t kstress_crit_nesting(void);

/* На початку обробників SysTick і TIM14 */
void kstress_note_isr(kstress_src_t src);

/* Обгортки ulSetInterruptMask / vClearInterruptMask (port_kstress_asm.c) */
void kstress_isr_mask_enter(void);
void kstress_isr_mask_exit(void);

/* traceTASK_SWITCHED_OUT / _IN (FreeRTOSConfig.h), tcb - pxCurrentTCB */
void kstress_switched_out(void *tcb);
void kstress_switched_in(void *tcb);

void kstress_port_counters(kstress_port_counters_t *out);

#endif /* KSTRESS_PORT_H */
