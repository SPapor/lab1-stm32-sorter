#include "app.h"
#include "stm32c0xx.h"
Q_DEFINE_THIS_MODULE("bsp")

#define ST_ESTOP   128u
#define ST_GATE    128u
#define ST_SORTER  160u
#define ST_BUS     192u
#define ST_IMU      96u
#define ST_CONSOLE 192u
#define ST_LCD      96u

static QF_MPOOL_EL(BusDoneEvt) smlPool[10];
static QF_MPOOL_EL(ParcelEvt) medPool[8];
static QF_MPOOL_EL(LogEvt) lrgPool[18];
static QSubscrList subscrSto[MAX_PUB_SIG];
static QEvtPtr qEstop[2], qGate[2], qSorter[6], qBus[3], qImu[3], qConsole[16], qLcd[4];
static StackType_t sEstop[ST_ESTOP], sGate[ST_GATE], sSorter[ST_SORTER], sBus[ST_BUS],
    sImu[ST_IMU], sConsole[ST_CONSOLE], sLcd[ST_LCD];
static StaticTask_t idleTcb;
static StackType_t idleStk[configMINIMAL_STACK_SIZE];
static QEvt const estopEdgeEvt = QEVT_INITIALIZER(ESTOP_EDGE_SIG);
static LineEvt *rxLine;
volatile uint32_t rx_lost;
volatile uint32_t probe_sink;

void EXTI4_15_IRQHandler(void)
{
    BaseType_t woken = pdFALSE;
    EXTI->FPR1 = 1u << 13;
    QACTIVE_POST_FROM_ISR(AO_Estop, &estopEdgeEvt, &woken, (void *)0);
    portYIELD_FROM_ISR(woken);
}

void USART2_IRQHandler(void)
{
    BaseType_t woken = pdFALSE;
    while (USART2->ISR & USART_ISR_RXNE_RXFNE) {
        char ch = (char)USART2->RDR;
        if (rxLine == (LineEvt *)0) {
            Q_NEW_X_FROM_ISR(rxLine, LineEvt, 1U, LINE_SIG);
            if (rxLine == (LineEvt *)0) { rx_lost++; continue; }
            rxLine->len = 0;
        }
        rxLine->buf[rxLine->len++] = ch;
        if (ch == '\r' || rxLine->len >= sizeof rxLine->buf) {
            if (!QACTIVE_POST_X_FROM_ISR(AO_Console, &rxLine->super, 1U, &woken, (void *)0))
                rx_lost++;
            rxLine = (LineEvt *)0;
        }
    }
    portYIELD_FROM_ISR(woken);
}

void vApplicationTickHook(void)
{
    BaseType_t woken = pdFALSE;
    QTIMEEVT_TICK_FROM_ISR(0U, &woken, (void *)0);
    portYIELD_FROM_ISR(woken);
}

void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stk, uint32_t *n)
{
    *tcb = &idleTcb; *stk = idleStk; *n = configMINIMAL_STACK_SIZE;
}

void vApplicationStackOverflowHook(TaskHandle_t t, char *name)
{
    (void)t; probe_sink = (uint32_t)name; for (;;) { }
}

Q_NORETURN Q_onError(char const * const module, int_t const id)
{
    probe_sink = (uint32_t)module + (uint32_t)id;
    NVIC_SystemReset();
    for (;;) { }
}

void QF_onStartup(void)
{
    NVIC_SetPriority(EXTI4_15_IRQn, 3);
    NVIC_EnableIRQ(EXTI4_15_IRQn);
    NVIC_SetPriority(USART2_IRQn, 3);
    NVIC_EnableIRQ(USART2_IRQn);
}

void QF_onCleanup(void) { }

void probe_measure(void)
{
    TaskStatus_t s;
    QActive *const ao[] = { AO_Estop, AO_Gate, AO_Sorter, AO_Bus, AO_Imu, AO_Console, AO_Lcd };
    for (unsigned i = 0; i < 7u; i++) {
        vTaskGetInfo(ao[i]->thread, &s, pdTRUE, eInvalid);
        probe_sink += s.ulRunTimeCounter + s.usStackHighWaterMark + uxTaskGetStackHighWaterMark(ao[i]->thread)
                    + (uint32_t)pcTaskGetName(ao[i]->thread)[0] + uxQueueMessagesWaiting(ao[i]->eQueue);
    }
    probe_sink += (uint32_t)ulTaskGetIdleRunTimeCounter() + (uint32_t)xTaskGetIdleTaskHandle() + xTaskGetTickCount();
    for (uint_fast8_t p = 1U; p <= 3U; p++)
        probe_sink += QF_getPoolMin(p);
}

int main(void)
{
    static struct { QActive *const *ao; uint8_t prio; QEvtPtr *q; uint16_t qn; StackType_t *s; uint16_t sb; const char *name; } const def[] = {
        { &AO_Estop,   7U, qEstop,   Q_DIM(qEstop),   sEstop,   sizeof sEstop,   "estop" },
        { &AO_Gate,    6U, qGate,    Q_DIM(qGate),    sGate,    sizeof sGate,    "gate" },
        { &AO_Sorter,  5U, qSorter,  Q_DIM(qSorter),  sSorter,  sizeof sSorter,  "sorter" },
        { &AO_Bus,     4U, qBus,     Q_DIM(qBus),     sBus,     sizeof sBus,     "bus" },
        { &AO_Imu,     3U, qImu,     Q_DIM(qImu),     sImu,     sizeof sImu,     "imu" },
        { &AO_Console, 2U, qConsole, Q_DIM(qConsole), sConsole, sizeof sConsole, "console" },
        { &AO_Lcd,     1U, qLcd,     Q_DIM(qLcd),     sLcd,     sizeof sLcd,     "lcd" },
    };
    QF_init();
    QF_poolInit(smlPool, sizeof smlPool, sizeof smlPool[0]);
    QF_poolInit(medPool, sizeof medPool, sizeof medPool[0]);
    QF_poolInit(lrgPool, sizeof lrgPool, sizeof lrgPool[0]);
    QActive_psInit(subscrSto, Q_DIM(subscrSto));
    aos_ctor();
    for (unsigned i = 0; i < Q_DIM(def); i++) {
        QActive_setAttr(*def[i].ao, TASK_NAME_ATTR, def[i].name);
        QActive_start(*def[i].ao, def[i].prio, def[i].q, def[i].qn, def[i].s, def[i].sb, (void *)0);
    }
    probe_measure();
    return QF_run();
}
