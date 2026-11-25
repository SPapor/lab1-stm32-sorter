#include "qstress.h"
#ifdef TEST_QSTRESS
#include "qpc.h"
#include "stm32c0xx_hal.h"
#include "rtos_hal.h"
#include "uart_hal.h"
#include "kstress_port.h"
#include "perf_tick.h"
#if SIM_CRIT
#include "port_sim.h"
#endif

#define ISR_HZ          2000u
#define ISR_MSG_EVERY   20u
#define ISR_PUB_EVERY   200u
#define PING_TICKS      10u
#define PUB_TICKS       20u
#define T_A             10u
#define T_B             15u
#define T_C             23u
#define IRQ_PRIO        2u
#define REPORT_S        10u
#define RUN_S           120u
#define ONESHOT_TICKS   17u
#define STACK_MIN_B     64u

enum {
    PUB_SIG = Q_USER_SIG, ISR_PUB_SIG, PUB_MAX_SIG,
    MSG_SIG = PUB_MAX_SIG, ISR_SIG, T1_SIG, T3_SIG, T7_SIG, ONE_SIG, PING_SIG, PUB_TICK_SIG,
    REPORT_SIG
};

enum { A_REPORT = 0, A_SUB2, A_PUB, A_PING, A_SUB1, A_PONG, A_TIMER, A_COUNT };

typedef struct {
    QEvt super;
    uint32_t seq;
    uint32_t sum;
} MsgEvt;

typedef struct {
    QActive super;
    QTimeEvt te[4];
    uint32_t seq;
    uint32_t n[4];
} Ao;

static StackType_t stk_report[176], stk_sub2[104], stk_pub[128], stk_ping[112], stk_sub1[104], stk_pong[112],
    stk_timer[104];

static const struct {
    const char *name;
    StackType_t *stk;
    uint16_t words;
    uint8_t prio;
    uint8_t qlen;
} def[A_COUNT] = {
    [A_REPORT] = { "report", stk_report, 176u, 1u, 4u },
    [A_SUB2]   = { "sub2",   stk_sub2,   104u, 2u, 8u },
    [A_PUB]    = { "pub",    stk_pub,    128u, 3u, 4u },
    [A_PING]   = { "ping",   stk_ping,   112u, 4u, 6u },
    [A_SUB1]   = { "sub1",   stk_sub1,   104u, 5u, 8u },
    [A_PONG]   = { "pong",   stk_pong,   112u, 6u, 12u },
    [A_TIMER]  = { "timer",  stk_timer,  104u, 7u, 8u },
};

static Ao ao[A_COUNT];
static QEvtPtr q_report[4], q_sub2[8], q_pub[4], q_ping[6], q_sub1[8], q_pong[12], q_timer[8];
static QEvtPtr *const qsto[A_COUNT] = { q_report, q_sub2, q_pub, q_ping, q_sub1, q_pong, q_timer };
static QF_MPOOL_EL(MsgEvt) pool[20];
static QSubscrList subscr[PUB_MAX_SIG];
static const QEvt isr_pub_evt = QEVT_INITIALIZER(ISR_PUB_SIG);

static struct {
    uint32_t ping_sent, pong_rx, ping_rx;
    uint32_t pub_sent, sub1_rx, sub2_rx, isr_pub_rx;
    uint32_t isr_calls, isr_posts, isr_sent, isr_rx, isr_nopool, isr_qfull;
    uint32_t sum_err, seq_err;
    uint32_t hook;
} cnt;

static uint32_t expect_pong, expect_ping, expect_sub1, expect_sub2, expect_isr;
static uint32_t isr_seq;
static uint32_t ktick0, ptick0, hook0, n0[4];
static uint32_t report_s;
static uint8_t done;

static uint32_t msg_sum(uint32_t seq, uint32_t src)
{
    return (seq * 2654435761u) ^ (src << 24) ^ 0xA5A5u;
}

static MsgEvt *msg_new(uint32_t seq, uint32_t src)
{
    MsgEvt *m = Q_NEW(MsgEvt, MSG_SIG);

    m->seq = seq;
    m->sum = msg_sum(seq, src);
    return m;
}

static void check(const MsgEvt *m, uint32_t *expect, uint32_t src)
{
    if (m->sum != msg_sum(m->seq, src))
        cnt.sum_err++;
    if (m->seq != *expect)
        cnt.seq_err++;
    *expect = m->seq + 1u;
}

void qstress_tick_hook(void)
{
    cnt.hook++;
}

