#include "spectrum_dsp.h"
#include <math.h>
#include <string.h>

#define SD_PI 3.14159265358979323846f
/* Ton complexe pleine échelle (|I|,|Q| = 512) centré sur un bin, Hann (gain cohérent 0,5) = 0 dBFS */
#define SD_NORM (512.0f * (float)SD_FFT_N * 0.5f)

void sd_init(sd_ctx_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    for (unsigned k = 0; k < SD_FFT_N; k++)
        ctx->window[k] = 0.5f - 0.5f * cosf(2.0f * SD_PI * (float)k / (float)SD_FFT_N);
    for (unsigned k = 0; k < SD_FFT_N / 2; k++) {
        ctx->tw_re[k] = cosf(-2.0f * SD_PI * (float)k / (float)SD_FFT_N);
        ctx->tw_im[k] = sinf(-2.0f * SD_PI * (float)k / (float)SD_FFT_N);
    }
}

void sd_fft(const sd_ctx_t *ctx, float *re, float *im)
{
    const unsigned n = SD_FFT_N;
    for (unsigned i = 1, j = 0; i < n; i++) {
        unsigned bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (unsigned len = 2; len <= n; len <<= 1) {
        unsigned half = len >> 1, step = n / len;
        for (unsigned i = 0; i < n; i += len) {
            for (unsigned k = 0; k < half; k++) {
                float wr = ctx->tw_re[k * step], wi = ctx->tw_im[k * step];
                unsigned a = i + k, b = a + half;
                float tr = re[b] * wr - im[b] * wi;
                float ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
        }
    }
}

void sd_accumulate(sd_ctx_t *ctx, const uint32_t *words)
{
    float mean_i = 0.0f, mean_q = 0.0f;
    for (unsigned k = 0; k < SD_FFT_N; k++) {
        ctx->re[k] = (float)sd_iq10_i(words[k]);
        ctx->im[k] = (float)sd_iq10_q(words[k]);
        mean_i += ctx->re[k];
        mean_q += ctx->im[k];
    }
    mean_i /= (float)SD_FFT_N;
    mean_q /= (float)SD_FFT_N;
    for (unsigned k = 0; k < SD_FFT_N; k++) {
        ctx->re[k] = (ctx->re[k] - mean_i) * ctx->window[k];
        ctx->im[k] = (ctx->im[k] - mean_q) * ctx->window[k];
    }
    sd_fft(ctx, ctx->re, ctx->im);
    for (unsigned s = 0; s < SD_FFT_N; s++) {
        unsigned k = (s + SD_FFT_N / 2) % SD_FFT_N;
        ctx->power_acc[s] += ctx->re[k] * ctx->re[k] + ctx->im[k] * ctx->im[k];
    }
    ctx->frames++;
}

bool sd_finish_db(sd_ctx_t *ctx, float *db_cols)
{
    if (!ctx->frames) return false;
    const float scale = 1.0f / (SD_NORM * SD_NORM * (float)ctx->frames);
    for (unsigned c = 0; c < SD_COLUMNS; c++) {
        float p = fmaxf(ctx->power_acc[2 * c], ctx->power_acc[2 * c + 1]) * scale;
        db_cols[c] = p > 1e-20f ? 10.0f * log10f(p) : SD_FLOOR_DB;
    }
    memset(ctx->power_acc, 0, sizeof(ctx->power_acc));
    ctx->frames = 0;
    return true;
}

/* Position relative de db dans [ref − range, ref] ; NaN → −1 */
static float sd_ratio(float db, float ref_db, float range_db)
{
    if (!(range_db > 0.0f) || isnan(db)) return -1.0f;
    return (db - (ref_db - range_db)) / range_db;
}

unsigned sd_db_to_height(float db, float ref_db, float range_db, unsigned height)
{
    float t = sd_ratio(db, ref_db, range_db);
    if (t <= 0.0f) return 0;
    if (t >= 1.0f) return height;
    return (unsigned)(t * (float)height + 0.5f);
}

uint8_t sd_db_to_color(float db, float ref_db, float range_db, uint8_t first, uint8_t count)
{
    if (!count) return first;
    float t = sd_ratio(db, ref_db, range_db);
    if (t <= 0.0f) return first;
    if (t >= 1.0f) return (uint8_t)(first + count - 1);
    unsigned i = (unsigned)(t * (float)count);
    return (uint8_t)(first + (i < count ? i : count - 1u));
}
