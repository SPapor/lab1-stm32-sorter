#include "cli.h"
#include <stddef.h>
#include <string.h>

#define CLI_MAX_WORDS 3u   /* найдовша команда: cap servo <кут> */

/* Команда-слово → ідентифікатор */
typedef struct {
    const char *name;
    cli_cmd_id_t id;
} cmd_entry_t;

#ifdef LAB2_CAP
/* «cap <name>» без аргументу; servo з аргументом розбирається окремо */
static const cmd_entry_t cap_table[] = {
    { "whoami", CLI_CAP_WHOAMI },
    { "accel",  CLI_CAP_ACCEL },
    { "wr2",    CLI_CAP_WR2 },
    { "nack",   CLI_CAP_NACK },
    { "lcd",    CLI_CAP_LCD },
    { "adc",    CLI_CAP_ADC },
    { "uart",   CLI_CAP_UART },
};
#endif

void cli_init(cli_t *c)
{
    c->len = 0;
    c->overflow = 0;
}

/* Ріже рядок на слова за пробілами (на місці, пробіли → '\0').
 * Повертає кількість слів або max + 1, якщо слів більше, ніж max. */
static unsigned split(char *s, char *w[], unsigned max)
{
    unsigned n = 0;

    for (;;) {
        while (*s == ' ')
            s++;
        if (*s == '\0')
            return n;
        if (n == max)
            return max + 1u;
        w[n++] = s;
        while (*s != ' ' && *s != '\0')
            s++;
        if (*s == ' ')
            *s++ = '\0';
    }
}

#if PERF || defined(LAB2_CAP)
/* Десяткове число без знака 0..max; 0 — успіх, -1 — не число або поза межами. */
static int parse_uint(const char *s, int32_t max, int32_t *out)
{
    int32_t v = 0;

    if (*s == '\0')
        return -1;
    for (; *s != '\0'; s++) {
        if (*s < '0' || *s > '9')
            return -1;
        v = v * 10 + (*s - '0');
        if (v > max)
            return -1;   /* виходимо одразу, тож переповнення int32_t неможливе */
    }
    *out = v;
    return 0;
}
#endif

/* «<name> on|off»: arg = 1 / 0; інший аргумент — CLI_CMD_BAD. */
static cli_cmd_id_t parse_on_off(const char *word, cli_cmd_id_t id, int32_t *arg)
{
    if (strcmp(word, "on") == 0) {
        *arg = 1;
        return id;
    }
    if (strcmp(word, "off") == 0)
        return id;
    return CLI_CMD_BAD;
}

static cli_cmd_id_t parse_words(char *w[], unsigned n, cli_cmd_t *out)
{
    static const cmd_entry_t single[] = {
        { "help",   CLI_HELP },
        { "status", CLI_STATUS },
        { "reset",  CLI_RESET },
#if PERF
        { "stats",  CLI_STATS },
        { "load",   CLI_LOAD },
        { "stack",  CLI_STACK },
        { "heap",   CLI_HEAP },
#endif
    };
    int32_t *arg = &out->arg;

    if (n == 1u)
        for (size_t i = 0; i < sizeof single / sizeof single[0]; i++)
            if (strcmp(w[0], single[i].name) == 0)
                return single[i].id;

    if (n == 2u && strcmp(w[0], "log") == 0)
        return parse_on_off(w[1], CLI_LOG, arg);

#if PERF
    if (n == 2u && strcmp(w[0], "stats") == 0)
        return strcmp(w[1], "reset") == 0 ? CLI_STATS_RESET : CLI_CMD_BAD;

    /* Назву задачі чи ручки лише копіюємо: які вони бувають і які межі
     * значень, знає main.c */
    if ((n == 2u && strcmp(w[0], "hist") == 0) || (n == 3u && strcmp(w[0], "set") == 0)) {
        size_t len = strlen(w[1]);

        if (len >= CLI_NAME_LEN)
            return CLI_CMD_BAD;
        if (n == 3u && parse_uint(w[2], CLI_SET_MAX, arg) != 0)
            return CLI_CMD_BAD;
        memcpy(out->name, w[1], len + 1u);
        return n == 2u ? CLI_HIST : CLI_SET;
    }
#endif

#ifdef LAB2_CAP
    if (n == 2u && strcmp(w[0], "stream") == 0)
        return parse_on_off(w[1], CLI_STREAM, arg);

    if (n >= 2u && strcmp(w[0], "cap") == 0) {
        if (strcmp(w[1], "servo") == 0)
            return (n == 3u && parse_uint(w[2], CLI_SERVO_MAX_DEG, arg) == 0)
                   ? CLI_CAP_SERVO : CLI_CMD_BAD;
        if (n == 2u)
            for (size_t i = 0; i < sizeof cap_table / sizeof cap_table[0]; i++)
                if (strcmp(w[1], cap_table[i].name) == 0)
                    return cap_table[i].id;
    }
#endif
    return CLI_CMD_BAD;
}

int cli_feed(cli_t *c, char ch, cli_cmd_t *out)
{
    if (ch == '\r' || ch == '\n') {
        uint8_t was_overflow = c->overflow;
        char *w[CLI_MAX_WORDS];

        c->buf[c->len] = '\0';
        c->len = 0;
        c->overflow = 0;
        if (was_overflow)
            return 0;   /* CLI_CMD_BAD уже повернуто в момент переповнення */

        unsigned n = split(c->buf, w, CLI_MAX_WORDS);
        if (n == 0u)
            return 0;
        out->arg = 0;
        out->name[0] = '\0';
        out->id = (n > CLI_MAX_WORDS) ? CLI_CMD_BAD : parse_words(w, n, out);
        if (out->id == CLI_CMD_BAD) {
            out->arg = 0;
            out->name[0] = '\0';
        }
        return 1;
    }

    if (c->overflow)
        return 0;

    if (ch == 0x08 || ch == 0x7F) {
        if (c->len > 0u)
            c->len--;
        return 0;
    }

    if (c->len >= CLI_BUF_LEN - 1u) {   /* останній байт — під '\0' */
        c->overflow = 1;
        out->id = CLI_CMD_BAD;
        out->arg = 0;
        out->name[0] = '\0';
        return 1;
    }
    c->buf[c->len++] = ch;
    return 0;
}
