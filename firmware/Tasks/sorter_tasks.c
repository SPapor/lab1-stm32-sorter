#include "sorter_tasks.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "stream_buffer.h"
#include "stm32c0xx_hal.h"
#include "rtos_hal.h"
#include "uart_hal.h"
#include "app_task.h"
#if PERF
#include "perf.h"
#include "perf_hal.h"
#endif
#ifdef TEST_APIPROBE
#include "api_probe.h"
#endif
#if SIM_CRIT
#include "port_sim.h"
#endif
#ifdef OSMETRICS
#include "rtos_osm.h"
#endif
#if PERF
#include "board.h"
#endif

/* Пріоритети й стеки - docs/rtos-design.md, розділ 6. Стек у словах (4 B):
 * стартова оцінка за tools/stack_graph.py, перевірена командою stack після
 * посилок і збоїв I2C (sort: рядок SORT із сімома аргументами - 440 B з 512, тому
 * 160 слів; imu: I2C_RECOVER у глибині транзакції - 560 B з 640, тому 192) */
enum {
    T_ESTOP = 0, T_GATE, T_SORT, T_IMU, T_CLI, T_LOGGER, T_LCD,
#ifdef TEST_INVERSION
    T_HOG,
#endif
#ifdef TEST_STARVE
    T_SPIN,
#endif
    T_COUNT
};

#define QLOG_LEN        16u     /* записів журналу в черзі */
#define QPARCEL_LEN     2u      /* PARCEL_IN, що чекають на sort */
#define SB_RX_BYTES     64u     /* потоковий буфер прийому UART */
#define USART2_IRQ_PRIO 3u
#define SECOND_MS       1000u   /* logger: LOAD і EVT ready_* */
#ifdef TEST_INVERSION
#define HOG_MS          50u
#endif
#ifdef TEST_STACKOVF
#define CLI_WORDS       96u
#else
#define CLI_WORDS       192u
#endif
#ifdef TEST_STARVE
#define SPIN_PERIOD_MS  20000u
#define SPIN_GATE_RUNS  1000u
#endif
#ifdef TEST_QFLOOD
#define QF_EVERY        200u
#define QF_BURST        40u
#if TEST_QFLOOD == 2
#define QF_WAIT_MS      20u
#endif
#endif

typedef struct {
    const char *event;
    const char *fmt;
    uint32_t ms;
    uint8_t n;
    fmt_arg_t arg[TASKS_LOG_ARGS];
} log_rec_t;

typedef struct {
    uint32_t ms;
    uint32_t cyc;
#ifdef OSMETRICS
    rtos_osm_mark_t sent;
#endif
} parcel_t;

typedef struct {
    uint32_t takes;
    uint32_t timeouts;
    uint32_t wait_max_cyc;
} lock_stat_t;

static void estop_task(void *arg);
static void gate_task(void *arg);
static void sort_task(void *arg);
static void imu_task(void *arg);
static void cli_task(void *arg);
static void logger_task(void *arg);
static void lcd_task(void *arg);
#ifdef TEST_INVERSION
static void hog_task(void *arg);
#endif
#ifdef TEST_STARVE
static void spin_task(void *arg);
#endif

static const struct {
    const char *name;
    TaskFunction_t fn;
    uint16_t words;
    uint8_t prio;
} task_def[T_COUNT] = {
    [T_ESTOP]  = { "estop",  estop_task,  128u, 6u },
    [T_GATE]   = { "gate",   gate_task,   128u, 5u },
    [T_SORT]   = { "sort",   sort_task,   160u, 4u },
    [T_IMU]    = { "imu",    imu_task,    192u, 3u },
    [T_CLI]    = { "cli",    cli_task,    CLI_WORDS, 2u },
    [T_LOGGER] = { "logger", logger_task, 160u, 2u },
    [T_LCD]    = { "lcd",    lcd_task,    160u, 1u },
#ifdef TEST_INVERSION
    [T_HOG]    = { "hog",    hog_task,     64u, 2u },
#endif
#ifdef TEST_STARVE
    [T_SPIN]   = { "spin",   spin_task,    64u, 4u },
#endif
};

/* Тайм-аут одного очікування м'ютекса, мс: I2C - довше за транзакцію з трьома
 * спробами після NACK (~100 мс у Wokwi), UART - за найдовший рядок */
