#include "perf_hal.h"
#include "stm32c0xx_hal.h"

static uint32_t cyc_per_tick;   /* SystemCoreClock / 1000; SysTick->LOAD = cyc_per_tick - 1 */
static uint32_t cyc_per_us;

void perf_hal_init(void)
{
    cyc_per_tick = SystemCoreClock / 1000u;   /* HAL_TICK_FREQ_DEFAULT: 1 тік = 1 мс */
    cyc_per_us = SystemCoreClock / 1000000u;
}

/* Номер тіку і такти від його початку — узгоджена пара.
 * SysTick рахує вниз від LOAD до 0, тож від початку тіку минуло LOAD - VAL тактів.
 * Якщо лічильник уже перейшов через 0, а SysTick_Handler ще не збільшив uwTick
 * (переривання заборонені), у SCB->ICSR виставлений PENDSTSET: тік уже настав,
 * додаємо його самі. VAL у такому разі читаємо ще раз — перехід міг статися між
 * першим читанням VAL і перевіркою PENDSTSET, і тоді перше значення «старе».
 * COUNTFLAG у SysTick->CTRL не чіпаємо: читання його скидає.
 * Похибка: якщо переривання були заборонені довше за 1 мс, тік губиться. */
static uint32_t read_tick(uint32_t *in_tick)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint32_t tick = uwTick;
    uint32_t val = SysTick->VAL;
    if (SCB->ICSR & SCB_ICSR_PENDSTSET_Msk) {
        val = SysTick->VAL;
        tick++;
    }
    __set_PRIMASK(primask);

    *in_tick = SysTick->LOAD - val;
    return tick;
}

uint32_t perf_now(void)
{
    uint32_t in_tick;
    uint32_t tick = read_tick(&in_tick);

    return tick * cyc_per_tick + in_tick;
}

uint64_t perf_now_us64(void)
{
    uint32_t in_tick;
    uint32_t tick = read_tick(&in_tick);

    return (uint64_t)tick * 1000u + in_tick / cyc_per_us;
}

uint32_t perf_hal_cpu_hz(void)
{
    return SystemCoreClock;
}
