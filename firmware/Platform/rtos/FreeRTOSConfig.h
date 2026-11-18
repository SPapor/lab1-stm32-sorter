#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* Конфігурація ядра FreeRTOS для NUCLEO-C031C6 (Cortex-M0+, 48 МГц, 32 КБ flash,
 * 12 КБ RAM), порт GCC/ARM_CM0, heap_4. Пояснення і таблиця змінених параметрів —
 * docs/rtos-design.md, розділ 1. Біля кожного параметра, що відрізняється від
 * типового значення ядра (include/FreeRTOS.h), — навіщо.
 *
 * Особливості Cortex-M0+ (ARMv6-M), через які частина методички тут не діє:
 * - немає BASEPRI: критична секція порту вимикає всі переривання через PRIMASK
 *   (cpsid i), тож configMAX_SYSCALL_INTERRUPT_PRIORITY і
 *   configKERNEL_INTERRUPT_PRIORITY порт ARM_CM0 не читає і вони не визначені;
 *   FromISR-функції можна викликати з переривання будь-якого пріоритету;
 * - пріоритетів NVIC лише 4 (__NVIC_PRIO_BITS = 2: 0, 64, 128, 192) і немає
 *   груп пріоритетів, тож NVIC_PRIORITYGROUP_4 з методички тут не існує;
 * - немає DWT CYCCNT: час для run-time stats — perf_now (SysTick + тіки).
 * PendSV і SysTick порт ставить найнижчими (0xFF -> 192), SVC — найвищим (0). */

#include <stdint.h>

/* --- Тактування і тік --------------------------------------------------- */
extern uint32_t SystemCoreClock;
#define configCPU_CLOCK_HZ                  (SystemCoreClock)   /* 48 МГц після SystemClock_Config */
/* Частота тіку з app.mk (TICK_HZ, за замовчуванням 1000): методичка 7.2, п. 6 —
 * S3 з 1000 і 100 Гц. Типово в ядрі значення немає, у шаблоні — 100 */
#define configTICK_RATE_HZ                  ((TickType_t)TICK_HZ)
#define configTICK_TYPE_WIDTH_IN_BITS       TICK_TYPE_WIDTH_32_BITS
/* Tickless idle вимкнено: SysTick має йти безперервно, від нього рахує perf_now.
 * Вектор SysTick — не порт, а SysTick_Handler з Platform/rtos/rtos_hal.c: він
 * рахує тіки з HAL_Init і після старту ядра викликає обробник порту
 * (у port.c — xPortSysTickHandler, app.mk) */
#define configUSE_TICKLESS_IDLE             0

/* --- Планувальник ------------------------------------------------------- */
#define configUSE_PREEMPTION                1
#define configUSE_TIME_SLICING              1
/* На M0+ немає CLZ, оптимізованого вибору задачі порт не має */
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
/* 7 рівнів: idle 0 і шість робочих (lcd < logger < cli < imu < sort < gate < estop,
 * крок 2 — docs/rtos-design.md). Кожен рівень — список у RAM, тож не більше, ніж треба */
#define configMAX_PRIORITIES                7
/* Стек idle, у словах: прологи prvIdleTask і vPortYield (~6 слів), кадр винятку і
 * контекст, який зберігає PendSV (17 слів), 4 слова контрольного шаблону перевірки
 * стека (метод 2) і запас. Хука idle немає, тож більше idle не потрібно.
 * Виміряний high water mark — docs/rtos-design.md */
#define configMINIMAL_STACK_SIZE            ((uint16_t)48)
/* Імена задач — до 7 символів (estop, logger, IDLE); типово 16 — 9 зайвих байтів на TCB */
#define configMAX_TASK_NAME_LEN             8
#define configIDLE_SHOULD_YIELD             1
#define configQUEUE_REGISTRY_SIZE           0
#define configENABLE_BACKWARD_COMPATIBILITY 0

/* --- Пам'ять ------------------------------------------------------------ */
/* Лише динамічне виділення з heap_4: стеки й TCB задач, черги. Статичне
 * (xTaskCreateStatic) вимагало б ще й vApplicationGetIdleTaskMemory */
#define configSUPPORT_STATIC_ALLOCATION     0
#define configSUPPORT_DYNAMIC_ALLOCATION    1
/* Стартове значення, масив ucHeap у .bss. Справжній розмір — за
 * xPortGetMinimumEverFreeHeapSize після S3 (крок 3) */
#define configTOTAL_HEAP_SIZE               ((size_t)(5u * 1024u))
/* stdio newlib у прошивці немає (форматер App/fmt.c), а strcmp, memcpy і memset
 * не мають стану: окрема struct _reent на задачу (~100 Б RAM кожна) не потрібна */
#define configUSE_NEWLIB_REENTRANT          0

/* --- Механізми, які потрібні продукту (docs/rtos-design.md) -------------- */
#define configUSE_MUTEXES                   1   /* шина I2C: imu і lcd */
#define configUSE_RECURSIVE_MUTEXES         0
#define configUSE_COUNTING_SEMAPHORES       1   /* облік кількох подій до обробки (методичка 4.4) */
#define configUSE_TASK_NOTIFICATIONS        1   /* EXTI E-STOP -> задача estop */
#define configTASK_NOTIFICATION_ARRAY_ENTRIES 1
#define configUSE_QUEUE_SETS                0
#define configUSE_STREAM_BUFFERS            1   /* UART RX у перериванні -> задача cli */
/* Програмних таймерів і груп подій продукт не потребує: timers.c і
 * event_groups.c не збираються (app.mk). Типово групи подій увімкнені */
#define configUSE_TIMERS                    0
#define configUSE_EVENT_GROUPS              0
#define configUSE_CO_ROUTINES               0
#define configUSE_APPLICATION_TASK_TAG      0

