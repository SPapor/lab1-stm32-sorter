#include "app_task.h"
#include "rtos_hal.h"
#include "FreeRTOS.h"
#include "task.h"

/* Пріоритет 1: вище за idle (0), щоб idle отримувала процесор лише тоді, коли
 * app заблокована (у кроці 1 — ніколи: цикл опитує без блокування) */
#define APP_TASK_PRIO          1u
/* Стартова оцінка 1 КБ: super loop ЛР3 займав 944 Б одного стека разом з
 * перериваннями; тепер переривання — на MSP, а в стеку задачі лишаються кадр
 * винятку і контекст PendSV (68 Б). Справжній розмір — за high water mark */
#define APP_TASK_STACK_WORDS   256u

static void (*app_pass)(void);

static void app_task(void *arg)
{
    (void)arg;
    for (;;)
        app_pass();
}

void app_task_start(void (*pass)(void))
{
    BaseType_t ok;

    app_pass = pass;
    ok = xTaskCreate(app_task, "app", APP_TASK_STACK_WORDS, NULL, APP_TASK_PRIO, NULL);
    configASSERT(ok == pdPASS);
    (void)ok;    /* без configASSERT більше ніде не потрібна */
    rtos_hal_start_scheduler();
}
