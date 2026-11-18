/* Стрес-тест ядра FreeRTOS у Wokwi (збірка TEST_KSTRESS=1, лише ARCH=rtos, у продукт
 * не входить; docs/rtos-design.md, розділ 2). Чи тримають списки ядра, черги й
 * м'ютекс, коли PRIMASK не маскує переривання (docs/journal.md, проблема 35).
 *
 *   tick  P4  xTaskDelayUntil 1 мс; м'ютекс зі спільним лічильником;
 *             на старті запам'ятовує тік ядра і тік perf (рядок KSTRESS_TICK);
 *             раз на 10 с друкує рядки KSTRESS* (на найвищому
 *             пріоритеті, щоб звіт був, навіть якщо нижчі задачі голодують;
 *             друк блокувальний, тож після нього кілька пробуджень запізнюються)
 *   pong  P3  черга q_ping: перевіряє контрольну суму кожного повідомлення
 *             (від ping і від ISR), на повідомлення ping відповідає в q_pong
 *   ping  P2  повідомлення в q_ping, чекає відповіді з q_pong; PING_BURST
 *             обмінів раз на PING_PERIOD тіків, щоб P1 і idle теж працювали
 *   mtx   P1  чекає сповіщення від ISR; м'ютекс зі спільним лічильником
 *   TIM14 2 кГц: vTaskNotifyGiveFromISR задачі mtx щоразу, xQueueSendFromISR у
 *             q_ping - кожне ISR_MSG_EVERY-те, portYIELD_FROM_ISR
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
#include "perf_tick.h"
#include "perf_hal.h"
#if SIM_CRIT
#include "port_sim.h"
#endif

#define ISR_HZ              2000u
/* Повідомлення в чергу - кожне ISR_MSG_EVERY-те переривання, сповіщення - кожне;
 * ping - раз на PING_PERIOD тіків. У Wokwi код іде в рази повільніше, ніж на
 * 48 МГц (порожній цикл на 600 обертів - 1,3 мс): з 2 000 повідомлень на секунду
 * pong займала 64 % процесора, а з 500 і ping щотіку idle все одно не лишалося
 * нічого, і mtx (P1) не отримувала процесора зовсім */
#define ISR_MSG_EVERY       8u
#define PING_PERIOD         4u
#define REPORT_TICKS        10000u   /* 10 с при тіку 1 мс */
#define MSG_QLEN            8u
#define PING_BURST          1u
#define WAIT_TICKS          pdMS_TO_TICKS(100)
#define MTX_WAIT_TICKS      pdMS_TO_TICKS(50)
/* Скільки тримати м'ютекс: за часом perf_now, а не числом обертів циклу - у
 * Wokwi 600 обертів порожнього циклу тривали 1,3 мс замість ~0,13, і tick (1 мс)
 * не встигав заблокуватися, тож нижчі задачі голодували */
#define HOLD_US             20u      /* P1 і P4 усе ще перетинаються (P1 - 2 кГц) */
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

/* Час у тактах perf_now: робота задачі tick за ітерацію (без очікування в
 * xTaskDelayUntil) і з неї - mtx_round. Обробник TIM14 не міряється: perf_now з
 * переривання, пріоритетнішого за SysTick, може відстати на тік, якщо застане
 * SysTick_Handler до rtos_hal_ticks++ (docs/journal.md) */
typedef struct {
    uint32_t n;
    uint32_t max;
    uint64_t sum;
} cyc_t;

static volatile cyc_t t_iter, t_mtx;

static void cyc_add(volatile cyc_t *c, uint32_t cyc)
{
    c->n++;
    c->sum += cyc;
    if (cyc > c->max)
        c->max = cyc;
}

