#include "app_controls.h"
#include "test_util.h"

static app_settings_t s;
static app_view_t v;

static int test_defaults(void)
{
    app_defaults(&s, &v, 30);
    CHECK(s.center_mhz == 2437 && s.rate == APP_RATE_40M && s.hardware_agc && s.gain_index == 30);
    CHECK_NEAR(v.ref_db, 0.0f, 0.0f);
    CHECK_NEAR(v.range_db, 80.0f, 0.0f);
    app_defaults(&s, &v, 100);
    CHECK(s.gain_index == 40);
    return 0;
}

static int test_freq_steps(void)
{
    app_defaults(&s, &v, 100);
    CHECK(app_apply_cmd(APP_CMD_FREQ_UP_1, &s, &v, 100) == APP_CHANGE_RADIO && s.center_mhz == 2438);
    CHECK(app_apply_cmd(APP_CMD_FREQ_UP_10, &s, &v, 100) == APP_CHANGE_RADIO && s.center_mhz == 2448);
    CHECK(app_apply_cmd(APP_CMD_FREQ_DOWN_10, &s, &v, 100) == APP_CHANGE_RADIO && s.center_mhz == 2438);
    CHECK(app_apply_cmd(APP_CMD_FREQ_DOWN_1, &s, &v, 100) == APP_CHANGE_RADIO && s.center_mhz == 2437);
    return 0;
}

static int test_freq_clamps(void)
{
    app_defaults(&s, &v, 100);
    s.center_mhz = 5995;
    CHECK(app_apply_cmd(APP_CMD_FREQ_UP_10, &s, &v, 100) == APP_CHANGE_RADIO && s.center_mhz == 6000);
    CHECK(app_apply_cmd(APP_CMD_FREQ_UP_1, &s, &v, 100) == APP_CHANGE_NONE && s.center_mhz == 6000);
    s.center_mhz = 105;
    CHECK(app_apply_cmd(APP_CMD_FREQ_DOWN_10, &s, &v, 100) == APP_CHANGE_RADIO && s.center_mhz == 100);
    CHECK(app_apply_cmd(APP_CMD_FREQ_DOWN_1, &s, &v, 100) == APP_CHANGE_NONE && s.center_mhz == 100);
    return 0;
}

static int test_rate_cycle(void)
{
    app_defaults(&s, &v, 100);
    CHECK(app_apply_cmd(APP_CMD_RATE_CYCLE, &s, &v, 100) == APP_CHANGE_RADIO && s.rate == APP_RATE_80M);
    app_apply_cmd(APP_CMD_RATE_CYCLE, &s, &v, 100);
    CHECK(s.rate == APP_RATE_16M);
    app_apply_cmd(APP_CMD_RATE_CYCLE, &s, &v, 100);
    CHECK(s.rate == APP_RATE_40M);
    CHECK(app_rate_msps(APP_RATE_16M) == 16 && app_rate_msps(APP_RATE_40M) == 40 && app_rate_msps(APP_RATE_80M) == 80);
    return 0;
}

static int test_gain(void)
{
    app_defaults(&s, &v, 41);
    CHECK(app_apply_cmd(APP_CMD_AGC_TOGGLE, &s, &v, 41) == APP_CHANGE_RADIO && !s.hardware_agc);
    CHECK(app_apply_cmd(APP_CMD_AGC_TOGGLE, &s, &v, 41) == APP_CHANGE_RADIO && s.hardware_agc);
    CHECK(app_apply_cmd(APP_CMD_GAIN_UP, &s, &v, 41) == APP_CHANGE_RADIO && !s.hardware_agc && s.gain_index == 41);
    CHECK(app_apply_cmd(APP_CMD_GAIN_UP, &s, &v, 41) == APP_CHANGE_NONE && s.gain_index == 41);
    s.gain_index = 0;
    CHECK(app_apply_cmd(APP_CMD_GAIN_DOWN, &s, &v, 41) == APP_CHANGE_NONE && s.gain_index == 0);
    return 0;
}

static int test_view(void)
{
    app_defaults(&s, &v, 100);
    CHECK(app_apply_cmd(APP_CMD_REF_UP, &s, &v, 100) == APP_CHANGE_VIEW);
    CHECK_NEAR(v.ref_db, 5.0f, 0.0f);
    v.ref_db = 20.0f;
    CHECK(app_apply_cmd(APP_CMD_REF_UP, &s, &v, 100) == APP_CHANGE_NONE);
    v.ref_db = -120.0f;
    CHECK(app_apply_cmd(APP_CMD_REF_DOWN, &s, &v, 100) == APP_CHANGE_NONE);
    CHECK(app_apply_cmd(APP_CMD_RANGE_DOWN, &s, &v, 100) == APP_CHANGE_VIEW);
    CHECK_NEAR(v.range_db, 70.0f, 0.0f);
    v.range_db = 20.0f;
    CHECK(app_apply_cmd(APP_CMD_RANGE_DOWN, &s, &v, 100) == APP_CHANGE_NONE);
    v.range_db = 120.0f;
    CHECK(app_apply_cmd(APP_CMD_RANGE_UP, &s, &v, 100) == APP_CHANGE_NONE);
    return 0;
}

int main(void)
{
    int failures = 0;
    RUN(test_defaults);
    RUN(test_freq_steps);
    RUN(test_freq_clamps);
    RUN(test_rate_cycle);
    RUN(test_gain);
    RUN(test_view);
    return failures ? 1 : 0;
}
