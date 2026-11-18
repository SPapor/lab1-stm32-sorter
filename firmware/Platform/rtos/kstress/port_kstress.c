/* Порт ARM_CM0 для збірки TEST_KSTRESS (docs/rtos-design.md, розділ 2): той самий
 * port.c без змін, плюс читання його лічильника вкладеності критичних секцій
 * (ulCriticalNesting - static у port.c). У цій збірці файл замінює port.c
 * (app.mk), у продукт не входить.
 *
 * Що рахується (на залізі всі три лічильники мали б бути 0):
 *   isr_in_crit      - переривання (SysTick, TIM14), що почалося, коли задача була
 *                      в критичній секції ядра (ulCriticalNesting != 0);
 *   pendsv_in_crit   - PendSV (vTaskSwitchContext) усередині критичної секції;
 *   switches_in_crit - з них ті, де ядро справді перемкнуло задачу. */
#include "port.c"
#include "kstress_port.h"

/* Значення до старту ядра (порт ставить 0 у xPortStartScheduler) */
#define NESTING_BEFORE_START    0xaaaaaaaaUL

static volatile uint32_t isr_in_crit;
static volatile uint32_t pendsv_in_crit;
static volatile uint32_t switches_in_crit;
static volatile uint32_t switches;
static void *volatile switched_out;

static int in_crit(void)
{
    uint32_t n = ulCriticalNesting;

    return n != 0u && n != NESTING_BEFORE_START;
}

void kstress_note_isr(void)
{
    if (in_crit())
        isr_in_crit++;
}

void kstress_switched_out(void *tcb)
{
    switched_out = tcb;
}

void kstress_switched_in(void *tcb)
{
    if (tcb != switched_out)
        switches++;
    if (in_crit()) {
        pendsv_in_crit++;
        if (tcb != switched_out)
            switches_in_crit++;
    }
}

void kstress_port_counters(kstress_port_counters_t *c)
{
    c->isr_in_crit = isr_in_crit;
    c->pendsv_in_crit = pendsv_in_crit;
    c->switches_in_crit = switches_in_crit;
    c->switches = switches;
}
