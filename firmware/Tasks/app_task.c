#include "app_task.h"
#include "rtos_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#ifdef TEST_APIPROBE
#include "api_probe.h"
#endif
#ifdef TEST_KSTRESS
#include "kstress.h"
#endif
#ifdef TEST_CRITCOST
#include "critcost.h"
#endif

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
#ifdef TEST_CRITCOST
    critcost_run();       /* ціна критичної секції, до першого проходу циклу */
#endif
    for (;;)
        app_pass();
}

void app_task_start(void (*pass)(void))
{
    BaseType_t ok;

    app_pass = pass;
#ifdef TEST_APIPROBE
    api_probe_ref();
#endif
#if PERF
    rtos_hal_mark_api();      /* далі до старту ядра переривання вимкнені */
#endif
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
