#include "uart_hal.h"
#include "board.h"
#include "fmt.h"

static uint32_t rx_bytes;
static uint32_t ore_events;

/* Рядок, що збирається; місце під "\r\n" лишається завжди */
static char line[UART_HAL_LINE_MAX];
static size_t line_len;
#define LINE_TEXT_MAX   (UART_HAL_LINE_MAX - 2u)

/* Право на UART і буфер рядка (у rtos - м'ютекс, Tasks/sorter_tasks.c); NULL - один виконавець */
static void (*lock_fn)(void);
static void (*unlock_fn)(void);

void uart_hal_set_lock(void (*lock)(void), void (*unlock)(void))
{
    lock_fn = lock;
    unlock_fn = unlock;
}

void uart_hal_lock(void)
{
    if (lock_fn != NULL)
        lock_fn();
}

void uart_hal_unlock(void)
{
    if (unlock_fn != NULL)
        unlock_fn();
}

void uart_hal_rx_irq_enable(void)
{
    __HAL_UART_ENABLE_IT(BOARD_UART, UART_IT_RXNE);
}

int uart_hal_getc(void)
{
    UART_HandleTypeDef *h = BOARD_UART;

    /* Символ, що прийшов, поки попередній ще лежав у RDR, втрачено, але
     * без скидання ORE USART більше не виставить RXNE. */
    if (__HAL_UART_GET_FLAG(h, UART_FLAG_ORE)) {
        __HAL_UART_CLEAR_OREFLAG(h);
        ore_events++;
    }
    if (!__HAL_UART_GET_FLAG(h, UART_FLAG_RXNE))
        return -1;
    rx_bytes++;
    return (int)(h->Instance->RDR & 0xFFu);   /* читання RDR скидає RXNE */
}

void uart_hal_counters(uint32_t *rx, uint32_t *ore)
{
    *rx = rx_bytes;
    *ore = ore_events;
}

void uart_hal_counters_reset(void)
{
    rx_bytes = 0;
    ore_events = 0;
}

void uart_hal_write(const char *s, size_t n)
{
    uart_hal_lock();
    HAL_UART_Transmit(BOARD_UART, (const uint8_t *)s, (uint16_t)n, HAL_MAX_DELAY);
    uart_hal_unlock();
}

/* Перший шматок рядка бере право на UART, uart_hal_line_end його віддає */
static void line_grow(size_t n)
{
    line_len += n;
    if (line_len > LINE_TEXT_MAX)
        line_len = LINE_TEXT_MAX;
}

void uart_hal_line_vadd(const char *fmt, va_list ap)
{
    if (line_len == 0u)
        uart_hal_lock();
    /* fmt_vbuf пише не більше cap - 1 символів і '\0' на місці "\r" */
    line_grow(fmt_vbuf(&line[line_len], LINE_TEXT_MAX + 1u - line_len, fmt, ap));
}

void uart_hal_line_aadd(const char *fmt, const fmt_arg_t *args, unsigned n)
{
    if (line_len == 0u)
        uart_hal_lock();
    line_grow(fmt_abuf(&line[line_len], LINE_TEXT_MAX + 1u - line_len, fmt, args, n));
}

void uart_hal_line_add(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    uart_hal_line_vadd(fmt, ap);
    va_end(ap);
}

size_t uart_hal_line_len(void)
{
    return line_len;
}

void uart_hal_line_pad(size_t len)
{
    if (len > LINE_TEXT_MAX)
        len = LINE_TEXT_MAX;
    while (line_len < len)
        line[line_len++] = ' ';
}

void uart_hal_line_end(void)
{
    if (line_len == 0u)
        uart_hal_lock();
    line[line_len++] = '\r';
    line[line_len++] = '\n';
    uart_hal_write(line, line_len);
    line_len = 0;
    uart_hal_unlock();
}

void uart_hal_printf(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    uart_hal_line_vadd(fmt, ap);
    va_end(ap);
    uart_hal_line_end();
}
