#ifndef APP_TASK_H
#define APP_TASK_H

/* Тестові збірки ядра (шар Tasks, лише ARCH=rtos) замість задач продукту
 * (Tasks/sorter_tasks.c): TEST_KSTRESS - стрес-тест (Tasks/kstress.c), TEST_CRITCOST -
 * ціна критичної секції (Tasks/critcost.c) в одній задачі "app". */

/* Створює задачі тестової збірки і запускає ядро. Не повертається. */
void app_task_start(void) __attribute__((noreturn));

#endif /* APP_TASK_H */
