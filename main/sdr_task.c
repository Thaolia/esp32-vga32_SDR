#include "sdr_task.h"
#include "esp_log.h"
#include "espsdr_rx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define SDR_CORE 0 /* core 1 réservé à l'ISR vidéo FabGL et à l'UI (aussi valable en unicore) */

static const char *TAG = "sdr_task";
static sd_ctx_t s_dsp;
static spectrum_frame_t s_work, s_shared;
static app_settings_t s_requested;
static uint32_t s_requested_version;
static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_frame_ready;

static espsdr_rate_t to_hw_rate(app_rate_t rate)
{
    return rate == APP_RATE_16M ? ESPSDR_RATE_16M : rate == APP_RATE_80M ? ESPSDR_RATE_80M : ESPSDR_RATE_40M;
}

static void apply_settings(const app_settings_t *want, const app_settings_t *active, bool first)
{
    if (first || want->center_mhz != active->center_mhz) {
        esp_err_t e = espsdr_rx_tune(want->center_mhz);
        if (e != ESP_OK) ESP_LOGE(TAG, "tune %u MHz : %s", want->center_mhz, esp_err_to_name(e));
    }
    espsdr_rx_set_gain(want->hardware_agc, want->gain_index);
}

static void sdr_task(void *arg)
{
    (void)arg;
    app_settings_t active = {0};
    uint32_t applied_version = 0, errors = 0, seq = 0;
    bool first = true;
    sd_init(&s_dsp);
    for (;;) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        app_settings_t want = s_requested;
        uint32_t version = s_requested_version;
        xSemaphoreGive(s_lock);
        if (first || version != applied_version) {
            apply_settings(&want, &active, first);
            active = want;
            applied_version = version;
            first = false;
        }
        unsigned us = 0;
        for (unsigned k = 0; k < SDR_AVERAGES; k++) {
            const uint32_t *words;
            unsigned elapsed;
            if (espsdr_rx_capture(SD_FFT_N, to_hw_rate(active.rate), &words, &elapsed) == ESP_OK) {
                sd_accumulate(&s_dsp, words);  /* avant la capture suivante : words est écrasé */
                us = elapsed;
            } else {
                errors++;
            }
        }
        if (sd_finish_db(&s_dsp, s_work.db)) {
            s_work.settings = active;
            s_work.seq = ++seq;
            s_work.capture_errors = errors;
            s_work.capture_us = us;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_shared = s_work;
            xSemaphoreGive(s_lock);
            xSemaphoreGive(s_frame_ready);
        }
        vTaskDelay(1); /* laisse tourner l'idle task du core 0 (task watchdog) */
    }
}

void sdr_task_start(const app_settings_t *initial)
{
    s_lock = xSemaphoreCreateMutex();
    s_frame_ready = xSemaphoreCreateBinary();
    configASSERT(s_lock && s_frame_ready);
    s_requested = *initial;
    /* Pile statique : réservée avant que FabGL ne prenne le reste du heap */
    static StackType_t stack[4096];
    static StaticTask_t tcb;
    TaskHandle_t task = xTaskCreateStaticPinnedToCore(sdr_task, "sdr", 4096, NULL, 2, stack, &tcb, SDR_CORE);
    configASSERT(task);
}

void sdr_task_request(const app_settings_t *settings)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_requested = *settings;
    s_requested_version++;
    xSemaphoreGive(s_lock);
}

bool sdr_task_get_frame(spectrum_frame_t *out, uint32_t timeout_ms)
{
    if (xSemaphoreTake(s_frame_ready, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_shared;
    xSemaphoreGive(s_lock);
    return true;
}
