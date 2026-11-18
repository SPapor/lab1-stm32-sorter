/* База часу в ARCH=rtos (docs/rtos-design.md, розділ 3).
 *
 * SysTick — тік ядра FreeRTOS, частота TICK_HZ. HAL (HAL_GetTick, HAL_Delay,
 * тайм-аути I2C і UART) — на TIM17, 1 кГц: TIM3 зайнятий серво, TIM14 лишаємо в
 * резерві.
 *
 * SysTick запускається вже в HAL_InitTick, тобто з HAL_Init, як і в super loop:
 * perf_now рахує від того самого моменту, а BOOT і самоперевірка до старту ядра
 * теж міряються. Вектор SysTick — SysTick_Handler нижче: він рахує кожен тік для
 * perf_now і лише після старту ядра передає тік обробнику порту (у port.c той
 * перейменований на xPortSysTickHandler, app.mk). До старту ядра тік обробляти
 * не можна: списків задач і поточної задачі ще немає. */

#include "rtos_hal.h"
#include "perf_tick.h"
#include "perf_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32c0xx_hal.h"
#if SIM_CRIT
#include "port_sim.h"
#endif
#ifdef TEST_KSTRESS
#include "kstress_port.h"
#endif

#define HAL_TICK_TIMER_HZ  1000000u   /* TIM17 тікає 1 МГц: ARR = 999 дає 1 кГц */
#define START_GUARD_CYC    256u       /* vPortSetupTimerInterrupt: «до переходу через 0 мало» */

/* perf_tick_us64 рахує мкс у тіку як ціле < 2^16, а 1 тік = 10^6 / TICK_HZ мкс */
#if TICK_HZ < 16 || (1000000 % TICK_HZ) != 0
#error "TICK_HZ: не нижче 16 Гц і має ділити 1 000 000 (perf_tick_us64, docs/rtos-design.md)"
#endif

volatile uint32_t rtos_hal_ticks;
static volatile uint8_t kernel_ticks;   /* 1 — ядро стартує: тіки йдуть порту */
#if PERF
static uint64_t runtime_base;           /* perf_now_cyc64 на старті ядра */
static uint32_t startup_api_us;         /* перед першим викликом API ядра */
static uint32_t startup_start_us;       /* ядро запускає першу задачу */
#endif

/* Таймери тактуються від PCLK, а якщо APB ділиться - від 2 * PCLK. На C0 поле
 * PPRE 0xx - без ділення (RM0490, RCC_CFGR), ділення - лише з бітом PPRE[2] */
uint32_t rtos_hal_timer_clk_hz(void)
{
    uint32_t hz = HAL_RCC_GetPCLK1Freq();

    if ((RCC->CFGR & RCC_CFGR_PPRE_2) != 0u)
        hz *= 2u;
    return hz;
}

/* --- HAL timebase: TIM17 --------------------------------------------------
 * Заміна слабких HAL_InitTick, HAL_SuspendTick, HAL_ResumeTick з stm32c0xx_hal.c.
 * HAL_InitTick викликають HAL_Init і HAL_RCC_ClockConfig (після зміни частоти). */
HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
    uint32_t tim_hz, arr;

    if (TickPriority >= (1UL << __NVIC_PRIO_BITS) || (uint32_t)uwTickFreq == 0u)
        return HAL_ERROR;
    /* TIM17 16-бітний: тік HAL довший за 65 мс (HAL_TICK_FREQ_10HZ) не вміщується */
    arr = HAL_TICK_TIMER_HZ / (1000u / (uint32_t)uwTickFreq) - 1u;
    if (arr > 0xFFFFu)
        return HAL_ERROR;

    /* Тік ядра і perf_now: SysTick з LOAD = SystemCoreClock / TICK_HZ - 1, як його
     * налаштував би порт (vPortSetupTimerInterrupt нижче лише перевіряє) */
#ifdef TEST_BOOTMARK
    perf_bootmark('i');       /* дослід: стан SysTick перед перезапуском */
#endif
    if (SysTick_Config(SystemCoreClock / configTICK_RATE_HZ) != 0u)
        return HAL_ERROR;

    __HAL_RCC_TIM17_CLK_ENABLE();
    tim_hz = rtos_hal_timer_clk_hz();

    /* Переривання вимкнене до UG: при другому виклику (після зміни частоти) UIE
     * ще стоїть, і UG дав би зайвий HAL_IncTick */
    TIM17->CR1 = 0u;
    TIM17->DIER = 0u;
    TIM17->PSC = tim_hz / HAL_TICK_TIMER_HZ - 1u;
    TIM17->ARR = arr;
    TIM17->EGR = TIM_EGR_UG;          /* PSC буферизований: завантажити зараз */
    TIM17->SR = 0u;                   /* UG виставив UIF — це не тік */
    NVIC_ClearPendingIRQ(TIM17_IRQn);
    TIM17->DIER = TIM_DIER_UIE;
    TIM17->CR1 = TIM_CR1_CEN;

    HAL_NVIC_SetPriority(TIM17_IRQn, TickPriority, 0u);
    HAL_NVIC_EnableIRQ(TIM17_IRQn);
    uwTickPrio = TickPriority;
    return HAL_OK;
}