void TIM14_IRQHandler(void);
void TIM14_IRQHandler(void)
{
    BaseType_t woken = pdFALSE;
    MsgEvt *m;

    if (rtos_hal_irq_defer(TIM14_IRQn))
        return;
    kstress_note_isr(KSTRESS_SRC_TIM14);
    TIM14->SR = ~TIM_SR_UIF;
    if (done)
        return;
    if (cnt.isr_calls++ % ISR_MSG_EVERY == 0u) {
        cnt.isr_posts++;
        Q_NEW_X_FROM_ISR(m, MsgEvt, 4U, ISR_SIG);
        if (m == NULL) {
            cnt.isr_nopool++;
            isr_seq++;
        } else {
            m->seq = isr_seq;
            m->sum = msg_sum(isr_seq++, 3u);
            if (QACTIVE_POST_X_FROM_ISR(&ao[A_PONG].super, &m->super, 1U, &woken, NULL))
                cnt.isr_sent++;
            else
                cnt.isr_qfull++;
        }
    }
    if (cnt.isr_calls % ISR_PUB_EVERY == 0u)
        QACTIVE_PUBLISH_FROM_ISR(&isr_pub_evt, &woken, NULL);
    portYIELD_FROM_ISR(woken);
}

static void tim14_start(void)
{
    uint32_t clk = rtos_hal_timer_clk_hz();

    __HAL_RCC_TIM14_CLK_ENABLE();
    TIM14->CR1 = 0;
    TIM14->PSC = clk / 1000000u - 1u;
    TIM14->ARR = 1000000u / ISR_HZ - 1u;
    TIM14->DIER = 0;
    TIM14->EGR = TIM_EGR_UG;
    TIM14->SR = 0;
    NVIC_SetPriority(TIM14_IRQn, IRQ_PRIO);
    NVIC_ClearPendingIRQ(TIM14_IRQn);
    NVIC_EnableIRQ(TIM14_IRQn);
    TIM14->DIER = TIM_DIER_UIE;
    TIM14->CR1 = TIM_CR1_CEN;
}

static void snapshot(uint32_t *ktick, uint32_t *ptick, uint32_t *hook)
{
    taskENTER_CRITICAL();
    *ktick = (uint32_t)xTaskGetTickCount();
    *ptick = perf_tick_count();
    *hook = cnt.hook;
    taskEXIT_CRITICAL();
}

static uint32_t off_by(uint32_t got, uint32_t ticks, uint32_t period)
{
    uint32_t want = ticks / period;

    return (got > want + 1u || got + 1u < want) ? 1u : 0u;
}

static void report(void)
{
    kstress_port_counters_t p;
    uint32_t ktick, ptick, hook, stack_min = UINT32_MAX, fail;
    uint32_t dk, dp, dh, dn[4];
    uint32_t crit;

    kstress_port_counters(&p);
    snapshot(&ktick, &ptick, &hook);
    dk = ktick - ktick0;
    dp = ptick - ptick0;
    dh = hook - hook0;
    for (unsigned i = 0; i < 4u; i++)
        dn[i] = ao[A_TIMER].n[i] - n0[i];
    for (unsigned i = 0; i < A_COUNT; i++) {
        uint32_t b = (uint32_t)uxTaskGetStackHighWaterMark(ao[i].super.thread) * sizeof(StackType_t);

        if (b < stack_min)
            stack_min = b;
    }
    crit = p.pendsv_in_task_crit + p.tick_in_task_crit + p.tim14_in_task_crit +
           p.pendsv_in_isr_crit + p.tick_in_isr_crit + p.tim14_in_isr_crit;
    uart_hal_printf("%lu QSTRESS t_s=%lu ping=%lu/%lu/%lu pub=%lu sub=%lu/%lu isr_pub=%lu isr=%lu/%lu sent=%lu rx=%lu "
                    "nopool=%lu qfull=%lu", (unsigned long)HAL_GetTick(), (unsigned long)report_s,
                    (unsigned long)cnt.ping_sent, (unsigned long)cnt.pong_rx, (unsigned long)cnt.ping_rx,
                    (unsigned long)cnt.pub_sent, (unsigned long)cnt.sub1_rx, (unsigned long)cnt.sub2_rx,
                    (unsigned long)cnt.isr_pub_rx, (unsigned long)cnt.isr_calls, (unsigned long)cnt.isr_posts,
                    (unsigned long)cnt.isr_sent,
                    (unsigned long)cnt.isr_rx, (unsigned long)cnt.isr_nopool, (unsigned long)cnt.isr_qfull);
    uart_hal_printf("%lu QSTRESS_ERR t_s=%lu sum_err=%lu seq_err=%lu crit=%lu nvic_off_free=%lu pool_min=%u "
                    "pool_use=%u stack_min=%lu", (unsigned long)HAL_GetTick(), (unsigned long)report_s,
                    (unsigned long)cnt.sum_err, (unsigned long)cnt.seq_err, (unsigned long)crit,
                    (unsigned long)p.nvic_off_free, (unsigned)QF_getPoolMin(1U), (unsigned)QF_getPoolUse(1U),
                    (unsigned long)stack_min);
    uart_hal_printf("%lu QSTRESS_TICK t_s=%lu ktick=%lu ptick=%lu hook=%lu t10=%lu t15=%lu t23=%lu one17=%lu "
                    "crit_task_n=%lu crit_isr_n=%lu", (unsigned long)HAL_GetTick(), (unsigned long)report_s,
                    (unsigned long)dk, (unsigned long)dp, (unsigned long)dh, (unsigned long)dn[0],
                    (unsigned long)dn[1], (unsigned long)dn[2], (unsigned long)dn[3],
                    (unsigned long)p.crit_task_n, (unsigned long)p.crit_isr_n);
    if (report_s < RUN_S)
        return;
    done = 1;
    fail = (cnt.sum_err != 0u) + (cnt.seq_err != 0u) + (crit != 0u) + (p.nvic_off_free != 0u) +
           (dk != dp) + (dh != dk) + off_by(dn[0], dk, T_A) + off_by(dn[1], dk, T_B) + off_by(dn[2], dk, T_C) +
           (dn[3] > dk / ONESHOT_TICKS + 1u || dn[3] + 1u < dk / (ONESHOT_TICKS + 1u)) + (stack_min < STACK_MIN_B) +
           (cnt.isr_rx + cnt.isr_qfull + cnt.isr_nopool != cnt.isr_posts) + (cnt.pong_rx != cnt.ping_sent);
    uart_hal_line_add("%lu QSTRESS_STACK t_s=%lu", (unsigned long)HAL_GetTick(), (unsigned long)report_s);
    for (unsigned i = 0; i < A_COUNT; i++)
        uart_hal_line_add(" %s=%lu/%lu", def[i].name,
                          (unsigned long)(uxTaskGetStackHighWaterMark(ao[i].super.thread) * sizeof(StackType_t)),
                          (unsigned long)(def[i].words * sizeof(StackType_t)));
    uart_hal_line_end();
    uart_hal_printf("%lu QSTRESS_END t_s=%lu result=%s fail=%lu", (unsigned long)HAL_GetTick(),
                    (unsigned long)report_s, fail ? "FAIL" : "PASS", (unsigned long)fail);
}

