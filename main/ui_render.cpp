#include "ui_render.h"
#include <cstdio>
#include <cstring>

namespace {

constexpr int UI_W = 512;
constexpr int UI_H = UI_HEIGHT;
constexpr int SPEC_Y0 = 20;
constexpr int SPEC_H = 128;
constexpr int WF_Y0 = 152;
// Le Canvas convertit ses couleurs RGB888 via une LUT calculée sur la palette PAR DÉFAUT
// (setPaletteItem ne la reconstruit pas) : noir → 0 et blanc → 15 doivent donc rester le
// fond et le texte. Les autres index ne sont écrits qu'en brut.
constexpr uint8_t PAL_BG = 0, PAL_GRID = 1, PAL_TRACE = 2, PAL_TEXT = 15;
constexpr uint8_t PAL_WF_FIRST = 3, PAL_WF_COUNT = 12;

// VGA16 sort en RGB222 : chaque composante est tronquée à ses 2 bits de poids fort
const RGB888 kColormap[PAL_WF_COUNT] = {
  {0, 0, 64}, {0, 0, 128}, {0, 0, 192}, {0, 64, 255}, {0, 160, 255}, {0, 255, 192},
  {0, 255, 64}, {160, 255, 0}, {255, 255, 0}, {255, 160, 0}, {255, 64, 0}, {255, 255, 170},
};

fabgl::VGA16Controller *s_vga;
fabgl::Canvas *s_canvas;
char s_header[2][72];

// VGA16 : nibble haut = pixel pair
inline void put4(uint8_t *row, int x, uint8_t c)
{
  uint8_t &b = row[x >> 1];
  b = (x & 1) ? ((b & 0xF0) | c) : ((b & 0x0F) | (c << 4));
}

// Le texte coûte une primitive par glyphe : ne redessiner que ce qui a changé
void draw_text_if_changed(int line, int y, const char *text)
{
  if (strcmp(s_header[line], text) == 0)
    return;
  snprintf(s_header[line], sizeof s_header[line], "%s", text);
  s_canvas->drawText(0, y, text);
}

void draw_header(const spectrum_frame_t &f, const app_view_t &v, bool keyboard_ok)
{
  const app_settings_t &s = f.settings;
  char gain[8];
  if (s.hardware_agc)
    snprintf(gain, sizeof gain, "AGC");
  else
    snprintf(gain, sizeof gain, "G%u", s.gain_index);
  char line[72];
  snprintf(line, sizeof line, "%4u MHz %2u MS/s %-4s ref %+4d rng %3d err %-6lu %s",
           s.center_mhz, app_rate_msps(s.rate), gain, (int) v.ref_db, (int) v.range_db,
           (unsigned long) f.capture_errors, keyboard_ok ? "    " : "KBD?");
  draw_text_if_changed(0, 2, line);
  const float half = app_rate_msps(s.rate) / 2.0f;
  // 64 colonnes de 8 px : bornes à gauche et à droite, centre au milieu
  snprintf(line, sizeof line, "%-7.1f%23s%4u%23s%7.1f",
           s.center_mhz - half, "", s.center_mhz, "", s.center_mhz + half);
  draw_text_if_changed(1, 11, line);
}

// Une seule passe par pixel (fond, barre, sommet) : pas d'effacement visible entre deux trames
void draw_spectrum(const spectrum_frame_t &f, const app_view_t &v)
{
  uint8_t *rows[SPEC_H];
  for (int k = 0; k < SPEC_H; ++k)
    rows[k] = s_vga->getScanline(SPEC_Y0 + SPEC_H - 1 - k); // rows[0] = ligne du bas
  for (int x = 0; x < UI_W; ++x) {
    const int h = (int) sd_db_to_height(f.db[x], v.ref_db, v.range_db, SPEC_H);
    const bool marker = x == UI_W / 2;
    for (int k = 0; k < SPEC_H; ++k) {
      uint8_t c = k + 1 < h ? PAL_GRID : k + 1 == h ? PAL_TRACE : PAL_BG;
      if (marker && c == PAL_BG && (k & 3) == 0)
        c = PAL_TEXT;
      put4(rows[k], x, c);
    }
  }
}

void draw_waterfall_line(const spectrum_frame_t &f, const app_view_t &v)
{
  uint8_t *row = s_vga->getScanline(WF_Y0);
  for (int x = 0; x < UI_W; ++x)
    put4(row, x, sd_db_to_color(f.db[x], v.ref_db, v.range_db, PAL_WF_FIRST, PAL_WF_COUNT));
}

} // namespace

void ui_begin(fabgl::VGA16Controller &vga, fabgl::Canvas &canvas)
{
  s_vga = &vga;
  s_canvas = &canvas;
  // Primitives exécutées dans la tâche ui : aucune course possible avec les écritures brutes
  vga.enableBackgroundPrimitiveExecution(false);
  vga.setPaletteItem(PAL_BG, RGB888(0, 0, 0));
  vga.setPaletteItem(PAL_GRID, RGB888(0, 128, 128));
  vga.setPaletteItem(PAL_TRACE, RGB888(255, 255, 0));
  vga.setPaletteItem(PAL_TEXT, RGB888(255, 255, 255));
  for (int i = 0; i < PAL_WF_COUNT; ++i)
    vga.setPaletteItem(PAL_WF_FIRST + i, kColormap[i]);
  canvas.setBrushColor(RGB888(0, 0, 0));
  canvas.setPenColor(RGB888(255, 255, 255));
  canvas.clear();
  canvas.selectFont(&fabgl::FONT_8x8);
  canvas.setGlyphOptions(GlyphOptions().FillBackground(true));
  canvas.setScrollingRegion(0, WF_Y0, UI_W - 1, UI_H - 1);
}

void ui_draw_frame(const spectrum_frame_t &frame, const app_view_t &view, bool keyboard_ok)
{
  s_canvas->scroll(0, 1); // waterfall vers le bas (permutation de pointeurs de lignes)
  draw_header(frame, view, keyboard_ok);
  draw_spectrum(frame, view);
  draw_waterfall_line(frame, view);
}

void ui_draw_status(const spectrum_frame_t &frame, const app_view_t &view, bool keyboard_ok)
{
  draw_header(frame, view, keyboard_ok);
}
