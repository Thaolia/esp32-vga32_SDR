set(ESPSDR_MAIN "${CMAKE_CURRENT_LIST_DIR}/../../third_party/esp-sdr/main")
idf_component_register(SRCS "espsdr_rx.c" "${ESPSDR_MAIN}/common/rx_recalibration.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "${ESPSDR_MAIN}/common"
                       REQUIRES esp_common
                       PRIV_REQUIRES esp_phy esp_wifi esp_event nvs_flash esp_timer
                                     esp_rom esp_hw_support heap soc)
# Garde linker de l'ouverture SRAM + hook de recalibration PHY (cf. esp-sdr main/CMakeLists.txt)
target_link_options(${COMPONENT_LIB} INTERFACE
    "-T${ESPSDR_MAIN}/targets/esp32/sram_guard.ld"
    "-Wl,--wrap=chip_v7_set_chan_ana")
