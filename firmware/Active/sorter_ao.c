#include "sorter_tasks.h"
#include "qpc.h"
#include "stm32c0xx_hal.h"
#include "rtos_hal.h"
#include "uart_hal.h"
#ifdef TEST_QSTRESS
#include "qstress.h"
#endif
#if PERF
#include "perf.h"
#include "perf_hal.h"
#include "board.h"
#endif
#if SIM_CRIT
#include "port_sim.h"
#endif


enum { T_ESTOP = 0, T_GATE, T_SORT, T_BUS, T_IMU, T_CLI, T_LOGGER, T_LCD, T_COUNT };
enum { LOG_SIG = Q_USER_SIG, TICK_SIG, PARCEL_SIG, ESTOP_SIG, RX_SIG, SECOND_SIG, BUS_REQ_SIG, BUS_DONE_SIG };
enum { BUS_MPU_READ = 0, BUS_LCD_ROW };

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
    QEvt super;
    QActive *reply_to;
    uint32_t t_post;
    uint8_t op;
    uint8_t row;
    char text[TASKS_LCD_ROW];
} BusReqEvt;

typedef struct {
    QEvt super;
    uint8_t st;
    uint8_t op;
    uint8_t data[6];
} BusDoneEvt;

typedef struct {
    QActive super;
    QTimeEvt te;
    uint16_t due;
    uint8_t step;
} Ao;

static const struct {
    const char *name;
    uint16_t words;
    uint8_t prio;
    uint8_t qlen;
} task_def[T_COUNT] = {
    [T_ESTOP]  = { "estop",  128u, 8u, 2u },
    [T_GATE]   = { "gate",   128u, 7u, 2u },
    [T_SORT]   = { "sort",   160u, 6u, 3u },
    [T_BUS]    = { "bus",    176u, 5u, 3u },
    [T_IMU]    = { "imu",    112u, 4u, 4u },
    [T_CLI]    = { "cli",    192u, 3u, 2u },
    [T_LOGGER] = { "logger", 160u, 2u, QLOG_LEN },
    [T_LCD]    = { "lcd",    128u, 1u, 4u },
};

static const tasks_ops_t *ops;
static Ao ao[T_COUNT];
static StackType_t stk_estop[128], stk_gate[128], stk_sort[160], stk_bus[176], stk_imu[112],
    stk_cli[192], stk_logger[160], stk_lcd[128];
static StackType_t *const stk[T_COUNT] = { stk_estop, stk_gate, stk_sort, stk_bus, stk_imu, stk_cli,
                                           stk_logger, stk_lcd };
static QEvtPtr q_estop[2], q_gate[2], q_sort[3], q_bus[3], q_imu[4], q_cli[2], q_logger[QLOG_LEN], q_lcd[4];
static QEvtPtr *const qsto[T_COUNT] = { q_estop, q_gate, q_sort, q_bus, q_imu, q_cli, q_logger, q_lcd };
static QF_MPOOL_EL(ParcelEvt) pool_small[8];
static QF_MPOOL_EL(BusReqEvt) pool_mid[3];
static QTimeEvt logger_second;
static char lcd_rows[2][TASKS_LCD_ROW];
static QF_MPOOL_EL(LogEvt) pool_log[QLOG_LEN + 2u];
static volatile uint8_t running;
static uint8_t rx_buf[SB_RX_BYTES];
static volatile uint8_t rx_head, rx_tail;

static const QEvt estop_evt = QEVT_INITIALIZER(ESTOP_SIG);
static const QEvt rx_evt = QEVT_INITIALIZER(RX_SIG);

static uint32_t parcel_drop;
static uint32_t bus_skip;
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

static void count(uint32_t *c)
{
    taskENTER_CRITICAL();
    (*c)++;
    taskEXIT_CRITICAL();
}

int tasks_running(void)
{
    return running;
}

#if SIM_CRIT
static uint32_t i2c_hold;
#endif

void tasks_lock(tasks_lock_t l)
{
    if (!running)
        return;
    if (l != TASKS_LOCK_I2C)
        vTaskSuspendAll();
#if SIM_CRIT
    else
        i2c_hold = port_sim_tick_hold();
#endif
}

void tasks_unlock(tasks_lock_t l)
{
    if (!running)
        return;
    if (l != TASKS_LOCK_I2C)
        (void)xTaskResumeAll();
#if SIM_CRIT
    else
        port_sim_tick_release(i2c_hold);
#endif
}

static void uart_lock(void)
{
    tasks_lock(TASKS_LOCK_UART);
}

