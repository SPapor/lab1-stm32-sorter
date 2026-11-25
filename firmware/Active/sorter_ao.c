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


Q_DEFINE_THIS_MODULE("sorter_ao")

#if defined(TEST_L5) && (TEST_L5 == 1 || TEST_L5 == 5)
#define IMU_STACK_WORDS 178
#else
#define IMU_STACK_WORDS 116
#endif

#if defined(TEST_L5) && TEST_L5 == 1
#define L5_UNUSED __attribute__((unused))
#else
#define L5_UNUSED
#endif

enum { T_ESTOP = 0, T_GATE, T_SORT, T_BUS, T_IMU, T_CONSOLE, T_LCD, T_COUNT };
enum { LOG_SIG = Q_USER_SIG, TICK_SIG, PARCEL_SIG, ESTOP_SIG, RX_SIG, SECOND_SIG, BUS_REQ_SIG, BUS_DONE_SIG,
       ESTOP_PRESS_SIG, PARCEL_OUT_SIG, IMPACT_SIG };
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
    int16_t x;
    int16_t y;
    int16_t z;
} ImpactEvt;

typedef struct {
    QEvt super;
    QActive *reply_to;
    uint32_t t_post;
    uint8_t op;
    uint8_t row;
    char text[TASKS_LCD_PART];
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
    uint32_t due;
    uint8_t step;
} Ao;

static const struct {
    const char *name;
    uint16_t words;
    uint8_t prio;
    uint8_t qlen;
} task_def[T_COUNT] = {
    [T_ESTOP]   = { "estop",   102u, 7u, 2u },
#if defined(TEST_L5) && TEST_L5 == 6
    [T_GATE]    = { "gate",    70u, 6u, 2u },
#else
    [T_GATE]    = { "gate",    126u, 6u, 2u },
#endif
    [T_SORT]    = { "sort",    158u, 5u, 3u },
#if defined(TEST_Q93) && TEST_Q93 >= 3
    [T_BUS]     = { "bus",     178u, 1u, 2u },
#else
    [T_BUS]     = { "bus",     178u, 4u, 2u },
#endif
#if defined(TEST_L5) && (TEST_L5 == 1 || TEST_L5 == 5)
    [T_IMU]     = { "imu",     178u, 3u, 3u },
#else
    [T_IMU]     = { "imu",     116u, 3u, 3u },
#endif
    [T_CONSOLE] = { "console", 208u, 2u, QLOG_LEN + 3u },
#if defined(TEST_Q93) && TEST_Q93 >= 3
    [T_LCD]     = { "lcd",     116u, 4u, 2u },
#else
    [T_LCD]     = { "lcd",     116u, 1u, 2u },
#endif
};

static const tasks_ops_t *ops;
static Ao ao[T_COUNT];
static StackType_t stk_estop[102], stk_gate[126], stk_sort[158], stk_bus[178], stk_imu[IMU_STACK_WORDS],
    stk_console[208], stk_lcd[116];
static StackType_t *const stk[T_COUNT] = { stk_estop, stk_gate, stk_sort, stk_bus, stk_imu, stk_console,
                                           stk_lcd };
static QEvtPtr q_estop[2], q_gate[2], q_sort[3], q_bus[2], q_imu[3], q_console[QLOG_LEN + 3u], q_lcd[2];
static QEvtPtr *const qsto[T_COUNT] = { q_estop, q_gate, q_sort, q_bus, q_imu, q_console, q_lcd };
static QF_MPOOL_EL(ParcelEvt) pool_small[8];
#if defined(TEST_Q93) && TEST_Q93 >= 3
static QF_MPOOL_EL(BusReqEvt) pool_mid[1];
#else
static QF_MPOOL_EL(BusReqEvt) pool_mid[3];
#endif
static QTimeEvt logger_second;
static char lcd_rows[2][TASKS_LCD_ROW];
static QF_MPOOL_EL(LogEvt) pool_log[QLOG_LEN + 2u];
static volatile uint8_t running;
static uint8_t rx_buf[SB_RX_BYTES];
static volatile uint8_t rx_head, rx_tail;
static volatile uint8_t rx_posted;

static const QEvt estop_evt = QEVT_INITIALIZER(ESTOP_SIG);
static const QEvt estop_press_evt = QEVT_INITIALIZER(ESTOP_PRESS_SIG);
static const QEvt rx_evt = QEVT_INITIALIZER(RX_SIG);

static uint32_t parcel_drop;
volatile uint32_t qp_post_fail;
static uint32_t bus_skip;
#ifdef TEST_QPCHECK
static uint8_t qmax[T_COUNT];
static volatile uint8_t qmax_new;
static volatile uint32_t q93_drop;

