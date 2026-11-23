#include "sorter_tasks.h"
#include "qpc.h"
#include "stm32c0xx_hal.h"
#include "rtos_hal.h"
#include "uart_hal.h"
#include "app_task.h"
#if PERF
#include "perf.h"
#include "perf_hal.h"
#include "board.h"
#endif
#if SIM_CRIT
#include "port_sim.h"
#endif


enum { T_ESTOP = 0, T_GATE, T_SORT, T_IMU, T_CLI, T_LOGGER, T_LCD, T_COUNT };
enum { LOG_SIG = Q_USER_SIG, TICK_SIG, PARCEL_SIG, ESTOP_SIG, RX_SIG, SECOND_SIG };

#define QLOG_LEN        16u
#define SB_RX_BYTES     64u
#define USART2_IRQ_PRIO 3u
#define SECOND_MS       1000u

typedef struct {
    QEvt super;
    const char *event;
    const char *fmt;
    uint32_t ms;
    uint8_t n;
    fmt_arg_t arg[TASKS_LOG_ARGS];
} LogEvt;

typedef struct {
    QEvt super;
    uint32_t ms;
    uint32_t cyc;
} ParcelEvt;

typedef struct {
    QActive super;
    QTimeEvt te;
    QTimeEvt te2;
    uint16_t period;
} Ao;

static const struct {
    const char *name;
    uint16_t words;
    uint8_t prio;
    uint8_t qlen;
} task_def[T_COUNT] = {
    [T_ESTOP]  = { "estop",  128u, 7u, 2u },
    [T_GATE]   = { "gate",   128u, 6u, 2u },
    [T_SORT]   = { "sort",   160u, 5u, 3u },
    [T_IMU]    = { "imu",    192u, 4u, 2u },
    [T_CLI]    = { "cli",    192u, 3u, 2u },
    [T_LOGGER] = { "logger", 160u, 2u, QLOG_LEN },
    [T_LCD]    = { "lcd",    160u, 1u, 2u },
};

static const tasks_ops_t *ops;
static Ao ao[T_COUNT];
static StackType_t stk_estop[128], stk_gate[128], stk_sort[160], stk_imu[192], stk_cli[192],
    stk_logger[160], stk_lcd[160];
static StackType_t *const stk[T_COUNT] = { stk_estop, stk_gate, stk_sort, stk_imu, stk_cli, stk_logger, stk_lcd };
static QEvtPtr q_estop[2], q_gate[2], q_sort[3], q_imu[2], q_cli[2], q_logger[QLOG_LEN], q_lcd[2];
static QEvtPtr *const qsto[T_COUNT] = { q_estop, q_gate, q_sort, q_imu, q_cli, q_logger, q_lcd };
static QF_MPOOL_EL(ParcelEvt) pool_small[4];
static QF_MPOOL_EL(LogEvt) pool_log[QLOG_LEN + 2u];
static StaticTask_t idle_tcb;
static StackType_t idle_stk[configMINIMAL_STACK_SIZE];
static volatile uint8_t running;
static uint8_t rx_buf[SB_RX_BYTES];
static volatile uint8_t rx_head, rx_tail;

static const QEvt estop_evt = QEVT_INITIALIZER(ESTOP_SIG);
static const QEvt rx_evt = QEVT_INITIALIZER(RX_SIG);

static uint32_t log_drop;
static uint32_t log_trunc;
static uint32_t log_lines;
static uint32_t rx_drop;
#if PERF
static uint64_t rt_base[T_COUNT + 1u];
static uint64_t rt_total_base;
#if SIM_CRIT
static port_sim_counters_t sim_base;
#endif
#endif

static uint32_t ms_ticks(uint32_t ms)
{
    uint32_t t = (ms * (uint32_t)configTICK_RATE_HZ + 999u) / 1000u;

    return t ? t : 1u;
}

int tasks_running(void)
{
    return running;
}