static const uint16_t lock_timeout_ms[TASKS_LOCK_COUNT] = {
    [TASKS_LOCK_ADC] = 50u, [TASKS_LOCK_I2C] = 150u,
    [TASKS_LOCK_SORTER] = 50u, [TASKS_LOCK_UART] = 500u,
};
#if PERF
static const char *const lock_name[TASKS_LOCK_COUNT] = {
    [TASKS_LOCK_ADC] = "adc", [TASKS_LOCK_I2C] = "i2c",
    [TASKS_LOCK_SORTER] = "sorter", [TASKS_LOCK_UART] = "uart",
};
#endif

static const tasks_ops_t *ops;
static TaskHandle_t th[T_COUNT];
static QueueHandle_t q_log;
static QueueHandle_t q_parcel;
static StreamBufferHandle_t sb_rx;
static SemaphoreHandle_t mtx[TASKS_LOCK_COUNT];
static volatile uint8_t running;

static TaskHandle_t lock_owner[TASKS_LOCK_COUNT];
static uint8_t lock_depth[TASKS_LOCK_COUNT];
#ifdef OSMETRICS
static volatile uint8_t estop_waiting;
#endif
#if defined(TEST_INVERSION) && PERF
static perf_stat_t inv_wait = { .name = "inv_imu_wait" };
#endif

static uint32_t log_drop;       /* черга повна */
static uint32_t log_trunc;      /* аргументів більше за TASKS_LOG_ARGS */
static uint32_t log_lines;      /* надруковано logger */
static UBaseType_t log_max_used;
static uint32_t rx_drop;        /* потоковий буфер повний */
static lock_stat_t lock_stat[TASKS_LOCK_COUNT];
#if PERF
static uint64_t rt_base[T_COUNT + 1u];   /* run-time на stats reset, останній - idle */
static uint64_t rt_total_base;
#if SIM_CRIT
static port_sim_counters_t sim_base;
#endif
#endif

/* Мс -> тіки з округленням угору, щонайменше 1 тік (TICK_HZ=100: gate 5 мс - 1 тік = 10 мс) */
static TickType_t ms_ticks(uint32_t ms)
{
    TickType_t t = (TickType_t)((ms * (uint32_t)configTICK_RATE_HZ + 999u) / 1000u);

    return t ? t : 1u;
}

int tasks_running(void)
{
    return running;
}

void tasks_lock(tasks_lock_t l)
{
    TaskHandle_t me;
#if PERF
    uint32_t t0;
#endif

    if (!running)
        return;
    me = xTaskGetCurrentTaskHandle();
    if (lock_owner[l] == me) {
        lock_depth[l]++;
        return;
    }
#if PERF
    t0 = perf_now();
#endif
    while (xSemaphoreTake(mtx[l], ms_ticks(lock_timeout_ms[l])) != pdPASS)
        lock_stat[l].timeouts++;   /* лише лічильник: усі, хто чекає, рахують тут */
    /* далі - під м'ютексом */
    lock_owner[l] = me;
    lock_depth[l] = 1;
#if PERF
    uint32_t w = perf_now() - t0;
    lock_stat[l].takes++;
    if (w > lock_stat[l].wait_max_cyc)
        lock_stat[l].wait_max_cyc = w;
#ifdef TEST_INVERSION
    if (l == TASKS_LOCK_I2C && me == th[T_IMU])
        perf_add(&inv_wait, w);
#endif
#endif
}

void tasks_unlock(tasks_lock_t l)
{
    if (!running || lock_depth[l] == 0u || lock_owner[l] != xTaskGetCurrentTaskHandle())
        return;
    if (--lock_depth[l] == 0u) {
        lock_owner[l] = NULL;
        (void)xSemaphoreGive(mtx[l]);
    }
}

#if !defined(TEST_KSTRESS) && !defined(TEST_CRITCOST)
static void uart_lock(void)
{
    tasks_lock(TASKS_LOCK_UART);
}

static void uart_unlock(void)
{
    tasks_unlock(TASKS_LOCK_UART);
}
#endif

void tasks_snapshot(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;

    taskENTER_CRITICAL();
    while (n-- > 0u)
        *d++ = *s++;
    taskEXIT_CRITICAL();
}

