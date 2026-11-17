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

#define HAL_TICK_TIMER_HZ  1000000u   /* TIM17 тікає 1 МГц: ARR = 999 дає 1 кГц */

volatile uint32_t rtos_hal_ticks;
static volatile uint8_t kernel_ticks;   /* 1 — ядро стартує: тіки йдуть порту */

/* --- HAL timebase: TIM17 --------------------------------------------------
 * Заміна слабких HAL_InitTick, HAL_SuspendTick, HAL_ResumeTick з stm32c0xx_hal.c.
 * HAL_InitTick викликають HAL_Init і HAL_RCC_ClockConfig (після зміни частоти). */
HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
    uint32_t tim_hz;

    if (TickPriority >= (1UL << __NVIC_PRIO_BITS) || (uint32_t)uwTickFreq == 0u)
        return HAL_ERROR;

    /* Тік ядра і perf_now: SysTick з LOAD = SystemCoreClock / TICK_HZ - 1, як його
     * налаштував би порт (vPortSetupTimerInterrupt нижче лише перевіряє) */
    if (SysTick_Config(SystemCoreClock / configTICK_RATE_HZ) != 0u)
        return HAL_ERROR;

    /* Таймер тактується від PCLK, а якщо дільник APB не 1 — від 2 * PCLK */
    __HAL_RCC_TIM17_CLK_ENABLE();
    tim_hz = HAL_RCC_GetPCLK1Freq();
    if ((RCC->CFGR & RCC_CFGR_PPRE) != 0u)
        tim_hz *= 2u;

    TIM17->CR1 = 0u;
    TIM17->PSC = tim_hz / HAL_TICK_TIMER_HZ - 1u;
    TIM17->ARR = HAL_TICK_TIMER_HZ / (1000u / (uint32_t)uwTickFreq) - 1u;
    TIM17->EGR = TIM_EGR_UG;          /* PSC буферизований: завантажити зараз */
    TIM17->SR = 0u;                   /* UG виставив UIF — це не тік */
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
    if (kernel_ticks)
        xPortSysTickHandler();
}

/* Порт викликає це з xPortStartScheduler (переривання вже вимкнені), щоб
 * запустити SysTick. Він уже йде з HAL_InitTick з тим самим LOAD; перезапуск
 * обнулив би VAL посеред тіку, і perf_now стрибнув би назад. Тож лише перевірка
 * і з цього моменту — тіки ядру. */
void vPortSetupTimerInterrupt(void)
{
    configASSERT(SysTick->LOAD == configCPU_CLOCK_HZ / configTICK_RATE_HZ - 1u);
    configASSERT((SysTick->CTRL & (SysTick_CTRL_ENABLE_Msk | SysTick_CTRL_TICKINT_Msk))
                 == (SysTick_CTRL_ENABLE_Msk | SysTick_CTRL_TICKINT_Msk));
    kernel_ticks = 1;
}

void rtos_hal_start_scheduler(void)
{
    vTaskStartScheduler();
    for (;;) {
    }
}

#if PERF
/* Лічильник run-time stats: такти CPU, 64 біти (FreeRTOSConfig.h) */
uint64_t rtos_hal_runtime_now(void)
{
    return perf_now_cyc64();
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
