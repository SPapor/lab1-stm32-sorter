#ifndef PORT_SIM_H
#define PORT_SIM_H

/* Обхід для Wokwi в копії порту ARM_CM0 (збірка SIM_CRIT=1, port.c у цьому
 * каталозі, docs/port_sim-V11.3.3.patch): критична секція відкладає переривання
 * NVIC, тік ядра і PendSV, бо Wokwi не враховує PRIMASK. */

#include <stdint.h>

typedef struct {
    uint32_t crit_task_n;      /* входів у taskENTER_CRITICAL після старту ядра */
    uint32_t crit_isr_n;       /* входів у taskENTER_CRITICAL_FROM_ISR після старту */
    uint32_t deferred_tick;    /* тіків ядра, відкладених до кінця секції */
    uint32_t deferred_pendsv;  /* PendSV, виставлених знову після секції */
} port_sim_counters_t;

/* З SysTick_Handler перед тіком ядра: 1 - усередині критичної секції, тік
 * відкладено (його прожене вихід із секції), 0 - обробляти зараз */
uint32_t port_sim_defer_tick(void);

void port_sim_counters(port_sim_counters_t *out);

#endif /* PORT_SIM_H */
