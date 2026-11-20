/* Діагностика ядра (методичка 4.11, 7.2 пп. 2 і 5): configASSERT, переповнення
 * стека задачі, вичерпання heap ОС, HardFault. Усі чотири зупиняють програму: переривання
 * вимкнено, LD4 блимає. Рядок FATAL іде прямим записом у USART2 з опитуванням TXE:
 * printf у цей момент небезпечний (стек задачі вже зіпсований або купа порожня),
 * а переривань, від яких залежить HAL, уже немає.
 * Переривання вимикаються не лише PRIMASK: Wokwi його не враховує і далі
 * викликає обробники (docs/journal.md), тож тік ядра перемикав би задачі й після
 * зупинки. Тому ще й NVIC і TICKINT SysTick. */

#include "rtos_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

#define ASSERT_MAGIC  0xA55E2710u
#define RAM_START     0x20000000u     /* SRAM 12 КБ, скрипт лінкера */
#define RAM_END       0x20003000u
#define BLINK_HZ      4u              /* LD4 при зупинці: 2 спалахи на секунду */

/* Останній configASSERT. Секції .noinit у скрипті лінкера CubeMX немає, і ld
 * ставить її як «сироту» з NOBITS одразу після .bss (перед ._user_heap_stack, тож
 * купа newlib починається після неї). Startup обнуляє лише [_sbss, _ebss), тож
 * запис переживає скидання кнопкою; check відрізняє його від сміття в RAM після
 * ввімкнення живлення. Де вона лягла — firmware.map */
static struct {
    uint32_t line;
    uint32_t pc;
    uint32_t check;
} assert_rec __attribute__((section(".noinit")));

static void fatal_putc(char c)
{
    USART_TypeDef *u = BOARD_UART->Instance;

    while ((u->ISR & USART_ISR_TXE_TXFNF) == 0u) {
    }
    u->TDR = (uint8_t)c;
}

void rtos_hal_raw_puts(const char *s)
{
    while (*s != '\0')
        fatal_putc(*s++);
}

void rtos_hal_raw_putu(uint32_t v, unsigned base)
{
    char buf[10];
    unsigned n = 0;

    do {
        uint32_t d = v % base;
        buf[n++] = (char)(d < 10u ? '0' + d : 'A' + d - 10u);
        v /= base;
    } while (v != 0u);
    while (n > 0u)
        fatal_putc(buf[--n]);
}

/* Жодних переривань і перемикань задач: PRIMASK, усі лінії NVIC, переривання
 * SysTick (сам лічильник іде — від нього блимає LD4) і відкладений PendSV */
static void fatal_stop(void)
{
    __disable_irq();
    NVIC->ICER[0] = 0xFFFFFFFFu;
    SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk;
    SCB->ICSR = SCB_ICSR_PENDSVCLR_Msk | SCB_ICSR_PENDSTCLR_Msk;
}

/* Початок рядка FR-10: "<мс> FATAL " */
static void fatal_begin(void)
{
    rtos_hal_raw_putu(HAL_GetTick(), 10u);
    rtos_hal_raw_puts(" FATAL ");
}

/* Зупинка: переривань немає, тож час — з прапорця COUNTFLAG SysTick (той іде й далі) */
static void fatal_halt(void) __attribute__((noreturn));
static void fatal_halt(void)
{
    uint32_t ticks = 0;

    for (;;) {
        if (SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) {
            if (++ticks >= configTICK_RATE_HZ / BLINK_HZ) {
                ticks = 0;
                BOARD_LED_MARK_PORT->ODR ^= BOARD_LED_MARK_PIN;
            }
        }
    }
}

/* configASSERT (FreeRTOSConfig.h). Адреса повернення — місце виклику в коді ядра:
 * arm-none-eabi-addr2line -e build/firmware.elf <pc> дає файл і функцію */
void rtos_hal_assert_failed(uint32_t line)
{
    uint32_t pc = (uint32_t)__builtin_return_address(0);

    fatal_stop();
    assert_rec.line = line;
    assert_rec.pc = pc;
    assert_rec.check = line ^ pc ^ ASSERT_MAGIC;
    fatal_begin();
    rtos_hal_raw_puts("assert line=");
    rtos_hal_raw_putu(line, 10u);
    rtos_hal_raw_puts(" pc=0x");
    rtos_hal_raw_putu(pc, 16u);
    rtos_hal_raw_puts("\r\n");
    fatal_halt();
}

int rtos_hal_assert_take(uint32_t *line, uint32_t *pc)
{
    int valid = (assert_rec.check == (assert_rec.line ^ assert_rec.pc ^ ASSERT_MAGIC));

    if (valid) {
        *line = assert_rec.line;
        *pc = assert_rec.pc;
    }
    assert_rec.check = ~ASSERT_MAGIC;
    return valid;
}

/* configCHECK_FOR_STACK_OVERFLOW 2: ядро викликає з vTaskSwitchContext (у PendSV),
 * коли вказівник стека задачі вийшов за межу або шаблон на дні стека зіпсовано */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    fatal_stop();
    fatal_begin();
    rtos_hal_raw_puts("stack overflow in ");
    rtos_hal_raw_puts(pcTaskName);
    rtos_hal_raw_puts("\r\n");
    fatal_halt();
}

void rtos_hal_hardfault(uint32_t exc_return)
{
    uint32_t pc = 0;
    uint32_t psp = __get_PSP();

    fatal_stop();
    /* EXC_RETURN - до будь-якого читання стека: зіпсований PSP дав би другий
     * HardFault усередині цього (lockup), і рядка не було б зовсім */
    fatal_begin();
    rtos_hal_raw_puts("hardfault exc=0x");
    rtos_hal_raw_putu(exc_return, 16u);
    /* Біт 2 EXC_RETURN: кадр винятку на PSP (задача). Кадр на MSP уже зсунутий
     * кадром самого обробника, тож там адреси не шукаємо */
    if ((exc_return & 0x4u) && psp >= RAM_START && psp <= RAM_END - 32u)
        pc = ((const uint32_t *)psp)[6];
    rtos_hal_raw_puts(" pc=0x");
    rtos_hal_raw_putu(pc, 16u);
    rtos_hal_raw_puts("\r\n");
    fatal_halt();
}

void rtos_hal_fatal_create(const char *name)
{
    fatal_stop();
    fatal_begin();
    rtos_hal_raw_puts("create ");
    rtos_hal_raw_puts(name);
    rtos_hal_raw_puts("\r\n");
    fatal_halt();
}

/* configUSE_MALLOC_FAILED_HOOK: pvPortMalloc повернув би NULL */
void vApplicationMallocFailedHook(void)
{
    fatal_stop();
    fatal_begin();
    rtos_hal_raw_puts("malloc\r\n");
    fatal_halt();
}