/* --- Хуки і діагностика (методичка 4.11) -------------------------------- */
#define configUSE_IDLE_HOOK                 0
/* Хука тіку немає: тіки для perf_now рахує сам SysTick_Handler
 * (Platform/rtos/rtos_hal.c). Хук ядро не викликає, поки xTaskResumeAll доганяє
 * відкладені тіки, тож тік, що прийшов саме тоді, хук пропустив би
 * (docs/rtos-design.md, розділ 3) */
#define configUSE_TICK_HOOK                 0
/* Вичерпання heap ОС — рядок FATAL malloc і зупинка (методичка 7.2, п. 5) */
#define configUSE_MALLOC_FAILED_HOOK        1
/* Метод 2: при кожному перемиканні — і вказівник стека, і 16 байтів шаблону на
 * дні стека задачі. Рядок FATAL stack overflow in <ім'я> (методичка 7.2, п. 2) */
#define configCHECK_FOR_STACK_OVERFLOW      2
/* Обробники SVC і PendSV мають бути саме з порту: ядро перевіряє таблицю
 * векторів при старті (configASSERT). Так ловиться обробник, згенерований CubeMX.
 * Лише в збірці ASSERT=1, як і сам configASSERT (нижче) */
#define configCHECK_HANDLER_INSTALLATION    RTOS_ASSERT

/* Run-time stats і trace — частина вимірювального модуля: є лише з PERF=1.
 * Лічильник часу — perf_now у тактах CPU (21 нс). 32 біти переповнюються за
 * 89,5 с, а S2 триває ~135 с, тому лічильник 64-бітний. Ціна і варіанти —
 * docs/rtos-design.md, бюджет пам'яті */
#define configGENERATE_RUN_TIME_STATS       PERF
#define configUSE_TRACE_FACILITY            PERF
/* vTaskList і vTaskGetRunTimeStats тягнуть sprintf; рядки tasks друкуємо самі
 * з uxTaskGetSystemState (крок 3) */
#define configUSE_STATS_FORMATTING_FUNCTIONS 0
#if PERF
#define configRUN_TIME_COUNTER_TYPE         uint64_t
void rtos_hal_runtime_start(void);
uint64_t rtos_hal_runtime_now(void);
/* Лічильник уже йде з HAL_Init; на старті ядра запам'ятовується база, і run-time
 * stats рахують від старту ядра, а не від скидання (Platform/rtos/rtos_hal.c) */
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() rtos_hal_runtime_start()
#define portGET_RUN_TIME_COUNTER_VALUE()    rtos_hal_runtime_now()
#endif

/* configASSERT - лише в збірці ASSERT=1 (app.mk; CI її збирає, TEST_KSTRESS
 * вмикає завжди). У продукті його немає: з обходом SIM_CRIT ядро з повним API
 * інакше лишало б менше 3 КБ flash на задачі кроків 2-3 (docs/rtos-design.md,
 * розділ 4). Переповнення стека, malloc і HardFault ловляться і без нього.
 * Без імені файлу (кожен рядок __FILE__ — ще кілька десятків байтів флешу на
 * виклик): номер рядка й адреса виклику — у .noinit, далі переривання вимкнено й
 * блимає LD4. Після скидання main друкує ASSERT line=N pc=0x... */
/* noinline: адреса повернення має бути місцем configASSERT, навіть якщо LTO
 * захоче вбудувати функцію в єдиного викликача */
void rtos_hal_assert_failed(uint32_t line) __attribute__((noreturn, noinline));
#if RTOS_ASSERT
#define configASSERT(x)                     do { if ((x) == 0) rtos_hal_assert_failed(__LINE__); } while (0)
#endif

/* Стрес-тест ядра в Wokwi (збірка TEST_KSTRESS=1, Tasks/kstress.c, у продукт не
 * входить): контрольні значення в кожному списку ядра (configASSERT, якщо список
 * зіпсовано) і лічильники перемикань усередині критичних секцій
 * (Platform/rtos/kstress/port_kstress.c) */
#ifdef TEST_KSTRESS
#define configUSE_LIST_DATA_INTEGRITY_CHECK_BYTES 1
void kstress_switched_out(void *tcb);
void kstress_switched_in(void *tcb);
#define traceTASK_SWITCHED_OUT()            kstress_switched_out((void *)pxCurrentTCB)
#define traceTASK_SWITCHED_IN()             kstress_switched_in((void *)pxCurrentTCB)
#endif

/* --- MPU немає (порт ARM_CM0 вимагає явно) ------------------------------- */
#define configENABLE_MPU                    0

/* --- Які функції API збирати -------------------------------------------- */
/* vTaskDelayUntil — періодичні задачі без накопичення похибки (gate 5 мс,
 * imu 10 мс). У V11 це INCLUDE_xTaskDelayUntil, INCLUDE_vTaskDelayUntil ядро
 * вважає застарілим і з обома разом не збирається */
#define INCLUDE_xTaskDelayUntil             1
#define INCLUDE_uxTaskGetStackHighWaterMark 1   /* stack по задачах (крок 3) */
#define INCLUDE_xTaskGetIdleTaskHandle      1   /* run-time і стек idle (крок 3) */
/* stream_buffer.c без неї не збирається (#error): буфер запам'ятовує задачу-читача */
#define INCLUDE_xTaskGetCurrentTaskHandle   1
/* Решта вимкнена (типово кілька з них 1). Без INCLUDE_vTaskSuspend очікування
 * portMAX_DELAY — не «назавжди», а 2^32 - 1 тіків (49,7 доби при 1 кГц) */
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

#endif /* FREERTOS_CONFIG_H */
