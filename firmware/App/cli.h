#ifndef CLI_H
#define CLI_H

/* Розбір рядкових команд з UART. Лише логіка: нічого не друкує і не знає про
 * HAL, тому збирається й тестується на хості. */

#include <stdint.h>

#define CLI_BUF_LEN        32u    /* буфер рядка разом з '\0': до 31 символу */
#define CLI_SERVO_MAX_DEG  180    /* межа аргументу «cap servo» */

typedef enum {
    CLI_HELP = 0,     /* help */
    CLI_STREAM,       /* stream on|off; arg = 1 / 0 */
    CLI_CAP_WHOAMI,   /* cap whoami */
    CLI_CAP_ACCEL,    /* cap accel */
    CLI_CAP_WR2,      /* cap wr2 */
    CLI_CAP_NACK,     /* cap nack */
    CLI_CAP_LCD,      /* cap lcd */
    CLI_CAP_SERVO,    /* cap servo <0..180>; arg = кут */
    CLI_CAP_ADC,      /* cap adc */
    CLI_CAP_UART,     /* cap uart */
    CLI_CMD_BAD       /* невідома команда, хибний аргумент або переповнення */
} cli_cmd_id_t;

typedef struct {
    cli_cmd_id_t id;
    int32_t arg;      /* 0, якщо команда без аргументу */
} cli_cmd_t;

typedef struct {
    char buf[CLI_BUF_LEN];
    uint8_t len;
    uint8_t overflow;  /* рядок задовгий: решту до кінця рядка відкидаємо */
} cli_t;

void cli_init(cli_t *c);

/* Додає символ до рядка. На '\r' або '\n' розбирає рядок; повертає 1 і
 * заповнює out, якщо є команда (порожній рядок — не команда, повертає 0).
 * Backspace (0x08, 0x7F) стирає останній символ. Переповнення буфера одразу
 * повертає CLI_CMD_BAD, решта символів до кінця рядка ігнорується. */
int cli_feed(cli_t *c, char ch, cli_cmd_t *out);

#endif /* CLI_H */
