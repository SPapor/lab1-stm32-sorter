/* Стрес-тест ядра FreeRTOS у Wokwi (збірка TEST_KSTRESS=1, лише ARCH=rtos, у продукт
 * не входить; docs/rtos-design.md, розділ 2). Чи тримають списки ядра, черги й
 * м'ютекс, коли PRIMASK не маскує переривання (docs/journal.md, проблема 35).
 *
 *   tick  P4  xTaskDelayUntil 1 мс; м'ютекс зі спільним лічильником;
 *             раз на 10 с друкує рядки KSTRESS і KSTRESS_CRIT (на найвищому
 *             пріоритеті, щоб звіт був, навіть якщо нижчі задачі голодують;
 *             друк блокувальний, тож після нього кілька пробуджень запізнюються)
 *   pong  P3  черга q_ping: перевіряє контрольну суму кожного повідомлення
 *             (від ping і від ISR), на повідомлення ping відповідає в q_pong
 *   ping  P2  повідомлення в q_ping, чекає відповіді з q_pong; пачка по
 *             PING_BURST обмінів раз на тік, щоб P1 і idle теж працювали
 *   mtx   P1  чекає сповіщення від ISR; м'ютекс зі спільним лічильником
 *   TIM14 2 кГц: xQueueSendFromISR у q_ping, vTaskNotifyGiveFromISR задачі mtx,
 *             portYIELD_FROM_ISR
 *
 * Пульс з ISR (рядок KSTRESS_ISR): обробник TIM14 раз на 5 с перевіряє, чи
 * просунулися ping і pong, і якщо ні - сам пише рядок прямо в регістр USART, тож
 * він є, навіть коли задачі стоять. Поточна задача, адреса, де її перервано (PC
 * з кадру винятку на PSP), і вкладеність критичної секції показують, де саме
 * зависло. Поки задачі йдуть, пульсу немає: друк в ISR (~7 мс) спотворив би
 * лічильники запізнень і з'їв би байти рядка, який саме передає задача.
 *
 * Помилка - будь-який лічильник *_err (рядок KSTRESS_ERR); assert, переповнення стека і HardFault
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
#include "rtos_hal.h"

#define ISR_HZ              2000u
#define REPORT_TICKS        10000u   /* 10 с при тіку 1 мс */
#define MSG_QLEN            8u
#define PING_BURST          2u
#define WAIT_TICKS          pdMS_TO_TICKS(100)
#define MTX_WAIT_TICKS      pdMS_TO_TICKS(50)
#define HOLD_LOOPS          600u     /* ~100 мкс з м'ютексом: P1 і P4 часто перетинаються */
#define IRQ_PRIO            2u       /* вище за SysTick і PendSV (3) */
#define PULSE_ISR_CALLS     (5u * ISR_HZ)   /* перевірка для KSTRESS_ISR раз на 5 с */

#define STACK_TICK          192u     /* друк: форматер і буфер рядка */
#define STACK_PONG          112u
#define STACK_PING          112u
#define STACK_MTX           112u

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
    uint32_t mtx_p1;          /* заходів у м'ютекс задачі mtx (P1) */
    uint32_t mtx_p4;          /* і задачі tick (P4) */
    uint32_t ticks;           /* пробуджень tick */
    uint32_t late;            /* xTaskDelayUntil без очікування: момент уже минув */
    uint32_t sum_err;         /* контрольна сума або джерело не ті */
    uint32_t seq_err;         /* відповідь новіша за запит або ISR не по порядку */
    uint32_t stale;           /* запізніла відповідь на старий запит (після тайм-ауту) */
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

/* Пульс пише з ISR сирим виводом (rtos_hal_raw_*), без HAL і буфера uart_hal */
static void raw_kv(const char *key, uint32_t v, unsigned base)
{
    rtos_hal_raw_puts(key);
    if (base == 16u)
        rtos_hal_raw_puts("0x");
    rtos_hal_raw_putu(v, base);
}

/* Рядок KSTRESS_ISR: лічильники і місце, де ISR перервав задачу */
static void pulse(void)
{
    TaskHandle_t cur = xTaskGetCurrentTaskHandle();
    uint32_t pc = ((const uint32_t *)__get_PSP())[6];

    rtos_hal_raw_putu(HAL_GetTick(), 10u);
    rtos_hal_raw_puts(" KSTRESS_ISR");
    raw_kv(" isr=", isr_seq, 10u);
    raw_kv(" tick=", cnt.ticks, 10u);
    raw_kv(" ping=", cnt.ping, 10u);
    raw_kv(" isr_msg=", cnt.isr_msg, 10u);
    raw_kv(" mtx=", cnt.mtx_p1, 10u);
    raw_kv("/", cnt.mtx_p4, 10u);
    rtos_hal_raw_puts(" cur=");
    rtos_hal_raw_puts(cur != NULL ? pcTaskGetName(cur) : "-");
    raw_kv(" pc=", pc, 16u);
    raw_kv(" crit=", kstress_crit_nesting(), 16u);
    rtos_hal_raw_puts("\r\n");
}

