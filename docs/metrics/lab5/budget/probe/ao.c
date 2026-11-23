#include "app.h"
Q_DEFINE_THIS_MODULE("ao")

typedef struct { QActive super; QTimeEvt te; } Estop;
typedef struct { QActive super; QTimeEvt te; uint16_t raw; } Gate;
typedef struct { QActive super; uint8_t impact; uint16_t cnt[3]; } Sorter;
typedef struct { QActive super; QTimeEvt te; uint8_t fails; } Bus;
typedef struct { QActive super; QTimeEvt te; uint16_t period; } Imu;
typedef struct { QActive super; QTimeEvt te; StatusEvt last; } Lcd;
typedef struct { QActive super; QTimeEvt te; } Console;
typedef struct { QActive super; QTimeEvt te; uint16_t period; uint8_t step; StatusEvt snap; } LcdAo;

static Estop l_estop; static Gate l_gate; static Sorter l_sorter; static Bus l_bus;
static Imu l_imu; static Console l_console; static LcdAo l_lcd;
QActive * const AO_Estop = &l_estop.super;
QActive * const AO_Gate = &l_gate.super;
QActive * const AO_Sorter = &l_sorter.super;
QActive * const AO_Bus = &l_bus.super;
QActive * const AO_Imu = &l_imu.super;
QActive * const AO_Console = &l_console.super;
QActive * const AO_Lcd = &l_lcd.super;

static QEvt const estopPressEvt = QEVT_INITIALIZER(ESTOP_PRESS_SIG);
static QEvt const statusReqEvt = QEVT_INITIALIZER(STATUS_REQ_SIG);

void log_ev(QActive *me, const char *event, const char *fmt, uintptr_t a0)
{
    LogEvt *e = Q_NEW_X(LogEvt, 2U, LOG_SIG);
    if (e == (LogEvt *)0)
        return;
    e->event = event; e->fmt = fmt; e->ms = 0; e->n = 1; e->reply = 0; e->arg[0] = a0;
    if (!QACTIVE_POST_X(AO_Console, &e->super, 1U, me)) {
    }
}

static QState Estop_armed(Estop *me, QEvt const *e);
static QState Estop_debounce(Estop *me, QEvt const *e);
static QState Estop_init(Estop *me, void const *par) { (void)par; (void)me; return Q_TRAN(&Estop_armed); }
static QState Estop_armed(Estop *me, QEvt const *e)
{
    switch (e->sig) {
    case ESTOP_EDGE_SIG: return Q_TRAN(&Estop_debounce);
    default: return Q_SUPER(&QHsm_top);
    }
}
static QState Estop_debounce(Estop *me, QEvt const *e)
{
    switch (e->sig) {
    case Q_ENTRY_SIG: QTimeEvt_armX(&me->te, 20U, 0U); return Q_HANDLED();
    case Q_EXIT_SIG: (void)QTimeEvt_disarm(&me->te); return Q_HANDLED();
    case DEBOUNCE_SIG: {
        uint8_t pressed;
        work_estop(&pressed);
        if (pressed)
            QACTIVE_POST(AO_Sorter, &estopPressEvt, me);
        return Q_TRAN(&Estop_armed);
    }
    default: return Q_SUPER(&QHsm_top);
    }
}

static QState Gate_run(Gate *me, QEvt const *e);
static QState Gate_init(Gate *me, void const *par) { (void)par; QTimeEvt_armX(&me->te, 5U, 5U); return Q_TRAN(&Gate_run); }
static QState Gate_run(Gate *me, QEvt const *e)
{
    switch (e->sig) {
    case GATE_TICK_SIG: {
        uint16_t raw = work_adc(0);
        int ev = work_gate(raw);
        if (ev == 1) {
            ParcelEvt *p = Q_NEW(ParcelEvt, PARCEL_IN_SIG);
            p->in_ms = 1; p->in_cyc = 2; p->gate_raw = raw; p->class_raw = work_adc(1);
            QACTIVE_POST(AO_Sorter, &p->super, me);
            log_ev(&me->super, "PARCEL_IN", "gate=%u", raw);
        } else if (ev == 2) {
            ParcelEvt *p = Q_NEW(ParcelEvt, PARCEL_OUT_SIG);
            p->gate_raw = raw;
            QACTIVE_POST(AO_Sorter, &p->super, me);
        }
        return Q_HANDLED();
    }
    default: return Q_SUPER(&QHsm_top);
    }
}

