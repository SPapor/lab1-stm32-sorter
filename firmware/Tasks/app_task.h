#ifndef APP_TASK_H
#define APP_TASK_H

/* Задачі FreeRTOS (шар Tasks, лише ARCH=rtos). Крок 1 ЛР4: увесь super loop —
 * одна задача "app"; розбиття на задачі — крок 2 (docs/rtos-design.md). */

/* Створює задачу "app", тіло якої викликає pass() у нескінченному циклі
 * (один прохід головного циклу ЛР3), і запускає ядро. Не повертається. */
void app_task_start(void (*pass)(void)) __attribute__((noreturn));

#endif /* APP_TASK_H */
