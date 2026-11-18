/* Стрес-тест ядра FreeRTOS у Wokwi (збірка TEST_KSTRESS=1, лише ARCH=rtos, у продукт
 * не входить; docs/rtos-design.md, розділ 2). Чи тримають списки ядра, черги й
 * м'ютекс, коли PRIMASK не маскує переривання (docs/journal.md, проблема 35).
 *
 *   tick  P4  xTaskDelayUntil 1 мс; м'ютекс зі спільним лічильником;
 *             раз на 10 с - знімок лічильників для рядка KSTRESS
 *   pong  P3  черга q_ping: перевіряє контрольну суму кожного повідомлення
 *             (від ping і від ISR), на повідомлення ping відповідає в q_pong
 *   ping  P2  повідомлення в q_ping, чекає відповіді з q_pong; пачка по
 *             PING_BURST обмінів раз на тік, щоб P1 і idle теж працювали
 *   mtx   P1  чекає сповіщення від ISR; м'ютекс зі спільним лічильником;
 *             друкує рядок KSTRESS (найнижчий пріоритет, друк блокувальний)
 *   TIM14 2 кГц: xQueueSendFromISR у q_ping, vTaskNotifyGiveFromISR задачі mtx,
 *             portYIELD_FROM_ISR
 *
 * Помилка - будь-який лічильник *_err; assert, переповнення стека і HardFault
 * зупиняють програму рядком FATAL (Platform/rtos/rtos_diag.c), і рядків KSTRESS
 * далі немає. Лічильники порту (переривання і перемикання всередині критичних
 * секцій) - Platform/rtos/kstress/port_kstress.c. */
#ifdef TEST_KSTRESS
#include "kstress.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "stm32c0xx_hal.h"
#include "uart_hal.h"
#include "kstress_port.h"

#define ISR_HZ              2000u
#define REPORT_TICKS        10000u   /* 10 с при тіку 1 мс */
#define MSG_QLEN            8u
#define PING_BURST          8u
#define WAIT_TICKS          pdMS_TO_TICKS(100)
#define MTX_WAIT_TICKS      pdMS_TO_TICKS(50)
#define HOLD_LOOPS          600u     /* ~100 мкс з м'ютексом: P1 і P4 часто перетинаються */
#define IRQ_PRIO            2u       /* вище за SysTick і PendSV (3) */

#define STACK_TICK          112u
#define STACK_PONG          112u
#define STACK_PING          112u
#define STACK_MTX           192u     /* друк: форматер і буфер рядка */

enum {
    SRC_PING = 0x50494E47u,
    SRC_PONG = 0x504F4E47u,
    SRC_ISR = 0x00495352u
};

typedef struct {
    uint32_t seq;
    uint32_t src;
    uint32_t data[3];
    uint32_t sum;
} msg_t;

typedef struct {
    uint32_t ping;            /* обмінів ping -> pong -> ping без помилок */
    uint32_t isr_msg;         /* повідомлень ISR, прийнятих pong */
    uint32_t isr_drop;        /* черга повна, ISR не відправив */
    uint32_t notify_given;
    uint32_t notify_taken;
    uint32_t mtx_rounds;
    uint32_t ticks;           /* пробуджень tick */
    uint32_t late;            /* xTaskDelayUntil без очікування: момент уже минув */
    uint32_t sum_err;         /* контрольна сума або джерело не ті */
    uint32_t seq_err;         /* відповідь не на те повідомлення або ISR не по порядку */
    uint32_t mtx_err;         /* у критичній ділянці м'ютекса - чужий власник або a != b */
    uint32_t notify_err;      /* узято сповіщень більше, ніж дано */
    uint32_t mtx_timeout;
    uint32_t q_timeout;
} kstress_cnt_t;

static volatile kstress_cnt_t cnt;
static QueueHandle_t q_ping;
static QueueHandle_t q_pong;
static SemaphoreHandle_t mtx;
static TaskHandle_t h_mtx;
static volatile uint32_t isr_seq;
static volatile uint8_t report_due;
static volatile uint32_t report_s;

