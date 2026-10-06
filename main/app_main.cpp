// Analyseur de spectre VGA32 : radio (esp-sdr) → DSP → FabGL VGA16 512x384, clavier PS/2
#include <cstdlib>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "fabgl.h"
#include "espsdr_rx.h"
#include "input_ps2.h"
#include "input_uart.h"
#include "sdr_task.h"
#include "ui_render.h"

#if CONFIG_FREERTOS_UNICORE
#define UI_CORE 0
#else
#define UI_CORE 1 // même core que l'ISR vidéo, le core 0 calcule la DSP
#endif

namespace {

constexpr const char *TAG = "app";
// Primitives exécutées en synchrone par la tâche ui (cf. ui_begin) : la file FabGL ne sert
// presque pas, la taille par défaut (1024) gaspillerait ~20 Ko
constexpr int APP_VGA_QUEUE_SIZE = 8;

fabgl::PS2Controller s_ps2;
fabgl::VGA16Controller s_vga;
spectrum_frame_t s_frame;
unsigned s_gain_max; // lu dans app_main : espsdr_rx_* n'est appelé que par la tâche sdr ensuite

void log_heap(const char *stage)
{
  ESP_LOGI(TAG, "%s : DMA libre %u (plus grand bloc %u)", stage,
           (unsigned) heap_caps_get_free_size(MALLOC_CAP_DMA),
           (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
}

void ui_task(void *)
{
  static fabgl::Canvas canvas(&s_vga);
  ui_begin(s_vga, canvas);
  const unsigned gain_max = s_gain_max;
  app_settings_t settings;
  app_view_t view;
  app_defaults(&settings, &view, gain_max);
  sdr_task_start(&settings);
  bool have_frame = false;
  for (;;) {
    fabgl::Keyboard *kbd = s_ps2.keyboard();
    const bool keyboard_ok = kbd && kbd->isKeyboardAvailable();
    bool radio_changed = input_poll(kbd, &settings, &view, gain_max).radio_changed;
    radio_changed |= input_uart_poll(&settings, &view, gain_max);
    if (radio_changed)
      sdr_task_request(&settings);
    if (sdr_task_get_frame(&s_frame, 50)) {
      have_frame = true;
      ui_draw_frame(s_frame, view, keyboard_ok);
    } else if (have_frame) {
      // Plus de trame (captures en échec) : le compteur d'erreurs doit continuer d'avancer
      s_frame.capture_errors = sdr_task_capture_errors();
      ui_draw_status(s_frame, view, keyboard_ok);
    }
  }
}

} // namespace

extern "C" void app_main()
{
  log_heap("boot");
  ESP_ERROR_CHECK(espsdr_rx_init()); // avant FabGL : le Wi-Fi alloue ses buffers internes en premier
  s_gain_max = espsdr_rx_gain_max();
  log_heap("après radio");
  s_ps2.begin(PS2Preset::KeyboardPort0);
  input_uart_begin();
  log_heap("après PS/2 + UART");
  fabgl::BitmappedDisplayController::queueSize = APP_VGA_QUEUE_SIZE;
  s_vga.begin();
  s_vga.setResolution(VGA_512x384_60Hz);
  log_heap("après VGA");
  // Faute de RAM, FabGL réduit la hauteur du viewport au lieu d'échouer
  if (s_vga.getViewPortHeight() != UI_HEIGHT) {
    ESP_LOGE(TAG, "viewport %d lignes au lieu de %d : RAM insuffisante (voir docs/ARCHITECTURE.md)",
             s_vga.getViewPortHeight(), UI_HEIGHT);
    abort();
  }
  // Pile statique : le heap restant appartient au framebuffer
  static StackType_t ui_stack[4096];
  static StaticTask_t ui_tcb;
  TaskHandle_t ui = xTaskCreateStaticPinnedToCore(ui_task, "ui", 4096, nullptr, 3, ui_stack, &ui_tcb, UI_CORE);
  configASSERT(ui);
}
