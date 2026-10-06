// Clavier PS/2 → commandes app_controls (non bloquant)
#pragma once
#include "fabgl.h"
#include "app_controls.h"

struct input_result_t {
  bool radio_changed;
};

input_result_t input_poll(fabgl::Keyboard *kbd, app_settings_t *s, app_view_t *v, unsigned gain_max);