/* Дані під м'ютексом: інваріант a == b і власник NULL поза ділянкою */
static struct {
    uint32_t a;
    uint32_t b;
    void *owner;
} shared;

static uint32_t msg_sum(const msg_t *m)
{
    uint32_t s = 0x5EEDu;

    s = ((s << 5) | (s >> 27)) ^ m->seq;
    s = ((s << 5) | (s >> 27)) ^ m->src;
    for (unsigned i = 0; i < 3u; i++)
        s = ((s << 5) | (s >> 27)) ^ m->data[i];
    return ~s;
}

static void msg_make(msg_t *m, uint32_t src, uint32_t seq)
{
    m->seq = seq;
    m->src = src;
    m->data[0] = seq * 2654435761u;
    m->data[1] = ~seq;
    m->data[2] = seq ^ src;
    m->sum = msg_sum(m);
}

void TIM14_IRQHandler(void);
void TIM14_IRQHandler(void)
{
    BaseType_t woken = pdFALSE;
    msg_t m;

    kstress_note_isr();
    TIM14->SR = ~TIM_SR_UIF;
    msg_make(&m, SRC_ISR, isr_seq++);
    if (xQueueSendFromISR(q_ping, &m, &woken) != pdPASS)
        cnt.isr_drop++;
    vTaskNotifyGiveFromISR(h_mtx, &woken);
    cnt.notify_given++;
    portYIELD_FROM_ISR(woken);
}