static uint32_t cyc_avg(const volatile cyc_t *c)
{
    return c->n != 0u ? (uint32_t)(c->sum / c->n) : 0u;
}
static QueueHandle_t q_ping;
static QueueHandle_t q_pong;
static SemaphoreHandle_t mtx;
static TaskHandle_t h_mtx;
static volatile uint32_t isr_seq;      /* повідомлень ISR */
static volatile uint32_t isr_calls;    /* переривань TIM14 */
static TickType_t ktick0;        /* тік ядра і тік perf на старті tick: */
static uint32_t ptick0;          /* за весь прогін вони мають іти однаково */

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
    kstress_port_counters_t p;

    kstress_port_counters(&p);
    rtos_hal_raw_putu(HAL_GetTick(), 10u);
    rtos_hal_raw_puts(" KSTRESS_ISR");
    raw_kv(" isr=", isr_calls, 10u);
    raw_kv(" tick=", cnt.ticks, 10u);
    raw_kv(" ping=", cnt.ping, 10u);
    raw_kv(" isr_msg=", cnt.isr_msg, 10u);
    raw_kv(" mtx=", cnt.mtx_p1, 10u);
    raw_kv("/", cnt.mtx_p4, 10u);
    rtos_hal_raw_puts(" cur=");
    rtos_hal_raw_puts(cur != NULL ? pcTaskGetName(cur) : "-");
    raw_kv(" pc=", pc, 16u);
    raw_kv(" crit=", kstress_crit_nesting(), 16u);
    raw_kv(" switches=", p.switches, 10u);
    raw_kv(" switches_in=", p.switches_in_task_crit, 10u);
    raw_kv(" tick_in=", p.tick_in_task_crit, 10u);
    rtos_hal_raw_puts("\r\n");
}

