#include "uart_hal.h"
#include "board.h"

static uint32_t rx_bytes;
static uint32_t ore_events;

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
