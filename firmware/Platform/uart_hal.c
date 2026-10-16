#include "uart_hal.h"
#include "board.h"

int uart_hal_getc(void)
{
    UART_HandleTypeDef *h = BOARD_UART;

    /* Символ, що прийшов, поки попередній ще лежав у RDR, втрачено, але
     * без скидання ORE USART більше не виставить RXNE. */
    if (__HAL_UART_GET_FLAG(h, UART_FLAG_ORE))
        __HAL_UART_CLEAR_OREFLAG(h);
    if (!__HAL_UART_GET_FLAG(h, UART_FLAG_RXNE))
        return -1;
    return (int)(h->Instance->RDR & 0xFFu);   /* читання RDR скидає RXNE */
}