void tasks_log(const char *event, const char *fmt, va_list ap)
{
    log_rec_t r;
    unsigned n;
    UBaseType_t used;

    r.event = event;
    r.fmt = fmt;
    r.ms = HAL_GetTick();
    n = fmt_vargs(fmt, ap, r.arg, TASKS_LOG_ARGS);
    if (n > TASKS_LOG_ARGS) {
        log_trunc++;
        n = TASKS_LOG_ARGS;
    }
    r.n = (uint8_t)n;
    if (xQueueSend(q_log, &r, 0) != pdPASS) {
        log_drop++;
        return;
    }
    used = uxQueueMessagesWaiting(q_log);
    if (used > log_max_used)
        log_max_used = used;
}

void tasks_parcel(uint32_t in_ms, uint32_t in_cyc)
{
    parcel_t p = { .ms = in_ms, .cyc = in_cyc };

#ifdef OSMETRICS
    rtos_osm_stamp(&p.sent);
#endif
    (void)xQueueSend(q_parcel, &p, 0);
}

void tasks_estop_from_isr(void)
{
    BaseType_t woken = pdFALSE;

    if (!running)
        return;
#ifdef OSMETRICS
    rtos_osm_set(OSM_ISR_ESTOP);
#endif
    vTaskNotifyGiveFromISR(th[T_ESTOP], &woken);
    portYIELD_FROM_ISR(woken);
}

int tasks_rx_getc(void)
{
    uint8_t b;

    return (xStreamBufferReceive(sb_rx, &b, 1u, 0) == 1u) ? (int)b : -1;
}

/* Прийом UART: усе, що є в RDR, - у потоковий буфер задачі cli. Вектор USART2
 * CubeMX не генерує (у .ioc переривання вимкнене), він тут, як і службові піни
 * (docs/rtos-design.md, розділ 6) */
void USART2_IRQHandler(void)
{
    BaseType_t woken = pdFALSE;
    int ch;

    if (rtos_hal_irq_defer(USART2_IRQn))
        return;
    while ((ch = uart_hal_getc()) >= 0) {
        uint8_t b = (uint8_t)ch;

        if (xStreamBufferSendFromISR(sb_rx, &b, 1u, &woken) != 1u)
            rx_drop++;
    }
#ifdef OSMETRICS
    rtos_osm_set(OSM_ISR_CLI);
#endif
    portYIELD_FROM_ISR(woken);
}

/* ---- задачі ---------------------------------------------------------------- */

static void sleep_forever(void)
{
    for (;;)
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

static void estop_task(void *arg)
{
    (void)arg;
    for (;;) {
        TickType_t t = xTaskGetTickCount();

        xTaskDelayUntil(&t, ms_ticks(ops->estop_debounce_ms));
        ops->estop();
        for (;;) {
#ifdef OSMETRICS
            estop_waiting = 1;
#endif
            (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
#ifdef OSMETRICS
            estop_waiting = 0;
            if (rtos_osm_take(OSM_CTXSW) && !rtos_osm_pending(OSM_ISR_ESTOP))
                continue;
            (void)rtos_osm_take(OSM_ISR_ESTOP);
#endif
            break;
        }
    }
}

#ifdef TEST_STARVE
static volatile uint8_t spin_armed;
static volatile uint8_t spin_release;
static uint32_t spin_windows;

static void gate_starve(void)
{
    static uint32_t runs;

    if (!spin_armed)
        return;
    if (++runs >= SPIN_GATE_RUNS) {
        runs = 0;
        spin_armed = 0;
        spin_release = 1;
    }
}

static void spin_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();

    (void)arg;
    for (;;) {
        xTaskDelayUntil(&last, ms_ticks(SPIN_PERIOD_MS));
        spin_release = 0;
        spin_armed = 1;
        while (!spin_release) {
        }
        spin_windows++;
    }
}
#endif

#ifdef TEST_QFLOOD
static uint32_t qf_sent;
static uint32_t qf_ok;
static uint32_t qf_lost;
static uint32_t qf_wait_max_cyc;

static void gate_qflood(void)
{
    static uint32_t runs;
    static uint32_t seq;
#if TEST_QFLOOD == 2
    const TickType_t wait = ms_ticks(QF_WAIT_MS);
#else
    const TickType_t wait = 0;
#endif

    if (++runs < QF_EVERY)
        return;
    runs = 0;
    for (unsigned i = 0; i < QF_BURST; i++) {
        log_rec_t r = { .event = "QFLOOD", .fmt = "n=%lu", .ms = HAL_GetTick(), .n = 1u };
        uint32_t t0 = perf_now();
        uint32_t w;

        r.arg[0] = (fmt_arg_t)++seq;
        qf_sent++;
        if (xQueueSend(q_log, &r, wait) == pdPASS) {
            qf_ok++;
        } else {
            qf_lost++;
            log_drop++;
        }
        w = perf_now() - t0;
        if (w > qf_wait_max_cyc)
            qf_wait_max_cyc = w;
    }
}
#endif

static void gate_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();

    (void)arg;
    for (;;) {
        xTaskDelayUntil(&last, ms_ticks(ops->gate_ms));
        ops->gate();
#ifdef TEST_STARVE
        gate_starve();
#endif
#ifdef TEST_QFLOOD
        gate_qflood();
#endif
    }
}

