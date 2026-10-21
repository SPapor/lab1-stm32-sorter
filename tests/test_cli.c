/* Тести розбору команд UART (firmware/App/cli.c). */

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

static void test_unknown_and_case(void)
{
    assert_line("foo\r", CLI_CMD_BAD, 0);
    assert_line("HELP\r", CLI_CMD_BAD, 0);
    assert_line("help me\r", CLI_CMD_BAD, 0);
}

static void test_extra_spaces(void)
{
    assert_line("   cap   servo   45   \r", CLI_CAP_SERVO, 45);
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
    TEST_ASSERT_EQUAL_INT(2, feed_str("cap lcd\r\ncap adc\r\n"));
    TEST_ASSERT_EQUAL_INT(CLI_CAP_ADC, last.id);
    TEST_ASSERT_EQUAL_INT(0, feed_str("\r\n\r\n"));
}

static void test_backspace(void)
{
    assert_line("helx\bp\r", CLI_HELP, 0);
    assert_line("cap servo 9\x7F" "18\r", CLI_CAP_SERVO, 18);
}

static void test_backspace_on_empty_line(void)
{
    assert_line("\b\b\x7Fhelp\r", CLI_HELP, 0);
}

static void test_arg_cleared_between_commands(void)
{
    assert_line("cap servo 120\r", CLI_CAP_SERVO, 120);
    assert_line("help\r", CLI_HELP, 0);
    assert_line("stream on\r", CLI_STREAM, 1);
    assert_line("cap servo 999\r", CLI_CMD_BAD, 0);
}

/* 31 символ — максимум (CLI_BUF_LEN − 1 під '\0'). */
static void test_longest_line_fits(void)
{
    char line[CLI_BUF_LEN + 1];

    memset(line, ' ', CLI_BUF_LEN - 1u);
    memcpy(line, "cap servo 90", 12);
    line[CLI_BUF_LEN - 1u] = '\r';
    line[CLI_BUF_LEN] = '\0';

    assert_line(line, CLI_CAP_SERVO, 90);
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
    RUN_TEST(test_stream_on_off);
    RUN_TEST(test_stream_bad_argument);
    RUN_TEST(test_cap_commands);
    RUN_TEST(test_cap_bad);
    RUN_TEST(test_cap_servo_range);
    RUN_TEST(test_cap_servo_bad_argument);
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
