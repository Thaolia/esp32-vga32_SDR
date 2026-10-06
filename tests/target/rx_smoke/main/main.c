// Smoke test RX : 100 captures/s à 2437 MHz, 40 MS/s ; log du heap pour dimensionner la VGA
#include <math.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "espsdr_rx.h"

static const char *TAG = "rx_smoke";

static void log_heap(const char *stage)
{
    ESP_LOGI(TAG, "%s : DMA libre %u (plus grand bloc %u)", stage,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
}

void app_main(void)
{
    log_heap("boot");
    ESP_ERROR_CHECK(espsdr_rx_init());
    log_heap("après espsdr_rx_init");
    ESP_ERROR_CHECK(espsdr_rx_tune(2437));
    for (;;) {
        unsigned ok = 0, fail = 0, us = 0;
        double acc = 0;
        for (int k = 0; k < 100; k++) {
            const uint32_t *w;
            if (espsdr_rx_capture(1024, ESPSDR_RATE_40M, &w, &us) != ESP_OK) { fail++; continue; }
            for (int j = 0; j < 1024; j++) {
                int32_t i = (int32_t)(w[j] << 22) >> 22, q = (int32_t)(w[j] << 12) >> 22;
                acc += (double)(i * i + q * q);
            }
            ok++;
        }
        ESP_LOGI(TAG, "ok=%u fail=%u last_us=%u rms=%.1f gain_max=%u", ok, fail, us,
                 ok ? sqrt(acc / (ok * 2048.0)) : 0.0, espsdr_rx_gain_max());
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
