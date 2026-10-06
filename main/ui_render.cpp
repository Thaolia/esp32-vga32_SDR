#include "ui_render.h"
#include <cstdio>
#include <cstring>

namespace {

constexpr int UI_W = 512;
constexpr int UI_H = UI_HEIGHT;
constexpr int SPEC_Y0 = 20;
constexpr int SPEC_H = 128;
constexpr int WF_Y0 = 152;
constexpr uint8_t PAL_BG = 0, PAL_GRID = 1, PAL_TRACE = 2, PAL_TEXT = 3;
constexpr uint8_t PAL_WF_FIRST = 4, PAL_WF_COUNT = 12;

const RGB888 kColormap[PAL_WF_COUNT] = {
  {0, 0, 32}, {0, 0, 96}, {0, 0, 170}, {0, 64, 255}, {0, 160, 255}, {0, 255, 200},
  {0, 255, 64}, {160, 255, 0}, {255, 255, 0}, {255, 160, 0}, {255, 64, 0}, {255, 255, 255},
};

fabgl::VGA16Controller *s_vga;
fabgl::Canvas *s_canvas;

// VGA16 : nibble haut = pixel pair
inline void put4(uint8_t *row, int x, uint8_t c)
{
  uint8_t &b = row[x >> 1];
  b = (x & 1) ? ((b & 0xF0) | c) : ((b & 0x0F) | (c << 4));
}

void draw_header(const spectrum_frame_t &f, const app_view_t &v, bool keyboard_ok)
{
  char line[72];
  const app_settings_t &s = f.settings;
  if (s.hardware_agc)
    snprintf(line, sizeof line, "%4u MHz %2u MS/s AGC     ref %+4d rng %3d err %-6lu %s",
             s.center_mhz, app_rate_msps(s.rate), (int) v.ref_db, (int) v.range_db,
             (unsigned long) f.capture_errors, keyboard_ok ? "    " : "KBD?");
  else
    snprintf(line, sizeof line, "%4u MHz %2u MS/s G%-3u    ref %+4d rng %3d err %-6lu %s",
             s.center_mhz, app_rate_msps(s.rate), s.gain_index, (int) v.ref_db, (int) v.range_db,
             (unsigned long) f.capture_errors, keyboard_ok ? "    " : "KBD?");
  s_canvas->drawText(0, 2, line);
  const float half = app_rate_msps(s.rate) / 2.0f;
  char edge[12];
  snprintf(edge, sizeof edge, "%-7.1f", s.center_mhz - half);
  s_canvas->drawText(0, 11, edge);
  snprintf(edge, sizeof edge, "%4u", s.center_mhz);
  s_canvas->drawText(UI_W / 2 - 16, 11, edge);
  snprintf(edge, sizeof edge, "%7.1f", s.center_mhz + half);
  s_canvas->drawText(UI_W - 56, 11, edge);
}

void draw_spectrum(const spectrum_frame_t &f, const app_view_t &v)
{
  constexpr uint8_t bg = (PAL_BG << 4) | PAL_BG;
  for (int y = SPEC_Y0; y < SPEC_Y0 + SPEC_H; ++y)
    memset(s_vga->getScanline(y), bg, UI_W / 2);
  for (int x = 0; x < UI_W; ++x) {
    unsigned h = sd_db_to_height(f.db[x], v.ref_db, v.range_db, SPEC_H);
    for (unsigned k = 0; k < h; ++k)
      put4(s_vga->getScanline(SPEC_Y0 + SPEC_H - 1 - (int) k), x, k + 1 == h ? PAL_TRACE : PAL_GRID);
  }
  for (int y = SPEC_Y0; y < SPEC_Y0 + SPEC_H; y += 4)
    put4(s_vga->getScanline(y), UI_W / 2, PAL_TEXT);
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
  vga.setPaletteItem(PAL_BG, RGB888(0, 0, 0));
  vga.setPaletteItem(PAL_GRID, RGB888(0, 96, 96));
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
  canvas.waitCompletion();
}

void ui_draw_frame(const spectrum_frame_t &frame, const app_view_t &view, bool keyboard_ok)
{
  s_canvas->scroll(0, 1);            // waterfall vers le bas (permutation de pointeurs de lignes)
  draw_header(frame, view, keyboard_ok);
  s_canvas->waitCompletion();        // scroll + texte exécutés avant les écritures brutes
  draw_spectrum(frame, view);
  draw_waterfall_line(frame, view);
}
