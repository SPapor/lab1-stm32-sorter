/* Зонд бюджету flash (збірка TEST_APIPROBE=1, лише ARCH=rtos, у продукт не входить):
 * посилається на все API ядра, яке потрібне задачам кроків 2-3, щоб --gc-sections
 * і LTO його не викинули, і бюджет не був занадто оптимістичним
 * (docs/rtos-design.md, розділ 4). Викликається лише за умови, яку компілятор
 * не може довести хибною, а на МК вона ніколи не виконується. */
#ifdef TEST_APIPROBE
#include "api_probe.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "stream_buffer.h"

volatile int api_probe_never;

static void probe_task(void *arg)
{
    (void)arg;
    for (;;) {
    }
}

static void probe(void)
{
    static TaskStatus_t st[8];
    BaseType_t woken = pdFALSE;
    TickType_t last = xTaskGetTickCount();
    uint8_t b = 0;
    TaskHandle_t h = NULL;
    QueueHandle_t q = xQueueCreate(8, 8);
    SemaphoreHandle_t m = xSemaphoreCreateMutex();
    SemaphoreHandle_t bs = xSemaphoreCreateBinary();
    SemaphoreHandle_t cs = xSemaphoreCreateCounting(4, 0);
    StreamBufferHandle_t sb = xStreamBufferCreate(64, 1);

    xTaskCreate(probe_task, "probe", 64, NULL, 2, &h);
    xQueueSend(q, &b, 1);
    xQueueReceive(q, &b, 1);
    xQueueSendFromISR(q, &b, &woken);
    xSemaphoreTake(m, 1);
    xSemaphoreGive(m);
    xSemaphoreTake(bs, 1);
    xSemaphoreGiveFromISR(bs, &woken);
    xSemaphoreGive(cs);
    vTaskNotifyGiveFromISR(h, &woken);
    xTaskNotifyGive(h);
    ulTaskNotifyTake(pdTRUE, 1);
    xStreamBufferSendFromISR(sb, &b, 1, &woken);
    xStreamBufferReceive(sb, &b, 1, 1);
    xTaskDelayUntil(&last, 5);
    api_probe_never = (int)uxTaskGetStackHighWaterMark(h);
    api_probe_never += (int)uxTaskGetStackHighWaterMark(xTaskGetIdleTaskHandle());
    api_probe_never += (int)uxTaskGetSystemState(st, 8, NULL);
    api_probe_never += (int)xPortGetFreeHeapSize() + (int)xPortGetMinimumEverFreeHeapSize();
    portYIELD_FROM_ISR(woken);
}

void api_probe_ref(void)
{
    if (api_probe_never)
        probe();
}
#endif
