#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

extern uint32_t SystemCoreClock;

#define configCPU_CLOCK_HZ                  (SystemCoreClock)
#define configTICK_RATE_HZ                  ((TickType_t)TICK_HZ)
#define configTICK_TYPE_WIDTH_IN_BITS       TICK_TYPE_WIDTH_32_BITS
#define configUSE_TICKLESS_IDLE             0

#define configUSE_PREEMPTION                1
#define configUSE_TIME_SLICING              1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configMAX_PRIORITIES                8
#define configMINIMAL_STACK_SIZE            ((uint16_t)48)
#define configMAX_TASK_NAME_LEN             8
#define configIDLE_SHOULD_YIELD             1
#define configQUEUE_REGISTRY_SIZE           0
#define configENABLE_BACKWARD_COMPATIBILITY 0

#define configSUPPORT_STATIC_ALLOCATION     1
#define configSUPPORT_DYNAMIC_ALLOCATION    0
#define configTOTAL_HEAP_SIZE               ((size_t)0)
#define configUSE_NEWLIB_REENTRANT          0

#define configUSE_MUTEXES                   0
#define configUSE_RECURSIVE_MUTEXES         0
#define configUSE_COUNTING_SEMAPHORES       0
#define configUSE_TASK_NOTIFICATIONS        0
#define configUSE_QUEUE_SETS                0
#define configUSE_STREAM_BUFFERS            0
#define configUSE_TIMERS                    0
#define configUSE_EVENT_GROUPS              0
#define configUSE_CO_ROUTINES               0
#define configUSE_APPLICATION_TASK_TAG      0

#define configUSE_IDLE_HOOK                 0
#define configUSE_TICK_HOOK                 1
#define configUSE_MALLOC_FAILED_HOOK        0
#define configCHECK_FOR_STACK_OVERFLOW      2
#define configCHECK_HANDLER_INSTALLATION    RTOS_ASSERT

#define configGENERATE_RUN_TIME_STATS       PERF
#define configUSE_TRACE_FACILITY            PERF
#define configUSE_STATS_FORMATTING_FUNCTIONS 0
#if PERF
#define configRUN_TIME_COUNTER_TYPE         uint64_t
void rtos_hal_runtime_start(void);
uint64_t rtos_hal_runtime_now(void);
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() rtos_hal_runtime_start()
#define portGET_RUN_TIME_COUNTER_VALUE()    rtos_hal_runtime_now()
#endif

void rtos_hal_assert_failed(uint32_t line) __attribute__((noreturn, noinline));
#if RTOS_ASSERT
#define configASSERT(x)                     do { if ((x) == 0) rtos_hal_assert_failed(__LINE__); } while (0)
#endif

#if PERF && !defined(TEST_QSTRESS)
void rtos_hal_trace_in(const void *tcb);
#define traceTASK_SWITCHED_IN()             rtos_hal_trace_in((const void *)pxCurrentTCB)
#endif

#ifdef TEST_QSTRESS
#define configUSE_LIST_DATA_INTEGRITY_CHECK_BYTES 1
void kstress_switched_out(void *tcb);
void kstress_switched_in(void *tcb);
#define traceTASK_SWITCHED_OUT()            kstress_switched_out((void *)pxCurrentTCB)
#define traceTASK_SWITCHED_IN()             kstress_switched_in((void *)pxCurrentTCB)
#endif

#if defined(TEST_QPCHECK) && !defined(TEST_QSTRESS)
void qmax_note(const void *queue, uint32_t waiting);
#define traceQUEUE_SEND(q)                  qmax_note((q), (q)->uxMessagesWaiting + 1u)
#define traceQUEUE_SEND_FROM_ISR(q)         qmax_note((q), (q)->uxMessagesWaiting + 1u)
#endif

#ifndef TEST_QSTRESS
extern volatile uint32_t qp_post_fail;
#define traceQUEUE_SEND_FAILED(q)           (qp_post_fail++)
#define traceQUEUE_SEND_FROM_ISR_FAILED(q)  (qp_post_fail++)
#endif

#define configENABLE_MPU                    0

#define INCLUDE_xTaskDelayUntil             0
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_xTaskGetIdleTaskHandle      1
#define INCLUDE_xTaskGetCurrentTaskHandle   0
#define INCLUDE_vTaskPrioritySet            0
#define INCLUDE_uxTaskPriorityGet           0
#define INCLUDE_vTaskDelete                 0
#define INCLUDE_vTaskSuspend                0
#define INCLUDE_vTaskDelay                  0
#define INCLUDE_xTaskGetSchedulerState      0
#define INCLUDE_eTaskGetState               0
#define INCLUDE_xTaskAbortDelay             0
#define INCLUDE_xTaskGetHandle              0
#define INCLUDE_xTaskResumeFromISR          0
#define INCLUDE_xTimerPendFunctionCall      0
#define INCLUDE_xSemaphoreGetMutexHolder    0
#define INCLUDE_xQueueGetMutexHolder        0

#endif