void tasks_lock(tasks_lock_t l)
{
    (void)l;
}

void tasks_unlock(tasks_lock_t l)
{
    (void)l;
}

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
    LogEvt *e = Q_NEW_X(LogEvt, 1U, LOG_SIG);
    unsigned n;

    if (e == NULL) {
        log_drop++;
        return;
    }
    e->event = event;
    e->fmt = fmt;
    e->ms = HAL_GetTick();
    n = fmt_vargs(fmt, ap, e->arg, TASKS_LOG_ARGS);
    if (n > TASKS_LOG_ARGS) {
        log_trunc++;
        n = TASKS_LOG_ARGS;
    }
    e->n = (uint8_t)n;
    if (!QACTIVE_POST_X(&ao[T_LOGGER].super, &e->super, 1U, NULL))
        log_drop++;
}

void tasks_parcel(uint32_t in_ms, uint32_t in_cyc)
{
    ParcelEvt *p = Q_NEW(ParcelEvt, PARCEL_SIG);

    p->ms = in_ms;
    p->cyc = in_cyc;
    QACTIVE_POST(&ao[T_SORT].super, &p->super, NULL);
}

void tasks_estop_from_isr(void)
{
    BaseType_t woken = pdFALSE;

    if (!running)
        return;
    QACTIVE_POST_FROM_ISR(&ao[T_ESTOP].super, &estop_evt, &woken, NULL);
    portYIELD_FROM_ISR(woken);
}

int tasks_rx_getc(void)
{
    int ch = -1;

    taskENTER_CRITICAL();
    if (rx_tail != rx_head) {
        ch = rx_buf[rx_tail];
        rx_tail = (uint8_t)((rx_tail + 1u) % SB_RX_BYTES);
    }
    taskEXIT_CRITICAL();
    return ch;
}

void USART2_IRQHandler(void)
{
    BaseType_t woken = pdFALSE;
    int ch;
    int was_empty;

    if (rtos_hal_irq_defer(USART2_IRQn))
        return;
    was_empty = rx_head == rx_tail;
    while ((ch = uart_hal_getc()) >= 0) {
        uint8_t next = (uint8_t)((rx_head + 1u) % SB_RX_BYTES);

        if (next == rx_tail) {
            rx_drop++;
            continue;
        }
        rx_buf[rx_head] = (uint8_t)ch;
        rx_head = next;
    }
    if (was_empty && rx_head != rx_tail)
        (void)QACTIVE_POST_X_FROM_ISR(&ao[T_CLI].super, &rx_evt, 1U, &woken, NULL);
    portYIELD_FROM_ISR(woken);
}

static void arm_period(Ao *me, uint16_t ms)
{
    uint32_t t = ms_ticks(ms);

    me->period = ms;
    QTimeEvt_armX(&me->te, t, t);
}

static void follow_knob(Ao *me, const volatile uint16_t *knob)
{
    if (*knob != me->period) {
        (void)QTimeEvt_disarm(&me->te);
        arm_period(me, *knob);
    }
}

static QState estop_run(Ao *me, QEvt const *e)
{
    switch (e->sig) {
    case ESTOP_SIG:
        (void)QTimeEvt_disarm(&me->te);
        QTimeEvt_armX(&me->te, ms_ticks(ops->estop_debounce_ms), 0U);
        return Q_HANDLED();
    case TICK_SIG:
        ops->estop();
        return Q_HANDLED();
    default:
        return Q_SUPER(&QHsm_top);
    }
}

static QState gate_run(Ao *me, QEvt const *e)
{
    (void)me;
    switch (e->sig) {
    case TICK_SIG:
        ops->gate();
        return Q_HANDLED();
    default:
        return Q_SUPER(&QHsm_top);
    }
}