static void sort_task(void *arg)
{
    parcel_t p;

    (void)arg;
    for (;;)
        if (xQueueReceive(q_parcel, &p, portMAX_DELAY) == pdPASS) {
#ifdef OSMETRICS
            rtos_osm_add(OSM_QUEUE, &p.sent);
#endif
            ops->sort(p.ms, p.cyc);
        }
}

static void imu_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();

    (void)arg;
    if (ops->imu == NULL)
        sleep_forever();
    for (;;) {
        xTaskDelayUntil(&last, ms_ticks(*ops->imu_ms));
        ops->imu();
    }
}

static void cli_task(void *arg)
{
    uint8_t b;

    (void)arg;
    /* Переривання прийому - лише тепер: до старту ядра буфера й задачі ще немає */
    NVIC_SetPriority(USART2_IRQn, USART2_IRQ_PRIO);
    NVIC_EnableIRQ(USART2_IRQn);
    uart_hal_rx_irq_enable();
    for (;;) {
#ifdef OSMETRICS
        rtos_osm_clear(OSM_ISR_CLI);
        int idle = xStreamBufferIsEmpty(sb_rx) == pdTRUE;
#endif
        if (xStreamBufferReceive(sb_rx, &b, 1u, portMAX_DELAY) == 1u) {
#ifdef OSMETRICS
            if (idle)
                (void)rtos_osm_take(OSM_ISR_CLI);
#endif
            ops->cli(b);
        }
    }
}

/* Друкує записи qLog; раз на SECOND_MS - ops->second, навіть якщо черга не
 * порожніє (перевірка після кожного запису) */
static void logger_task(void *arg)
{
    log_rec_t r;
    TickType_t next = xTaskGetTickCount() + ms_ticks(SECOND_MS);

    (void)arg;
    for (;;) {
        TickType_t now = xTaskGetTickCount();
        TickType_t wait = ((TickType_t)(next - now) > ms_ticks(SECOND_MS)) ? 0u : (TickType_t)(next - now);

        if (xQueueReceive(q_log, &r, wait) == pdPASS) {
            ops->log_line(r.ms, r.event, r.fmt, r.arg, r.n);
            log_lines++;
        }
        if ((TickType_t)(xTaskGetTickCount() - next) <= ms_ticks(SECOND_MS)) {
            next += ms_ticks(SECOND_MS);
            ops->second();
        }
    }
}

static void lcd_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();

    (void)arg;
    if (ops->lcd == NULL)
        sleep_forever();
    for (;;) {
        xTaskDelayUntil(&last, ms_ticks(*ops->lcd_ms));
#ifdef TEST_INVERSION
        tasks_lock(TASKS_LOCK_I2C);
        xTaskNotifyGive(th[T_HOG]);
        ops->lcd();
        tasks_unlock(TASKS_LOCK_I2C);
#else
        ops->lcd();
#endif
    }
}

#ifdef TEST_INVERSION
static void hog_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t t0;

        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        t0 = HAL_GetTick();
        while (HAL_GetTick() - t0 < HOG_MS) {
        }
    }
}
#endif

/* ---- старт ------------------------------------------------------------------- */