static void uart_unlock(void)
{
    tasks_unlock(TASKS_LOCK_UART);
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
        count(&log_drop);
        return;
    }
    e->event = event;
    e->fmt = fmt;
    e->ms = HAL_GetTick();
    n = fmt_vargs(fmt, ap, e->arg, TASKS_LOG_ARGS);
    if (n > TASKS_LOG_ARGS) {
        count(&log_trunc);
        n = TASKS_LOG_ARGS;
    }
    e->n = (uint8_t)n;
    if (!QACTIVE_POST_X(&ao[T_LOGGER].super, &e->super, 1U, NULL))
        count(&log_drop);
}

void tasks_parcel(uint32_t in_ms, uint32_t in_cyc)
{
    ParcelEvt *p = Q_NEW_X(ParcelEvt, 1U, PARCEL_SIG);

    if (p == NULL) {
        count(&parcel_drop);
        return;
    }
    p->ms = in_ms;
    p->cyc = in_cyc;
    if (!QACTIVE_POST_X(&ao[T_SORT].super, &p->super, 1U, NULL))
        count(&parcel_drop);
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

static void arm_next(Ao *me, const volatile uint16_t *knob)
{
    uint16_t now = (uint16_t)xTaskGetTickCount();
    int16_t left;

    me->due = (uint16_t)(me->due + ms_ticks(*knob));
    left = (int16_t)(me->due - now);
    if (left <= 0) {
        me->due = (uint16_t)(now + 1u);
        left = 1;
    }
    QTimeEvt_armX(&me->te, (uint32_t)left, 0U);
}

static QState estop_debouncing(Ao *me, QEvt const *e);

static QState estop_armed(Ao *me, QEvt const *e)
{
    (void)me;
    switch (e->sig) {
    case ESTOP_SIG:
        return Q_TRAN(&estop_debouncing);
    default:
        return Q_SUPER(&QHsm_top);
    }
}

static QState estop_debouncing(Ao *me, QEvt const *e)
{
    switch (e->sig) {
    case Q_ENTRY_SIG:
        QTimeEvt_armX(&me->te, ms_ticks(ops->estop_debounce_ms), 0U);
        return Q_HANDLED();
    case Q_EXIT_SIG:
        (void)QTimeEvt_disarm(&me->te);
        return Q_HANDLED();
    case TICK_SIG:
        ops->estop();
        return Q_TRAN(&estop_armed);
    case ESTOP_SIG:
        return Q_HANDLED();
    default:
        return Q_SUPER(&QHsm_top);
    }
}

static QState gate_occupied(Ao *me, QEvt const *e);

static QState gate_clear(Ao *me, QEvt const *e)
{
    switch (e->sig) {
    case TICK_SIG:
        arm_next(me, &ops->gate_ms);
        if (ops->gate() == TASKS_GATE_IN)
            return Q_TRAN(&gate_occupied);
        return Q_HANDLED();
    default:
        return Q_SUPER(&QHsm_top);
    }
}

static QState gate_occupied(Ao *me, QEvt const *e)
{
    switch (e->sig) {
    case TICK_SIG:
        arm_next(me, &ops->gate_ms);
        if (ops->gate() == TASKS_GATE_OUT)
            return Q_TRAN(&gate_clear);
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

static uint32_t now_cyc(void)
{
#if PERF
    return perf_now();
#else
    return 0u;
#endif
}

static void bus_request(Ao *me, uint8_t op, uint8_t row)
{
    BusReqEvt *r = Q_NEW(BusReqEvt, BUS_REQ_SIG);

    r->reply_to = &me->super;
    r->op = op;
    r->row = row;
    if (op == BUS_LCD_ROW)
        for (unsigned i = 0; i < TASKS_LCD_ROW; i++)
            r->text[i] = lcd_rows[row][i];
    r->t_post = now_cyc();
    QACTIVE_POST(&ao[T_BUS].super, &r->super, &me->super);
}

static QState bus_serving(Ao *me, QEvt const *e)
{
    switch (e->sig) {
    case BUS_REQ_SIG: {
        BusReqEvt const *r = Q_EVT_CAST(BusReqEvt);
        BusDoneEvt *d = Q_NEW(BusDoneEvt, BUS_DONE_SIG);

        d->op = r->op;
        if (r->op == BUS_MPU_READ) {
            ops->bus_wait(now_cyc() - r->t_post);
            d->st = ops->bus_imu_read(d->data);
        } else {
            d->st = ops->bus_lcd_row(r->row, r->text);
        }
        QACTIVE_POST(r->reply_to, &d->super, &me->super);
        return Q_HANDLED();
    }
    default:
        return Q_SUPER(&QHsm_top);
    }
}

static QState imu_reading(Ao *me, QEvt const *e);

static QState imu_idle(Ao *me, QEvt const *e)
{
    switch (e->sig) {
    case TICK_SIG:
        arm_next(me, ops->imu_ms);
        ops->imu_begin();
        bus_request(me, BUS_MPU_READ, 0u);
        return Q_TRAN(&imu_reading);
    default:
        return Q_SUPER(&QHsm_top);
    }
}

static QState imu_reading(Ao *me, QEvt const *e)
{
    switch (e->sig) {
    case BUS_DONE_SIG:
        ops->imu_end(Q_EVT_CAST(BusDoneEvt)->st, Q_EVT_CAST(BusDoneEvt)->data);
        return Q_TRAN(&imu_idle);
    case TICK_SIG:
        arm_next(me, ops->imu_ms);
        count(&bus_skip);
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

static QState lcd_drawing(Ao *me, QEvt const *e);

static QState lcd_idle(Ao *me, QEvt const *e)
{
    switch (e->sig) {
    case TICK_SIG:
        arm_next(me, ops->lcd_ms);
        ops->lcd_begin(lcd_rows);
        me->step = 0u;
        bus_request(me, BUS_LCD_ROW, 0u);
        return Q_TRAN(&lcd_drawing);
    default:
        return Q_SUPER(&QHsm_top);
    }
}

static QState lcd_drawing(Ao *me, QEvt const *e)
{
    switch (e->sig) {
    case BUS_DONE_SIG: {
        uint8_t st = Q_EVT_CAST(BusDoneEvt)->st;

        if (st != 0u || me->step != 0u) {
            ops->lcd_end(st);
            return Q_TRAN(&lcd_idle);
        }
        me->step = 1u;
        bus_request(me, BUS_LCD_ROW, 1u);
        return Q_HANDLED();
    }
    case TICK_SIG:
        arm_next(me, ops->lcd_ms);
        count(&bus_skip);
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
        return Q_TRAN(&estop_armed);
    case T_GATE:
        arm_next(me, &ops->gate_ms);
        return Q_TRAN(&gate_clear);
    case T_SORT:
        return Q_TRAN(&sort_run);
    case T_BUS:
        return Q_TRAN(&bus_serving);
    case T_IMU:
        if (ops->imu != NULL)
            arm_next(me, ops->imu_ms);
        return Q_TRAN(&imu_idle);
    case T_CLI:
        return Q_TRAN(&cli_run);
    case T_LOGGER:
        QTimeEvt_armX(&logger_second, ms_ticks(SECOND_MS), ms_ticks(SECOND_MS));
        return Q_TRAN(&logger_run);
    default:
        if (ops->lcd != NULL)
            arm_next(me, ops->lcd_ms);
        return Q_TRAN(&lcd_idle);
    }
}

#ifndef TEST_QSTRESS
void QF_onStartup(void)
{
    NVIC_SetPriority(USART2_IRQn, USART2_IRQ_PRIO);
    NVIC_EnableIRQ(USART2_IRQn);
    uart_hal_rx_irq_enable();
}

#endif

void tasks_start(const tasks_ops_t *o)
{
#if PERF
    rtos_hal_mark_api();
#endif
#ifdef TEST_QSTRESS
    (void)o;
    (void)ao_initial;
    (void)uart_lock;
    (void)uart_unlock;
    (void)pool_log;
    (void)pool_small;
    (void)pool_mid;
    (void)qsto;
    (void)stk;
    qstress_start();
#else
    ops = o;
    QF_init();
    QF_poolInit(pool_small, sizeof pool_small, sizeof pool_small[0]);
    QF_poolInit(pool_mid, sizeof pool_mid, sizeof pool_mid[0]);
    QF_poolInit(pool_log, sizeof pool_log, sizeof pool_log[0]);
    QTimeEvt_ctorX(&logger_second, &ao[T_LOGGER].super, SECOND_SIG, 0U);
    for (unsigned i = 0; i < T_COUNT; i++) {
        QActive_ctor(&ao[i].super, Q_STATE_CAST(&ao_initial));
        QTimeEvt_ctorX(&ao[i].te, &ao[i].super, TICK_SIG, 0U);
        QActive_setAttr(&ao[i].super, TASK_NAME_ATTR, task_def[i].name);
        QActive_start(&ao[i].super, task_def[i].prio, qsto[i], task_def[i].qlen, stk[i],
                      task_def[i].words * sizeof(StackType_t), NULL);
    }
#if PERF
    rtos_hal_trace_task(0u, ao[T_LCD].super.thread, BOARD_PROBE_LCD_PIN);
#endif
    uart_hal_set_lock(uart_lock, uart_unlock);
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
            s.pcTaskName, (unsigned long)s.uxCurrentPriority,
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
    out("BUSQ", "skip=%lu parcel_drop=%lu", (unsigned long)bus_skip, (unsigned long)parcel_drop);
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
    bus_skip = 0;
    parcel_drop = 0;
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
