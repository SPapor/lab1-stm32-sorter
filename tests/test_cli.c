/* Тести розбору команд UART (firmware/App/cli.c). Файл збирається тричі, як
 * варіанти прошивки: test_cli — продукт (-DPERF=1, команди вимірювань),
 * test_cli_noperf — -DPERF=0, test_cli_lab2 — -DLAB2_CAP (команди stream, cap …;
 * з LAB2_CAP прошивка збирається без PERF). */

#include <string.h>
#include "unity.h"
#include "cli.h"

static cli_t c;
static cli_cmd_t last;

void setUp(void)
{
    cli_init(&c);
    memset(&last, 0xA5, sizeof last);
}

void tearDown(void)
{
}

/* Подає рядок посимвольно; повертає кількість команд, last — остання. */
static int feed(const char *s, size_t n)
{
    int cmds = 0;

    for (size_t i = 0; i < n; i++) {
        cli_cmd_t out;
        if (cli_feed(&c, s[i], &out)) {
            last = out;
            cmds++;
        }
    }
    return cmds;
}

static int feed_str(const char *s)
{
    return feed(s, strlen(s));
}

static void assert_line(const char *line, cli_cmd_id_t id, int32_t arg)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, feed_str(line), line);
    TEST_ASSERT_EQUAL_INT_MESSAGE(id, last.id, line);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(arg, last.arg, line);
}

static void test_help(void)
{
    assert_line("help\r", CLI_HELP, 0);
}

static void test_status_reset(void)
{
    assert_line("status\r", CLI_STATUS, 0);
    assert_line("reset\r", CLI_RESET, 0);
}

static void test_status_reset_take_no_argument(void)
{
    assert_line("status now\r", CLI_CMD_BAD, 0);
    assert_line("reset all\r", CLI_CMD_BAD, 0);
}

static void test_log_on_off(void)
{
    assert_line("log on\r", CLI_LOG, 1);
    assert_line("log off\r", CLI_LOG, 0);
}

#if PERF
static void test_perf_commands(void)
{
    assert_line("stats\r", CLI_STATS, 0);
    assert_line("stats reset\r", CLI_STATS_RESET, 0);
    assert_line("load\r", CLI_LOAD, 0);
    assert_line("stack\r", CLI_STACK, 0);
    assert_line("heap\r", CLI_HEAP, 0);
    TEST_ASSERT_EQUAL_STRING("", last.name);
}

static void test_perf_commands_bad(void)
{
    assert_line("stats all\r", CLI_CMD_BAD, 0);
    assert_line("stats reset now\r", CLI_CMD_BAD, 0);
    assert_line("load 1\r", CLI_CMD_BAD, 0);
    assert_line("stack now\r", CLI_CMD_BAD, 0);
    assert_line("heap 2\r", CLI_CMD_BAD, 0);
}

/* hist <задача>: назву лише копіюємо — які задачі мають гістограму, знає main.c */
static void test_hist_name(void)
{
    assert_line("hist gate\r", CLI_HIST, 0);
    TEST_ASSERT_EQUAL_STRING("gate", last.name);
    assert_line("  hist   lcd  \r", CLI_HIST, 0);
    TEST_ASSERT_EQUAL_STRING("lcd", last.name);
    assert_line("hist 12345678901\r", CLI_HIST, 0);         /* 11 символів — максимум */
    TEST_ASSERT_EQUAL_STRING("12345678901", last.name);
}

static void test_hist_bad(void)
{
    assert_line("hist\r", CLI_CMD_BAD, 0);
    TEST_ASSERT_EQUAL_STRING("", last.name);
    assert_line("hist gate imu\r", CLI_CMD_BAD, 0);
    assert_line("hist 123456789012\r", CLI_CMD_BAD, 0);     /* 12 — не вміщується з '\0' */
    TEST_ASSERT_EQUAL_STRING("", last.name);
}

/* set <ручка> <0..65535>: ручку й значення лише розбираємо, межі перевіряє main.c */
static void test_set(void)
{
    assert_line("set lcd_ms 100\r", CLI_SET, 100);
    TEST_ASSERT_EQUAL_STRING("lcd_ms", last.name);
    assert_line("  set   lcd_chars   0 \r", CLI_SET, 0);
    TEST_ASSERT_EQUAL_STRING("lcd_chars", last.name);
    assert_line("set imu_ms 65535\r", CLI_SET, CLI_SET_MAX);
    assert_line("set log 007\r", CLI_SET, 7);
    TEST_ASSERT_EQUAL_STRING("log", last.name);
}

static void test_set_bad(void)
{
    assert_line("set\r", CLI_CMD_BAD, 0);
    assert_line("set lcd_ms\r", CLI_CMD_BAD, 0);
    assert_line("set lcd_ms 65536\r", CLI_CMD_BAD, 0);
    assert_line("set lcd_ms -1\r", CLI_CMD_BAD, 0);
    assert_line("set lcd_ms 1e3\r", CLI_CMD_BAD, 0);
    assert_line("set lcd_ms 10 20\r", CLI_CMD_BAD, 0);
    assert_line("set 123456789012 1\r", CLI_CMD_BAD, 0);
    TEST_ASSERT_EQUAL_STRING("", last.name);
    assert_line("hist gate 5\r", CLI_CMD_BAD, 0);      /* у hist немає значення */
}