void TIM14_IRQHandler(void);
void TIM14_IRQHandler(void)
{
    BaseType_t woken = pdFALSE;
    msg_t m;

    if (rtos_hal_irq_defer(TIM14_IRQn))
        return;                          /* виконається після критичної секції */
    kstress_note_isr(KSTRESS_SRC_TIM14);
    if (isr_calls != 0u && isr_calls % PULSE_ISR_CALLS == 0u) {
        static uint32_t ping_seen, pong_seen;

        if (cnt.ping == ping_seen && cnt.isr_msg == pong_seen)
            pulse();                     /* за 5 с ні ping, ні pong не просунулися */
        ping_seen = cnt.ping;
        pong_seen = cnt.isr_msg;
    }
    TIM14->SR = ~TIM_SR_UIF;
    if (isr_calls++ % ISR_MSG_EVERY == 0u) {
        msg_make(&m, SRC_ISR, isr_seq++);
        if (xQueueSendFromISR(q_ping, &m, &woken) != pdPASS)
            cnt.isr_drop++;
    }
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
    {
        uint32_t t0 = perf_now();
        uint32_t hold = HOLD_US * (SystemCoreClock / 1000000u);

        while (perf_now() - t0 < hold) {
        }
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
    taskENTER_CRITICAL();
    ktick0 = xTaskGetTickCount();
    ptick0 = perf_tick_count();
    taskEXIT_CRITICAL();
    last = xTaskGetTickCount();
    for (;;) {
        uint32_t t0, t1;

        if (xTaskDelayUntil(&last, 1) == pdFALSE)
            cnt.late++;
        t0 = perf_now();
        cnt.ticks++;
        cnt.mtx_p4 += (uint32_t)mtx_round();
        t1 = perf_now();
        cyc_add(&t_mtx, t1 - t0);
        if (cnt.ticks % REPORT_TICKS == 0u)
            report(cnt.ticks / 1000u);
        cyc_add(&t_iter, perf_now() - t0);
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
        (void)xTaskDelayUntil(&last, PING_PERIOD);   /* vTaskDelay у прошивці не збирається */
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
    /* Критичні секції задач (taskENTER_CRITICAL): crit_task_n - входів, crit_task -
     * подій усередині (PendSV, тік ядра, TIM14); на залізі crit_task = 0 */
    uart_hal_printf("%lu KSTRESS_CRIT t_s=%lu crit_task_n=%lu crit_task=%lu pendsv_in=%lu "
                    "switches_in=%lu tick_in=%lu tim14_in=%lu switches=%lu pc=0x%08lX",
                    (unsigned long)HAL_GetTick(), (unsigned long)t_s, (unsigned long)p.crit_task_n,
                    (unsigned long)(p.pendsv_in_task_crit + p.tick_in_task_crit +
                                    p.tim14_in_task_crit),
                    (unsigned long)p.pendsv_in_task_crit, (unsigned long)p.switches_in_task_crit,
                    (unsigned long)p.tick_in_task_crit, (unsigned long)p.tim14_in_task_crit,
                    (unsigned long)p.switches, (unsigned long)p.first_in_crit_pc);
    /* Те саме для критичних секцій ISR (taskENTER_CRITICAL_FROM_ISR) */
    uart_hal_printf("%lu KSTRESS_CRIT_ISR t_s=%lu crit_isr_n=%lu crit_isr=%lu pendsv_in=%lu "
                    "switches_in=%lu tick_in=%lu tim14_in=%lu", (unsigned long)HAL_GetTick(),
                    (unsigned long)t_s, (unsigned long)p.crit_isr_n,
                    (unsigned long)(p.pendsv_in_isr_crit + p.tick_in_isr_crit + p.tim14_in_isr_crit),
                    (unsigned long)p.pendsv_in_isr_crit, (unsigned long)p.switches_in_isr_crit,
                    (unsigned long)p.tick_in_isr_crit, (unsigned long)p.tim14_in_isr_crit);
    /* idle - частка часу задачі idle від старту ядра (run-time stats): якщо 0,
     * задачі з пріоритетом 1 теж не отримують процесора */
    uart_hal_printf("%lu KSTRESS_TIME t_s=%lu iter_avg_cyc=%lu iter_max_cyc=%lu mtx_avg_cyc=%lu "
                    "mtx_max_cyc=%lu", (unsigned long)HAL_GetTick(), (unsigned long)t_s,
                    (unsigned long)cyc_avg(&t_iter), (unsigned long)t_iter.max,
                    (unsigned long)cyc_avg(&t_mtx), (unsigned long)t_mtx.max);
    /* Частка процесора за задачами від старту ядра, проміле (run-time stats; час
     * переривань ядро зараховує перерваній задачі) */
    {
        static TaskStatus_t st[6];
        UBaseType_t n = uxTaskGetSystemState(st, 6, NULL);
        uint64_t total = portGET_RUN_TIME_COUNTER_VALUE() + 1u;

        uart_hal_line_add("%lu KSTRESS_CPU t_s=%lu", (unsigned long)HAL_GetTick(), (unsigned long)t_s);
        for (UBaseType_t i = 0; i < n; i++)
            uart_hal_line_add(" %s=%lu", st[i].pcTaskName,
                              (unsigned long)(st[i].ulRunTimeCounter * 1000u / total));
        uart_hal_line_end();
    }
    /* Тіки ядра і perf від старту tick (мають збігатися: відкладений тік не губиться)
     * і робота обходу SIM_CRIT - відкладені тіки й PendSV (це не помилки) */
    {
        uint32_t deferred_tick = 0, deferred_pendsv = 0, deferred_irq = 0;
        uint32_t ktick, ptick;
#if SIM_CRIT
        port_sim_counters_t sim;

        port_sim_counters(&sim);
        deferred_tick = sim.deferred_tick;
        deferred_pendsv = sim.deferred_pendsv;
        deferred_irq = sim.deferred_irq;
#endif
        taskENTER_CRITICAL();
        ktick = (uint32_t)(xTaskGetTickCount() - ktick0);
        ptick = perf_tick_count() - ptick0;
        taskEXIT_CRITICAL();
        uart_hal_printf("%lu KSTRESS_TICK t_s=%lu ktick=%lu ptick=%lu deferred_tick=%lu "
                        "deferred_pendsv=%lu heap_min=%lu", (unsigned long)HAL_GetTick(),
                        (unsigned long)t_s, (unsigned long)ktick, (unsigned long)ptick,
                        (unsigned long)deferred_tick, (unsigned long)deferred_pendsv,
                        (unsigned long)xPortGetMinimumEverFreeHeapSize());
        /* Лінія NVIC TIM17 на кожному тіку: поза секцією вона має бути ввімкнена.
         * hal_ms - час HAL (TIM17) від скидання: відстає від тіку, якщо його
         * переривання губляться */
        uart_hal_printf("%lu KSTRESS_NVIC t_s=%lu samples=%lu off_in_crit=%lu off_free=%lu "
                        "deferred_irq=%lu hal_ms=%lu", (unsigned long)HAL_GetTick(),
                        (unsigned long)t_s, (unsigned long)p.nvic_samples,
                        (unsigned long)p.nvic_off_in_crit, (unsigned long)p.nvic_off_free,
                        (unsigned long)deferred_irq, (unsigned long)HAL_GetTick());
    }
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