static QState timer_run(Ao *me, QEvt const *e)
{
    switch (e->sig) {
    case T1_SIG:
        if (me->n[0]++ == 0u) {
            snapshot(&ktick0, &ptick0, &hook0);
            for (unsigned i = 0; i < 4u; i++)
                n0[i] = me->n[i];
            tim14_start();
        }
        return Q_HANDLED();
    case T3_SIG: me->n[1]++; return Q_HANDLED();
    case T7_SIG: me->n[2]++; return Q_HANDLED();
    case ONE_SIG:
        me->n[3]++;
        QTimeEvt_armX(&me->te[3], ONESHOT_TICKS, 0U);
        return Q_HANDLED();
    default:
        return Q_SUPER(&QHsm_top);
    }
}

static QState pong_run(Ao *me, QEvt const *e)
{
    (void)me;
    switch (e->sig) {
    case MSG_SIG: {
        MsgEvt const *m = Q_EVT_CAST(MsgEvt);

        check(m, &expect_pong, 1u);
        cnt.pong_rx++;
        QACTIVE_POST(&ao[A_PING].super, &msg_new(m->seq, 2u)->super, &ao[A_PONG].super);
        return Q_HANDLED();
    }
    case ISR_SIG: {
        MsgEvt const *m = Q_EVT_CAST(MsgEvt);

        if (m->sum != msg_sum(m->seq, 3u))
            cnt.sum_err++;
        if ((int32_t)(m->seq - expect_isr) < 0)
            cnt.seq_err++;
        expect_isr = m->seq + 1u;
        cnt.isr_rx++;
        return Q_HANDLED();
    }
    default:
        return Q_SUPER(&QHsm_top);
    }
}

static QState ping_run(Ao *me, QEvt const *e)
{
    switch (e->sig) {
    case PING_SIG:
        QACTIVE_POST(&ao[A_PONG].super, &msg_new(me->seq++, 1u)->super, &me->super);
        cnt.ping_sent++;
        return Q_HANDLED();
    case MSG_SIG:
        check(Q_EVT_CAST(MsgEvt), &expect_ping, 2u);
        cnt.ping_rx++;
        return Q_HANDLED();
    default:
        return Q_SUPER(&QHsm_top);
    }
}

static QState pub_run(Ao *me, QEvt const *e)
{
    switch (e->sig) {
    case PUB_TICK_SIG: {
        MsgEvt *m = Q_NEW(MsgEvt, PUB_SIG);

        m->seq = me->seq;
        m->sum = msg_sum(me->seq++, 4u);
        QACTIVE_PUBLISH(&m->super, &me->super);
        cnt.pub_sent++;
        return Q_HANDLED();
    }
    default:
        return Q_SUPER(&QHsm_top);
    }
}