/* Назва від попередньої hist не «протікає» в наступні команди */
static void test_name_cleared_between_commands(void)
{
    assert_line("hist imu\r", CLI_HIST, 0);
    assert_line("status\r", CLI_STATUS, 0);
    TEST_ASSERT_EQUAL_STRING("", last.name);
}
#else
/* Без PERF команди вимірювань не розпізнаються */
static void test_perf_commands_rejected(void)
{
    assert_line("stats\r", CLI_CMD_BAD, 0);
    assert_line("stats reset\r", CLI_CMD_BAD, 0);
    assert_line("load\r", CLI_CMD_BAD, 0);
    assert_line("stack\r", CLI_CMD_BAD, 0);
    assert_line("heap\r", CLI_CMD_BAD, 0);
    assert_line("hist gate\r", CLI_CMD_BAD, 0);
    assert_line("set lcd_ms 100\r", CLI_CMD_BAD, 0);
}
#endif

static void test_log_bad_argument(void)
{
    assert_line("log\r", CLI_CMD_BAD, 0);
    assert_line("log 1\r", CLI_CMD_BAD, 0);
    assert_line("log ON\r", CLI_CMD_BAD, 0);
    assert_line("log on now\r", CLI_CMD_BAD, 0);
}

#ifdef LAB2_CAP
static void test_stream_on_off(void)
{
    assert_line("stream on\r", CLI_STREAM, 1);
    assert_line("stream off\r", CLI_STREAM, 0);
}

static void test_stream_bad_argument(void)
{
    assert_line("stream\r", CLI_CMD_BAD, 0);
    assert_line("stream 1\r", CLI_CMD_BAD, 0);
    assert_line("stream on now\r", CLI_CMD_BAD, 0);
}

static void test_cap_commands(void)
{
    static const struct { const char *line; cli_cmd_id_t id; } t[] = {
        { "cap whoami\r", CLI_CAP_WHOAMI },
        { "cap accel\r",  CLI_CAP_ACCEL },
        { "cap wr2\r",    CLI_CAP_WR2 },
        { "cap nack\r",   CLI_CAP_NACK },
        { "cap lcd\r",    CLI_CAP_LCD },
        { "cap adc\r",    CLI_CAP_ADC },
        { "cap uart\r",   CLI_CAP_UART },
    };

    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++)
        assert_line(t[i].line, t[i].id, 0);
}

static void test_cap_bad(void)
{
    assert_line("cap\r", CLI_CMD_BAD, 0);
    assert_line("cap foo\r", CLI_CMD_BAD, 0);
    assert_line("cap whoami now\r", CLI_CMD_BAD, 0);
}

static void test_cap_servo_range(void)
{
    assert_line("cap servo 0\r", CLI_CAP_SERVO, 0);
    assert_line("cap servo 90\r", CLI_CAP_SERVO, 90);
    assert_line("cap servo 180\r", CLI_CAP_SERVO, CLI_SERVO_MAX_DEG);
    assert_line("cap servo 007\r", CLI_CAP_SERVO, 7);
}

static void test_cap_servo_bad_argument(void)
{
    assert_line("cap servo 200\r", CLI_CMD_BAD, 0);
    assert_line("cap servo 181\r", CLI_CMD_BAD, 0);
    assert_line("cap servo 99999999999\r", CLI_CMD_BAD, 0);
    assert_line("cap servo\r", CLI_CMD_BAD, 0);
    assert_line("cap servo -1\r", CLI_CMD_BAD, 0);
    assert_line("cap servo 9x\r", CLI_CMD_BAD, 0);
    assert_line("cap servo 90 1\r", CLI_CMD_BAD, 0);
}

#else
/* Без LAB2_CAP команди harness ЛР2 не розпізнаються */
static void test_lab2_commands_rejected(void)
{
    assert_line("stream on\r", CLI_CMD_BAD, 0);
    assert_line("cap whoami\r", CLI_CMD_BAD, 0);
    assert_line("cap servo 90\r", CLI_CMD_BAD, 0);
}
#endif

static void test_unknown_and_case(void)
{
    assert_line("foo\r", CLI_CMD_BAD, 0);
    assert_line("HELP\r", CLI_CMD_BAD, 0);
    assert_line("help me\r", CLI_CMD_BAD, 0);
}

static void test_extra_spaces(void)
{
#ifdef LAB2_CAP
    assert_line("   cap   servo   45   \r", CLI_CAP_SERVO, 45);
#endif
    assert_line("   log   on   \r", CLI_LOG, 1);
    assert_line(" help \n", CLI_HELP, 0);
}

static void test_empty_lines_give_no_command(void)
{
    TEST_ASSERT_EQUAL_INT(0, feed_str("\r"));
    TEST_ASSERT_EQUAL_INT(0, feed_str("\n"));
    TEST_ASSERT_EQUAL_INT(0, feed_str("    \r"));
}