void qmax_note(const void *queue, uint32_t waiting)
{
    for (unsigned i = 0; i < T_COUNT; i++) {
        if ((const void *)ao[i].super.eQueue == queue) {
            if (waiting > qmax[i]) {
                qmax[i] = (uint8_t)waiting;
                qmax_new = 1u;
            }
            return;
        }
    }
}

static void qmax_print(void)
{
    static const char *const key[T_COUNT] = { " estop=", " gate=", " sort=", " bus=", " imu=",
                                              " console=", " lcd=" };

    static uint16_t pool_seen[3] = { 0xFFFFu, 0xFFFFu, 0xFFFFu };
    static uint32_t drop_seen;
    uint16_t pool[3];
    uint32_t drop = q93_drop;

    for (unsigned i = 0; i < 3u; i++) {
        pool[i] = (uint16_t)QF_getPoolMin((uint_fast8_t)(i + 1u));
        if (pool[i] != pool_seen[i]) {
            pool_seen[i] = pool[i];
            qmax_new = 1u;
        }
    }
    if (drop != drop_seen) {
        drop_seen = drop;
        qmax_new = 1u;
    }
    if (!qmax_new)
        return;
    qmax_new = 0u;
    rtos_hal_raw_putu(HAL_GetTick(), 10u);
    rtos_hal_raw_puts(" QMAX");
    for (unsigned i = 0; i < T_COUNT; i++) {
        rtos_hal_raw_puts(key[i]);
        rtos_hal_raw_putu(qmax[i], 10u);
    }
    rtos_hal_raw_puts(" pool_min=");
    for (unsigned i = 0; i < 3u; i++) {
        rtos_hal_raw_putu(pool[i], 10u);
        rtos_hal_raw_puts(i < 2u ? "/" : "");
    }
    rtos_hal_raw_puts(" drop=");
    rtos_hal_raw_putu(drop, 10u);
    rtos_hal_raw_puts("\r\n");
}
#endif
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

void tasks_snapshot(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;

    taskENTER_CRITICAL();
    while (n-- > 0u)
        *d++ = *s++;
    taskEXIT_CRITICAL();
}

#if defined(TEST_L5) && TEST_L5 == 5
static void l5_log(const char *event, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    tasks_log(event, fmt, ap);
    va_end(ap);
}
#endif

#if defined(TEST_L5) && TEST_L5 == 1
#include "semphr.h"

static StaticSemaphore_t bus_mtx_mem;
static SemaphoreHandle_t bus_mtx;
#endif

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
    if (!QACTIVE_POST_X(&ao[T_CONSOLE].super, &e->super, 3U, NULL))
        count(&log_drop);
}

static void parcel_post(enum_t sig, uint16_t margin, uint32_t in_ms, uint32_t in_cyc)
{
    ParcelEvt *p = Q_NEW_X(ParcelEvt, margin, sig);

    if (p == NULL) {
        count(&parcel_drop);
        return;
    }
    p->ms = in_ms;
    p->cyc = in_cyc;
    if (!QACTIVE_POST_X(&ao[T_SORT].super, &p->super, margin, NULL))
        count(&parcel_drop);
}

void tasks_parcel(uint32_t in_ms, uint32_t in_cyc)
{
    parcel_post(PARCEL_SIG, 1U, in_ms, in_cyc);
}

void tasks_parcel_out(uint16_t raw)
{
    parcel_post(PARCEL_OUT_SIG, 0U, raw, 0u);
}

void tasks_impact(int16_t x, int16_t y, int16_t z)
{
    ImpactEvt *m = Q_NEW_X(ImpactEvt, 1U, IMPACT_SIG);

    if (m == NULL)
        return;
    m->x = x;
    m->y = y;
    m->z = z;
    (void)QACTIVE_POST_X(&ao[T_SORT].super, &m->super, 1U, NULL);
}

void tasks_estop_from_isr(void)
{
    BaseType_t woken = pdFALSE;

    if (!running)
        return;
#if defined(TEST_Q93) && TEST_Q93 == 2
    if (!QACTIVE_POST_X_FROM_ISR(&ao[T_ESTOP].super, &estop_evt, 1U, &woken, NULL))
        q93_drop++;
#else
    QACTIVE_POST_FROM_ISR(&ao[T_ESTOP].super, &estop_evt, &woken, NULL);
#endif
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

    if (rtos_hal_irq_defer(USART2_IRQn))
        return;
    while ((ch = uart_hal_getc()) >= 0) {
        uint8_t next = (uint8_t)((rx_head + 1u) % SB_RX_BYTES);

        if (next == rx_tail) {
            rx_drop++;
            continue;
        }
        rx_buf[rx_head] = (uint8_t)ch;
        rx_head = next;
    }
    if (!rx_posted && rx_head != rx_tail
        && QACTIVE_POST_X_FROM_ISR(&ao[T_CONSOLE].super, &rx_evt, 0U, &woken, NULL))
        rx_posted = 1u;
    portYIELD_FROM_ISR(woken);
}

