/* Порт ARM_CM0 для збірки TEST_KSTRESS (docs/rtos-design.md, розділ 2): той самий
 * port.c без змін, плюс лічильники навколо його критичних секцій. У цій збірці
 * файл замінює port.c (app.mk), у продукт не входить. ulCriticalNesting - static
 * у port.c, тому port.c включено сюди, а vPortEnterCritical перейменовано: ядро
 * (tasks.c, queue.c) кличе обгортку нижче, вона - функцію порту.
 *
 * Два види критичних секцій ядра:
 *   задачі - taskENTER_CRITICAL (vPortEnterCritical, лічильник ulCriticalNesting);
 *   ISR    - taskENTER_CRITICAL_FROM_ISR (ulSetInterruptMask, обгортка нижче).
 * На залізі обидві - cpsid i, і всередині них не виконуються ні переривання, ні
 * PendSV. У Wokwi PRIMASK не маскує (журнал, проблема 35), тож рахуємо, скільки
 * разів усередині них:
 *   PendSV (vTaskSwitchContext) і з них справжнє перемикання задачі;
 *   тік SysTick (xTaskIncrementTick міняє списки затриманих і готових задач);
 *   переривання TIM14 стрес-тесту (FromISR-виклики черги і сповіщення).
 * До старту ядра (ulCriticalNesting = 0xaaaaaaaa + k) нічого не рахується. */
#define vPortEnterCritical  port_vPortEnterCritical
#define vPortExitCritical   port_vPortExitCritical
#define ulSetInterruptMask  port_ulSetInterruptMask
#define vClearInterruptMask port_vClearInterruptMask
#include "port.c"
#undef vPortEnterCritical
#undef vPortExitCritical
#undef ulSetInterruptMask
#undef vClearInterruptMask
#include "kstress_port.h"

/* Справжня вкладеність - кілька рівнів; усе зі старшим бітом - ще до старту */
#define NESTING_BEFORE_START_MIN    0x80000000UL

void vPortEnterCritical(void);
void vPortExitCritical(void);
uint32_t ulSetInterruptMask(void);
void vClearInterruptMask(uint32_t ulMask);

static volatile kstress_port_counters_t c;
static volatile uint32_t isr_mask_depth;
static void *volatile switched_out;

static int started(void)
{
    return ulCriticalNesting < NESTING_BEFORE_START_MIN;
}

static int in_task_crit(void)
{
    return ulCriticalNesting != 0u && started();
}

void vPortEnterCritical(void)
{
    port_vPortEnterCritical();
    if (started())
        c.crit_task_n++;
}

void vPortExitCritical(void)
{
    port_vPortExitCritical();
}

/* Функції порту: у SIM_CRIT=1 - з port.c вище, у SIM_CRIT=0 - з portasm.c
 * (port_kstress_asm.c) */
uint32_t ulSetInterruptMask(void)
{
    uint32_t mask = port_ulSetInterruptMask();

    kstress_isr_mask_enter();
    return mask;
}

void vClearInterruptMask(uint32_t ulMask)
{
    kstress_isr_mask_exit();
    port_vClearInterruptMask(ulMask);
}

uint32_t kstress_crit_nesting(void)
{
    return ulCriticalNesting;
}

void kstress_isr_mask_enter(void)
{
    isr_mask_depth++;
    if (started())
        c.crit_isr_n++;
}

void kstress_isr_mask_exit(void)
{
    isr_mask_depth--;
}

void kstress_note_isr(kstress_src_t src)
{
    if (!started())
        return;
    if (src == KSTRESS_SRC_TICK) {
        if (in_task_crit())
            c.tick_in_task_crit++;
        if (isr_mask_depth != 0u)
            c.tick_in_isr_crit++;
    } else {
        if (in_task_crit())
            c.tim14_in_task_crit++;
        if (isr_mask_depth != 0u)
            c.tim14_in_isr_crit++;
    }
}

void kstress_switched_out(void *tcb)
{
    switched_out = tcb;
}

void kstress_switched_in(void *tcb)
{
    int moved = (tcb != switched_out);

    if (!started())
        return;
    if (moved)
        c.switches++;
    if (in_task_crit()) {
        c.pendsv_in_task_crit++;
        if (moved)
            c.switches_in_task_crit++;
    }
    if (isr_mask_depth != 0u) {
        c.pendsv_in_isr_crit++;
        if (moved)
            c.switches_in_isr_crit++;
    }
}

void kstress_port_counters(kstress_port_counters_t *out)
{
    *out = c;
}