void tasks_start(const tasks_ops_t *o)
{
#if PERF
    rtos_hal_mark_api();      /* далі до старту ядра переривання вимкнені */
#endif
#if defined(TEST_KSTRESS) || defined(TEST_CRITCOST)
    (void)o;
    app_task_start();
#else
    /* Купи не вистачило - vApplicationMallocFailedHook (рядок FATAL malloc) */
    ops = o;
#ifdef TEST_APIPROBE
    api_probe_ref();
#endif
    q_log = xQueueCreate(QLOG_LEN, sizeof(log_rec_t));
    q_parcel = xQueueCreate(QPARCEL_LEN, sizeof(parcel_t));
    sb_rx = xStreamBufferCreate(SB_RX_BYTES, 1u);
#ifdef OSMETRICS
    rtos_osm_init();
#endif
    for (unsigned i = 0; i < TASKS_LOCK_COUNT; i++) {
#if TEST_INVERSION == 1
        if (i == TASKS_LOCK_I2C) {
            mtx[i] = xSemaphoreCreateBinary();
            (void)xSemaphoreGive(mtx[i]);
            continue;
        }
#endif
        mtx[i] = xSemaphoreCreateMutex();
    }
    for (unsigned i = 0; i < T_COUNT; i++)
        (void)xTaskCreate(task_def[i].fn, task_def[i].name, task_def[i].words, NULL,
                          task_def[i].prio, &th[i]);
#if PERF
    rtos_hal_trace_task(0u, th[T_LCD], BOARD_PROBE_LCD_PIN);
#ifdef TEST_INVERSION
    rtos_hal_trace_task(1u, th[T_IMU], BOARD_PROBE_GATE_PIN);
    rtos_hal_trace_task(2u, th[T_HOG], BOARD_PROBE_ESTOP_PIN);
#endif
#endif
    uart_hal_set_lock(uart_lock, uart_unlock);
    running = 1;
    rtos_hal_start_scheduler();
#endif
}

/* ---- звіти ------------------------------------------------------------------- */

#if PERF
/* ‰ від part у total; total - такти run-time, тож total / 1000 вміщується в 32 біти до ~25 год */
static uint32_t per_mille(uint64_t part, uint64_t total)
{
    uint32_t k = perf_udiv64(total, 1000u, NULL);

    return (k == 0u) ? 0u : perf_udiv64(part, k, NULL);
}

void tasks_list(tasks_out_fn out)
{
    static const char *const state_name[] = {
        [eRunning] = "run", [eReady] = "ready", [eBlocked] = "blocked",
        [eSuspended] = "suspended", [eDeleted] = "deleted",
    };
    uint64_t total = rtos_hal_runtime_now() - rt_total_base;

    for (unsigned i = 0; i <= T_COUNT; i++) {
        TaskHandle_t h = (i < T_COUNT) ? th[i] : xTaskGetIdleTaskHandle();
        TaskStatus_t s;
        uint64_t rt;

        vTaskGetInfo(h, &s, pdTRUE, eInvalid);
        rt = s.ulRunTimeCounter - rt_base[i];
        out("TASK", "name=%s prio=%lu cpu_pm=%lu stack_free=%u state=%s run_ms=%lu",
            s.pcTaskName, (unsigned long)s.uxBasePriority,
            (unsigned long)per_mille(rt, total),
            (unsigned)(s.usStackHighWaterMark * sizeof(StackType_t)),
            (s.eCurrentState <= eDeleted) ? state_name[s.eCurrentState] : "?",
            (unsigned long)perf_udiv64(rt, perf_hal_cpu_hz() / 1000u, NULL));
    }
}