static void arm_next(Ao *me, const volatile uint16_t *knob)
{
    uint32_t now = xTaskGetTickCount();
    int32_t left;

    me->due += ms_ticks(*knob);
    left = (int32_t)(me->due - now);
    if (left <= 0) {
        me->due = now + 1u;
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
        if (ops->estop())
            QACTIVE_POST(&ao[T_SORT].super, &estop_press_evt, &me->super);
        return Q_TRAN(&estop_armed);
    case ESTOP_SIG:
        return Q_HANDLED();
    default:
        return Q_SUPER(&QHsm_top);
    }
}

static QState gate_occupied(Ao *me, QEvt const *e);

#if defined(TEST_L5) && TEST_L5 == 7
static const QEvt starve_evt = QEVT_INITIALIZER(TICK_SIG);
#endif

static QState gate_clear(Ao *me, QEvt const *e)
{
    switch (e->sig) {
    case TICK_SIG:
#if defined(TEST_L5) && TEST_L5 == 7
        if (HAL_GetTick() >= 30000u && HAL_GetTick() < 35000u) {
            QACTIVE_POST(&me->super, &starve_evt, &me->super);
            return Q_HANDLED();
        }
        if (e == &starve_evt)
            me->due = xTaskGetTickCount();
#endif
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

#ifdef Q_UNSAFE
#define SORTER_IS(st)
#else
#define SORTER_IS(st)                                              \
    case Q_ENTRY_SIG:                                              \
        Q_ASSERT_ID(900U + (st), ops->sorter_state() == (st));     \
        return Q_HANDLED();
#endif

static QState sorter_operational(Ao *me, QEvt const *e);
static QState sorter_ready(Ao *me, QEvt const *e);
static QState sorter_sorting(Ao *me, QEvt const *e);
static QState sorter_estop(Ao *me, QEvt const *e);
static QState sorter_fault(Ao *me, QEvt const *e);

static void sort_parcel(QEvt const *e)
{
    ops->sort(Q_EVT_CAST(ParcelEvt)->ms, Q_EVT_CAST(ParcelEvt)->cyc);
}

static void sort_out(QEvt const *e)
{
    ops->parcel_out((uint16_t)Q_EVT_CAST(ParcelEvt)->ms);
}

static QState sorter_top(Ao *me, QEvt const *e)
{
    (void)me;
    switch (e->sig) {
    case PARCEL_SIG:
        sort_parcel(e);
        return Q_HANDLED();
    case PARCEL_OUT_SIG:
        sort_out(e);
        return Q_HANDLED();
    case ESTOP_PRESS_SIG:
        ops->estop_apply();
        return Q_HANDLED();
    case IMPACT_SIG:
        return Q_HANDLED();
    default:
        return Q_SUPER(&QHsm_top);
    }
}

static QState sorter_operational(Ao *me, QEvt const *e)
{
    (void)me;
    switch (e->sig) {
    case ESTOP_PRESS_SIG:
        ops->estop_apply();
        return Q_TRAN(&sorter_estop);
    case IMPACT_SIG:
        ops->impact(Q_EVT_CAST(ImpactEvt)->x, Q_EVT_CAST(ImpactEvt)->y, Q_EVT_CAST(ImpactEvt)->z);
        return Q_HANDLED();
    default:
        return Q_SUPER(&sorter_top);
    }
}

static QState sorter_ready(Ao *me, QEvt const *e)
{
    (void)me;
    switch (e->sig) {
    SORTER_IS(TASKS_ST_READY)
    case PARCEL_SIG:
        sort_parcel(e);
        return Q_TRAN(&sorter_sorting);
    default:
        return Q_SUPER(&sorter_operational);
    }
}

static QState sorter_sorting(Ao *me, QEvt const *e)
{
    (void)me;
    switch (e->sig) {
    SORTER_IS(TASKS_ST_SORTING)
    case PARCEL_OUT_SIG:
        sort_out(e);
        return Q_TRAN(&sorter_ready);
    default:
        return Q_SUPER(&sorter_operational);
    }
}

static QState sorter_estop(Ao *me, QEvt const *e)
{
    (void)me;
    switch (e->sig) {
    SORTER_IS(TASKS_ST_ESTOP)
    case ESTOP_PRESS_SIG:
        ops->estop_apply();
        return Q_TRAN(&sorter_ready);
    default:
        return Q_SUPER(&sorter_top);
    }
}

static QState sorter_fault(Ao *me, QEvt const *e)
{
    (void)me;
    switch (e->sig) {
    SORTER_IS(TASKS_ST_FAULT)
    default:
        return Q_SUPER(&sorter_top);
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
#if defined(TEST_Q93) && TEST_Q93 == 4
    BusReqEvt *r = Q_NEW_X(BusReqEvt, 0U, BUS_REQ_SIG);

    if (r == NULL) {
        BusDoneEvt *d = Q_NEW(BusDoneEvt, BUS_DONE_SIG);

        q93_drop++;
        d->op = op;
        d->st = 3u;
        QACTIVE_POST(&me->super, &d->super, &me->super);
        return;
    }
#else
    BusReqEvt *r = Q_NEW(BusReqEvt, BUS_REQ_SIG);
#endif

    r->reply_to = &me->super;
    r->op = op;
    r->row = row;
    if (op == BUS_LCD_ROW)
        for (unsigned i = 0; i < TASKS_LCD_PART; i++)
            r->text[i] = lcd_rows[row / TASKS_LCD_PARTS][(row % TASKS_LCD_PARTS) * TASKS_LCD_PART + i];
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
#if defined(TEST_L5) && TEST_L5 == 3
        vTaskDelay(ms_ticks(5u));
#endif
        if (r->op == BUS_MPU_READ) {
            ops->bus_wait(now_cyc() - r->t_post);
#if defined(TEST_L5) && TEST_L5 == 5
            if (HAL_GetTick() >= 30000u && HAL_GetTick() < 32000u)
                d->st = 3u;
            else
#endif
            d->st = ops->bus_imu_read(d->data);
        } else {
            d->st = ops->bus_lcd_row(r->row, r->text);
        }
#if defined(TEST_L5) && TEST_L5 == 4
        if (r->op == BUS_MPU_READ) {
            BusDoneEvt *late = Q_NEW(BusDoneEvt, BUS_DONE_SIG);

            late->op = d->op;
            late->st = d->st;
            for (unsigned i = 0; i < sizeof late->data; i++)
                late->data[i] = d->data[i];
            QACTIVE_POST(r->reply_to, &d->super, &me->super);
            QACTIVE_POST(r->reply_to, &late->super, &me->super);
        } else
#endif
        QACTIVE_POST(r->reply_to, &d->super, &me->super);
        return Q_HANDLED();
    }
    default:
        return Q_SUPER(&QHsm_top);
    }
}

static QState imu_reading(Ao *me, QEvt const *e) L5_UNUSED;

static QState imu_idle(Ao *me, QEvt const *e)
{
    switch (e->sig) {
    case TICK_SIG:
        arm_next(me, ops->imu_ms);
        ops->imu_begin();
#if defined(TEST_L5) && TEST_L5 == 1
        {
            uint8_t data[6];
            uint32_t t0 = now_cyc();
            uint8_t st;

            (void)xSemaphoreTake(bus_mtx, portMAX_DELAY);
            ops->bus_wait(now_cyc() - t0);
            st = ops->bus_imu_read(data);
            (void)xSemaphoreGive(bus_mtx);
            ops->imu_end(st, data);
        }
        return Q_HANDLED();
#else
        bus_request(me, BUS_MPU_READ, 0u);
        return Q_TRAN(&imu_reading);
#endif
    default:
        return Q_SUPER(&QHsm_top);
    }
}

static QState imu_reading(Ao *me, QEvt const *e)
{
    switch (e->sig) {
    case BUS_DONE_SIG:
#if defined(TEST_L5) && TEST_L5 == 5
        if (Q_EVT_CAST(BusDoneEvt)->st == 0u && me->step != 0u) {
            l5_log("L5_SENSOR", "ok after=%u", (unsigned)me->step);
            me->step = 0u;
        } else if (Q_EVT_CAST(BusDoneEvt)->st != 0u && me->step < 255u) {
            me->step++;
        }
#endif
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

static QState console_run(Ao *me, QEvt const *e)
{
    int ch;

    switch (e->sig) {
    case RX_SIG:
        rx_posted = 0u;
        if ((ch = tasks_rx_getc()) >= 0)
            ops->cli(ch);
        if (rx_head != rx_tail && !rx_posted && QACTIVE_POST_X(&me->super, &rx_evt, 0U, &me->super))
            rx_posted = 1u;
        return Q_HANDLED();
    case LOG_SIG: {
        LogEvt const *l = Q_EVT_CAST(LogEvt);

#if defined(TEST_L5) && TEST_L5 == 2
        vTaskDelay(ms_ticks(1u));
#endif
        ops->log_line(l->ms, l->event, l->fmt, l->arg, l->n);
        log_lines++;
        return Q_HANDLED();
    }
    case SECOND_SIG:
#ifdef TEST_QPCHECK
        qmax_print();
#endif
        ops->second();
        return Q_HANDLED();
    default:
        return Q_SUPER(&QHsm_top);
    }
}

static QState lcd_drawing(Ao *me, QEvt const *e) L5_UNUSED;

static QState lcd_idle(Ao *me, QEvt const *e)
{
    switch (e->sig) {
    case TICK_SIG:
        arm_next(me, ops->lcd_ms);
        ops->lcd_begin(lcd_rows);
#if defined(TEST_L5) && TEST_L5 == 1
        {
            uint8_t st = 0u;
            char part[TASKS_LCD_PART];

            (void)xSemaphoreTake(bus_mtx, portMAX_DELAY);
            for (unsigned p = 0; p < 2u * TASKS_LCD_PARTS && st == 0u; p++) {
                for (unsigned i = 0; i < TASKS_LCD_PART; i++)
                    part[i] = lcd_rows[p / TASKS_LCD_PARTS][(p % TASKS_LCD_PARTS) * TASKS_LCD_PART + i];
                st = ops->bus_lcd_row((uint8_t)p, part);
            }
            (void)xSemaphoreGive(bus_mtx);
            ops->lcd_end(st);
        }
        return Q_HANDLED();
#else
        me->step = 0u;
        bus_request(me, BUS_LCD_ROW, 0u);
        return Q_TRAN(&lcd_drawing);
#endif
    default:
        return Q_SUPER(&QHsm_top);
    }
}

static QState lcd_drawing(Ao *me, QEvt const *e)
{
    switch (e->sig) {
    case BUS_DONE_SIG: {
        uint8_t st = Q_EVT_CAST(BusDoneEvt)->st;

        if (st != 0u || me->step + 1u >= 2u * TASKS_LCD_PARTS) {
            ops->lcd_end(st);
            return Q_TRAN(&lcd_idle);
        }
        me->step++;
        bus_request(me, BUS_LCD_ROW, me->step);
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
        return Q_TRAN(&estop_debouncing);
    case T_GATE:
        arm_next(me, &ops->gate_ms);
        return Q_TRAN(&gate_clear);
    case T_SORT:
        if (ops->sorter_state() == TASKS_ST_FAULT)
            return Q_TRAN(&sorter_fault);
        return Q_TRAN(&sorter_ready);
    case T_BUS:
        return Q_TRAN(&bus_serving);
    case T_IMU:
        if (ops->imu != NULL)
            arm_next(me, ops->imu_ms);
        return Q_TRAN(&imu_idle);
    case T_CONSOLE:
        QTimeEvt_armX(&logger_second, ms_ticks(SECOND_MS), ms_ticks(SECOND_MS));
        return Q_TRAN(&console_run);
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
    QTimeEvt_ctorX(&logger_second, &ao[T_CONSOLE].super, SECOND_SIG, 0U);
#if defined(TEST_L5) && TEST_L5 == 1
    bus_mtx = xSemaphoreCreateMutexStatic(&bus_mtx_mem);
#endif
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
#if SIM_CRIT && (!defined(TEST_L5) || TEST_L5 != 1)
    uint64_t total = rtos_hal_runtime_now() - rt_total_base;
#endif

#if !defined(TEST_L5) || TEST_L5 != 1
    tasks_list(out);
#endif
    out("BUSQ", "skip=%lu parcel_drop=%lu post_fail=%lu", (unsigned long)bus_skip, (unsigned long)parcel_drop,
        (unsigned long)qp_post_fail);
    out("LOGQ", "len=%u max_used=%lu lines=%lu drop=%lu trunc=%lu rx_drop=%lu",
        (unsigned)QLOG_LEN, (unsigned long)(QLOG_LEN + 2u - QF_getPoolMin(3U)), (unsigned long)log_lines,
        (unsigned long)log_drop, (unsigned long)log_trunc, (unsigned long)rx_drop);
#if SIM_CRIT && (!defined(TEST_L5) || TEST_L5 != 1)
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
