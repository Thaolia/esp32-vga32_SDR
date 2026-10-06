#include "app_controls.h"

void app_defaults(app_settings_t *s, app_view_t *v, unsigned gain_max)
{
    s->center_mhz = 2437;
    s->rate = APP_RATE_40M;
    s->hardware_agc = true;
    s->gain_index = gain_max < 40u ? gain_max : 40u;
    v->ref_db = 0.0f;
    v->range_db = 80.0f;
}

static app_change_t step_freq(app_settings_t *s, int delta)
{
    int f = (int)s->center_mhz + delta;
    if (f < (int)APP_FREQ_MIN_MHZ) f = (int)APP_FREQ_MIN_MHZ;
    if (f > (int)APP_FREQ_MAX_MHZ) f = (int)APP_FREQ_MAX_MHZ;
    if ((unsigned)f == s->center_mhz) return APP_CHANGE_NONE;
    s->center_mhz = (unsigned)f;
    return APP_CHANGE_RADIO;
}

static app_change_t step_view(float *value, float delta, float lo, float hi)
{
    float next = *value + delta;
    if (next < lo) next = lo;
    if (next > hi) next = hi;
    if (next == *value) return APP_CHANGE_NONE;
    *value = next;
    return APP_CHANGE_VIEW;
}

/* Toute action sur le gain passe en manuel */
static app_change_t step_gain(app_settings_t *s, int delta, unsigned gain_max)
{
    int g = (int)s->gain_index + delta;
    if (g < 0) g = 0;
    if (g > (int)gain_max) g = (int)gain_max;
    if ((unsigned)g == s->gain_index && !s->hardware_agc) return APP_CHANGE_NONE;
    s->gain_index = (unsigned)g;
    s->hardware_agc = false;
    return APP_CHANGE_RADIO;
}

app_change_t app_apply_cmd(app_cmd_t cmd, app_settings_t *s, app_view_t *v, unsigned gain_max)
{
    switch (cmd) {
    case APP_CMD_FREQ_UP_1: return step_freq(s, 1);
    case APP_CMD_FREQ_DOWN_1: return step_freq(s, -1);
    case APP_CMD_FREQ_UP_10: return step_freq(s, 10);
    case APP_CMD_FREQ_DOWN_10: return step_freq(s, -10);
    case APP_CMD_RATE_CYCLE:
        s->rate = s->rate == APP_RATE_16M ? APP_RATE_40M : s->rate == APP_RATE_40M ? APP_RATE_80M : APP_RATE_16M;
        return APP_CHANGE_RADIO;
    case APP_CMD_AGC_TOGGLE:
        s->hardware_agc = !s->hardware_agc;
        return APP_CHANGE_RADIO;
    case APP_CMD_GAIN_UP: return step_gain(s, 1, gain_max);
    case APP_CMD_GAIN_DOWN: return step_gain(s, -1, gain_max);
    case APP_CMD_REF_UP: return step_view(&v->ref_db, APP_REF_STEP_DB, APP_REF_MIN_DB, APP_REF_MAX_DB);
    case APP_CMD_REF_DOWN: return step_view(&v->ref_db, -APP_REF_STEP_DB, APP_REF_MIN_DB, APP_REF_MAX_DB);
    case APP_CMD_RANGE_DOWN: return step_view(&v->range_db, -APP_RANGE_STEP_DB, APP_RANGE_MIN_DB, APP_RANGE_MAX_DB);
    case APP_CMD_RANGE_UP: return step_view(&v->range_db, APP_RANGE_STEP_DB, APP_RANGE_MIN_DB, APP_RANGE_MAX_DB);
    }
    return APP_CHANGE_NONE;
}

unsigned app_rate_msps(app_rate_t rate)
{
    return rate == APP_RATE_16M ? 16u : rate == APP_RATE_80M ? 80u : 40u;
}
