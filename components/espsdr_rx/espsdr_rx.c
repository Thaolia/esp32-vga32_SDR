/* Backend RX ESP32 extrait d'ESPARGOS/esp-sdr main/targets/esp32/receiver.c (GPL-3.0-or-later).
 * Séquence registres et réserve SRAM inchangées ; protocole série retiré. */
#include "sdkconfig.h" /* rx_lo.h teste CONFIG_IDF_TARGET_ESP32 sans l'inclure */
#include "espsdr_rx.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "heap_memory_layout.h"
#include "nvs_flash.h"
#include "soc/dport_access.h"
#include "soc/dport_reg.h"
#include "soc/rtc.h"
#include "soc/soc.h"
#include "rx_lo.h"
#include "rx_recalibration.h"
#include "rx_tuning.h"

/* Les deux bancs SRAM servent d'ouverture de capture au MAC (mode 3 = 64 KiB) */
SOC_RESERVE_MEMORY_REGION(0x3ffe8000, 0x3fff8000, esp32_rf_dump);

#define DUMP_CTRL 0x60033d90u
#define DUMP_STATUS 0x60033d94u
#define DUMP_BYTES 0x60033dc4u
#define RX_GAIN 0x3ff5c02cu
#define CAPACITY 16384u
#define SENTINEL 0xa55aa55au

static const char *TAG = "espsdr_rx";
static uint32_t *const samples = (void *)0x3ffe8000;
static unsigned s_gain_max;
static unsigned s_gain_code = 40;
static bool s_hardware_agc = true;
static unsigned s_frequency_mhz = 2412;

extern void rom_pbus_workmode(void);
extern void rom_pbus_xpd_rx_on(int);
extern void rom_pbus_xpd_tx_off(void);
extern void rom_set_rxclk_en(int);
extern void set_chanfreq(unsigned mhz, unsigned mode);
extern void rom_set_rf_freq_offset(unsigned crystal, unsigned mhz, int offset);

static void prepare_rx(void)
{
    rom_pbus_workmode();
    rom_pbus_xpd_tx_off();
    rom_pbus_xpd_rx_on(1);
    rom_set_rxclk_en(1);
    if (rx_lo_select(rx_lo_plan(s_frequency_mhz).alternate)) esp_rom_delay_us(3000);
}

static void apply_gain(void)
{
    uint32_t gain = REG_READ(RX_GAIN);
    if (s_hardware_agc) REG_CLR_BIT(RX_GAIN, BIT(23));
    else REG_WRITE(RX_GAIN, (gain & 0x7fffff) | (s_gain_code << 24) | BIT(23));
}

static void tune_rx(unsigned mhz)
{
    static unsigned calibrated_mhz;
    if (calibrated_mhz != mhz) {
        rx_recalibrate(mhz);
        calibrated_mhz = mhz;
    }
    rx_lo_plan_t plan = rx_lo_plan(mhz);
    rx_lo_select(false);
    bool channel = (mhz >= 2412 && mhz <= 2472 && (mhz - 2412) % 5 == 0) || mhz == 2484;
    /* Calibrer sur un vrai canal avant de contourner le mapping des canaux */
    set_chanfreq(channel ? mhz : 2412, 0);
    if (!channel) {
        unsigned xtal = rtc_clk_xtal_freq_get();
        /* Libérer le code capacité figé par le canal avant le retune ROM */
        unsigned cap = ram_chip_i2c_readReg(0x62, 1, 0);
        ram_chip_i2c_writeReg(0x62, 1, 0, cap & ~0x80u);
        /* Cette ROM utilise des offsets en 1/1024 MHz */
        int offset = (plan.offset_khz * 1024 + 500) / 1000;
        rom_set_rf_freq_offset(xtal == 26 ? 1 : xtal == 24 ? 2 : 0, plan.mhz, offset);
    }
}

esp_err_t espsdr_rx_init(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase");
        e = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(e, TAG, "nvs init");
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_NULL), TAG, "mode");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "ps");
    ESP_RETURN_ON_ERROR(esp_wifi_set_promiscuous(true), TAG, "promiscuous");
    ESP_RETURN_ON_ERROR(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE), TAG, "channel");
    prepare_rx();
    s_gain_max = (REG_READ(RX_GAIN) >> 8) & 127;
    if (s_gain_code > s_gain_max) s_gain_code = s_gain_max;
    REG_CLR_BIT(RX_GAIN, BIT(23)); /* AGC matériel par défaut */
    return ESP_OK;
}

esp_err_t espsdr_rx_tune(unsigned mhz)
{
    if (!rx_frequency_valid(mhz)) return ESP_ERR_INVALID_ARG;
    s_frequency_mhz = mhz;
    tune_rx(mhz);
    prepare_rx();
    apply_gain();
    return ESP_OK;
}

void espsdr_rx_set_gain(bool hardware_agc, unsigned manual_index)
{
    s_hardware_agc = hardware_agc;
    s_gain_code = manual_index > s_gain_max ? s_gain_max : manual_index;
    apply_gain();
}

unsigned espsdr_rx_gain_max(void)
{
    return s_gain_max;
}

esp_err_t espsdr_rx_capture(unsigned n, espsdr_rate_t rate, const uint32_t **words, unsigned *elapsed_us)
{
    unsigned clock = (unsigned)rate;
    if (n < ESPSDR_MIN_SAMPLES || n > ESPSDR_MAX_SAMPLES || clock > 2) return ESP_ERR_INVALID_ARG;
    REG_WRITE(DUMP_CTRL, 0);
    for (unsigned j = 0; j < CAPACITY; j++) samples[j] = SENTINEL;
    uint32_t owner = DPORT_REG_READ(DPORT_IRAM_DRAM_AHB_SEL_REG);
    uint32_t byte_select = REG_READ(DUMP_BYTES);
    DPORT_REG_WRITE(DPORT_IRAM_DRAM_AHB_SEL_REG,
        (owner & ~DPORT_MAC_DUMP_MODE_M) | (3 << DPORT_MAC_DUMP_MODE_S));
    REG_WRITE(DUMP_BYTES, 0x03020100); /* Quatre octets distincts, comme mac_init */
    uint32_t ctrl = n | ((clock == 0) << 16) | ((clock == 2) << 15);
    REG_WRITE(DUMP_CTRL, ctrl | BIT(31));
    int64_t start = esp_timer_get_time();
    REG_SET_BIT(DUMP_CTRL, BIT(19));
    REG_CLR_BIT(DUMP_CTRL, BIT(19));
    while (!(REG_READ(DUMP_CTRL) & BIT(18)) && esp_timer_get_time() - start < 20000) {}
    uint32_t result = REG_READ(DUMP_CTRL), status = REG_READ(DUMP_STATUS);
    unsigned elapsed = (unsigned)(esp_timer_get_time() - start);
    REG_WRITE(DUMP_CTRL, 0);
    esp_rom_delay_us(10);
    DPORT_REG_WRITE(DPORT_IRAM_DRAM_AHB_SEL_REG, owner);
    REG_WRITE(DUMP_BYTES, byte_select);
    if (!(result & BIT(18)) || (status & 0x7fff) != n) return ESP_ERR_TIMEOUT;
    for (unsigned j = 0; j < CAPACITY; j++) {
        if ((j < n && samples[j] == SENTINEL) || (j >= n && samples[j] != SENTINEL))
            return ESP_ERR_INVALID_RESPONSE;
    }
    *words = samples;
    *elapsed_us = elapsed;
    return ESP_OK;
}