void TIM14_IRQHandler(void);
void TIM14_IRQHandler(void)
{
    BaseType_t woken = pdFALSE;
    msg_t m;

    kstress_note_isr();
    if (isr_seq != 0u && isr_seq % PULSE_ISR_CALLS == 0u) {
        static uint32_t ping_seen, pong_seen;

        if (cnt.ping == ping_seen && cnt.isr_msg == pong_seen)
            pulse();                     /* за 5 с ні ping, ні pong не просунулися */
        ping_seen = cnt.ping;
        pong_seen = cnt.isr_msg;
    }
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
    uint32_t clk = rtos_hal_timer_clk_hz();

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

/* Один захід у м'ютекс: інваріант на вході й на виході; 1 - зайшли */
static int mtx_round(void)
{
    void *self = xTaskGetCurrentTaskHandle();

    if (xSemaphoreTake(mtx, MTX_WAIT_TICKS) != pdTRUE) {
        cnt.mtx_timeout++;
        return 0;
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
    xSemaphoreGive(mtx);
    return 1;
}

static void report(uint32_t t_s);

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
        cnt.mtx_p4 += (uint32_t)mtx_round();
        if (cnt.ticks % REPORT_TICKS == 0u)
            report(cnt.ticks / 1000u);
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
            /* Відповідь на старий запит (той, що вийшов за тайм-аут) - не помилка:
             * пропускаємо її і чекаємо свою, інакше ping і pong розійшлися б назавжди */
            for (;;) {
                if (xQueueReceive(q_pong, &r, WAIT_TICKS) != pdPASS) {
                    cnt.q_timeout++;
                    break;
                }
                if (r.sum != msg_sum(&r) || r.src != SRC_PONG) {
                    cnt.sum_err++;
                    break;
                }
                if (r.seq == seq) {
                    cnt.ping++;
                    break;
                }
                if (r.seq > seq) {
                    cnt.seq_err++;
                    break;
                }
                cnt.stale++;
            }
        }
        (void)xTaskDelayUntil(&last, 1);   /* vTaskDelay у прошивці не збирається */
    }
}

static void report(uint32_t t_s)
{
    kstress_port_counters_t p;

    kstress_port_counters(&p);
    /* Три рядки: кожен і з 10-значними лічильниками вміщується в UART_HAL_LINE_MAX */
    uart_hal_printf("%lu KSTRESS t_s=%lu ping=%lu isr_msg=%lu isr_drop=%lu notify=%lu/%lu "
                    "mtx=%lu/%lu tick=%lu late=%lu", (unsigned long)HAL_GetTick(),
                    (unsigned long)t_s, (unsigned long)cnt.ping,
                    (unsigned long)cnt.isr_msg, (unsigned long)cnt.isr_drop,
                    (unsigned long)cnt.notify_taken, (unsigned long)cnt.notify_given,
                    (unsigned long)cnt.mtx_p1, (unsigned long)cnt.mtx_p4, (unsigned long)cnt.ticks,
                    (unsigned long)cnt.late);
    uart_hal_printf("%lu KSTRESS_ERR t_s=%lu sum_err=%lu seq_err=%lu mtx_err=%lu notify_err=%lu "
                    "mtx_timeout=%lu q_timeout=%lu stale=%lu", (unsigned long)HAL_GetTick(),
                    (unsigned long)t_s, (unsigned long)cnt.sum_err, (unsigned long)cnt.seq_err,
                    (unsigned long)cnt.mtx_err, (unsigned long)cnt.notify_err,
                    (unsigned long)cnt.mtx_timeout, (unsigned long)cnt.q_timeout,
                    (unsigned long)cnt.stale);
    uart_hal_printf("%lu KSTRESS_CRIT t_s=%lu switches=%lu switches_in_crit=%lu pendsv_in_crit=%lu "
                    "isr_in_crit=%lu heap_min=%lu", (unsigned long)HAL_GetTick(),
                    (unsigned long)t_s, (unsigned long)p.switches,
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
        cnt.mtx_p1 += (uint32_t)mtx_round();
    }
}

void kstress_create(void)
{
    BaseType_t ok = pdPASS;   /* помилка створення - pdFAIL або errCOULD_NOT_... (-1) */

    q_ping = xQueueCreate(MSG_QLEN, sizeof(msg_t));
    q_pong = xQueueCreate(MSG_QLEN, sizeof(msg_t));
    mtx = xSemaphoreCreateMutex();
    configASSERT(q_ping != NULL && q_pong != NULL && mtx != NULL);
    if (xTaskCreate(tick_task, "tick", STACK_TICK, NULL, 4, NULL) != pdPASS)
        ok = pdFAIL;
    if (xTaskCreate(pong_task, "pong", STACK_PONG, NULL, 3, NULL) != pdPASS)
        ok = pdFAIL;
    if (xTaskCreate(ping_task, "ping", STACK_PING, NULL, 2, NULL) != pdPASS)
        ok = pdFAIL;
    if (xTaskCreate(mtx_task, "mtx", STACK_MTX, NULL, 1, &h_mtx) != pdPASS)
        ok = pdFAIL;
    configASSERT(ok == pdPASS);
    (void)ok;
}
#endif