static QState Sorter_operational(Sorter *me, QEvt const *e);
static QState Sorter_ready(Sorter *me, QEvt const *e);
static QState Sorter_sorting(Sorter *me, QEvt const *e);
static QState Sorter_estop(Sorter *me, QEvt const *e);
static QState Sorter_fault(Sorter *me, QEvt const *e);
static void publish_status(Sorter *me, uint8_t st)
{
    StatusEvt *s = Q_NEW(StatusEvt, STATUS_SIG);
    s->state = st; s->cnt[0] = me->cnt[0]; s->cnt[1] = me->cnt[1]; s->cnt[2] = me->cnt[2]; s->fault = "";
    QACTIVE_PUBLISH(&s->super, me);
}
static QState Sorter_init(Sorter *me, void const *par) { (void)par; QActive_subscribe(&me->super, STATUS_SIG); return Q_TRAN(&Sorter_ready); }
static QState Sorter_root(Sorter *me, QEvt const *e)
{
    switch (e->sig) {
    case STATUS_REQ_SIG: log_ev(&me->super, "STATUS", "state=%s", 0); return Q_HANDLED();
    case RESET_REQ_SIG: me->cnt[0] = me->cnt[1] = me->cnt[2] = 0; log_ev(&me->super, "RESET", "total=%lu", 0); return Q_HANDLED();
    case PARCEL_IN_SIG: log_ev(&me->super, "SORT_SKIP", "state=%s", 0); return Q_HANDLED();
    default: return Q_SUPER(&QHsm_top);
    }
}
static QState Sorter_operational(Sorter *me, QEvt const *e)
{
    switch (e->sig) {
    case ESTOP_PRESS_SIG: return Q_TRAN(&Sorter_estop);
    case IMPACT_SIG: if (!me->impact) { me->impact = 1; log_ev(&me->super, "IMPACT", "amag_mg=%lu", Q_EVT_CAST(ImpactEvt)->amag); } return Q_HANDLED();
    default: return Q_SUPER(&Sorter_root);
    }
}
static QState Sorter_ready(Sorter *me, QEvt const *e)
{
    switch (e->sig) {
    case Q_ENTRY_SIG: publish_status(me, 0); return Q_HANDLED();
    case PARCEL_IN_SIG: {
        (void)work_sort(Q_EVT_CAST(ParcelEvt)->class_raw, me->impact, me->cnt);
        me->impact = 0;
        log_ev(&me->super, "SORT", "n=%lu", 1);
        return Q_TRAN(&Sorter_sorting);
    }
    default: return Q_SUPER(&Sorter_operational);
    }
}
static QState Sorter_sorting(Sorter *me, QEvt const *e)
{
    switch (e->sig) {
    case Q_ENTRY_SIG: publish_status(me, 1); return Q_HANDLED();
    case PARCEL_OUT_SIG: log_ev(&me->super, "PARCEL_OUT", "gate=%u", Q_EVT_CAST(ParcelEvt)->gate_raw); return Q_TRAN(&Sorter_ready);
    default: return Q_SUPER(&Sorter_operational);
    }
}
static QState Sorter_estop(Sorter *me, QEvt const *e)
{
    switch (e->sig) {
    case Q_ENTRY_SIG: publish_status(me, 2); log_ev(&me->super, "ESTOP", "state=%s", 2); return Q_HANDLED();
    case ESTOP_PRESS_SIG: return Q_TRAN(&Sorter_ready);
    default: return Q_SUPER(&Sorter_root);
    }
}
static QState Sorter_fault(Sorter *me, QEvt const *e)
{
    switch (e->sig) {
    case Q_ENTRY_SIG: publish_status(me, 3); return Q_HANDLED();
    default: return Q_SUPER(&Sorter_root);
    }
}

