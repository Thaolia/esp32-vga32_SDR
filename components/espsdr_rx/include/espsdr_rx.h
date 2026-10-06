/* Récepteur I/Q brut ESP32 dérivé d'ESPARGOS/esp-sdr (GPL-3.0-or-later).
 * Non thread-safe : un seul appelant (la tâche SDR). Le Wi-Fi n'est plus utilisable. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Valeurs = sélecteurs d'horloge matériels du dump MAC */
typedef enum { ESPSDR_RATE_16M = 0, ESPSDR_RATE_80M = 1, ESPSDR_RATE_40M = 2 } espsdr_rate_t;

#define ESPSDR_MIN_SAMPLES 256u
#define ESPSDR_MAX_SAMPLES 16380u

esp_err_t espsdr_rx_init(void);
/* ESP_ERR_INVALID_ARG hors 100..6000 MHz */
esp_err_t espsdr_rx_tune(unsigned mhz);
/* manual_index borné à espsdr_rx_gain_max() */
void espsdr_rx_set_gain(bool hardware_agc, unsigned manual_index);
unsigned espsdr_rx_gain_max(void);
/* *words pointe dans la SRAM réservée, valide jusqu'à la capture suivante.
 * Mot : I = bits 0-9, Q = bits 10-19 (signés). ESP_ERR_TIMEOUT, ESP_ERR_INVALID_RESPONSE, ESP_ERR_INVALID_ARG. */
esp_err_t espsdr_rx_capture(unsigned n, espsdr_rate_t rate, const uint32_t **words, unsigned *elapsed_us);

#ifdef __cplusplus
}
#endif
