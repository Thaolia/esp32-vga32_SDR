/* Tâche SDR (core 0) : capture → FFT → moyennage ; publie la dernière trame */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "app_controls.h"
#include "spectrum_dsp.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDR_AVERAGES 8u

typedef struct {
    float db[SD_COLUMNS];
    app_settings_t settings;   /* réglages effectifs pour cette trame */
    uint32_t seq;
    uint32_t capture_errors;   /* cumulé depuis le boot */
    unsigned capture_us;       /* durée de la dernière capture réussie */
} spectrum_frame_t;

void sdr_task_start(const app_settings_t *initial);
void sdr_task_request(const app_settings_t *settings);
bool sdr_task_get_frame(spectrum_frame_t *out, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
