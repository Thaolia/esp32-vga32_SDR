idf_component_register(SRCS "app_main.cpp" "sdr_task.c" "ui_render.cpp" "input_ps2.cpp" "input_uart.c"
                       INCLUDE_DIRS "."
                       REQUIRES FabGL espsdr_rx spectrum_dsp app_controls esp_driver_uart)
