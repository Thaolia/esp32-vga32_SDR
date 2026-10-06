// Rendu FabGL : bandeau texte, spectre en barres, waterfall défilant
#pragma once
#include "fabgl.h"
#include "app_controls.h"
#include "sdr_task.h"

// Hauteur du viewport VGA attendue par le rendu
constexpr int UI_HEIGHT = 384;

void ui_begin(fabgl::VGA16Controller &vga, fabgl::Canvas &canvas);
void ui_draw_frame(const spectrum_frame_t &frame, const app_view_t &view, bool keyboard_ok);
// Bandeau seul : rafraîchit le compteur d'erreurs quand aucune trame n'arrive
void ui_draw_status(const spectrum_frame_t &frame, const app_view_t &view, bool keyboard_ok);
