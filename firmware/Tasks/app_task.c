#include "app_task.h"
#include "rtos_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#ifdef TEST_KSTRESS
#include "kstress.h"
#endif
#ifdef TEST_CRITCOST
#include "critcost.h"
#endif

/* Пріоритет 1: вище за idle (0). Стек - як у задачі app кроку 1 */
#define APP_TASK_PRIO          1u
#define APP_TASK_STACK_WORDS   256u

/* TEST_CRITCOST: вимір без інших задач, далі задача спить */
static void app_task(void *arg)
{
    (void)arg;
#ifdef TEST_CRITCOST
    critcost_run();
#endif
    for (;;)
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

void app_task_start(void)
{
    BaseType_t ok;

#ifdef TEST_KSTRESS
    /* стрес-тест ядра замість задачі app */
    kstress_create();
    (void)app_task;
    ok = pdPASS;
#else
    ok = xTaskCreate(app_task, "app", APP_TASK_STACK_WORDS, NULL, APP_TASK_PRIO, NULL);
#endif
    configASSERT(ok == pdPASS);
    (void)ok;    /* без configASSERT більше ніде не потрібна */
    rtos_hal_start_scheduler();
}