static QState Bus_serving(Bus *me, QEvt const *e);
static QState Bus_fault(Bus *me, QEvt const *e);
static QState Bus_init(Bus *me, void const *par) { (void)par; (void)me; return Q_TRAN(&Bus_serving); }
static void bus_reply(Bus *me, BusReqEvt const *r, uint8_t st, const uint8_t *d)
{
    BusDoneEvt *done = Q_NEW(BusDoneEvt, BUS_DONE_SIG);
    done->st = st; done->op = r->op;
    for (unsigned i = 0; i < 6u; i++) done->data[i] = d ? d[i] : 0u;
    QACTIVE_POST(r->reply_to, &done->super, me);
}
static QState Bus_serving(Bus *me, QEvt const *e)
{
    switch (e->sig) {
    case BUS_REQ_SIG: {
        BusReqEvt const *r = Q_EVT_CAST(BusReqEvt);
        uint8_t d[6];
        uint8_t st = work_bus(r->op, r->arg, d);
        bus_reply(me, r, st, d);
        if (st != 0u && ++me->fails >= 3u)
            return Q_TRAN(&Bus_fault);
        if (st == 0u) me->fails = 0;
        return Q_HANDLED();
    }
    default: return Q_SUPER(&QHsm_top);
    }
}
static QState Bus_fault(Bus *me, QEvt const *e)
{
    switch (e->sig) {
    case Q_ENTRY_SIG: QTimeEvt_armX(&me->te, 100U, 0U); return Q_HANDLED();
    case Q_EXIT_SIG: (void)QTimeEvt_disarm(&me->te); me->fails = 0; return Q_HANDLED();
    case BUS_REQ_SIG: bus_reply(me, Q_EVT_CAST(BusReqEvt), 1u, 0); return Q_HANDLED();
    case BUS_RETRY_SIG: return Q_TRAN(&Bus_serving);
    default: return Q_SUPER(&QHsm_top);
    }
}

static QState Imu_idle(Imu *me, QEvt const *e);
static QState Imu_reading(Imu *me, QEvt const *e);
static QState Imu_init(Imu *me, void const *par) { (void)par; me->period = 10; QTimeEvt_armX(&me->te, 10U, 10U); return Q_TRAN(&Imu_idle); }
static QState Imu_common(Imu *me, QEvt const *e)
{
    switch (e->sig) {
    case SET_PERIOD_SIG:
        me->period = Q_EVT_CAST(SetEvt)->value;
        (void)QTimeEvt_disarm(&me->te);
        QTimeEvt_armX(&me->te, me->period, me->period);
        return Q_HANDLED();
    default: return Q_SUPER(&QHsm_top);
    }
}
static QState Imu_idle(Imu *me, QEvt const *e)
{
    switch (e->sig) {
    case IMU_TICK_SIG: {
        BusReqEvt *r = Q_NEW(BusReqEvt, BUS_REQ_SIG);
        r->reply_to = &me->super; r->op = 1; r->arg = 0;
        QACTIVE_POST(AO_Bus, &r->super, me);
        return Q_TRAN(&Imu_reading);
    }
    default: return Q_SUPER(&Imu_common);
    }
}
static QState Imu_reading(Imu *me, QEvt const *e)
{
    switch (e->sig) {
    case BUS_DONE_SIG: {
        int16_t xyz[3];
        if (work_imu(Q_EVT_CAST(BusDoneEvt)->data, xyz)) {
            ImpactEvt *im = Q_NEW(ImpactEvt, IMPACT_SIG);
            im->x = xyz[0]; im->y = xyz[1]; im->z = xyz[2]; im->amag = 1;
            QACTIVE_POST(AO_Sorter, &im->super, me);
        }
        return Q_TRAN(&Imu_idle);
    }
    case IMU_TICK_SIG: return Q_HANDLED();
    default: return Q_SUPER(&Imu_common);
    }
}