void HAL_SuspendTick(void)
{
    TIM17->DIER &= ~TIM_DIER_UIE;
}

void HAL_ResumeTick(void)
{
    TIM17->DIER |= TIM_DIER_UIE;
}

void TIM17_IRQHandler(void);
void TIM17_IRQHandler(void)
{
    if (TIM17->SR & TIM_SR_UIF) {
        TIM17->SR = ~TIM_SR_UIF;      /* rc_w0: нуль скидає лише UIF */
        HAL_IncTick();
    }
}

/* --- Тік ядра ------------------------------------------------------------ */

void xPortSysTickHandler(void);      /* SysTick_Handler порту (port.c), перейменований */
void SysTick_Handler(void);

/* Номер тіку для perf_now (perf_tick.h) — кожне переривання SysTick, і до старту
 * ядра, і після. Хук тіку FreeRTOS для цього не годиться: поки xTaskResumeAll
 * доганяє відкладені тіки, ядро його не викликає, і тік, що прийшов у цей
 * момент, загубився б (docs/rtos-design.md, розділ 3) */
void SysTick_Handler(void)
{
    rtos_hal_ticks++;
    if (!kernel_ticks)
        return;
#if SIM_CRIT
    /* Усередині критичної секції тік ядра чекає її кінця (Platform/rtos/port_sim):
     * на залізі cpsid i так само затримав би весь цей обробник */
    if (port_sim_defer_tick())
        return;
#endif
#ifdef TEST_KSTRESS
    kstress_note_isr(KSTRESS_SRC_TICK);
#endif
    xPortSysTickHandler();
}

/* Порт викликає це з xPortStartScheduler (переривання вже вимкнені), щоб
 * запустити SysTick. Він уже йде з HAL_InitTick з тим самим LOAD; перезапуск
 * обнулив би VAL посеред тіку, і perf_now стрибнув би назад. Тож перевірка і з
 * цього моменту — тіки ядру.
 * Але тік, що настав, поки vTaskStartScheduler тримав переривання вимкненими,
 * чекає, і після cpsie у vStartFirstTask обробник порту виконався б раніше, ніж
 * svc запустить першу задачу. Якщо xTaskIncrementTick попросить перемикання
 * (кілька задач на найвищому пріоритеті), PendSV зберіг би контекст у ще не
 * налаштований PSP. Тому такий тік рахується тут лише для perf_now і знімається
 * з очікування, а якщо до переходу через 0 лишилося менше START_GUARD_CYC тактів
 * (~5 мкс), спершу дочікуємося його. Після цього до наступного тіку - майже
 * цілий тік, а до svc - кілька десятків тактів, як у порту, що запускає SysTick
 * саме тут. */
void vPortSetupTimerInterrupt(void)
{
    configASSERT(SysTick->LOAD == configCPU_CLOCK_HZ / configTICK_RATE_HZ - 1u);
    configASSERT((SysTick->CTRL & (SysTick_CTRL_ENABLE_Msk | SysTick_CTRL_TICKINT_Msk))
                 == (SysTick_CTRL_ENABLE_Msk | SysTick_CTRL_TICKINT_Msk));
    while (SysTick->VAL < START_GUARD_CYC && (SCB->ICSR & SCB_ICSR_PENDSTSET_Msk) == 0u) {
    }
    if (SCB->ICSR & SCB_ICSR_PENDSTSET_Msk) {
        SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
        rtos_hal_ticks++;
    }
    kernel_ticks = 1;
#if PERF
    startup_start_us = (uint32_t)perf_now_us64();
#endif
}

void rtos_hal_start_scheduler(void)
{
    vTaskStartScheduler();
    for (;;) {
    }
}

#if PERF
/* Від першого виклику API ядра до старту першої задачі переривання вимкнені
 * (ulCriticalNesting = 0xaaaaaaaa у port.c): вікно має бути коротшим за тік, бо
 * PENDSTSET зберігає щонайбільше один тік, що настав за цей час */
void rtos_hal_mark_api(void)
{
    startup_api_us = (uint32_t)perf_now_us64();
}

void rtos_hal_startup_us(uint32_t *api_us, uint32_t *start_us)
{
    *api_us = startup_api_us;
    *start_us = startup_start_us;
}

/* Лічильник run-time stats: такти CPU, 64 біти (FreeRTOSConfig.h), від старту
 * ядра. perf_now_cyc64 рахує від HAL_Init, а ядро бере перший відлік за 0: без
 * бази час BOOT і самоперевірки дістався б першій задачі, що поступиться
 * процесором. Ядро викликає rtos_hal_runtime_start з vTaskStartScheduler */
void rtos_hal_runtime_start(void)
{
    runtime_base = perf_now_cyc64();
}

uint64_t rtos_hal_runtime_now(void)
{
    return perf_now_cyc64() - runtime_base;
}
#endif

const char *rtos_hal_kernel_version(void)
{
    return tskKERNEL_VERSION_NUMBER;
}

uint32_t rtos_hal_heap_total(void)
{
    return configTOTAL_HEAP_SIZE;
}
