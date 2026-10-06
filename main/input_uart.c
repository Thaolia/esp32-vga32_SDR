#include "input_uart.h"
#include "app_serial.h"
#include "driver/uart.h"
#include "esp_log.h"

#define INPUT_UART UART_NUM_0

static const char *TAG = "input_uart";
static app_serial_t s_decoder;
static bool s_ready;

void input_uart_begin(void)
{
    app_serial_init(&s_decoder);
    /* La console garde l'émission ; le driver ne sert qu'à la réception */
    esp_err_t e = uart_driver_install(INPUT_UART, 256, 0, 0, NULL, 0);
    s_ready = e == ESP_OK;
    if (!s_ready) ESP_LOGE(TAG, "uart_driver_install : %s", esp_err_to_name(e));
}

bool input_uart_poll(app_settings_t *s, app_view_t *v, unsigned gain_max)
{
    if (!s_ready) return false;
    uint8_t buf[32];
    bool radio_changed = false;
    int n;
    while ((n = uart_read_bytes(INPUT_UART, buf, sizeof(buf), 0)) > 0) {
        for (int i = 0; i < n; i++) {
            app_cmd_t cmd;
            if (app_serial_feed(&s_decoder, buf[i], &cmd) &&
                app_apply_cmd(cmd, s, v, gain_max) == APP_CHANGE_RADIO)
                radio_changed = true;
        }
    }
    return radio_changed;
}