static void test_crlf_gives_one_command(void)
{
    TEST_ASSERT_EQUAL_INT(1, feed_str("help\r\n"));
    TEST_ASSERT_EQUAL_INT(CLI_HELP, last.id);
    TEST_ASSERT_EQUAL_INT(2, feed_str("status\r\nreset\r\n"));
    TEST_ASSERT_EQUAL_INT(CLI_RESET, last.id);
#ifdef LAB2_CAP
    TEST_ASSERT_EQUAL_INT(2, feed_str("cap lcd\r\ncap adc\r\n"));
    TEST_ASSERT_EQUAL_INT(CLI_CAP_ADC, last.id);
#endif
    TEST_ASSERT_EQUAL_INT(0, feed_str("\r\n\r\n"));
}

static void test_backspace(void)
{
    assert_line("helx\bp\r", CLI_HELP, 0);
    assert_line("log of\x7F\x7F" "on\r", CLI_LOG, 1);
#ifdef LAB2_CAP
    assert_line("cap servo 9\x7F" "18\r", CLI_CAP_SERVO, 18);
#endif
}

static void test_backspace_on_empty_line(void)
{
    assert_line("\b\b\x7Fhelp\r", CLI_HELP, 0);
}

static void test_arg_cleared_between_commands(void)
{
    assert_line("log on\r", CLI_LOG, 1);
    assert_line("status\r", CLI_STATUS, 0);
    assert_line("log on\r", CLI_LOG, 1);
    assert_line("log maybe\r", CLI_CMD_BAD, 0);
#ifdef LAB2_CAP
    assert_line("cap servo 120\r", CLI_CAP_SERVO, 120);
    assert_line("help\r", CLI_HELP, 0);
    assert_line("stream on\r", CLI_STREAM, 1);
    assert_line("cap servo 999\r", CLI_CMD_BAD, 0);
#endif
}

/* 31 символ — максимум (CLI_BUF_LEN − 1 під '\0'). */
static void test_longest_line_fits(void)
{
    char line[CLI_BUF_LEN + 1];

    memset(line, ' ', CLI_BUF_LEN - 1u);
    memcpy(line, "log on", 6);
    line[CLI_BUF_LEN - 1u] = '\r';
    line[CLI_BUF_LEN] = '\0';
    assert_line(line, CLI_LOG, 1);

#ifdef LAB2_CAP
    memcpy(line, "cap servo 90", 12);
    assert_line(line, CLI_CAP_SERVO, 90);
#endif
}

/* 32-й символ одразу дає CLI_CMD_BAD; решта рядка, включно з backspace, відкидається,
 * кінець рядка вже не дає команди, наступний рядок розбирається нормально. */
static void test_overflow(void)
{
    char line[CLI_BUF_LEN];
    cli_cmd_t out;

    memset(line, 'a', sizeof line);
    TEST_ASSERT_EQUAL_INT(0, feed(line, CLI_BUF_LEN - 1u));
    TEST_ASSERT_EQUAL_INT(1, cli_feed(&c, 'a', &out));
    TEST_ASSERT_EQUAL_INT(CLI_CMD_BAD, out.id);
    TEST_ASSERT_EQUAL_INT32(0, out.arg);

    TEST_ASSERT_EQUAL_INT(0, feed_str("help\b\b\b\b"));
    TEST_ASSERT_EQUAL_INT(0, feed_str("\r\n"));
    assert_line("help\r", CLI_HELP, 0);
}

static void test_init_drops_partial_line(void)
{
    TEST_ASSERT_EQUAL_INT(0, feed_str("hel"));
    cli_init(&c);
    assert_line("help\r", CLI_HELP, 0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_help);
    RUN_TEST(test_status_reset);
    RUN_TEST(test_status_reset_take_no_argument);
    RUN_TEST(test_log_on_off);
#if PERF
    RUN_TEST(test_perf_commands);
    RUN_TEST(test_perf_commands_bad);
    RUN_TEST(test_hist_name);
    RUN_TEST(test_hist_bad);
    RUN_TEST(test_set);
    RUN_TEST(test_set_bad);
    RUN_TEST(test_name_cleared_between_commands);
#else
    RUN_TEST(test_perf_commands_rejected);
#endif
    RUN_TEST(test_log_bad_argument);
#ifdef LAB2_CAP
    RUN_TEST(test_stream_on_off);
    RUN_TEST(test_stream_bad_argument);
    RUN_TEST(test_cap_commands);
    RUN_TEST(test_cap_bad);
    RUN_TEST(test_cap_servo_range);
    RUN_TEST(test_cap_servo_bad_argument);
#else
    RUN_TEST(test_lab2_commands_rejected);
#endif
    RUN_TEST(test_unknown_and_case);
    RUN_TEST(test_extra_spaces);
    RUN_TEST(test_empty_lines_give_no_command);
    RUN_TEST(test_crlf_gives_one_command);
    RUN_TEST(test_backspace);
    RUN_TEST(test_backspace_on_empty_line);
    RUN_TEST(test_arg_cleared_between_commands);
    RUN_TEST(test_longest_line_fits);
    RUN_TEST(test_overflow);
    RUN_TEST(test_init_drops_partial_line);
    return UNITY_END();
}