static QState sort_run(Ao *me, QEvt const *e)
{
    (void)me;
    switch (e->sig) {
    case PARCEL_SIG:
        ops->sort(Q_EVT_CAST(ParcelEvt)->ms, Q_EVT_CAST(ParcelEvt)->cyc);
        return Q_HANDLED();
    default:
        return Q_SUPER(&QHsm_top);
    }
}

static QState imu_run(Ao *me, QEvt const *e)
{
    switch (e->sig) {
    case TICK_SIG:
        follow_knob(me, ops->imu_ms);
        ops->imu();
        return Q_HANDLED();
    default:
        return Q_SUPER(&QHsm_top);
    }
}

static QState cli_run(Ao *me, QEvt const *e)
{
    int ch;

    (void)me;
    switch (e->sig) {
    case RX_SIG:
        if ((ch = tasks_rx_getc()) >= 0)
            ops->cli(ch);
        return Q_HANDLED();
    default:
        return Q_SUPER(&QHsm_top);
    }
}

static QState logger_run(Ao *me, QEvt const *e)
{
    (void)me;
    switch (e->sig) {
    case LOG_SIG: {
        LogEvt const *l = Q_EVT_CAST(LogEvt);

        ops->log_line(l->ms, l->event, l->fmt, l->arg, l->n);
        log_lines++;
        return Q_HANDLED();
    }
    case SECOND_SIG:
        ops->second();
        return Q_HANDLED();
    default:
        return Q_SUPER(&QHsm_top);
    }
}

static QState lcd_run(Ao *me, QEvt const *e)
{
    switch (e->sig) {
    case TICK_SIG:
        follow_knob(me, ops->lcd_ms);
        ops->lcd();
        return Q_HANDLED();
    default:
        return Q_SUPER(&QHsm_top);
    }
}

static QState ao_initial(Ao *me, void const *par)
{
    (void)par;
    switch (me - ao) {
    case T_ESTOP:
        ops->estop();
        return Q_TRAN(&estop_run);
    case T_GATE:
        arm_period(me, ops->gate_ms);
        return Q_TRAN(&gate_run);
    case T_SORT:
        return Q_TRAN(&sort_run);
    case T_IMU:
        if (ops->imu != NULL)
            arm_period(me, *ops->imu_ms);
        return Q_TRAN(&imu_run);
    case T_CLI:
        return Q_TRAN(&cli_run);
    case T_LOGGER:
        QTimeEvt_armX(&me->te2, ms_ticks(SECOND_MS), ms_ticks(SECOND_MS));
        return Q_TRAN(&logger_run);
    default:
        if (ops->lcd != NULL)
            arm_period(me, *ops->lcd_ms);
        return Q_TRAN(&lcd_run);
    }
}

void vApplicationTickHook(void)
{
    BaseType_t woken = pdFALSE;

    QTIMEEVT_TICK_FROM_ISR(0U, &woken, NULL);
    portYIELD_FROM_ISR(woken);
}

void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **st, uint32_t *n)
{
    *tcb = &idle_tcb;
    *st = idle_stk;
    *n = configMINIMAL_STACK_SIZE;
}

Q_NORETURN Q_onError(char const * const module, int_t const id)
{
    rtos_hal_raw_puts("FATAL qp ");
    rtos_hal_raw_puts(module);
    rtos_hal_raw_puts(" ");
    rtos_hal_raw_putu((uint32_t)id, 10u);
    rtos_hal_fatal_create("qp");
}

void QF_onStartup(void)
{
    NVIC_SetPriority(USART2_IRQn, USART2_IRQ_PRIO);
    NVIC_EnableIRQ(USART2_IRQn);
    uart_hal_rx_irq_enable();
}

void QF_onCleanup(void)
{
}

