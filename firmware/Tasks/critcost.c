/* Ціна критичної секції ядра (збірка TEST_CRITCOST=1, лише ARCH=rtos з PERF=1, у
 * продукт не входить; docs/rtos-design.md, розділ 2). Задача app першим ділом
 * міряє CRITCOST_N пар вхід + вихід:
 *   kind=task  taskENTER_CRITICAL / taskEXIT_CRITICAL;
 *   kind=isr   taskENTER_CRITICAL_FROM_ISR / taskEXIT_CRITICAL_FROM_ISR (з задачі,
 *              той самий код, що в ISR);
 *   kind=none  дві мітки perf_now без нічого між ними - ціна самого виміру.
 * Порівняння SIM_CRIT=1 і SIM_CRIT=0 дає ціну обходу PRIMASK. Тіки й переривання
 * між мітками потрапляють у max, а в min - ні. */
#ifdef TEST_CRITCOST
#include "critcost.h"
#include "FreeRTOS.h"
#include "task.h"
#include "perf_hal.h"
#include "uart_hal.h"
#include "stm32c0xx_hal.h"

#define CRITCOST_N  10000u

typedef struct {
    uint32_t min;
    uint32_t max;
    uint64_t sum;
} cost_t;

static void cost_add(cost_t *c, uint32_t cyc)
{
    if (cyc < c->min)
        c->min = cyc;
    if (cyc > c->max)
        c->max = cyc;
    c->sum += cyc;
}

static void cost_line(const char *kind, const cost_t *c)
{
    uint32_t avg_x100 = (uint32_t)(c->sum * 100u / CRITCOST_N);

    uart_hal_printf("%lu CRITCOST kind=%s sim_crit=%u n=%lu min_cyc=%lu avg_cyc_x100=%lu max_cyc=%lu",
                    (unsigned long)HAL_GetTick(), kind, (unsigned)SIM_CRIT,
                    (unsigned long)CRITCOST_N, (unsigned long)c->min, (unsigned long)avg_x100,
                    (unsigned long)c->max);
}

void critcost_run(void)
{
    cost_t none = { UINT32_MAX, 0, 0 };
    cost_t task = { UINT32_MAX, 0, 0 };
    cost_t isr = { UINT32_MAX, 0, 0 };

    for (uint32_t i = 0; i < CRITCOST_N; i++) {
        uint32_t t0, t1;
        UBaseType_t mask;

        t0 = perf_now();
        t1 = perf_now();
        cost_add(&none, t1 - t0);

        t0 = perf_now();
        taskENTER_CRITICAL();
        taskEXIT_CRITICAL();
        t1 = perf_now();
        cost_add(&task, t1 - t0);

        t0 = perf_now();
        mask = taskENTER_CRITICAL_FROM_ISR();
        taskEXIT_CRITICAL_FROM_ISR(mask);
        t1 = perf_now();
        cost_add(&isr, t1 - t0);
    }
    cost_line("none", &none);
    cost_line("task", &task);
    cost_line("isr", &isr);
}
#endif
