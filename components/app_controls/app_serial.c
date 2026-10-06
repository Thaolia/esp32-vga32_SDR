#include "app_serial.h"
#include <string.h>

enum { ST_IDLE, ST_ESC, ST_CSI };

void app_serial_init(app_serial_t *d)
{
    memset(d, 0, sizeof(*d));
}

static bool decode_char(uint8_t c, app_cmd_t *cmd)
{
    switch (c) {
    case 'd': *cmd = APP_CMD_FREQ_UP_1; return true;
    case 'a': *cmd = APP_CMD_FREQ_DOWN_1; return true;
    case 'D': *cmd = APP_CMD_FREQ_UP_10; return true;
    case 'A': *cmd = APP_CMD_FREQ_DOWN_10; return true;
    case 'w': *cmd = APP_CMD_REF_UP; return true;
    case 's': *cmd = APP_CMD_REF_DOWN; return true;
    case ']': *cmd = APP_CMD_GAIN_UP; return true;
    case '[': *cmd = APP_CMD_GAIN_DOWN; return true;
    case 'r': case 'R': *cmd = APP_CMD_RATE_CYCLE; return true;
    case 'g': case 'G': *cmd = APP_CMD_AGC_TOGGLE; return true;
    case '+': case '=': *cmd = APP_CMD_RANGE_DOWN; return true;
    case '-': *cmd = APP_CMD_RANGE_UP; return true;
    default: return false;
    }
}

static bool params_are(const app_serial_t *d, const char *s)
{
    size_t n = strlen(s);
    return d->len == n && memcmp(d->params, s, n) == 0;
}

/* Séquences CSI xterm : flèches, Shift+flèches (1;2), PgUp/PgDn (5~ / 6~) */
static bool decode_csi(const app_serial_t *d, uint8_t final, app_cmd_t *cmd)
{
    bool plain = params_are(d, ""), shift = params_are(d, "1;2");
    switch (final) {
    case 'C': if (plain || shift) { *cmd = shift ? APP_CMD_FREQ_UP_10 : APP_CMD_FREQ_UP_1; return true; } break;
    case 'D': if (plain || shift) { *cmd = shift ? APP_CMD_FREQ_DOWN_10 : APP_CMD_FREQ_DOWN_1; return true; } break;
    case 'A': if (plain) { *cmd = APP_CMD_REF_UP; return true; } break;
    case 'B': if (plain) { *cmd = APP_CMD_REF_DOWN; return true; } break;
    case '~':
        if (params_are(d, "5")) { *cmd = APP_CMD_GAIN_UP; return true; }
        if (params_are(d, "6")) { *cmd = APP_CMD_GAIN_DOWN; return true; }
        break;
    default: break;
    }
    return false;
}

bool app_serial_feed(app_serial_t *d, uint8_t c, app_cmd_t *cmd)
{
    switch (d->state) {
    case ST_ESC:
        if (c == '[') {
            d->state = ST_CSI;
            d->len = 0;
            return false;
        }
        /* Échap seul : l'octet suivant est une touche ordinaire */
        d->state = c == 0x1b ? ST_ESC : ST_IDLE;
        return d->state == ST_IDLE && decode_char(c, cmd);
    case ST_CSI:
        if ((c >= '0' && c <= '9') || c == ';') {
            if (d->len < APP_SERIAL_MAX_PARAMS) d->params[d->len++] = (char)c;
            else d->len = APP_SERIAL_MAX_PARAMS + 1u; /* marque débordement : séquence ignorée */
            return false;
        }
        d->state = ST_IDLE;
        return d->len <= APP_SERIAL_MAX_PARAMS && decode_csi(d, c, cmd);
    default:
        if (c == 0x1b) {
            d->state = ST_ESC;
            return false;
        }
        return decode_char(c, cmd);
    }
}
