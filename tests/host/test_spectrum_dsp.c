#include <stdint.h>
#include <stdlib.h>
#include "spectrum_dsp.h"
#include "test_util.h"

#define TWO_PI 6.28318530717958647692f
static sd_ctx_t ctx;
static uint32_t words[SD_FFT_N];
static float db[SD_COLUMNS];

static uint32_t pack(int32_t i, int32_t q) { return ((uint32_t)i & 0x3ffu) | (((uint32_t)q & 0x3ffu) << 10); }

static void make_tone(int bin, float amp)
{
    for (unsigned n = 0; n < SD_FFT_N; n++) {
        float ph = TWO_PI * (float)bin * (float)n / (float)SD_FFT_N;
        words[n] = pack((int32_t)lroundf(amp * cosf(ph)), (int32_t)lroundf(amp * sinf(ph)));
    }
}

static int test_iq10_sign(void)
{
    uint32_t w = pack(-1, 511);
    CHECK(sd_iq10_i(w) == -1 && sd_iq10_q(w) == 511);
    w = pack(-512, -512) | (0x3fu << 20); /* bits hauts (gain) ignorés */
    CHECK(sd_iq10_i(w) == -512 && sd_iq10_q(w) == -512);
    return 0;
}

static int test_fft_impulse(void)
{
    static float re[SD_FFT_N], im[SD_FFT_N];
    for (unsigned k = 0; k < SD_FFT_N; k++) re[k] = im[k] = 0.0f;
    re[0] = 1.0f;
    sd_fft(&ctx, re, im);
    for (unsigned k = 0; k < SD_FFT_N; k++) { CHECK_NEAR(re[k], 1.0f, 1e-5f); CHECK_NEAR(im[k], 0.0f, 1e-5f); }
    return 0;
}

static int test_fft_tone_bin37(void)
{
    static float re[SD_FFT_N], im[SD_FFT_N];
    for (unsigned n = 0; n < SD_FFT_N; n++) {
        re[n] = cosf(TWO_PI * 37.0f * (float)n / (float)SD_FFT_N);
        im[n] = sinf(TWO_PI * 37.0f * (float)n / (float)SD_FFT_N);
    }
    sd_fft(&ctx, re, im);
    for (unsigned k = 0; k < SD_FFT_N; k++) {
        float mag = sqrtf(re[k] * re[k] + im[k] * im[k]);
        if (k == 37) CHECK_NEAR(mag, 1024.0f, 0.05f);
        else CHECK(mag < 0.05f);
    }
    return 0;
}

static int check_peak(unsigned col)
{
    CHECK_NEAR(db[col], -6.02f, 0.2f);
    for (unsigned c = 0; c < SD_COLUMNS; c++)
        if (abs((int)c - (int)col) > 1) CHECK(db[c] < -60.0f);
    return 0;
}

static int test_tone_positive(void)
{
    make_tone(256, 256.0f);          /* +fs/4 → index décalé 768 → colonne 384 */
    sd_accumulate(&ctx, words);
    CHECK(sd_finish_db(&ctx, db));
    return check_peak(384);
}

static int test_tone_negative(void)
{
    make_tone(-256, 256.0f);         /* −fs/4 → index décalé 256 → colonne 128 */
    sd_accumulate(&ctx, words);
    CHECK(sd_finish_db(&ctx, db));
    return check_peak(128);
}

static int test_average_equals_single(void)
{
    make_tone(256, 256.0f);
    sd_accumulate(&ctx, words);
    sd_accumulate(&ctx, words);
    CHECK(sd_finish_db(&ctx, db));
    CHECK_NEAR(db[384], -6.02f, 0.2f);
    return 0;
}

static int test_dc_removed(void)
{
    for (unsigned n = 0; n < SD_FFT_N; n++) words[n] = pack(100, -50);
    sd_accumulate(&ctx, words);
    CHECK(sd_finish_db(&ctx, db));
    for (unsigned c = 0; c < SD_COLUMNS; c++) CHECK(db[c] <= -150.0f);
    return 0;
}

static int test_finish_without_frames(void)
{
    CHECK(!sd_finish_db(&ctx, db));
    make_tone(10, 100.0f);
    sd_accumulate(&ctx, words);
    CHECK(sd_finish_db(&ctx, db));
    CHECK(!sd_finish_db(&ctx, db));  /* accumulateur remis à zéro */
    return 0;
}

static int test_db_to_height(void)
{
    CHECK(sd_db_to_height(0.0f, 0.0f, 80.0f, 128) == 128);
    CHECK(sd_db_to_height(-80.0f, 0.0f, 80.0f, 128) == 0);
    CHECK(sd_db_to_height(-40.0f, 0.0f, 80.0f, 128) == 64);
    CHECK(sd_db_to_height(10.0f, 0.0f, 80.0f, 128) == 128);
    CHECK(sd_db_to_height(-100.0f, 0.0f, 80.0f, 128) == 0);
    CHECK(sd_db_to_height(NAN, 0.0f, 80.0f, 128) == 0);
    CHECK(sd_db_to_height(-INFINITY, 0.0f, 80.0f, 128) == 0);
    CHECK(sd_db_to_height(INFINITY, 0.0f, 80.0f, 128) == 128);
    CHECK(sd_db_to_height(-10.0f, 0.0f, 0.0f, 128) == 0);
    return 0;
}

static int test_db_to_color(void)
{
    CHECK(sd_db_to_color(-80.0f, 0.0f, 80.0f, 4, 12) == 4);
    CHECK(sd_db_to_color(0.0f, 0.0f, 80.0f, 4, 12) == 15);
    CHECK(sd_db_to_color(-40.0f, 0.0f, 80.0f, 4, 12) == 10);
    CHECK(sd_db_to_color(NAN, 0.0f, 80.0f, 4, 12) == 4);
    CHECK(sd_db_to_color(INFINITY, 0.0f, 80.0f, 4, 12) == 15);
    CHECK(sd_db_to_color(-10.0f, 0.0f, 80.0f, 4, 0) == 4);
    return 0;
}

int main(void)
{
    int failures = 0;
    sd_init(&ctx);
    RUN(test_iq10_sign);
    RUN(test_fft_impulse);
    RUN(test_fft_tone_bin37);
    RUN(test_tone_positive);
    RUN(test_tone_negative);
    RUN(test_average_equals_single);
    RUN(test_dc_removed);
    RUN(test_finish_without_frames);
    RUN(test_db_to_height);
    RUN(test_db_to_color);
    return failures ? 1 : 0;
}
