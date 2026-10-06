/* Réglages radio/affichage et commandes clavier. C pur (testé sur PC). */
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_FREQ_MIN_MHZ 100u
#define APP_FREQ_MAX_MHZ 6000u
#define APP_REF_MIN_DB (-120.0f)
#define APP_REF_MAX_DB 20.0f
#define APP_REF_STEP_DB 5.0f
#define APP_RANGE_MIN_DB 20.0f
#define APP_RANGE_MAX_DB 120.0f
#define APP_RANGE_STEP_DB 10.0f

typedef enum { APP_RATE_16M, APP_RATE_40M, APP_RATE_80M, APP_RATE_COUNT } app_rate_t;
typedef struct { unsigned center_mhz; app_rate_t rate; bool hardware_agc; unsigned gain_index; } app_settings_t;
typedef struct { float ref_db; float range_db; } app_view_t;
typedef enum {
    APP_CMD_FREQ_UP_1, APP_CMD_FREQ_DOWN_1, APP_CMD_FREQ_UP_10, APP_CMD_FREQ_DOWN_10,
    APP_CMD_RATE_CYCLE, APP_CMD_AGC_TOGGLE, APP_CMD_GAIN_UP, APP_CMD_GAIN_DOWN,
    APP_CMD_REF_UP, APP_CMD_REF_DOWN, APP_CMD_RANGE_DOWN, APP_CMD_RANGE_UP
} app_cmd_t;
typedef enum { APP_CHANGE_NONE, APP_CHANGE_VIEW, APP_CHANGE_RADIO } app_change_t;

void app_defaults(app_settings_t *s, app_view_t *v, unsigned gain_max);
app_change_t app_apply_cmd(app_cmd_t cmd, app_settings_t *s, app_view_t *v, unsigned gain_max);
unsigned app_rate_msps(app_rate_t rate);

#ifdef __cplusplus
}
#endif
