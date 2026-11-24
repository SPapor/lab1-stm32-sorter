#include "qpc.h"
#include "rtos_hal.h"
#ifdef TEST_QSTRESS
#include "qstress.h"
#endif

static StaticTask_t idle_tcb;
static StackType_t idle_stk[configMINIMAL_STACK_SIZE];

void vApplicationTickHook(void)
{
    BaseType_t woken = pdFALSE;

#ifdef TEST_QSTRESS
    qstress_tick_hook();
#endif
    QTIMEEVT_TICK_FROM_ISR(0U, &woken, NULL);
    portYIELD_FROM_ISR(woken);
}

void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stk, uint32_t *words)
{
    *tcb = &idle_tcb;
    *stk = idle_stk;
    *words = configMINIMAL_STACK_SIZE;
}

Q_NORETURN Q_onError(char const * const module, int_t const id)
{
#ifdef TEST_QSTRESS
    qstress_on_error();
#endif
    rtos_hal_fatal_qp(module, (int32_t)id);
}

void QF_onCleanup(void)
{
}