void tasks_start(const tasks_ops_t *o)
{
#if PERF
    rtos_hal_mark_api();
#endif
#if defined(TEST_KSTRESS) || defined(TEST_CRITCOST)
    (void)o;
    app_task_start();
#else
    ops = o;
    QF_init();
    QF_poolInit(pool_small, sizeof pool_small, sizeof pool_small[0]);
    QF_poolInit(pool_log, sizeof pool_log, sizeof pool_log[0]);
    for (unsigned i = 0; i < T_COUNT; i++) {
        QActive_ctor(&ao[i].super, Q_STATE_CAST(&ao_initial));
        QTimeEvt_ctorX(&ao[i].te, &ao[i].super, TICK_SIG, 0U);
        QTimeEvt_ctorX(&ao[i].te2, &ao[i].super, SECOND_SIG, 0U);
        QActive_setAttr(&ao[i].super, TASK_NAME_ATTR, task_def[i].name);
        QActive_start(&ao[i].super, task_def[i].prio, qsto[i], task_def[i].qlen, stk[i],
                      task_def[i].words * sizeof(StackType_t), NULL);
    }
#if PERF
    rtos_hal_trace_task(0u, ao[T_LCD].super.thread, BOARD_PROBE_LCD_PIN);
#endif
    running = 1;
    (void)QF_run();
    for (;;) {
    }
#endif
}

#if PERF
static uint32_t per_mille(uint64_t part, uint64_t total)
{
    uint32_t k = perf_udiv64(total, 1000u, NULL);

    return (k == 0u) ? 0u : perf_udiv64(part, k, NULL);
}

static TaskHandle_t th(unsigned i)
{
    return (i < T_COUNT) ? ao[i].super.thread : xTaskGetIdleTaskHandle();
}

void tasks_list(tasks_out_fn out)
{
    static const char *const state_name[] = {
        [eRunning] = "run", [eReady] = "ready", [eBlocked] = "blocked",
        [eSuspended] = "suspended", [eDeleted] = "deleted",
    };
    uint64_t total = rtos_hal_runtime_now() - rt_total_base;

    for (unsigned i = 0; i <= T_COUNT; i++) {
        TaskStatus_t s;
        uint64_t rt;

        vTaskGetInfo(th(i), &s, pdTRUE, eInvalid);
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
    out("LOGQ", "len=%u max_used=%lu lines=%lu drop=%lu trunc=%lu rx_drop=%lu",
        (unsigned)QLOG_LEN, (unsigned long)(QLOG_LEN + 2u - QF_getPoolMin(2U)), (unsigned long)log_lines,
        (unsigned long)log_drop, (unsigned long)log_trunc, (unsigned long)rx_drop);
#if SIM_CRIT
    {
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
        TaskStatus_t s;

        vTaskGetInfo(th(i), &s, pdFALSE, eInvalid);
        rt_base[i] = s.ulRunTimeCounter;
    }
    rt_total_base = rtos_hal_runtime_now();
#if SIM_CRIT
    port_sim_counters(&sim_base);
#endif
    log_drop = 0;
    log_trunc = 0;
    log_lines = 0;
    rx_drop = 0;
}

void tasks_stack_report(tasks_out_fn out)
{
    for (unsigned i = 0; i <= T_COUNT; i++) {
        uint32_t words = (i < T_COUNT) ? task_def[i].words : configMINIMAL_STACK_SIZE;
        uint32_t free_b = (uint32_t)uxTaskGetStackHighWaterMark(th(i)) * sizeof(StackType_t);
        uint32_t size_b = words * sizeof(StackType_t);

        out("STACK", "name=%s used=%lu reserved=%lu free=%lu", pcTaskGetName(th(i)),
            (unsigned long)(size_b - free_b), (unsigned long)size_b, (unsigned long)free_b);
    }
}

void tasks_heap_report(tasks_out_fn out)
{
    for (uint_fast8_t p = 1U; p <= 2U; p++)
        out("POOL", "n=%u min_free=%u", (unsigned)p, (unsigned)QF_getPoolMin(p));
}

uint64_t tasks_idle_cyc(void)
{
    return ulTaskGetIdleRunTimeCounter();
}
#endif