static void tim14_start(void)
{
    uint32_t clk = HAL_RCC_GetPCLK1Freq();

    if ((RCC->CFGR & RCC_CFGR_PPRE) != 0u)
        clk *= 2u;                       /* таймери при діленні APB - удвічі */
    __HAL_RCC_TIM14_CLK_ENABLE();
    TIM14->CR1 = 0;
    TIM14->PSC = clk / 1000000u - 1u;    /* 1 МГц */
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

/* Один захід у м'ютекс: інваріант на вході й на виході */
static void mtx_round(void)
{
    void *self = xTaskGetCurrentTaskHandle();

    if (xSemaphoreTake(mtx, MTX_WAIT_TICKS) != pdTRUE) {
        cnt.mtx_timeout++;
        return;
    }
    if (shared.owner != NULL || shared.a != shared.b)
        cnt.mtx_err++;
    shared.owner = self;
    shared.a++;
    for (volatile uint32_t i = 0; i < HOLD_LOOPS; i++) {
    }
    shared.b++;
    if (shared.owner != self || shared.a != shared.b)
        cnt.mtx_err++;
    shared.owner = NULL;
    cnt.mtx_rounds++;
    xSemaphoreGive(mtx);
}

static void tick_task(void *arg)
{
    TickType_t last;

    (void)arg;
    tim14_start();
    last = xTaskGetTickCount();
    for (;;) {
        if (xTaskDelayUntil(&last, 1) == pdFALSE)
            cnt.late++;
        cnt.ticks++;
        mtx_round();
        if (cnt.ticks % REPORT_TICKS == 0u) {
            report_s = cnt.ticks / 1000u;
            report_due = 1;
        }
    }
}

static void pong_task(void *arg)
{
    msg_t m;
    uint32_t last_isr = 0;
    int have_isr = 0;

    (void)arg;
    for (;;) {
        if (xQueueReceive(q_ping, &m, WAIT_TICKS) != pdPASS) {
            cnt.q_timeout++;
            continue;
        }
        if (m.sum != msg_sum(&m)) {
            cnt.sum_err++;
        } else if (m.src == SRC_ISR) {
            /* пропуски допустимі (isr_drop), назад - ні */
            if (have_isr && m.seq <= last_isr)
                cnt.seq_err++;
            last_isr = m.seq;
            have_isr = 1;
            cnt.isr_msg++;
        } else if (m.src == SRC_PING) {
            msg_t r = m;

            r.src = SRC_PONG;
            r.data[0] ^= 0xA5A5A5A5u;
            r.sum = msg_sum(&r);
            if (xQueueSend(q_pong, &r, WAIT_TICKS) != pdPASS)
                cnt.q_timeout++;
        } else {
            cnt.sum_err++;
        }
    }
}

static void ping_task(void *arg)
{
    uint32_t seq = 0;
    msg_t m, r;
    TickType_t last = xTaskGetTickCount();

    (void)arg;
    for (;;) {
        for (unsigned i = 0; i < PING_BURST; i++, seq++) {
            msg_make(&m, SRC_PING, seq);
            if (xQueueSend(q_ping, &m, WAIT_TICKS) != pdPASS) {
                cnt.q_timeout++;
                continue;
            }
            if (xQueueReceive(q_pong, &r, WAIT_TICKS) != pdPASS) {
                cnt.q_timeout++;
                continue;
            }
            if (r.sum != msg_sum(&r) || r.src != SRC_PONG)
                cnt.sum_err++;
            else if (r.seq != seq)
                cnt.seq_err++;
            else
                cnt.ping++;
        }
        (void)xTaskDelayUntil(&last, 1);   /* vTaskDelay у прошивці не збирається */
    }
}

static void report(void)
{
    kstress_port_counters_t p;

    kstress_port_counters(&p);
    uart_hal_line_add("%lu KSTRESS t_s=%lu ping=%lu isr_msg=%lu isr_drop=%lu notify=%lu/%lu "
                      "mtx=%lu tick=%lu late=%lu", (unsigned long)HAL_GetTick(),
                      (unsigned long)report_s, (unsigned long)cnt.ping,
                      (unsigned long)cnt.isr_msg, (unsigned long)cnt.isr_drop,
                      (unsigned long)cnt.notify_taken, (unsigned long)cnt.notify_given,
                      (unsigned long)cnt.mtx_rounds, (unsigned long)cnt.ticks,
                      (unsigned long)cnt.late);
    uart_hal_line_add(" sum_err=%lu seq_err=%lu mtx_err=%lu notify_err=%lu mtx_timeout=%lu "
                      "q_timeout=%lu", (unsigned long)cnt.sum_err, (unsigned long)cnt.seq_err,
                      (unsigned long)cnt.mtx_err, (unsigned long)cnt.notify_err,
                      (unsigned long)cnt.mtx_timeout, (unsigned long)cnt.q_timeout);
    uart_hal_line_end();
    /* Окремим рядком: разом з першим не вміщується в UART_HAL_LINE_MAX */
    uart_hal_printf("%lu KSTRESS_CRIT t_s=%lu switches=%lu switches_in_crit=%lu pendsv_in_crit=%lu "
                    "isr_in_crit=%lu heap_min=%lu", (unsigned long)HAL_GetTick(),
                    (unsigned long)report_s, (unsigned long)p.switches,
                    (unsigned long)p.switches_in_crit, (unsigned long)p.pendsv_in_crit,
                    (unsigned long)p.isr_in_crit, (unsigned long)xPortGetMinimumEverFreeHeapSize());
}

static void mtx_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t n = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));

        cnt.notify_taken += n;
        if (cnt.notify_taken > cnt.notify_given)
            cnt.notify_err++;
        mtx_round();
        if (report_due) {
            report_due = 0;
            report();
        }
    }
}

void kstress_create(void)
{
    BaseType_t ok = pdPASS;

    q_ping = xQueueCreate(MSG_QLEN, sizeof(msg_t));
    q_pong = xQueueCreate(MSG_QLEN, sizeof(msg_t));
    mtx = xSemaphoreCreateMutex();
    configASSERT(q_ping != NULL && q_pong != NULL && mtx != NULL);
    ok &= xTaskCreate(tick_task, "tick", STACK_TICK, NULL, 4, NULL);
    ok &= xTaskCreate(pong_task, "pong", STACK_PONG, NULL, 3, NULL);
    ok &= xTaskCreate(ping_task, "ping", STACK_PING, NULL, 2, NULL);
    ok &= xTaskCreate(mtx_task, "mtx", STACK_MTX, NULL, 1, &h_mtx);
    configASSERT(ok == pdPASS);
    (void)ok;
}
#endif
