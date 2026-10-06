/* DSP spectre : FFT complexe 1024 points, moyennage, dBFS, quantification écran. C pur (testé sur PC). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SD_FFT_N 1024u
#define SD_COLUMNS 512u
#define SD_FLOOR_DB (-200.0f)

typedef struct {
    float re[SD_FFT_N];
    float im[SD_FFT_N];
    float window[SD_FFT_N];
    float power_acc[SD_FFT_N]; /* ordre fftshift : index 0 = −fs/2 */
    float tw_re[SD_FFT_N / 2];
    float tw_im[SD_FFT_N / 2];
    unsigned frames;
} sd_ctx_t;

/* Mot esp-sdr : I signé bits 0-9, Q signé bits 10-19 */
static inline int32_t sd_iq10_i(uint32_t w) { return (int32_t)(w << 22) >> 22; }
static inline int32_t sd_iq10_q(uint32_t w) { return (int32_t)(w << 12) >> 22; }

void sd_init(sd_ctx_t *ctx);
void sd_fft(const sd_ctx_t *ctx, float *re, float *im);
void sd_accumulate(sd_ctx_t *ctx, const uint32_t *words);
bool sd_finish_db(sd_ctx_t *ctx, float *db_cols);
unsigned sd_db_to_height(float db, float ref_db, float range_db, unsigned height);
uint8_t sd_db_to_color(float db, float ref_db, float range_db, uint8_t first, uint8_t count);

#ifdef __cplusplus
}
#endif