static QState Lcd_idle(LcdAo *me, QEvt const *e);
static QState Lcd_drawing(LcdAo *me, QEvt const *e);
static QState Lcd_init(LcdAo *me, void const *par) { (void)par; QActive_subscribe(&me->super, STATUS_SIG); QTimeEvt_armX(&me->te, 500U, 500U); return Q_TRAN(&Lcd_idle); }
static QState Lcd_common(LcdAo *me, QEvt const *e)
{
    switch (e->sig) {
    case STATUS_SIG: me->snap = *Q_EVT_CAST(StatusEvt); return Q_HANDLED();
    case SET_PERIOD_SIG:
        me->period = Q_EVT_CAST(SetEvt)->value;
        (void)QTimeEvt_disarm(&me->te);
        QTimeEvt_armX(&me->te, me->period, me->period);
        return Q_HANDLED();
    default: return Q_SUPER(&QHsm_top);
    }
}
static void lcd_next(LcdAo *me)
{
    BusReqEvt *r = Q_NEW(BusReqEvt, BUS_REQ_SIG);
    r->reply_to = &me->super; r->op = 2; r->arg = work_lcd_byte(me->step, &me->snap);
    QACTIVE_POST(AO_Bus, &r->super, me);
}
static QState Lcd_idle(LcdAo *me, QEvt const *e)
{
    switch (e->sig) {
    case LCD_TICK_SIG: me->step = 0; return Q_TRAN(&Lcd_drawing);
    default: return Q_SUPER(&Lcd_common);
    }
}
static QState Lcd_drawing(LcdAo *me, QEvt const *e)
{
    switch (e->sig) {
    case Q_ENTRY_SIG: lcd_next(me); return Q_HANDLED();
    case BUS_DONE_SIG:
        if (Q_EVT_CAST(BusDoneEvt)->st != 0u) { log_ev(&me->super, "LCD_ERR", "isr=0x%08lX", 0); return Q_TRAN(&Lcd_idle); }
        if (++me->step >= 34u) return Q_TRAN(&Lcd_idle);
        lcd_next(me);
        return Q_HANDLED();
    case LCD_TICK_SIG: return Q_HANDLED();
    default: return Q_SUPER(&Lcd_common);
    }
}

static QState Console_run(Console *me, QEvt const *e);
static QState Console_init(Console *me, void const *par) { (void)par; QTimeEvt_armX(&me->te, 1000U, 1000U); return Q_TRAN(&Console_run); }
static QState Console_run(Console *me, QEvt const *e)
{
    switch (e->sig) {
    case LOG_SIG: work_print(Q_EVT_CAST(LogEvt)); return Q_HANDLED();
    case SECOND_SIG: work_second(); return Q_HANDLED();
    case LINE_SIG: {
        LineEvt const *l = Q_EVT_CAST(LineEvt);
        work_line(l);
        if (l->buf[0] == 's') QACTIVE_POST(AO_Sorter, &statusReqEvt, me);
        if (l->buf[0] == 'i' || l->buf[0] == 'l') {
            SetEvt *s = Q_NEW(SetEvt, SET_PERIOD_SIG);
            s->value = l->len;
            QACTIVE_POST(l->buf[0] == 'i' ? AO_Imu : AO_Lcd, &s->super, me);
        }
        return Q_HANDLED();
    }
    default: return Q_SUPER(&QHsm_top);
    }
}

void aos_ctor(void)
{
    (void)Sorter_fault;
    QActive_ctor(&l_estop.super, Q_STATE_CAST(&Estop_init));
    QTimeEvt_ctorX(&l_estop.te, &l_estop.super, DEBOUNCE_SIG, 0U);
    QActive_ctor(&l_gate.super, Q_STATE_CAST(&Gate_init));
    QTimeEvt_ctorX(&l_gate.te, &l_gate.super, GATE_TICK_SIG, 0U);
    QActive_ctor(&l_sorter.super, Q_STATE_CAST(&Sorter_init));
    QActive_ctor(&l_bus.super, Q_STATE_CAST(&Bus_init));
    QTimeEvt_ctorX(&l_bus.te, &l_bus.super, BUS_RETRY_SIG, 0U);
    QActive_ctor(&l_imu.super, Q_STATE_CAST(&Imu_init));
    QTimeEvt_ctorX(&l_imu.te, &l_imu.super, IMU_TICK_SIG, 0U);
    QActive_ctor(&l_console.super, Q_STATE_CAST(&Console_init));
    QTimeEvt_ctorX(&l_console.te, &l_console.super, SECOND_SIG, 0U);
    QActive_ctor(&l_lcd.super, Q_STATE_CAST(&Lcd_init));
    QTimeEvt_ctorX(&l_lcd.te, &l_lcd.super, LCD_TICK_SIG, 0U);
}
