#include "perf_hal.h"
#include "perf.h"
#include "perf_tick.h"
#include "board.h"

static uint32_t cyc_per_tick;   /* SysTick->LOAD + 1: такти одного тіку */
static uint32_t cyc_per_us;
static uint32_t us_per_tick;    /* 1000 при тіку 1 кГц, 10000 при 100 Гц */

void perf_hal_init(void)
{
    GPIO_InitTypeDef g = {0};

    /* Тік береться з налаштованого SysTick, а не з константи: у ARCH=rtos його
     * частота — TICK_HZ (1000 або 100 Гц), у super loop — 1 кГц HAL */
    cyc_per_tick = SysTick->LOAD + 1u;
    cyc_per_us = SystemCoreClock / 1000000u;
    us_per_tick = cyc_per_tick / cyc_per_us;

    /* PB13, PB14 — push-pull виходи, як LED_MARK і ADC_MARK у CubeMX */
    BOARD_PROBE_CLK_ENABLE();
    BOARD_PROBE_OFF(BOARD_PROBE_TASK_PIN | BOARD_PROBE_LCD_PIN);
    g.Pin = BOARD_PROBE_TASK_PIN | BOARD_PROBE_LCD_PIN;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BOARD_PROBE_PORT, &g);
}

/* Номер тіку і такти від його початку — узгоджена пара.
 * SysTick рахує вниз від LOAD до 0, тож від початку тіку минуло LOAD - VAL тактів.
 * Номер тіку — perf_tick_count() (perf_tick.h): uwTick HAL у super loop, лічильник
 * з SysTick_Handler у ARCH=rtos. Обидва збільшує переривання SysTick.
 * Якщо лічильник уже перейшов через 0, а SysTick_Handler ще не збільшив тік
 * (переривання заборонені), у SCB->ICSR виставлений PENDSTSET: тік уже настав,
 * додаємо його самі. VAL у такому разі читаємо ще раз — перехід міг статися між
 * першим читанням VAL і перевіркою PENDSTSET, і тоді перше значення «старе».
 * COUNTFLAG у SysTick->CTRL не чіпаємо: читання його скидає.
 * Wokwi не враховує PRIMASK (docs/journal.md): SysTick_Handler може виконатися
 * посеред читання пари, між номером тіку і VAL. Тому номер тіку читається ще раз
 * після VAL, і якщо він змінився, пара читається знову. Це не залежить від того,
 * скільки триває обробник тіку (у FreeRTOS — сотні тактів).
 * Ще в Wokwi PENDSTSET з'являється не відразу після переходу через 0: кілька
 * тактів VAL уже новий, а тік ще старий. Тому, якщо від початку тіку минуло
 * менше PERF_RELOAD_GUARD тактів, а PENDSTSET немає, пара читається ще раз з
 * дозволеними перериваннями - SysTick_Handler устигає збільшити тік. На залізі це
 * щонайбільше PERF_RELOAD_GUARD тактів очікування.
 * Похибка: якщо переривання були заборонені довше за 1 тік, тік губиться. */
#define PERF_RELOAD_GUARD 128u

static uint32_t read_tick(uint32_t *in_tick)
{
    uint32_t tick, val, pend, moved;

    do {
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        tick = perf_tick_count();
        val = SysTick->VAL;
        pend = SCB->ICSR & SCB_ICSR_PENDSTSET_Msk;
        if (pend)
            val = SysTick->VAL;
        moved = (perf_tick_count() != tick);
        __set_PRIMASK(primask);
    } while (moved || (!pend && SysTick->LOAD - val < PERF_RELOAD_GUARD));

    *in_tick = SysTick->LOAD - val;
    return pend ? tick + 1u : tick;
}

uint32_t perf_now(void)
{
    uint32_t in_tick;
    uint32_t tick = read_tick(&in_tick);

    return perf_tick_cyc(tick, cyc_per_tick, in_tick);
}

uint64_t perf_now_cyc64(void)
{
    uint32_t in_tick;
    uint32_t tick = read_tick(&in_tick);

    return perf_tick_cyc64(tick, cyc_per_tick, in_tick);
}

uint64_t perf_now_us64(void)
{
    uint32_t in_tick;
    uint32_t tick = read_tick(&in_tick);

    return perf_tick_us64(tick, us_per_tick, in_tick, cyc_per_us);
}

uint32_t perf_hal_cpu_hz(void)
{
    return SystemCoreClock;
}