static QState sub_run(Ao *me, QEvt const *e)
{
    int first = (me == &ao[A_SUB1]);

    switch (e->sig) {
    case PUB_SIG:
        check(Q_EVT_CAST(MsgEvt), first ? &expect_sub1 : &expect_sub2, 4u);
        if (first)
            cnt.sub1_rx++;
        else
            cnt.sub2_rx++;
        return Q_HANDLED();
    case ISR_PUB_SIG:
        cnt.isr_pub_rx++;
        return Q_HANDLED();
    default:
        return Q_SUPER(&QHsm_top);
    }
}

static QState report_run(Ao *me, QEvt const *e)
{
    (void)me;
    switch (e->sig) {
    case REPORT_SIG:
        if (done)
            return Q_HANDLED();
        report_s += REPORT_S;
        report();
        return Q_HANDLED();
    default:
        return Q_SUPER(&QHsm_top);
    }
}

static QState ao_initial(Ao *me, void const *par)
{
    (void)par;
    switch (me - ao) {
    case A_TIMER:
        QTimeEvt_armX(&me->te[0], T_A, T_A);
        QTimeEvt_armX(&me->te[1], T_B, T_B);
        QTimeEvt_armX(&me->te[2], T_C, T_C);
        QTimeEvt_armX(&me->te[3], ONESHOT_TICKS, 0U);
        return Q_TRAN(&timer_run);
    case A_PONG:
        return Q_TRAN(&pong_run);
    case A_PING:
        QTimeEvt_armX(&me->te[0], PING_TICKS, PING_TICKS);
        return Q_TRAN(&ping_run);
    case A_PUB:
        QTimeEvt_armX(&me->te[0], PUB_TICKS, PUB_TICKS);
        return Q_TRAN(&pub_run);
    case A_SUB1:
        QActive_subscribe(&me->super, PUB_SIG);
        QActive_subscribe(&me->super, ISR_PUB_SIG);
        return Q_TRAN(&sub_run);
    case A_SUB2:
        QActive_subscribe(&me->super, PUB_SIG);
        return Q_TRAN(&sub_run);
    default:
        QTimeEvt_armX(&me->te[0], REPORT_S * configTICK_RATE_HZ, REPORT_S * configTICK_RATE_HZ);
        return Q_TRAN(&report_run);
    }
}

void qstress_on_error(void)
{
    for (unsigned i = 0; i < A_COUNT; i++) {
        rtos_hal_raw_puts(def[i].name);
        rtos_hal_raw_puts(" q=");
        rtos_hal_raw_putu((uint32_t)uxQueueMessagesWaiting(ao[i].super.eQueue), 10u);
        rtos_hal_raw_puts(" ");
    }
    rtos_hal_raw_puts(" t1=");
    rtos_hal_raw_putu(ao[A_TIMER].n[0], 10u);
    rtos_hal_raw_puts(" ping=");
    rtos_hal_raw_putu(cnt.ping_sent, 10u);
    rtos_hal_raw_puts(" pub=");
    rtos_hal_raw_putu(cnt.pub_sent, 10u);
    rtos_hal_raw_puts(" hook=");
    rtos_hal_raw_putu(cnt.hook, 10u);
    rtos_hal_raw_puts("\r\n");
}

void QF_onStartup(void)
{
}

void qstress_start(void)
{
    static const uint16_t sig[A_COUNT][4] = {
        [A_REPORT] = { REPORT_SIG },
        [A_PUB]    = { PUB_TICK_SIG },
        [A_PING]   = { PING_SIG },
        [A_TIMER]  = { T1_SIG, T3_SIG, T7_SIG, ONE_SIG },
    };

    QF_init();
    QF_poolInit(pool, sizeof pool, sizeof pool[0]);
    QActive_psInit(subscr, Q_DIM(subscr));
    for (unsigned i = 0; i < A_COUNT; i++) {
        QActive_ctor(&ao[i].super, Q_STATE_CAST(&ao_initial));
        for (unsigned t = 0; t < 4u; t++)
            if (sig[i][t] != 0u)
                QTimeEvt_ctorX(&ao[i].te[t], &ao[i].super, sig[i][t], 0U);
        QActive_setAttr(&ao[i].super, TASK_NAME_ATTR, def[i].name);
        QActive_start(&ao[i].super, def[i].prio, qsto[i], def[i].qlen, def[i].stk,
                      def[i].words * sizeof(StackType_t), NULL);
    }
    uart_hal_printf("%lu QSTRESS_START run_s=%lu isr_hz=%lu", (unsigned long)HAL_GetTick(),
                    (unsigned long)RUN_S, (unsigned long)ISR_HZ);
    (void)QF_run();
    for (;;) {
    }
}
#endif
