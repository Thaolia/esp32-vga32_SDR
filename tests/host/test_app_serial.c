#include <string.h>
#include "app_serial.h"
#include "test_util.h"

/* Injecte une chaîne ; renvoie le nombre de commandes décodées, la dernière dans *last */
static int feed(app_serial_t *d, const char *s, app_cmd_t *last)
{
    int n = 0;
    for (size_t i = 0; i < strlen(s); i++) {
        app_cmd_t cmd;
        if (app_serial_feed(d, (unsigned char)s[i], &cmd)) {
            *last = cmd;
            n++;
        }
    }
    return n;
}

static int expect(const char *s, app_cmd_t want)
{
    app_serial_t d;
    app_cmd_t got = APP_CMD_RANGE_UP;
    app_serial_init(&d);
    CHECK(feed(&d, s, &got) == 1);
    CHECK(got == want);
    return 0;
}

static int test_arrows(void)
{
    CHECK(!expect("\x1b[C", APP_CMD_FREQ_UP_1));
    CHECK(!expect("\x1b[D", APP_CMD_FREQ_DOWN_1));
    CHECK(!expect("\x1b[A", APP_CMD_REF_UP));
    CHECK(!expect("\x1b[B", APP_CMD_REF_DOWN));
    return 0;
}

static int test_shift_arrows(void)
{
    CHECK(!expect("\x1b[1;2C", APP_CMD_FREQ_UP_10));
    CHECK(!expect("\x1b[1;2D", APP_CMD_FREQ_DOWN_10));
    return 0;
}

static int test_page_keys(void)
{
    CHECK(!expect("\x1b[5~", APP_CMD_GAIN_UP));
    CHECK(!expect("\x1b[6~", APP_CMD_GAIN_DOWN));
    return 0;
}

static int test_letters(void)
{
    CHECK(!expect("r", APP_CMD_RATE_CYCLE));
    CHECK(!expect("R", APP_CMD_RATE_CYCLE));
    CHECK(!expect("g", APP_CMD_AGC_TOGGLE));
    CHECK(!expect("G", APP_CMD_AGC_TOGGLE));
    CHECK(!expect("+", APP_CMD_RANGE_DOWN));
    CHECK(!expect("=", APP_CMD_RANGE_DOWN));
    CHECK(!expect("-", APP_CMD_RANGE_UP));
    CHECK(!expect("d", APP_CMD_FREQ_UP_1));
    CHECK(!expect("a", APP_CMD_FREQ_DOWN_1));
    CHECK(!expect("D", APP_CMD_FREQ_UP_10));
    CHECK(!expect("A", APP_CMD_FREQ_DOWN_10));
    CHECK(!expect("w", APP_CMD_REF_UP));
    CHECK(!expect("s", APP_CMD_REF_DOWN));
    CHECK(!expect("]", APP_CMD_GAIN_UP));
    CHECK(!expect("[", APP_CMD_GAIN_DOWN));
    return 0;
}

static int test_ignored(void)
{
    app_serial_t d;
    app_cmd_t got;
    app_serial_init(&d);
    CHECK(feed(&d, "x\r\n \x1b[H\x1b[3~\x1b[1;5C", &got) == 0); /* touches inconnues */
    CHECK(feed(&d, "\x1b[123456C", &got) == 0);                 /* paramètres trop longs */
    CHECK(feed(&d, "r", &got) == 1 && got == APP_CMD_RATE_CYCLE); /* le décodeur s'est resynchronisé */
    return 0;
}

static int test_esc_then_letter(void)
{
    /* ESC isolé (touche Échap) suivi d'une lettre : la lettre n'est pas perdue */
    CHECK(!expect("\x1br", APP_CMD_RATE_CYCLE));
    return 0;
}

static int test_ss3_cursor_keys(void)
{
    /* Mode « application cursor keys » (DECCKM) : ESC O x */
    CHECK(!expect("\x1bOC", APP_CMD_FREQ_UP_1));
    CHECK(!expect("\x1bOD", APP_CMD_FREQ_DOWN_1));
    CHECK(!expect("\x1bOA", APP_CMD_REF_UP));
    CHECK(!expect("\x1bOB", APP_CMD_REF_DOWN));
    app_serial_t d;
    app_cmd_t got;
    app_serial_init(&d);
    CHECK(feed(&d, "\x1bOR\x1bOP", &got) == 0); /* F3, F1 : ignorées, pas RATE_CYCLE */
    CHECK(feed(&d, "g", &got) == 1 && got == APP_CMD_AGC_TOGGLE);
    return 0;
}

static int test_esc_esc_key(void)
{
    CHECK(!expect("\x1b\x1b[C", APP_CMD_FREQ_UP_1));
    return 0;
}

static int test_sequence_of_commands(void)
{
    app_serial_t d;
    app_cmd_t got;
    app_serial_init(&d);
    CHECK(feed(&d, "\x1b[C\x1b[Cg", &got) == 3 && got == APP_CMD_AGC_TOGGLE);
    return 0;
}

int main(void)
{
    int failures = 0;
    RUN(test_arrows);
    RUN(test_shift_arrows);
    RUN(test_page_keys);
    RUN(test_letters);
    RUN(test_ignored);
    RUN(test_esc_then_letter);
    RUN(test_ss3_cursor_keys);
    RUN(test_esc_esc_key);
    RUN(test_sequence_of_commands);
    return failures ? 1 : 0;
}