void tasks_report(tasks_out_fn out)
{
#if SIM_CRIT
    uint64_t total = rtos_hal_runtime_now() - rt_total_base;
#endif

    tasks_list(out);
    for (unsigned i = 0; i < TASKS_LOCK_COUNT; i++)
        out("MTX", "name=%s takes=%lu timeouts=%lu wait_max_us=%lu", lock_name[i],
            (unsigned long)lock_stat[i].takes, (unsigned long)lock_stat[i].timeouts,
            (unsigned long)perf_cyc_to_us(lock_stat[i].wait_max_cyc, perf_hal_cpu_hz() / 1000000u));
    out("LOGQ", "len=%u max_used=%lu lines=%lu drop=%lu trunc=%lu rx_drop=%lu",
        (unsigned)QLOG_LEN, (unsigned long)log_max_used, (unsigned long)log_lines,
        (unsigned long)log_drop, (unsigned long)log_trunc, (unsigned long)rx_drop);
#ifdef TEST_QFLOOD
    out("QFLOOD", "burst=%u every_ms=%lu sent=%lu ok=%lu lost=%lu wait_max_us=%lu", (unsigned)QF_BURST,
        (unsigned long)(QF_EVERY * ops->gate_ms), (unsigned long)qf_sent, (unsigned long)qf_ok,
        (unsigned long)qf_lost,
        (unsigned long)perf_cyc_to_us(qf_wait_max_cyc, perf_hal_cpu_hz() / 1000000u));
#endif
#ifdef TEST_STARVE
    out("STARVE", "windows=%lu period_ms=%lu spin_ms=%lu", (unsigned long)spin_windows,
        (unsigned long)SPIN_PERIOD_MS, (unsigned long)(SPIN_GATE_RUNS * ops->gate_ms));
#endif
#if SIM_CRIT
    {
        /* Обхід PRIMASK для Wokwi (docs/rtos-design.md, розділ 2): скільки
         * критичних секцій за вікно від stats reset - для ціни обходу в
         * «вартості ОС» (крок 3: секції × такти пари з TEST_CRITCOST) */
        port_sim_counters_t c;

        port_sim_counters(&c);
        out("SIMCRIT", "win_ms=%lu task_n=%lu isr_n=%lu deferred_tick=%lu deferred_pendsv=%lu "
            "deferred_irq=%lu",
            (unsigned long)perf_udiv64(total, perf_hal_cpu_hz() / 1000u, NULL),
            (unsigned long)(c.crit_task_n - sim_base.crit_task_n),
            (unsigned long)(c.crit_isr_n - sim_base.crit_isr_n),
            (unsigned long)(c.deferred_tick - sim_base.deferred_tick),
            (unsigned long)(c.deferred_pendsv - sim_base.deferred_pendsv),
            (unsigned long)(c.deferred_irq - sim_base.deferred_irq));
    }
#endif
}

void tasks_stats_reset(void)
{
    for (unsigned i = 0; i <= T_COUNT; i++) {
        TaskHandle_t h = (i < T_COUNT) ? th[i] : xTaskGetIdleTaskHandle();
        TaskStatus_t s;

        vTaskGetInfo(h, &s, pdFALSE, eInvalid);
        rt_base[i] = s.ulRunTimeCounter;
    }
    rt_total_base = rtos_hal_runtime_now();
#if SIM_CRIT
    port_sim_counters(&sim_base);
#endif
    for (unsigned i = 0; i < TASKS_LOCK_COUNT; i++)
        lock_stat[i] = (lock_stat_t){ 0u, 0u, 0u };
    log_max_used = 0;
    log_drop = 0;
    log_trunc = 0;
    log_lines = 0;
    rx_drop = 0;
#ifdef OSMETRICS
    rtos_osm_reset();
#endif
#ifdef TEST_INVERSION
    perf_stat_reset(&inv_wait);
#endif
}

#ifdef TEST_INVERSION
const perf_stat_t *tasks_inv_wait(void)
{
    return &inv_wait;
}
#endif

#ifdef OSMETRICS
void tasks_osbench(unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        rtos_osm_mark_t m;

        rtos_osm_clear(OSM_CTXSW);
        if (estop_waiting) {
            rtos_osm_set(OSM_CTXSW);
            xTaskNotifyGive(th[T_ESTOP]);
        }
        rtos_osm_stamp(&m);
        (void)xSemaphoreTake(mtx[TASKS_LOCK_SORTER], portMAX_DELAY);
        (void)xSemaphoreGive(mtx[TASKS_LOCK_SORTER]);
        rtos_osm_add(OSM_MUTEX, &m);
    }
}
#endif

void tasks_stack_report(tasks_out_fn out)
{
    for (unsigned i = 0; i <= T_COUNT; i++) {
        TaskHandle_t h = (i < T_COUNT) ? th[i] : xTaskGetIdleTaskHandle();
        uint32_t words = (i < T_COUNT) ? task_def[i].words : configMINIMAL_STACK_SIZE;
        uint32_t free_b = (uint32_t)uxTaskGetStackHighWaterMark(h) * sizeof(StackType_t);
        uint32_t size_b = words * sizeof(StackType_t);

        out("STACK", "name=%s used=%lu reserved=%lu free=%lu", pcTaskGetName(h),
            (unsigned long)(size_b - free_b), (unsigned long)size_b, (unsigned long)free_b);
    }
}

void tasks_heap_report(tasks_out_fn out)
{
    out("OSHEAP", "total=%lu free=%lu min_free=%lu", (unsigned long)configTOTAL_HEAP_SIZE,
        (unsigned long)xPortGetFreeHeapSize(), (unsigned long)xPortGetMinimumEverFreeHeapSize());
}

uint64_t tasks_idle_cyc(void)
{
    return ulTaskGetIdleRunTimeCounter();
}
#endif
